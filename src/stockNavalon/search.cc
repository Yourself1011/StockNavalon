#include "search.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

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
            // PUCT: the most promising move, balancing value and prior
            double scale = config.exploration *
                           std::sqrt(std::max(1, node.parentVisits));
            int best = 0;
            double bestScore = -1e9;
            for (std::size_t a = 0; a < node.moves.size(); a++) {
                double q = node.visits[a] ? node.total[a] / node.visits[a] : 0;
                double u = scale * node.prior[a] / (1 + node.visits[a]);
                if (q + u > bestScore) {
                    bestScore = q + u;
                    best = static_cast<int>(a);
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
        if (n == 0 && config.rootNoise) {
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
    }
    backup(tree, n, node.value);
    tree.simulations++;
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
                            node.atHorizon ? nullptr : &node.moves});
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
    for (const Tree &tree : trees) {
        if (tree.game != game) {
            continue;
        }
        out.samples++;
        const Node &root = tree.nodes[0];
        for (std::size_t a = 0; a < root.moves.size(); a++) {
            auto [it, added] = index.try_emplace(root.moves[a], out.moves.size());
            if (added) {
                out.moves.push_back(root.moves[a]);
                out.visits.push_back(0);
                totals.push_back(0);
            }
            out.visits[it->second] += root.visits[a];
            totals[it->second] += root.total[a];
        }
    }
    for (std::size_t i = 0; i < out.moves.size(); i++) {
        out.values.push_back(out.visits[i] ? totals[i] / out.visits[i] : 0);
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
