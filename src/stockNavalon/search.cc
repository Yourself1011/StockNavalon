#include "search.h"
#include "profile.h"
#include "view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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

std::vector<double> regularizedPolicy(const std::vector<double> &prior,
                                      const std::vector<double> &q, double lambda) {
    const std::size_t n = prior.size();
    std::vector<double> out(n, 0.0);
    double top = -1e300;
    for (std::size_t a = 0; a < n; a++) {
        if (prior[a] > 0) {
            top = std::max(top, q[a]);
        }
    }
    if (n == 0 || top == -1e300) {
        return out;
    }
    if (!(lambda > 0)) {
        // all on the best
        for (std::size_t a = 0; a < n; a++) {
            if (prior[a] > 0 && q[a] == top) {
                out[a] = 1;
                break;
            }
        }
        return out;
    }
    // alpha between where the sum is at least 1 and at most 1 (the sum
    // falls as alpha grows), found by bisection
    double low = -1e300, high = top + lambda;
    for (std::size_t a = 0; a < n; a++) {
        if (prior[a] > 0) {
            low = std::max(low, q[a] + lambda * prior[a]);
        }
    }
    auto sum = [&](double alpha) {
        double s = 0;
        for (std::size_t a = 0; a < n; a++) {
            if (prior[a] > 0) {
                s += lambda * prior[a] / (alpha - q[a]);
            }
        }
        return s;
    };
    for (int i = 0; i < 50 && high - low > 1e-12; i++) {
        double mid = 0.5 * (low + high);
        (sum(mid) > 1 ? low : high) = mid;
    }
    double alpha = high, total = 0;
    for (std::size_t a = 0; a < n; a++) {
        if (prior[a] > 0) {
            out[a] = lambda * prior[a] / (alpha - q[a]);
            total += out[a];
        }
    }
    for (double &p : out) {
        p /= total;
    }
    return out;
}

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
        // over, with the whole lead, now
        node.utility = utilityOf(node.value, node.value, 0);
        node.backedValue = node.value;
        node.backedUtility = node.utility;
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
    node.utilityTotal.assign(n, 0);
    node.child.assign(n, -1);
    return node;
}

bool BatchedSearch::descend(Tree &tree) {
    profiling::Timer timer(profiling::DESCEND);
    while (budgetLeft(tree)) {
        int n = 0;
        if (tree.resume >= 0) {
            // carrying on below a sampled turn drawn again
            n = std::exchange(tree.resume, -1);
        } else {
            tree.path.clear();
        }
        while (true) {
            Node &node = tree.nodes[n];
            if (!node.evaluated || node.terminal || node.atHorizon || node.moves.empty()) {
                noteLeaf(tree, n);
            }
            if (!node.evaluated) {
                tree.waiting = n;
                return true;
            }
            if (node.terminal || node.atHorizon || node.moves.empty()) {
                backup(tree, n, node.value, node.utility);
                tree.simulations++;
                break;
            }
            if (n != 0 && sampledTurns(node)) {
                int next = turnMove(tree, n);
                if (next == TURN_WAITING) {
                    return true;
                }
                if (next == TURN_DONE) {
                    break;
                }
                n = next;
                continue;
            }
            int best = 0;
            if (n == 0 && config.gumbel) {
                best = gumbelRootMove(tree);
            } else {
                // PUCT: the most promising move, balancing value and prior
                double scale = config.exploration *
                               std::sqrt(std::max(1, node.parentVisits));
                // what a move not tried yet counts as: the node's value so far
                // (its own evaluation and everything backed up through it,
                // sampled turns included), less a reduction growing with the
                // prior already explored
                double explored = 0, sum = node.utility;
                for (std::size_t a = 0; a < node.moves.size(); a++) {
                    if (node.visits[a]) {
                        explored += node.prior[a];
                        sum += node.utilityTotal[a];
                    }
                }
                for (double t : node.turnUtilityTotal) {
                    sum += t;
                }
                double estimate = config.backup == SearchConfig::Backup::MEAN
                                      ? sum / (1 + node.parentVisits)
                                      : node.backedUtility;
                double untried = estimate - config.fpuReduction * std::sqrt(explored);
                double bestScore = -1e9;
                for (std::size_t a = 0; a < node.moves.size(); a++) {
                    double q = node.visits[a]
                                   ? edgeUtility(tree, node, static_cast<int>(a))
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
                profiling::Timer nodeTimer(profiling::NEW_NODE);
                profiling::Laps laps;
                Game game = node.game;
                LegalMoves legal = node.legal;
                laps.mark(profiling::NODE_COPY);
                // the parent's game is the "before": no copy of its own
                // (LegalMoves::apply would make one)
                game.applyUnchecked(node.moves[best]);
                legal.update(node.game, game);
                laps.mark(profiling::NODE_APPLY);
                bool newTurn = game.currentPlayer != node.toMove;
                int crossed = node.crossed + newTurn;
                tree.nodes.push_back(makeNode(std::move(game), std::move(legal), crossed));
                laps.mark(profiling::NODE_MAKE);
                // push_back may not move nodes (reserved), but re-fetch anyway
                tree.nodes[n].child[best] = static_cast<int>(tree.nodes.size()) - 1;
            }
            tree.path.push_back({n, best});
            n = tree.nodes[n].child[best];
        }
    }
    return false;
}

bool BatchedSearch::budgetLeft(const Tree &tree) const {
    // In evaluations, simulations count too: one ending at a result already
    // known (a won game, a horizon already scored) costs no evaluation, so
    // a search down such a line would otherwise never end. They rarely bind:
    // a simulation usually costs at least one evaluation.
    return tree.simulations < config.simulations &&
           (!config.countEvaluations || tree.evaluations < config.simulations);
}

int BatchedSearch::simulationsLeft(const Tree &tree) const {
    if (!config.countEvaluations) {
        return config.simulations - tree.simulations;
    }
    // what a simulation has cost so far
    double each = tree.simulations > 0
                      ? std::max(1.0, static_cast<double>(tree.evaluations) / tree.simulations)
                      : 1.0;
    return std::min(config.simulations - tree.simulations,
                    static_cast<int>((config.simulations - tree.evaluations) / each));
}

void BatchedSearch::noteLeaf(const Tree &tree, int n) {
    const Node &node = tree.nodes[n];
    depthStats.simulations++;
    depthStats.depth += static_cast<std::int64_t>(tree.path.size());
    depthStats.reachedOpponent += node.crossed > 0;
    depthStats.turns += node.crossed;
}

bool BatchedSearch::sampledTurns(const Node &node) const {
    return config.turnSearch && (node.crossed > 0 || node.parentVisits < config.commandVisits);
}

int BatchedSearch::turnMove(Tree &tree, int n) {
    Node &node = tree.nodes[n];
    int drawn = std::accumulate(node.turnCount.begin(), node.turnCount.end(), 0);
    double allowed = config.turnWidening *
                     std::pow(node.parentVisits + 1.0, config.turnWideningPower);
    if (drawn == 0 || drawn < allowed) {
        // a new turn: its first command from the node's own priors, so with
        // no evaluation of its own
        tree.sampling = Sampling{.node = n, .game = node.game, .legal = node.legal};
        Sampling &s = *tree.sampling;
        bool replies = repliesAt(tree, node);
        if (replies && drawn == 0) {
            // the most likely reply first
            s.likely = true;
            int next = sampleStep(tree, likelyCommand(tree, node.moves, node.prior));
            return next == TURN_SAMPLING ? TURN_WAITING : next;
        }
        if (replies && config.replies == SearchConfig::Replies::DISCREPANCY &&
            !node.deviations.empty()) {
            // the deviation giving up the least: the earlier turn to its
            // step, then the runner-up there, then the most likely commands
            auto it = std::min_element(node.deviations.begin(), node.deviations.end(),
                                       [](const Deviation &x, const Deviation &y) {
                                           return x.cost < y.cost;
                                       });
            Deviation d = *it;
            *it = node.deviations.back();
            node.deviations.pop_back();
            s.likely = true;
            s.cost = d.cost;
            s.from = d.step + 1;
            std::vector<Command> prefix(node.turns[d.turn].begin(),
                                        node.turns[d.turn].begin() + d.step);
            for (const Command &c : prefix) {
                int next = sampleStep(tree, c);
                if (next != TURN_SAMPLING) {
                    return next;
                }
            }
            int next = sampleStep(tree, d.command);
            return next == TURN_SAMPLING ? TURN_WAITING : next;
        }
        Command first = sampleCommand(tree, node.moves, node.prior);
        int next = sampleStep(tree, first);
        return next == TURN_SAMPLING ? TURN_WAITING : next;
    }
    // PUCT over the turns drawn, by their weights as priors
    double scale = config.exploration * std::sqrt(std::max(1, node.parentVisits));
    double sum = node.utility;
    for (double t : node.turnUtilityTotal) {
        sum += t;
    }
    double estimate = config.backup == SearchConfig::Backup::MEAN
                          ? sum / (1 + node.parentVisits)
                          : node.backedUtility;
    double weights = std::accumulate(node.turnWeight.begin(), node.turnWeight.end(), 0.0);
    int best = 0;
    double bestScore = -1e9;
    for (std::size_t e = 0; e < node.turns.size(); e++) {
        double q = node.turnVisits[e] ? edgeUtility(tree, node, -static_cast<int>(e) - 1)
                                      : estimate;
        double u = scale * node.turnWeight[e] / weights / (1 + node.turnVisits[e]);
        if (q + u > bestScore) {
            bestScore = q + u;
            best = static_cast<int>(e);
        }
    }
    tree.path.push_back({n, -(best + 1)});
    return node.turnChild[best];
}

int BatchedSearch::sampleStep(Tree &tree, const Command &command) {
    Sampling &s = *tree.sampling;
    int player = tree.nodes[s.node].toMove;
    s.legal.apply(s.game, command);
    s.commands.push_back(command);
    if (!s.game.gameOver() && s.game.currentPlayer == player &&
        static_cast<int>(s.commands.size()) >= config.maxTurnCommands) {
        // long enough
        Command end{CommandTypes::ENDTURN};
        s.legal.apply(s.game, end);
        s.commands.push_back(end);
    }
    if (s.game.gameOver() || s.game.currentPlayer != player) {
        return finishTurn(tree);
    }
    s.moves.clear();
    for (const Command &c : s.legal.commands()) {
        if (c.type != CommandTypes::RESIGN && c.type != CommandTypes::STAY) {
            s.moves.push_back(c);
        }
    }
    return TURN_SAMPLING;
}

int BatchedSearch::finishTurn(Tree &tree) {
    Sampling s = std::move(*tree.sampling);
    tree.sampling.reset();
    const int n = s.node;
    {
        Node &node = tree.nodes[n];
        for (std::size_t e = 0; e < node.turns.size(); e++) {
            if (node.turns[e] == s.commands) {
                // drawn before: it counts twice, and the simulation goes on
                // below it
                node.turnCount[e]++;
                node.turnWeight[e] += s.likely ? std::exp(-s.cost) : 1.0;
                tree.path.push_back({n, -(static_cast<int>(e) + 1)});
                return node.turnChild[e];
            }
        }
    }
    int crossed = tree.nodes[n].crossed + (s.game.currentPlayer != tree.nodes[n].toMove);
    tree.nodes.push_back(makeNode(std::move(s.game), std::move(s.legal), crossed));
    int child = static_cast<int>(tree.nodes.size()) - 1;
    Node &node = tree.nodes[n];
    if (s.likely) {
        // its runners-up after where it left the turn it came from
        int turn = static_cast<int>(node.turns.size());
        for (Deviation &d : s.runnersUp) {
            if (d.step >= s.from) {
                node.deviations.push_back({turn, d.step, d.command, s.cost + d.cost});
            }
        }
    }
    node.turnWeight.push_back(s.likely ? std::exp(-s.cost) : 1.0);
    node.turns.push_back(std::move(s.commands));
    node.turnCount.push_back(1);
    node.turnVisits.push_back(0);
    node.turnTotal.push_back(0);
    node.turnUtilityTotal.push_back(0);
    node.turnChild.push_back(child);
    tree.path.push_back({n, -static_cast<int>(node.turns.size())});
    // the new position is where this simulation ends
    noteLeaf(tree, child);
    const Node &c = tree.nodes[child];
    if (!c.evaluated) {
        tree.waiting = child;
        return TURN_WAITING;
    }
    backup(tree, child, c.value, c.utility);
    tree.simulations++;
    return TURN_DONE;
}

Command BatchedSearch::sampleCommand(Tree &tree, const std::vector<Command> &moves,
                                     const std::vector<float> &priors) {
    std::vector<double> weights(moves.size(), 0.0);
    double sum = 0;
    for (std::size_t a = 0; a < moves.size(); a++) {
        double p = a < priors.size() ? std::max(0.0f, priors[a]) : 0.0;
        weights[a] = config.turnTemperature == 1.0 ? p : std::pow(p, 1.0 / config.turnTemperature);
        sum += weights[a];
    }
    if (!(sum > 0)) {
        weights.assign(moves.size(), 1.0);
    }
    std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
    return moves[pick(tree.rng)];
}

Command BatchedSearch::likelyCommand(Tree &tree, const std::vector<Command> &moves,
                                     const std::vector<float> &priors) {
    Sampling &s = *tree.sampling;
    int best = -1, second = -1;
    auto prior = [&](int a) {
        return a < static_cast<int>(priors.size()) ? std::max(0.0f, priors[a]) : 0.0f;
    };
    for (int a = 0; a < static_cast<int>(moves.size()); a++) {
        if (best < 0 || prior(a) > prior(best)) {
            second = best;
            best = a;
        } else if (second < 0 || prior(a) > prior(second)) {
            second = a;
        }
    }
    if (second >= 0 && prior(second) > 0) {
        s.runnersUp.push_back({0, static_cast<int>(s.commands.size()), moves[second],
                               std::log(prior(best)) - std::log(prior(second))});
    }
    return moves[best];
}

bool BatchedSearch::repliesAt(const Tree &tree, const Node &node) const {
    return config.replies != SearchConfig::Replies::SAMPLED &&
           node.toMove != tree.nodes[0].toMove;
}

double BatchedSearch::edgeUtility(const Tree &tree, const Node &node, int edge) const {
    bool move = edge >= 0;
    int e = move ? edge : -edge - 1;
    if (config.backup == SearchConfig::Backup::MEAN) {
        return move ? node.utilityTotal[e] / node.visits[e]
                    : node.turnUtilityTotal[e] / node.turnVisits[e];
    }
    const Node &child = tree.nodes[move ? node.child[e] : node.turnChild[e]];
    return child.toMove == node.toMove ? child.backedUtility : -child.backedUtility;
}

std::vector<double> BatchedSearch::rootUtilityTotals(const Tree &tree) const {
    const Node &root = tree.nodes[0];
    if (config.backup == SearchConfig::Backup::MEAN) {
        return root.utilityTotal;
    }
    std::vector<double> out(root.moves.size(), 0.0);
    for (std::size_t a = 0; a < root.moves.size(); a++) {
        if (root.visits[a]) {
            out[a] = edgeUtility(tree, root, static_cast<int>(a)) * root.visits[a];
        }
    }
    return out;
}

void BatchedSearch::refresh(Tree &tree, int n) {
    Node &node = tree.nodes[n];
    if (!node.evaluated || node.terminal || node.atHorizon) {
        return;
    }
    // The edges a simulation would take here now (sampled turns or
    // commands) are weighed; visits through the others (a turn the policy
    // completed before the node went command by command) count as plain
    // samples, like the node's own evaluation.
    bool turns = n != 0 && sampledTurns(node);
    std::size_t count = turns ? node.turns.size() : node.moves.size();
    double plainValue = node.value, plainUtility = node.utility;
    int plain = 1;
    auto childOf = [&](bool turn, std::size_t e) -> const Node & {
        return tree.nodes[turn ? node.turnChild[e] : node.child[e]];
    };
    for (std::size_t e = 0; e < (turns ? node.moves.size() : node.turns.size()); e++) {
        int v = turns ? node.visits[e] : node.turnVisits[e];
        if (v > 0) {
            const Node &c = childOf(!turns, e);
            double sign = c.toMove == node.toMove ? 1.0 : -1.0;
            plainValue += v * sign * c.backedValue;
            plainUtility += v * sign * c.backedUtility;
            plain += v;
        }
    }
    // the weighed edges: prior, visits, value and utility (for toMove)
    std::vector<double> prior, q, value;
    std::vector<int> visits;
    double unvisited = 0, weightSum = 0;
    int visited = 0;
    if (turns) {
        weightSum = std::accumulate(node.turnWeight.begin(), node.turnWeight.end(), 0.0);
    }
    for (std::size_t e = 0; e < count; e++) {
        int v = turns ? node.turnVisits[e] : node.visits[e];
        double p = turns ? (weightSum > 0 ? node.turnWeight[e] / weightSum : 0.0)
                         : node.prior[e];
        if (v == 0) {
            unvisited += p;
            continue;
        }
        const Node &c = childOf(turns, e);
        double sign = c.toMove == node.toMove ? 1.0 : -1.0;
        prior.push_back(p);
        q.push_back(sign * c.backedUtility);
        value.push_back(sign * c.backedValue);
        visits.push_back(v);
        visited += v;
    }
    if (visited == 0) {
        node.backedValue = static_cast<float>(plainValue / plain);
        node.backedUtility = static_cast<float>(plainUtility / plain);
        return;
    }
    std::vector<double> weight;
    bool weighed = config.backup == SearchConfig::Backup::PBAR ||
                   node.toMove != tree.nodes[0].toMove;
    if (weighed) {
        // the edges not tried yet as one, at what an untried move counts as
        // (first-play urgency, as in descend)
        double explored = 1 - unvisited;
        double untried = config.firstPlayUrgency
                             ? node.backedUtility - config.fpuReduction * std::sqrt(explored)
                             : 0.0;
        prior.push_back(unvisited);
        q.push_back(untried);
        double lambda = config.exploration * std::sqrt(static_cast<double>(visited)) /
                        (static_cast<double>(count) + visited);
        weight = regularizedPolicy(prior, q, lambda);
        weight.pop_back();
        prior.pop_back();
        q.pop_back();
    } else {
        weight.assign(visits.begin(), visits.end());
    }
    // the weights over the visited edges, then blended with the plain
    // samples as visits would be
    double total = std::accumulate(weight.begin(), weight.end(), 0.0);
    double meanValue = 0, meanUtility = 0;
    for (std::size_t i = 0; i < weight.size(); i++) {
        double w = total > 0 ? weight[i] / total : 1.0 / weight.size();
        meanValue += w * value[i];
        meanUtility += w * q[i];
    }
    node.backedValue = static_cast<float>((plainValue + visited * meanValue) / (plain + visited));
    node.backedUtility =
        static_cast<float>((plainUtility + visited * meanUtility) / (plain + visited));
}

float BatchedSearch::utilityOf(float value, float scoreLead, float turnsLeft) const {
    return static_cast<float>(value + config.scoreUtility * scoreLead -
                              config.winSooner * value * turnsLeft);
}

void BatchedSearch::backup(Tree &tree, int leaf, float value, float utility) {
    int mover = tree.nodes[leaf].toMove;
    if (!tree.path.empty()) {
        // where the simulation ended, for the root move it took
        const Node &root = tree.nodes[0], &end = tree.nodes[leaf];
        if (tree.leafCounts.size() != root.moves.size()) {
            tree.leafCounts.assign(root.moves.size(), {});
            tree.leafValues.assign(root.moves.size(), {});
        }
        int kind = end.terminal                 ? LEAF_GAME_OVER
                   : end.crossed == 0           ? LEAF_OWN_TURN
                   : end.toMove != root.toMove  ? LEAF_OPPONENT_TURN
                                                : LEAF_LATER_TURN;
        int a = tree.path[0].second;
        tree.leafCounts[a][kind]++;
        tree.leafValues[a][kind] += end.toMove == root.toMove ? value : -value;
    }
    for (auto [n, a] : tree.path) {
        Node &node = tree.nodes[n];
        // both are for the leaf's player to move; flip them for the other
        bool same = node.toMove == mover;
        if (a >= 0) {
            node.visits[a]++;
            node.total[a] += same ? value : -value;
            node.utilityTotal[a] += same ? utility : -utility;
        } else {
            // a sampled turn (turn search)
            int e = -a - 1;
            node.turnVisits[e]++;
            node.turnTotal[e] += same ? value : -value;
            node.turnUtilityTotal[e] += same ? utility : -utility;
        }
        node.parentVisits++;
    }
    if (config.backup != SearchConfig::Backup::MEAN) {
        // from the leaf up, each node's value from its children's
        for (auto it = tree.path.rbegin(); it != tree.path.rend(); ++it) {
            refresh(tree, it->first);
        }
    }
    tree.path.clear();
}

void BatchedSearch::expand(Tree &tree, int n, const Evaluation &evaluation) {
    Node &node = tree.nodes[n];
    node.evaluated = true;
    node.value = evaluation.value;
    node.utility = utilityOf(evaluation.value, evaluation.scoreLead, evaluation.turnsLeft);
    node.backedValue = node.value;
    node.backedUtility = node.utility;
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
    backup(tree, n, node.value, node.utility);
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
    int left = simulationsLeft(tree) - (root.evaluated && tree.simulations == 0);
    std::size_t k = static_cast<std::size_t>(std::max(1, std::min(config.gumbelCandidates, left)));
    k = std::min(m, k);
    // some of them picked at random from the rest instead, for exploration
    std::size_t explore = config.exploreCandidates > 0 && k > 1 && m > k
                              ? std::min<std::size_t>(config.exploreCandidates, k - 1)
                              : 0;
    tree.exploring.assign(m, 0);
    if (explore > 0) {
        std::vector<int> rest(order.begin() + static_cast<std::ptrdiff_t>(k - explore), order.end());
        // a random order of the rest, the same for every tree of the game
        std::uint64_t seed = gumbelSeeds[tree.game] ^ 0x5bd1e9955bd1e995ULL;
        std::stable_sort(rest.begin(), rest.end(), [&](int a, int b) {
            return gumbelNoise(seed, root.moves[a]) > gumbelNoise(seed, root.moves[b]);
        });
        for (std::size_t j = 0; j < explore; j++) {
            order[k - explore + j] = rest[j];
            tree.exploring[rest[j]] = 1;
        }
    }
    order.resize(k);
    tree.remaining = std::move(order);
    // no phase yet
    tree.phaseTarget = -1;
}

double BatchedSearch::choiceLogit(const Tree &tree, int a) const {
    const Node &root = tree.nodes[0];
    double l = logit(root.prior[a]);
    if (a < static_cast<int>(tree.exploring.size()) && tree.exploring[a]) {
        // no worse than an even spread over the moves
        l = std::max(l, -std::log(static_cast<double>(root.moves.size())));
    }
    return l;
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
                sigmaQ(root.prior, root.visits, rootUtilityTotals(tree), root.utility, config);
            std::stable_sort(remaining.begin(), remaining.end(), [&](int a, int b) {
                return tree.gumbel[a] + choiceLogit(tree, a) + sigma[a] >
                       tree.gumbel[b] + choiceLogit(tree, b) + sigma[b];
            });
            remaining.resize((remaining.size() + 1) / 2);
            if (remaining.size() == 1) {
                break;
            }
        }
        // the next phase: the budget left, split evenly over the candidates
        // and the phases still to come
        int left = std::max(1, simulationsLeft(tree));
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
    // no trees (no determinized game matched what the player knows): nothing
    // to continue
    if (games != 1 || !done() || trees.empty()) {
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
            const Node &node = tree.nodes[order[k]];
            for (const std::vector<int> *children : {&node.child, &node.turnChild}) {
                for (int c : *children) {
                    if (c >= 0 && index[c] < 0) {
                        index[c] = static_cast<int>(order.size());
                        order.push_back(c);
                    }
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
            for (int &c : node.turnChild) {
                c = index[c];
            }
            kept.push_back(std::move(node));
        }
        tree.nodes = std::move(kept);
        // the visits kept count towards the next decision's simulations (and
        // their evaluations, at what a simulation cost so far)
        double each = tree.simulations > 0
                          ? static_cast<double>(tree.evaluations) / tree.simulations
                          : 1.0;
        tree.simulations = tree.nodes[0].parentVisits + 1;
        tree.evaluations = static_cast<int>(tree.simulations * each);
        tree.resume = -1;
        tree.sampling.reset();
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

std::vector<std::vector<Command>> BatchedSearch::replies(int t, const Command &move) const {
    const Tree &tree = trees.at(t);
    const Node &root = tree.nodes[0];
    auto it = std::find(root.moves.begin(), root.moves.end(), move);
    if (it == root.moves.end() || root.child[it - root.moves.begin()] < 0) {
        return {};
    }
    return tree.nodes[root.child[it - root.moves.begin()]].turns;
}

bool BatchedSearch::done() const {
    return std::all_of(trees.begin(), trees.end(), [&](const Tree &t) {
        return t.waiting < 0 && !t.sampling && !budgetLeft(t);
    });
}

const std::vector<EvalRequest> &BatchedSearch::pending() {
    requests.clear();
    requestTree.clear();
    for (std::size_t i = 0; i < trees.size(); i++) {
        Tree &tree = trees[i];
        if (!tree.sampling && tree.waiting < 0 && !descend(tree)) {
            continue;
        }
        if (tree.sampling) {
            // the next command of a turn being sampled
            const Sampling &s = *tree.sampling;
            requests.push_back({&s.game, s.game.currentPlayer, &s.moves,
                                config.fullInformation});
        } else {
            const Node &node = tree.nodes[tree.waiting];
            requests.push_back({&node.game, node.toMove,
                                node.atHorizon ? nullptr : &node.moves,
                                config.fullInformation});
        }
        requestTree.push_back(static_cast<int>(i));
    }
    return requests;
}

void BatchedSearch::submit(const std::vector<Evaluation> &results) {
    for (std::size_t i = 0; i < requestTree.size() && i < results.size(); i++) {
        Tree &tree = trees[requestTree[i]];
        tree.evaluations++;
        depthStats.evaluations++;
        if (tree.sampling) {
            Command next =
                tree.sampling->likely
                    ? likelyCommand(tree, tree.sampling->moves, results[i].priors)
                    : sampleCommand(tree, tree.sampling->moves, results[i].priors);
            int r = sampleStep(tree, next);
            if (r >= 0) {
                tree.resume = r;
            }
            continue;
        }
        int n = std::exchange(tree.waiting, -1);
        expand(tree, n, results[i]);
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
    std::vector<char> standing, exploring;
    // what moves are chosen by (SearchConfig::scoreUtility); the values
    // reported stay pure
    std::vector<double> utilities, backedTotals;
    double rootUtility = 0;
    for (const Tree &tree : trees) {
        if (tree.game != game) {
            continue;
        }
        out.samples++;
        const Node &root = tree.nodes[0];
        rootUtility += root.utility;
        const std::vector<double> rootUtilities = rootUtilityTotals(tree);
        for (std::size_t a = 0; a < root.moves.size(); a++) {
            auto [it, added] = index.try_emplace(root.moves[a], out.moves.size());
            if (added) {
                out.moves.push_back(root.moves[a]);
                out.visits.push_back(0);
                out.leafCounts.push_back({});
                out.leafValues.push_back({});
                totals.push_back(0);
                utilities.push_back(0);
                backedTotals.push_back(0);
                priorSum.push_back(0);
                priorCount.push_back(0);
                standing.push_back(0);
                exploring.push_back(0);
            }
            out.visits[it->second] += root.visits[a];
            if (a < tree.leafCounts.size()) {
                for (int k = 0; k < LEAF_KINDS; k++) {
                    out.leafCounts[it->second][k] += tree.leafCounts[a][k];
                    out.leafValues[it->second][k] += tree.leafValues[a][k];
                }
            }
            totals[it->second] += root.total[a];
            if (root.visits[a]) {
                const Node &c = tree.nodes[root.child[a]];
                backedTotals[it->second] +=
                    root.visits[a] * (config.backup == SearchConfig::Backup::MEAN
                                          ? root.total[a] / root.visits[a]
                                      : c.toMove == root.toMove ? c.backedValue
                                                                : -c.backedValue);
            }
            utilities[it->second] += rootUtilities[a];
            priorSum[it->second] += root.prior[a];
            priorCount[it->second]++;
        }
        for (int a : tree.remaining) {
            standing[index.at(root.moves[a])] = 1;
            if (a < static_cast<int>(tree.exploring.size()) && tree.exploring[a]) {
                exploring[index.at(root.moves[a])] = 1;
            }
        }
    }
    const std::size_t n = out.moves.size();
    for (std::size_t i = 0; i < n; i++) {
        out.values.push_back(out.visits[i] ? totals[i] / out.visits[i] : 0);
        out.backed.push_back(out.visits[i] ? backedTotals[i] / out.visits[i] : 0);
        out.priors.push_back(priorSum[i] / priorCount[i]);
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
    std::vector<double> sigma = sigmaQ(prior, out.visits, utilities, rootUtility / out.samples, config);
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
        // an exploration candidate's logit no worse than an even spread's
        double choice = exploring[i]
                            ? std::max(logit(prior[i]), -std::log(static_cast<double>(n))) + sigma[i]
                            : score[i];
        if (standing[i] && g + choice > best) {
            best = g + choice;
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

namespace {
// For playTurn's `explain`: the command played, then the root's leading
// moves (by visits, then prior) and End Turn, with prior, visits and value.
void explainDecision(const Game &game, const SearchResult &result, const Command &played,
                     double rootValue, std::string &out) {
    constexpr std::size_t SHOWN = 6;
    char line[256];
    std::snprintf(line, sizeof line, "%s  (value %+.2f, %zu moves)\n",
                  toString(game, played).c_str(), rootValue, result.moves.size());
    out += line;
    std::vector<std::size_t> order(result.moves.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return std::tie(result.visits[a], result.priors[a]) >
               std::tie(result.visits[b], result.priors[b]);
    });
    std::size_t shown = 0;
    for (std::size_t i : order) {
        bool endTurn = result.moves[i].type == CommandTypes::ENDTURN;
        if (shown >= SHOWN && !endTurn) {
            continue;
        }
        shown++;
        char value[16];
        if (result.visits[i]) {
            std::snprintf(value, sizeof value, "%+.2f", result.values[i]);
        } else {
            std::snprintf(value, sizeof value, "  -  ");
        }
        std::snprintf(line, sizeof line, "      prior %5.1f%%  visits %4d  value %s  %s\n",
                      100 * result.priors[i], result.visits[i], value,
                      toString(game, result.moves[i]).c_str());
        out += line;
    }
}
} // namespace

std::vector<Command> playTurn(Game &game, Evaluator &evaluator,
                              const SearchConfig &config, std::mt19937_64 &rng,
                              bool networkBeliefs, float *lastValue,
                              std::string *explain) {
    if (lastValue) {
        *lastValue = std::nanf("");
    }
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
        // the root's value: its moves' values, weighted by visits
        double total = 0, visits = 0;
        for (std::size_t i = 0; i < result.moves.size(); i++) {
            total += result.values[i] * result.visits[i];
            visits += result.visits[i];
        }
        if (lastValue && visits > 0) {
            *lastValue = static_cast<float>(total / visits);
        }
        Command command = result.choose(0, rng);
        if (result.moves.empty() || !game.isLegal(command)) {
            command = game.legalCommands().front();
        }
        if (explain) {
            explainDecision(game, result, command, visits > 0 ? total / visits : 0, *explain);
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
