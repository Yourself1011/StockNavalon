#include "search.h"
#include "view.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>

namespace {
// A Gumbel(0, 1) draw for a command, from the seed and the command alone, so
// every tree of a game draws the same noise for the same move
double gumbelNoise(std::uint64_t seed, const Command &command) {
    std::uint64_t x = seed;
    for (int v : {static_cast<int>(command.type), command.from, command.to, command.arg}) {
        // splitmix64, mixing in each field
        x += 0x9e3779b97f4a7c15ULL + static_cast<std::uint32_t>(v);
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x ^= x >> 31;
    }
    double u = (static_cast<double>(x >> 11) + 0.5) * 0x1.0p-53;
    return -std::log(-std::log(u));
}

double logit(float prior) { return std::log(std::max(prior, 1e-12f)); }

// Gumbel search's sigma(completed Q) for each root move: a visited move's
// mean value, an unvisited one's the mixed value (the root's own estimate
// blended with the visited moves' values, weighted by their priors), all
// rescaled to [0, 1] across the moves, then scaled up as visits grow
std::vector<double> sigmaQ(const std::vector<float> &prior,
                           const std::vector<int> &visits,
                           const std::vector<double> &total, double rootValue,
                           const SearchConfig &config) {
    std::size_t n = prior.size();
    double visited = 0, weighted = 0, weights = 0;
    int most = 0;
    for (std::size_t a = 0; a < n; a++) {
        if (visits[a] > 0) {
            double q = total[a] / visits[a];
            weighted += prior[a] * q;
            weights += prior[a];
            visited += visits[a];
            most = std::max(most, visits[a]);
        }
    }
    double mixed = weights > 0 ? (rootValue + visited * weighted / weights) / (visited + 1)
                               : rootValue;
    std::vector<double> q(n);
    for (std::size_t a = 0; a < n; a++) {
        q[a] = visits[a] > 0 ? total[a] / visits[a] : mixed;
    }
    if (n == 0) {
        return q;
    }
    auto [low, high] = std::minmax_element(q.begin(), q.end());
    double lo = *low, range = std::max(*high - *low, 1e-8);
    double scale = (config.gumbelVisitScale + most) * config.gumbelValueScale;
    for (double &v : q) {
        v = scale * (v - lo) / range;
    }
    return q;
}
} // namespace

void ScoreEvaluator::evaluate(const std::vector<EvalRequest> &requests,
                              std::vector<Evaluation> &results) {
    results.resize(requests.size());
    for (std::size_t i = 0; i < requests.size(); i++) {
        const EvalRequest &r = requests[i];
        const Game &game = *r.game;
        Evaluation &e = results[i];
        std::size_t n = r.moves ? r.moves->size() : 0;
        e.priors.assign(n, n ? 1.0f / static_cast<float>(n) : 0.0f);
        if (game.gameOver()) {
            e.value = game.winner == r.player ? 1.0f : -1.0f;
            continue;
        }
        int best = 0;
        for (std::size_t p = 0; p < game.players.size(); p++) {
            if (static_cast<int>(p) != r.player) {
                best = std::max(best, game.score(static_cast<int>(p)));
            }
        }
        e.value = static_cast<float>(
            std::tanh((game.score(r.player) - best) / scale));
    }
}

Command SearchResult::choose(double temperature, std::mt19937_64 &rng) const {
    if (moves.empty()) {
        return {CommandTypes::ENDTURN};
    }
    if (chosen >= 0) {
        return moves[chosen];
    }
    if (temperature <= 0) {
        auto most = std::max_element(visits.begin(), visits.end());
        return moves[most - visits.begin()];
    }
    std::vector<double> weights;
    for (int v : visits) {
        weights.push_back(std::pow(static_cast<double>(v), 1.0 / temperature));
    }
    std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
    return moves[pick(rng)];
}

BatchedSearch::BatchedSearch(const std::vector<const Game *> &gameList,
                             const std::vector<Beliefs> &beliefs,
                             const SearchConfig &config, std::uint64_t seed)
    : config(config), games(static_cast<int>(gameList.size())) {
    std::mt19937_64 seeds(seed);
    // Beyond the player's own turn, the opponent's stars and techs matter,
    // so their score has to be matched too
    Detail detail = config.horizon > 0 ? Detail::EVERYTHING : Detail::TILES_AND_UNITS;
    for (int i = 0; i < games; i++) {
        const Game &real = *gameList[i];
        if (config.fullInformation) {
            // the real game, but not its future dice
            Game copy = real;
            copy.settings.seed = seeds();
            copy.rng = seeds();
            Tree tree{.game = i, .rng = std::mt19937_64(seeds())};
            tree.nodes.reserve(config.simulations + 2);
            LegalMoves legal(copy);
            tree.nodes.push_back(makeNode(std::move(copy), legal, 0));
            trees.push_back(std::move(tree));
            continue;
        }
        for (int k = 0; k < config.samples; k++) {
            std::uint64_t s = seeds();
            std::optional<Game> sample = determinize(
                real, real.currentPlayer, beliefs.at(i), s, detail,
                config.determinizeTries);
            if (!sample && detail == Detail::EVERYTHING) {
                // no sample matched their score: the tiles and units at least
                sample = determinize(real, real.currentPlayer, beliefs.at(i), s,
                                     Detail::TILES_AND_UNITS,
                                     config.determinizeTries);
            }
            if (!sample) {
                continue;
            }
            Tree tree{.game = i, .rng = std::mt19937_64(seeds())};
            // at most one new node a simulation, so references stay valid
            tree.nodes.reserve(config.simulations + 2);
            LegalMoves legal(*sample);
            tree.nodes.push_back(makeNode(std::move(*sample), legal, 0));
            trees.push_back(std::move(tree));
        }
    }
    // drawn last, so a search without Gumbel noise draws as before
    for (int i = 0; i < games; i++) {
        gumbelSeeds.push_back(config.gumbel ? seeds() : 0);
    }
}

BatchedSearch::Node BatchedSearch::makeNode(Game game, LegalMoves legal,
                                            int crossed) const {
    Node node{.game = std::move(game), .legal = std::move(legal),
              .toMove = 0, .crossed = crossed};
    node.toMove = node.game.currentPlayer;
    if (node.game.gameOver()) {
        node.terminal = node.evaluated = true;
        node.value = node.game.winner == node.toMove ? 1.0f : -1.0f;
        return node;
    }
    if (crossed > config.horizon) {
        node.atHorizon = true;
        return node;
    }
    for (const Command &c : node.legal.commands()) {
        // giving up and doing nothing aren't worth searching
        if (c.type != CommandTypes::RESIGN && c.type != CommandTypes::STAY) {
            node.moves.push_back(c);
        }
    }
    std::size_t n = node.moves.size();
    node.prior.assign(n, n ? 1.0f / static_cast<float>(n) : 0.0f);
    node.visits.assign(n, 0);
    node.total.assign(n, 0);
    node.child.assign(n, -1);
    return node;
}

bool BatchedSearch::descend(Tree &tree) {
    while (tree.simulations < config.simulations) {
        tree.path.clear();
        int n = 0;
        while (true) {
            Node &node = tree.nodes[n];
            if (!node.evaluated) {
                tree.waiting = n;
                return true;
            }
            if (node.terminal || node.atHorizon || node.moves.empty()) {
                backup(tree, n, node.value);
                tree.simulations++;
                break;
            }
            int best = 0;
            if (n == 0 && config.gumbel) {
                best = gumbelRootMove(tree);
            } else {
                // PUCT: the most promising move, balancing value and prior
                double scale = config.exploration *
                               std::sqrt(std::max(1, node.parentVisits));
                // what a move not tried yet counts as: the node's value so far
                // (its own evaluation and everything backed up through it), less
                // a reduction growing with the prior already explored
                double explored = 0, sum = node.value;
                for (std::size_t a = 0; a < node.moves.size(); a++) {
                    if (node.visits[a]) {
                        explored += node.prior[a];
                        sum += node.total[a];
                    }
                }
                double untried = sum / (1 + node.parentVisits) -
                                 config.fpuReduction * std::sqrt(explored);
                double bestScore = -1e9;
                for (std::size_t a = 0; a < node.moves.size(); a++) {
                    double q = node.visits[a]            ? node.total[a] / node.visits[a]
                               : config.firstPlayUrgency ? untried
                                                         : 0.0;
                    double u = scale * node.prior[a] / (1 + node.visits[a]);
                    if (q + u > bestScore) {
                        bestScore = q + u;
                        best = static_cast<int>(a);
                    }
                }
            }
            if (node.child[best] < 0) {
                Game game = node.game;
                LegalMoves legal = node.legal;
                legal.apply(game, node.moves[best]);
                bool newTurn = game.currentPlayer != node.toMove;
                int crossed = node.crossed + newTurn;
                tree.nodes.push_back(makeNode(std::move(game), std::move(legal), crossed));
                // push_back may not move nodes (reserved), but re-fetch anyway
                tree.nodes[n].child[best] = static_cast<int>(tree.nodes.size()) - 1;
            }
            tree.path.push_back({n, best});
            n = tree.nodes[n].child[best];
        }
    }
    return false;
}

void BatchedSearch::backup(Tree &tree, int leaf, float value) {
    int mover = tree.nodes[leaf].toMove;
    for (auto [n, a] : tree.path) {
        Node &node = tree.nodes[n];
        // the value is for the leaf's player to move; flip it for the other
        double v = node.toMove == mover ? value : -value;
        node.visits[a]++;
        node.total[a] += v;
        node.parentVisits++;
    }
    tree.path.clear();
}

void BatchedSearch::expand(Tree &tree, int n, const Evaluation &evaluation) {
    Node &node = tree.nodes[n];
    node.evaluated = true;
    node.value = evaluation.value;
    if (!node.moves.empty()) {
        if (evaluation.priors.size() == node.moves.size()) {
            double sum = 0;
            for (float p : evaluation.priors) {
                sum += std::max(0.0f, p);
            }
            for (std::size_t a = 0; a < node.moves.size(); a++) {
                node.prior[a] = sum > 0
                                    ? static_cast<float>(std::max(0.0f, evaluation.priors[a]) / sum)
                                    : 1.0f / static_cast<float>(node.moves.size());
            }
        }
        if (n == 0 && config.gumbel) {
            startGumbel(tree);
        } else if (n == 0 && config.rootNoise) {
            addRootNoise(tree);
        }
    }
    backup(tree, n, node.value);
    tree.simulations++;
}

void BatchedSearch::startGumbel(Tree &tree) {
    const Node &root = tree.nodes[0];
    std::size_t m = root.moves.size();
    tree.gumbel.assign(m, 0);
    if (config.rootNoise) {
        for (std::size_t a = 0; a < m; a++) {
            tree.gumbel[a] = gumbelNoise(gumbelSeeds[tree.game], root.moves[a]);
        }
    }
    // Gumbel-top-k: k moves sampled from the policy without replacement
    std::vector<int> order(m);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return tree.gumbel[a] + logit(root.prior[a]) > tree.gumbel[b] + logit(root.prior[b]);
    });
    // no more than the simulations left can visit (the root's own
    // evaluation, if not done yet, is one of them)
    int left = config.simulations - tree.simulations - (root.evaluated && tree.simulations == 0);
    std::size_t k = static_cast<std::size_t>(std::max(1, std::min(config.gumbelCandidates, left)));
    order.resize(std::min(m, k));
    tree.remaining = std::move(order);
    // no phase yet
    tree.phaseTarget = -1;
}

int BatchedSearch::gumbelRootMove(Tree &tree) {
    const Node &root = tree.nodes[0];
    std::vector<int> &remaining = tree.remaining;
    if (remaining.empty()) {
        startGumbel(tree);
    }
    while (remaining.size() > 1) {
        // the candidate furthest from this phase's visits
        int pick = -1;
        for (int a : remaining) {
            if (root.visits[a] < tree.phaseTarget &&
                (pick < 0 || root.visits[a] < root.visits[pick])) {
                pick = a;
            }
        }
        if (pick >= 0) {
            return pick;
        }
        if (tree.phaseTarget >= 0) {
            // the phase is done: the better half stays in
            std::vector<double> sigma =
                sigmaQ(root.prior, root.visits, root.total, root.value, config);
            std::stable_sort(remaining.begin(), remaining.end(), [&](int a, int b) {
                return tree.gumbel[a] + logit(root.prior[a]) + sigma[a] >
                       tree.gumbel[b] + logit(root.prior[b]) + sigma[b];
            });
            remaining.resize((remaining.size() + 1) / 2);
            if (remaining.size() == 1) {
                break;
            }
        }
        // the next phase: the budget left, split evenly over the candidates
        // and the phases still to come
        int left = std::max(1, config.simulations - tree.simulations);
        int phases = static_cast<int>(std::ceil(std::log2(remaining.size())));
        int each = std::max(1, left / (static_cast<int>(remaining.size()) * phases));
        int least = root.visits[remaining[0]];
        for (int a : remaining) {
            least = std::min(least, root.visits[a]);
        }
        tree.phaseTarget = least + each;
    }
    // the last one standing takes whatever budget is left
    return remaining[0];
}

void BatchedSearch::addRootNoise(Tree &tree) {
    Node &node = tree.nodes[0];
    std::gamma_distribution<double> gamma(config.noiseAlpha, 1.0);
    std::vector<double> noise(node.moves.size());
    double sum = 0;
    for (double &x : noise) {
        x = gamma(tree.rng);
        sum += x;
    }
    for (std::size_t a = 0; a < node.moves.size(); a++) {
        node.prior[a] = static_cast<float>(
            (1 - config.noiseWeight) * node.prior[a] +
            config.noiseWeight * (sum > 0 ? noise[a] / sum : 0));
    }
}

bool BatchedSearch::reuse(const Command &played, const SearchConfig &next) {
    if (games != 1 || !done()) {
        return false;
    }
    // every tree must have the child, evaluated, with the same player to move
    std::vector<int> roots;
    for (const Tree &tree : trees) {
        const Node &root = tree.nodes[0];
        auto it = std::find(root.moves.begin(), root.moves.end(), played);
        if (it == root.moves.end()) {
            return false;
        }
        int child = root.child[it - root.moves.begin()];
        if (child < 0) {
            return false;
        }
        const Node &c = tree.nodes[child];
        if (!c.evaluated || c.terminal || c.atHorizon || c.toMove != root.toMove ||
            c.crossed != 0) {
            return false;
        }
        roots.push_back(child);
    }
    config = next;
    if (config.gumbel) {
        // a new decision, new noise
        gumbelSeeds[0] = trees[0].rng();
    }
    for (std::size_t t = 0; t < trees.size(); t++) {
        Tree &tree = trees[t];
        // the subtree, renumbered from 0 in breadth-first order
        std::vector<int> order{roots[t]};
        std::vector<int> index(tree.nodes.size(), -1);
        index[roots[t]] = 0;
        for (std::size_t k = 0; k < order.size(); k++) {
            for (int c : tree.nodes[order[k]].child) {
                if (c >= 0 && index[c] < 0) {
                    index[c] = static_cast<int>(order.size());
                    order.push_back(c);
                }
            }
        }
        std::vector<Node> kept;
        kept.reserve(std::max<std::size_t>(order.size(), config.simulations + 2) +
                     config.simulations + 2);
        for (int n : order) {
            Node node = std::move(tree.nodes[n]);
            for (int &c : node.child) {
                c = c >= 0 ? index[c] : -1;
            }
            kept.push_back(std::move(node));
        }
        tree.nodes = std::move(kept);
        // the visits kept count towards the next decision's simulations
        tree.simulations = tree.nodes[0].parentVisits + 1;
        tree.path.clear();
        tree.waiting = -1;
        tree.remaining.clear();
        if (config.gumbel) {
            if (!tree.nodes[0].moves.empty()) {
                startGumbel(tree);
            }
        } else if (config.rootNoise) {
            addRootNoise(tree);
        }
    }
    return true;
}

bool BatchedSearch::done() const {
    return std::all_of(trees.begin(), trees.end(), [&](const Tree &t) {
        return t.waiting < 0 && t.simulations >= config.simulations;
    });
}

const std::vector<EvalRequest> &BatchedSearch::pending() {
    requests.clear();
    requestTree.clear();
    for (std::size_t i = 0; i < trees.size(); i++) {
        Tree &tree = trees[i];
        if (tree.waiting < 0 && !descend(tree)) {
            continue;
        }
        const Node &node = tree.nodes[tree.waiting];
        requests.push_back({&node.game, node.toMove,
                            node.atHorizon ? nullptr : &node.moves,
                            config.fullInformation});
        requestTree.push_back(static_cast<int>(i));
    }
    return requests;
}

void BatchedSearch::submit(const std::vector<Evaluation> &results) {
    for (std::size_t i = 0; i < requestTree.size() && i < results.size(); i++) {
        Tree &tree = trees[requestTree[i]];
        expand(tree, tree.waiting, results[i]);
        tree.waiting = -1;
    }
    requests.clear();
    requestTree.clear();
}

SearchResult BatchedSearch::result(int game) const {
    SearchResult out;
    auto before = [](const Command &x, const Command &y) {
        return std::tie(x.type, x.from, x.to, x.arg) <
               std::tie(y.type, y.from, y.to, y.arg);
    };
    std::map<Command, std::size_t, decltype(before)> index(before);
    std::vector<double> totals;
    // for Gumbel search: the priors (averaged over the trees with the
    // move), the root's own value (averaged), and the candidates still in
    std::vector<double> priorSum;
    std::vector<int> priorCount;
    std::vector<char> standing;
    double rootValue = 0;
    for (const Tree &tree : trees) {
        if (tree.game != game) {
            continue;
        }
        out.samples++;
        const Node &root = tree.nodes[0];
        rootValue += root.value;
        for (std::size_t a = 0; a < root.moves.size(); a++) {
            auto [it, added] = index.try_emplace(root.moves[a], out.moves.size());
            if (added) {
                out.moves.push_back(root.moves[a]);
                out.visits.push_back(0);
                totals.push_back(0);
                priorSum.push_back(0);
                priorCount.push_back(0);
                standing.push_back(0);
            }
            out.visits[it->second] += root.visits[a];
            totals[it->second] += root.total[a];
            priorSum[it->second] += root.prior[a];
            priorCount[it->second]++;
        }
        for (int a : tree.remaining) {
            standing[index.at(root.moves[a])] = 1;
        }
    }
    const std::size_t n = out.moves.size();
    for (std::size_t i = 0; i < n; i++) {
        out.values.push_back(out.visits[i] ? totals[i] / out.visits[i] : 0);
    }
    if (!config.gumbel || out.samples == 0 || n == 0) {
        double sum = std::accumulate(out.visits.begin(), out.visits.end(), 0.0);
        for (int v : out.visits) {
            out.policy.push_back(sum > 0 ? v / sum : 1.0 / static_cast<double>(n));
        }
        return out;
    }
    std::vector<float> prior(n);
    for (std::size_t i = 0; i < n; i++) {
        prior[i] = static_cast<float>(priorSum[i] / priorCount[i]);
    }
    std::vector<double> sigma = sigmaQ(prior, out.visits, totals, rootValue / out.samples, config);
    // the improved policy: softmax(logit + sigma(completed Q))
    std::vector<double> score(n);
    for (std::size_t i = 0; i < n; i++) {
        score[i] = logit(prior[i]) + sigma[i];
    }
    double top = *std::max_element(score.begin(), score.end()), sum = 0;
    for (double s : score) {
        out.policy.push_back(std::exp(s - top));
        sum += out.policy.back();
    }
    for (double &p : out.policy) {
        p /= sum;
    }
    // the move played: the best of the last ones standing, with the noise
    // that picked the candidates
    double best = -1e300;
    for (std::size_t i = 0; i < n; i++) {
        double g = config.rootNoise ? gumbelNoise(gumbelSeeds[game], out.moves[i]) : 0;
        if (standing[i] && g + score[i] > best) {
            best = g + score[i];
            out.chosen = static_cast<int>(i);
        }
    }
    return out;
}

void run(BatchedSearch &search, Evaluator &evaluator) {
    std::vector<Evaluation> results;
    while (!search.done()) {
        const std::vector<EvalRequest> &requests = search.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        search.submit(results);
    }
}

SearchResult search(const Game &game, const Beliefs &beliefs,
                    Evaluator &evaluator, const SearchConfig &config,
                    std::uint64_t seed) {
    BatchedSearch batched({&game}, {beliefs}, config, seed);
    run(batched, evaluator);
    return batched.result(0);
}

std::vector<Command> playTurn(Game &game, Evaluator &evaluator,
                              const SearchConfig &config, std::mt19937_64 &rng,
                              bool networkBeliefs) {
    std::vector<Command> played;
    int me = game.currentPlayer;
    // once for the turn, as self-play does
    std::optional<Beliefs> beliefs;
    if (networkBeliefs) {
        std::vector<Evaluation> results;
        evaluator.evaluate({EvalRequest{.game = &game,
                                        .player = me,
                                        .moves = nullptr,
                                        .wantsBeliefs = true}},
                           results);
        beliefs = std::move(results.at(0).beliefs);
    }
    if (!beliefs) {
        beliefs = priorBeliefs(makeView(game, me));
    }
    // a turn always ends, but cap it in case
    while (!game.gameOver() && game.currentPlayer == me && played.size() < 200) {
        SearchResult result = search(game, *beliefs, evaluator, config, rng());
        Command command = result.choose(0, rng);
        if (result.moves.empty() || !game.isLegal(command)) {
            command = game.legalCommands().front();
        }
        game.apply(command);
        played.push_back(command);
    }
    if (!game.gameOver() && game.currentPlayer == me) {
        game.apply({CommandTypes::ENDTURN});
        played.push_back({CommandTypes::ENDTURN});
    }
    return played;
}
