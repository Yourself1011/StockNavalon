#pragma once

// stockNavalon's search, stage 1 (docs/stockNavalon.md sections 3, 4.3 and
// 6.2): PUCT over commands, across turn boundaries up to a horizon, on
// several determinized games at once, with positions handed to an evaluator
// in batches.

#include "command.h"
#include "determinize.h"
#include "game.h"
#include "legalMoves.h"
#include <cstdint>
#include <random>
#include <vector>

struct SearchConfig {
    // simulations in each determinized game
    int simulations = 200;
    // determinized games per decision, their root visits added up
    int samples = 4;
    // c in Q + c * P * sqrt(N) / (1 + n)
    double exploration = 1.5;
    // How many turn changes a line may cross before it's scored without
    // looking further: 0 is the player's own turn only, 1 includes the
    // opponent's reply
    int horizon = 1;
    // Dirichlet noise on the root priors, for self-play
    bool rootNoise = false;
    double noiseAlpha = 0.3, noiseWeight = 0.25;
    int determinizeTries = 20;
};

// A position to evaluate, for the player to move in it
struct EvalRequest {
    const Game *game;
    int player;
    // the commands to give priors for; empty when only the value is needed
    // (a position at the horizon)
    const std::vector<Command> *moves;
};

struct Evaluation {
    // one per move of the request, summing to 1
    std::vector<float> priors;
    // the expected result for the player to move, in [-1, 1]
    float value = 0;
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
struct SearchResult {
    std::vector<Command> moves;
    std::vector<int> visits;
    std::vector<double> values;
    // determinized games actually searched
    int samples = 0;

    // the most visited command, or with a temperature, one drawn in
    // proportion to visits^(1 / temperature)
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

  private:
    struct Node {
        Game game;
        LegalMoves legal;
        int toMove;
        // turn changes since the root
        int crossed;
        // a finished game, or a line at the horizon: scored, not searched
        bool terminal = false, atHorizon = false;
        // scored yet, and the value for toMove
        bool evaluated = false;
        float value = 0;
        // per move: the command, prior, visits, total value (for toMove)
        // and child node (-1 until visited)
        std::vector<Command> moves;
        std::vector<float> prior;
        std::vector<int> visits;
        std::vector<double> total;
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
    };

    SearchConfig config;
    int games;
    std::vector<Tree> trees;
    std::vector<EvalRequest> requests;
    // the tree each request is for
    std::vector<int> requestTree;

    Node makeNode(Game game, LegalMoves legal, int crossed) const;
    // runs the tree's current simulation until it needs an evaluation (true)
    // or completes without one (false)
    bool descend(Tree &tree);
    void backup(Tree &tree, int leaf, float value);
    void expand(Tree &tree, int node, const Evaluation &evaluation);
};

// runs a batched search to the end with this evaluator
void run(BatchedSearch &search, Evaluator &evaluator);

// one decision for the player to move in `game`
SearchResult search(const Game &game, const Beliefs &beliefs,
                    Evaluator &evaluator, const SearchConfig &config,
                    std::uint64_t seed);
