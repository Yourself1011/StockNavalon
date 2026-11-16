#pragma once

// stockNavalon's search, stage 1 (docs/stockNavalon.md sections 3
// and 4): PUCT over commands, across turn boundaries up to a horizon, on
// several determinized games at once, with positions handed to an evaluator
// in batches.

#include "command.h"
#include <array>
#include "determinize.h"
#include "game.h"
#include "legalMoves.h"
#include <cstdint>
#include <optional>
#include <random>
#include <utility>
#include <vector>

// A horizon no game reaches: no cutoff, the simulations go as deep as they
// pay (the budget limits depth anyway: few lines even reach the opponent's
// turn at 64 simulations)
constexpr int UNLIMITED_HORIZON = 1000;

struct SearchConfig {
    // simulations in each determinized game
    int simulations = 200;
    // determinized games per decision, their root visits added up
    int samples = 4;
    // c in Q + c * P * sqrt(N) / (1 + n)
    double exploration = 1.5;
    // First-play urgency (as in Leela and KataGo): a move not tried yet
    // counts as the node's own value estimate, less this times the square
    // root of the prior already explored, rather than as 0. With 0, in a
    // position that looks lost every untried move beat every tried one, so
    // the search kept sampling moves it knew nothing about.
    double fpuReduction = 0.2;
    // off: untried moves count as 0, the old behaviour (for comparisons)
    bool firstPlayUrgency = true;
    // How many turn changes a line may cross before it's scored without
    // looking further: 0 is the player's own turn only, 1 includes the
    // opponent's reply. No cutoff by default.
    int horizon = UNLIMITED_HORIZON;
    // Dirichlet noise on the root priors, for self-play; with Gumbel search,
    // Gumbel noise on the candidates' logits instead
    bool rootNoise = false;
    double noiseAlpha = 0.3, noiseWeight = 0.25;
    int determinizeTries = 20;
    // Search the real game instead of determinized samples, for training
    // without fog (docs/stockNavalon.md section 4): one tree, on a copy with
    // the random state reseeded, so chance (ruins, Explorer paths) stays
    // chance. The network's input is then encoded with full information.
    bool fullInformation = false;

    // Gumbel search at the root (Danihelka et al. 2022, notes/design.md):
    // `gumbelCandidates` moves sampled from the policy without replacement
    // (Gumbel-top-k; the most likely ones without root noise), the budget
    // spent on them by sequential halving, the last one standing played, and
    // the policy target the improved policy softmax(logit + sigma(completed
    // Q)) over every root move. Below the root, PUCT as before.
    bool gumbel = false;
    int gumbelCandidates = 16;
    // sigma(q) = (gumbelVisitScale + most visits of a root move) x
    // gumbelValueScale x q, with the completed Q rescaled to [0, 1] across the
    // root's moves (the paper's and mctx's defaults)
    double gumbelVisitScale = 50, gumbelValueScale = 0.1;
    // For self-play: this many of the Gumbel candidates are legal moves
    // picked at random instead of by the policy, so moves it has all but
    // ruled out (a bridge, say) still get tried. In the halving and the final
    // choice their logit is at least an even spread's, so a clearly better
    // one can win; the policy target keeps the real logits.
    int exploreCandidates = 0;

    // Tie-breaks for choosing moves, so the bot doesn't play aimlessly once a
    // game is decided (every move then looks equally won). Moves are ranked
    // by a utility: the value + scoreUtility x the predicted final score
    // lead, - winSooner x the value x the predicted turns left (preferring a
    // quicker win, or a slower loss). Only the choice of moves uses it; the
    // values the search reports, and trains on, stay pure chances of
    // winning. KataGo's score utility; Leela Chess Zero's moves-left head.
    double scoreUtility = 0, winSooner = 0;
};

// A position to evaluate, for the player to move in it
struct EvalRequest {
    const Game *game;
    int player;
    // the commands to give priors for; empty when only the value is needed
    // (a position at the horizon)
    const std::vector<Command> *moves;
    // encode with full information (SearchConfig::fullInformation)
    bool fullInformation = false;
    // only the network's beliefs are wanted, not an evaluation: a
    // self-play decision about to be searched (SelfPlayConfig::networkBeliefs)
    bool wantsBeliefs = false;
};

struct Evaluation {
    // one per move of the request, summing to 1
    std::vector<float> priors;
    // the expected result for the player to move, in [-1, 1]
    float value = 0;
    // for the tie-breaks (SearchConfig::scoreUtility, winSooner): the
    // predicted final score lead, in [-1, 1], and turns left, scaled
    // (SideTargets::TURNS_LEFT_SCALE); 0 without them
    float scoreLead = 0, turnsLeft = 0;
    // for a request that wants beliefs: the network's, decoded
    std::optional<Beliefs> beliefs;
};

class Evaluator {
  public:
    virtual ~Evaluator() = default;
    virtual void evaluate(const std::vector<EvalRequest> &requests,
                          std::vector<Evaluation> &results) = 0;
};

// Until there's a network: uniform priors, and the score lead as the value,
// squashed into [-1, 1]. A finished game is +1 or -1.
class ScoreEvaluator : public Evaluator {
  public:
    // a lead of this much is worth about 0.76
    explicit ScoreEvaluator(double scale = 500) : scale(scale) {}
    void evaluate(const std::vector<EvalRequest> &requests,
                  std::vector<Evaluation> &results) override;

  private:
    double scale;
};

// The search's answer for one game: each root command, how often it was
// visited across the determinized games, and its average value for the
// player to move
// Where a simulation ended, for SearchResult's leaf counts: in the searching
// player's own turn, in an opponent's, back in the searching player's own
// turn after one, or at the end of the game
enum LeafKind { LEAF_OWN_TURN, LEAF_OPPONENT_TURN, LEAF_LATER_TURN, LEAF_GAME_OVER, LEAF_KINDS };

struct SearchResult {
    std::vector<Command> moves;
    std::vector<int> visits;
    std::vector<double> values;
    // the policy target over the moves, summing to 1: the visit shares, or
    // with Gumbel search the improved policy
    std::vector<double> policy;
    // the network's prior for each move, averaged over the trees with it
    std::vector<double> priors;
    // Where each move's simulations ended (LeafKind), how often, and their
    // values summed, for the searching player: a diagnostic of which views
    // a move's value comes from
    std::vector<std::array<int, LEAF_KINDS>> leafCounts;
    std::vector<std::array<double, LEAF_KINDS>> leafValues;
    // with Gumbel search, the move to play (the last one standing); -1
    // otherwise
    int chosen = -1;
    // determinized games actually searched
    int samples = 0;

    // Gumbel search's move; otherwise the most visited command, or with a
    // temperature, one drawn in proportion to visits^(1 / temperature)
    Command choose(double temperature, std::mt19937_64 &rng) const;
};

// Searches many games at once, for the player to move in each. The
// evaluator is called with every waiting position at once:
//
//   BatchedSearch search(games, beliefs, config, seed);
//   while (!search.done()) {
//       search.submit(evaluate(search.pending()));
//   }
//
// or just run(search, evaluator).
class BatchedSearch {
  public:
    BatchedSearch(const std::vector<const Game *> &games,
                  const std::vector<Beliefs> &beliefs,
                  const SearchConfig &config, std::uint64_t seed);

    bool done() const;
    // The positions waiting for an evaluation. Valid until submit().
    const std::vector<EvalRequest> &pending();
    // their evaluations, in the same order
    void submit(const std::vector<Evaluation> &results);

    SearchResult result(int game) const;

    // Tree reuse, for a search of one game: after `played` was played, each
    // tree's root moves down to that move's child, keeping the subtree and
    // its visits, and the next decision is searched with `next` (its
    // simulations count the visits kept; root noise applied afresh). Only
    // within the same player's turn, and only if every tree has that child,
    // evaluated; false otherwise, and the search is then spent. The caller
    // checks the new roots against the real game (rootGame).
    bool reuse(const Command &played, const SearchConfig &next);
    int treeCount() const { return static_cast<int>(trees.size()); }

    // How deep the simulations went, since the last call: how many ended
    // (at a position to evaluate, a finished game or the horizon), their
    // total depth in commands below the root, and how many reached the
    // opponent's turn
    struct DepthStats {
        std::int64_t simulations = 0, depth = 0, reachedOpponent = 0;
        DepthStats &operator+=(const DepthStats &o) {
            simulations += o.simulations;
            depth += o.depth;
            reachedOpponent += o.reachedOpponent;
            return *this;
        }
    };
    DepthStats takeDepthStats() { return std::exchange(depthStats, {}); }
    const Game &rootGame(int tree) const { return trees.at(tree).nodes[0].game; }

  private:
    struct Node {
        Game game;
        LegalMoves legal;
        int toMove;
        // turn changes since the root
        int crossed;
        // a finished game, or a line at the horizon: scored, not searched
        bool terminal = false, atHorizon = false;
        // scored yet, and the value for toMove, and the utility moves are
        // chosen by (SearchConfig::scoreUtility)
        bool evaluated = false;
        float value = 0, utility = 0;
        // per move: the command, prior, visits, total value (for toMove)
        // and child node (-1 until visited)
        std::vector<Command> moves;
        std::vector<float> prior;
        std::vector<int> visits;
        std::vector<double> total, utilityTotal;
        std::vector<int> child;
        int parentVisits = 0;
    };
    struct Tree {
        int game;
        std::vector<Node> nodes;
        int simulations = 0;
        // the path of the simulation in progress: node, move taken
        std::vector<std::pair<int, int>> path;
        // the node waiting for an evaluation, -1 if none
        int waiting = -1;
        std::mt19937_64 rng;
        // Gumbel search: the root moves' Gumbel noise, the candidates still
        // in (indices of root moves), and the visits each must reach before
        // the next halving
        std::vector<double> gumbel;
        std::vector<int> remaining;
        // per root move, whether it's an exploration candidate
        std::vector<char> exploring;
        int phaseTarget = 0;
        // per root move, its simulations' leaves (SearchResult::leafCounts)
        std::vector<std::array<int, LEAF_KINDS>> leafCounts;
        std::vector<std::array<double, LEAF_KINDS>> leafValues;
    };

    SearchConfig config;
    int games;
    std::vector<Tree> trees;
    DepthStats depthStats;
    // per game, the seed of its root moves' Gumbel noise, so every
    // determinized tree of a game draws the same noise for the same command
    std::vector<std::uint64_t> gumbelSeeds;
    std::vector<EvalRequest> requests;
    // the tree each request is for
    std::vector<int> requestTree;

    Node makeNode(Game game, LegalMoves legal, int crossed) const;
    // runs the tree's current simulation until it needs an evaluation (true)
    // or completes without one (false)
    bool descend(Tree &tree);
    void addRootNoise(Tree &tree);
    // Gumbel search: the candidates, once the root is evaluated
    void startGumbel(Tree &tree);
    // the root move the next simulation takes, halving when a phase is done
    int gumbelRootMove(Tree &tree);
    // a root move's logit for the halving and the final choice
    // (SearchConfig::exploreCandidates)
    double choiceLogit(const Tree &tree, int a) const;
    void backup(Tree &tree, int leaf, float value, float utility);
    float utilityOf(float value, float scoreLead, float turnsLeft) const;
    void expand(Tree &tree, int node, const Evaluation &evaluation);
};

// runs a batched search to the end with this evaluator
void run(BatchedSearch &search, Evaluator &evaluator);

// one decision for the player to move in `game`
SearchResult search(const Game &game, const Beliefs &beliefs,
                    Evaluator &evaluator, const SearchConfig &config,
                    std::uint64_t seed);

// Plays the whole of the current player's turn, each command the most
// visited of a search on what that player can see; returns the commands. The
// CLI's bot, with whichever evaluator. With `networkBeliefs`, the hidden state
// is sampled from beliefs the evaluator gives for the turn's first position
// (a request that wantsBeliefs, as in self-play), else from priorBeliefs.
// With `lastValue`, the search's value of the position at its last decision
// of the turn, for the player whose turn it was (NaN if it never searched).
// With `explain`, a few lines per decision appended to it: the command
// played and the root's leading moves, with their priors, visits and values.
std::vector<Command> playTurn(Game &game, Evaluator &evaluator,
                              const SearchConfig &config, std::mt19937_64 &rng,
                              bool networkBeliefs = false, float *lastValue = nullptr,
                              std::string *explain = nullptr);
