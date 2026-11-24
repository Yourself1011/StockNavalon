#include "doctest/doctest.h"
#include "helpers.h"
#include "search.h"
#include "view.h"
#include <stdexcept>

using namespace CommandTypes;

namespace {
// An 8x8 field map, capitals at (1,1) and (6,6), everything explored.
// Without stars there's no research or training, so the player's turn is
// narrow enough for the score evaluator to see the opponent's reply: with
// uniform priors, a wide turn spreads the simulations within it, and lines
// that haven't ended their turn can't see a loss coming (the trained value
// head is what will).
Game emptyGame() {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    for (Player &p : game.players) {
        p.stars = 0;
    }
    return game;
}

SearchResult searchFor(const Game &game, int horizon, std::uint64_t seed = 1) {
    ScoreEvaluator evaluator;
    SearchConfig config{.simulations = 300, .samples = 2, .horizon = horizon};
    return search(game, priorBeliefs(makeView(game, game.currentPlayer)),
                  evaluator, config, seed);
}

double valueOf(const SearchResult &r, CommandType type) {
    for (std::size_t i = 0; i < r.moves.size(); i++) {
        if (r.moves[i].type == type) {
            return r.values[i];
        }
    }
    return -2;
}

Command best(const SearchResult &r) {
    std::mt19937_64 rng(0);
    return r.choose(0, rng);
}
} // namespace

TEST_CASE("the search takes a win on the spot") {
    Game game = emptyGame();
    addUnit(game, 0, UnitTypes::WARRIOR, 6, 6);
    REQUIRE(hasCommand(game, {CAPTURE, game.index(6, 6)}));
    for (int horizon : {0, 1}) {
        SearchResult r = searchFor(game, horizon);
        CHECK(best(r).type == CAPTURE);
        CHECK(valueOf(r, CAPTURE) == doctest::Approx(1));
    }
}

TEST_CASE("looking into the opponent's turn sees a loss coming") {
    // their warrior stands on the player's capital, and nothing can stop it
    // capturing next turn
    Game game = emptyGame();
    addUnit(game, 1, UnitTypes::WARRIOR, 1, 1);
    SearchResult own = searchFor(game, 0), ahead = searchFor(game, 1);
    // the player's own turn alone can't see it
    CHECK(valueOf(own, ENDTURN) > -0.5);
    // a turn further, every line ends in the capture
    for (double v : ahead.values) {
        CHECK(v < -0.5);
    }
}

TEST_CASE("looking into the opponent's turn finds the move that stops the loss") {
    Game game = emptyGame();
    int intruder = addUnit(game, 1, UnitTypes::WARRIOR, 1, 1);
    game.units[intruder].hp = 10;
    addUnit(game, 0, UnitTypes::WARRIOR, 2, 2);
    REQUIRE(hasCommand(game, {ATTACK, game.index(2, 2), game.index(1, 1)}));
    SearchResult r = searchFor(game, 1);
    Command chosen = best(r);
    CHECK(chosen.type == ATTACK);
    CHECK(valueOf(r, ATTACK) > valueOf(r, ENDTURN));
}

TEST_CASE("search bookkeeping") {
    Game game = emptyGame();
    addUnit(game, 0, UnitTypes::WARRIOR, 3, 3);
    addUnit(game, 1, UnitTypes::WARRIOR, 5, 5);
    SearchResult a = searchFor(game, 1, 7), b = searchFor(game, 1, 7);
    int visits = 0;
    for (int v : a.visits) {
        visits += v;
    }
    CHECK(a.samples == 2);
    // the first simulation of each tree evaluates its root
    CHECK(visits == 2 * (300 - 1));
    CHECK(a.moves == b.moves);
    CHECK(a.visits == b.visits);
}

TEST_CASE("first-play urgency: in a position that looks lost, the search stays with what it knows") {
    // a network that likes one move and thinks every position is lost
    struct Gloomy : Evaluator {
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                std::size_t n = requests[i].moves ? requests[i].moves->size() : 0;
                results[i].priors.assign(n, n > 1 ? 0.5f / (n - 1) : 1.0f);
                if (n > 1) {
                    results[i].priors[0] = 0.5f;
                }
                results[i].value = -0.6f;
            }
        }
    } network;
    Game game = makeGame(bigMap());
    game.players[0].stars = 30;
    REQUIRE(game.legalCommands().size() > 10);
    struct Spread {
        double favourite;
        int tried;
    };
    auto spread = [&](bool urgency) {
        SearchConfig config{.simulations = 200, .samples = 1,
                            .firstPlayUrgency = urgency, .horizon = 0};
        SearchResult r = search(game, priorBeliefs(makeView(game, 0)), network, config, 3);
        int total = 0, tried = 0;
        for (int v : r.visits) {
            total += v;
            tried += v > 0;
        }
        return Spread{static_cast<double>(r.visits[0]) / total, tried};
    };
    // untried moves count a little below the position's value, so the
    // favourite keeps its share of the visits; counted optimistically (as 0
    // used to), the untried moves beat it until each had its turn, and it
    // ended up with hardly any (4% against 30% here)
    Spread urgent = spread(true), optimistic = spread(false);
    MESSAGE("favourite's share " << urgent.favourite << " against " << optimistic.favourite
                                 << ", moves tried " << urgent.tried << " against "
                                 << optimistic.tried);
    CHECK(urgent.favourite > optimistic.favourite + 0.1);
}

TEST_CASE("tree reuse continues from the played move's subtree, within the budget") {
    Game game = makeGame(bigMap());
    game.players[0].stars = 30;
    ScoreEvaluator evaluator;
    SearchConfig config{.simulations = 120, .samples = 1, .horizon = 1,
                        .fullInformation = true};
    BatchedSearch search({&game}, {Beliefs{}}, config, 5);
    run(search, evaluator);
    SearchResult first = search.result(0);
    // the most visited move that keeps it the player's turn
    int best = -1;
    for (std::size_t i = 0; i < first.moves.size(); i++) {
        if (first.moves[i].type != CommandTypes::ENDTURN &&
            (best < 0 || first.visits[i] > first.visits[best])) {
            best = static_cast<int>(i);
        }
    }
    REQUIRE(best >= 0);
    REQUIRE(first.visits[best] > 1);
    Command played = first.moves[best];
    // a move never explored, or the end of the turn, can't be continued from
    {
        BatchedSearch other({&game}, {Beliefs{}}, config, 5);
        run(other, evaluator);
        CHECK_FALSE(other.reuse({CommandTypes::ENDTURN}, config));
    }
    REQUIRE(search.reuse(played, config));
    Game after = game;
    after.apply(played);
    CHECK(search.rootGame(0).currentPlayer == after.currentPlayer);
    // the kept visits are there before any new simulation
    SearchResult kept = search.result(0);
    int keptVisits = 0;
    for (int v : kept.visits) {
        keptVisits += v;
    }
    CHECK(keptVisits == first.visits[best] - 1);
    // and the search finishes at the same budget, counting them
    run(search, evaluator);
    SearchResult second = search.result(0);
    int total = 0;
    for (int v : second.visits) {
        total += v;
    }
    CHECK(total == config.simulations - 1);
}

TEST_CASE("the CLI bot asks for its beliefs once a turn when told to") {
    // the score evaluator, answering belief requests with the prior
    struct Asking : Evaluator {
        ScoreEvaluator score;
        int asked = 0;
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            std::vector<EvalRequest> rest;
            for (const EvalRequest &r : requests) {
                if (r.wantsBeliefs) {
                    CHECK(r.moves == nullptr);
                    asked++;
                } else {
                    rest.push_back(r);
                }
            }
            score.evaluate(rest, results);
            for (std::size_t i = 0; i < requests.size(); i++) {
                if (requests[i].wantsBeliefs) {
                    Evaluation e;
                    e.beliefs = priorBeliefs(makeView(*requests[i].game, requests[i].player));
                    results.insert(results.begin() + static_cast<std::ptrdiff_t>(i), e);
                }
            }
        }
    };
    SearchConfig config{.simulations = 20, .samples = 1};
    for (bool network : {false, true}) {
        CAPTURE(network);
        Game game = makeGame(fieldMap());
        Asking evaluator;
        std::mt19937_64 rng(3);
        for (int turn = 0; turn < 4; turn++) {
            float value = 5;
            REQUIRE(!playTurn(game, evaluator, config, rng, network, &value).empty());
            // the last search's value, for the CLI to show
            CHECK(std::abs(value) <= 1);
        }
        CHECK(evaluator.asked == (network ? 4 : 0));
    }
}

TEST_CASE("Gumbel search spends its budget on its candidates, halving them, and plays the last one") {
    // a network with uneven priors, so the candidates aren't a tie
    struct Leaning : Evaluator {
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                std::size_t n = requests[i].moves ? requests[i].moves->size() : 0;
                results[i].priors.resize(n);
                double sum = 0;
                for (std::size_t a = 0; a < n; a++) {
                    sum += results[i].priors[a] = 1.0f / static_cast<float>(a + 1);
                }
                for (float &p : results[i].priors) {
                    p = static_cast<float>(p / sum);
                }
                results[i].value = 0.1f;
            }
        }
    } network;
    Game game = makeGame(bigMap());
    game.players[0].stars = 30;
    REQUIRE(game.legalCommands().size() > 10);
    for (bool noise : {false, true}) {
        CAPTURE(noise);
        SearchConfig config{.simulations = 64, .samples = 1, .horizon = 0,
                            .rootNoise = noise, .gumbel = true, .gumbelCandidates = 8};
        SearchResult r = search(game, priorBeliefs(makeView(game, 0)), network, config, 3);
        int total = 0, tried = 0, most = 0;
        for (int v : r.visits) {
            total += v;
            tried += v > 0;
            most = std::max(most, v);
        }
        CHECK(total == config.simulations - 1);
        CHECK(tried == 8);
        // the last one standing had the most visits, and is played
        REQUIRE(r.chosen >= 0);
        CHECK(r.visits[r.chosen] == most);
        std::mt19937_64 rng(0);
        CHECK(r.choose(1.0, rng) == r.moves[r.chosen]);
        // the policy target covers every move
        REQUIRE(r.policy.size() == r.moves.size());
        double sum = 0;
        for (double p : r.policy) {
            CHECK(p > 0);
            sum += p;
        }
        CHECK(sum == doctest::Approx(1));
        // the same seed, the same search
        SearchResult again = search(game, priorBeliefs(makeView(game, 0)), network, config, 3);
        CHECK(again.visits == r.visits);
        CHECK(again.chosen == r.chosen);
    }
    // without noise the candidates are the most likely moves
    SearchConfig config{.simulations = 64, .samples = 1, .horizon = 0,
                        .gumbel = true, .gumbelCandidates = 8};
    SearchResult r = search(game, priorBeliefs(makeView(game, 0)), network, config, 3);
    for (std::size_t a = 0; a < r.visits.size(); a++) {
        CHECK((r.visits[a] > 0) == (a < 8));
    }
}

TEST_CASE("Gumbel search takes a win on the spot, and its policy target says so") {
    Game game = emptyGame();
    addUnit(game, 0, UnitTypes::WARRIOR, 6, 6);
    ScoreEvaluator evaluator;
    for (int simulations : {16, 64}) {
        CAPTURE(simulations);
        SearchConfig config{.simulations = simulations, .samples = 2, .horizon = 1,
                            .rootNoise = true, .gumbel = true};
        SearchResult r = search(game, priorBeliefs(makeView(game, 0)), evaluator, config, 1);
        CHECK(best(r).type == CAPTURE);
        // the priors are uniform; the improved policy puts the capture first
        int capture = static_cast<int>(
            std::find_if(r.moves.begin(), r.moves.end(),
                         [](const Command &c) { return c.type == CAPTURE; }) -
            r.moves.begin());
        REQUIRE(capture < static_cast<int>(r.moves.size()));
        CHECK(r.policy[capture] == *std::max_element(r.policy.begin(), r.policy.end()));
        CHECK(r.policy[capture] > 2.0 / static_cast<double>(r.moves.size()));
    }
}

TEST_CASE("Gumbel search continues a reused tree within the budget") {
    Game game = makeGame(bigMap());
    game.players[0].stars = 30;
    ScoreEvaluator evaluator;
    SearchConfig config{.simulations = 64, .samples = 1, .horizon = 1,
                        .rootNoise = true, .fullInformation = true, .gumbel = true};
    BatchedSearch search({&game}, {Beliefs{}}, config, 5);
    run(search, evaluator);
    SearchResult first = search.result(0);
    Command played = first.moves[first.chosen];
    if (played.type == CommandTypes::ENDTURN || first.visits[first.chosen] < 2) {
        return; // nothing to continue from with this seed
    }
    REQUIRE(search.reuse(played, config));
    run(search, evaluator);
    SearchResult second = search.result(0);
    int total = 0;
    for (int v : second.visits) {
        total += v;
    }
    CHECK(total == config.simulations - 1);
    CHECK(second.chosen >= 0);
}

TEST_CASE("a search without trees can't be reused") {
    // no determinized game: what a failed determinization leaves
    Game game = emptyGame();
    ScoreEvaluator evaluator;
    for (bool gumbel : {false, true}) {
        CAPTURE(gumbel);
        SearchConfig config{.simulations = 16, .samples = 0, .gumbel = gumbel};
        BatchedSearch search({&game}, {priorBeliefs(makeView(game, 0))}, config, 1);
        run(search, evaluator);
        CHECK(search.treeCount() == 0);
        // nothing to choose from is an error, not an empty result
        CHECK_THROWS_AS(search.result(0), std::runtime_error);
        CHECK_FALSE(search.reuse({CommandTypes::ENDTURN}, config));
    }
}

TEST_CASE("tie-breaks pick between equally valued moves, and reported values stay pure") {
    // a network that thinks every position is won at 0.8; one square, (2,2),
    // is better by the tie-break being tested
    struct Flat : Evaluator {
        bool scoreBreak;
        explicit Flat(bool score) : scoreBreak(score) {}
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                const Game &g = *requests[i].game;
                std::size_t n = requests[i].moves ? requests[i].moves->size() : 0;
                results[i].priors.assign(n, n ? 1.0f / static_cast<float>(n) : 0.0f);
                // player 0 is winning, whoever's turn it is
                float side = requests[i].player == 0 ? 1.0f : -1.0f;
                results[i].value = 0.8f * side;
                const Unit *u = g.unitAt(g.index(2, 2));
                bool there = u && u->owner == 0;
                // ahead on score there, or closer to the end
                results[i].scoreLead = scoreBreak && there ? 0.5f * side : 0.0f;
                results[i].turnsLeft = !scoreBreak && there ? 0.2f : 1.0f;
            }
        }
    };
    Game game = emptyGame();
    addUnit(game, 0, UnitTypes::WARRIOR, 1, 1);
    REQUIRE(hasCommand(game, {MOVE, game.index(1, 1), game.index(2, 2)}));
    for (bool score : {true, false}) {
        CAPTURE(score);
        Flat network(score);
        for (bool gumbel : {false, true}) {
            CAPTURE(gumbel);
            SearchConfig config{.simulations = 120, .samples = 1, .horizon = 0,
                                .gumbel = gumbel, .gumbelCandidates = 32,
                                .scoreUtility = score ? 0.05 : 0.0,
                                .winSooner = score ? 0.0 : 0.05};
            SearchResult r = search(game, priorBeliefs(makeView(game, 0)), network, config, 2);
            Command chosen = best(r);
            CHECK(chosen.type == MOVE);
            CHECK(chosen.to == game.index(2, 2));
            // what's reported is the pure value
            for (std::size_t i = 0; i < r.moves.size(); i++) {
                if (r.visits[i]) {
                    CHECK(r.values[i] == doctest::Approx(0.8));
                }
            }
        }
    }
}

TEST_CASE("depth stats: a narrower search goes deeper") {
    // a network with a clear favourite at every node, so a low exploration
    // constant follows it down
    struct Peaked : Evaluator {
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                std::size_t n = requests[i].moves ? requests[i].moves->size() : 0;
                results[i].priors.assign(n, n > 1 ? 0.3f / static_cast<float>(n - 1) : 1.0f);
                if (n > 1) {
                    results[i].priors[0] = 0.7f;
                }
                results[i].value = 0;
            }
        }
    } network;
    Game game = makeGame(bigMap());
    game.players[0].stars = 30;
    auto depth = [&](double exploration) {
        SearchConfig config{.simulations = 200, .samples = 1, .exploration = exploration};
        BatchedSearch search({&game}, {priorBeliefs(makeView(game, 0))}, config, 4);
        run(search, network);
        BatchedSearch::DepthStats d = search.takeDepthStats();
        CHECK(d.simulations == config.simulations);
        // taken: the next read starts from zero
        CHECK(search.takeDepthStats().simulations == 0);
        return static_cast<double>(d.depth) / d.simulations;
    };
    double wide = depth(3.0), narrow = depth(0.3);
    MESSAGE("mean depth " << wide << " wide, " << narrow << " narrow");
    CHECK(narrow > wide);
}

TEST_CASE("exploration candidates find a move the policy has all but ruled out") {
    // a winning capture the network rates at almost nothing
    struct Blind : Evaluator {
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                const EvalRequest &r = requests[i];
                std::size_t n = r.moves ? r.moves->size() : 0;
                results[i].priors.assign(n, 1.0f);
                for (std::size_t a = 0; a < n; a++) {
                    if ((*r.moves)[a].type == CAPTURE) {
                        results[i].priors[a] = 1e-7f;
                    }
                }
                float sum = 0;
                for (float p : results[i].priors) {
                    sum += p;
                }
                for (float &p : results[i].priors) {
                    p /= sum;
                }
                const Game &g = *r.game;
                results[i].value = g.gameOver() ? (g.winner == r.player ? 1.0f : -1.0f) : 0.0f;
            }
        }
    } network;
    Game game = emptyGame();
    addUnit(game, 0, UnitTypes::WARRIOR, 6, 6);
    REQUIRE(hasCommand(game, {CAPTURE, game.index(6, 6)}));
    int found = 0, without = 0;
    for (std::uint64_t seed = 1; seed <= 12; seed++) {
        for (int explore : {0, 2}) {
            SearchConfig config{.simulations = 32, .samples = 1, .horizon = 0,
                                .rootNoise = true, .gumbel = true, .gumbelCandidates = 4,
                                .exploreCandidates = explore};
            SearchResult r = search(game, priorBeliefs(makeView(game, 0)), network, config, seed);
            bool captured = best(r).type == CAPTURE;
            (explore ? found : without) += captured;
            // the policy target keeps the network's real logits: a captured
            // win raises it, but it's still a target, not a ban lifted by fiat
            for (std::size_t i = 0; i < r.moves.size(); i++) {
                CHECK(r.policy[i] >= 0);
            }
        }
    }
    MESSAGE("capture played in " << found << " of 12 searches with exploration, " << without
                                 << " without");
    CHECK(without == 0);
    CHECK(found > 0);
}

namespace {
// the score evaluator, with a policy that knows to capture: what sampled
// turns rely on (with uniform priors a turn's capture is down to chance)
class CapturingEvaluator : public Evaluator {
  public:
    void evaluate(const std::vector<EvalRequest> &requests,
                  std::vector<Evaluation> &results) override {
        score.evaluate(requests, results);
        for (std::size_t i = 0; i < requests.size(); i++) {
            if (!requests[i].moves) {
                continue;
            }
            std::vector<float> &p = results[i].priors;
            float sum = 0;
            for (std::size_t a = 0; a < p.size(); a++) {
                p[a] = (*requests[i].moves)[a].type == CAPTURE ? 20.0f : 1.0f;
                sum += p[a];
            }
            for (float &x : p) {
                x /= sum;
            }
        }
    }

  private:
    ScoreEvaluator score;
};

SearchConfig turnSearchConfig(int commandVisits) {
    SearchConfig config{.simulations = 300, .samples = 1, .horizon = 3,
                        .fullInformation = true};
    config.turnSearch = true;
    config.commandVisits = commandVisits;
    return config;
}
} // namespace

TEST_CASE("turn search sees a win that only comes in the player's next turn") {
    // the player's warrior next to the enemy's empty capital: step on now,
    // capture next turn (a unit must start its turn there)
    Game game = emptyGame();
    addUnit(game, 0, UnitTypes::WARRIOR, 5, 5);
    Command step{MOVE, game.index(5, 5), game.index(6, 6)};
    REQUIRE(hasCommand(game, step));
    CapturingEvaluator evaluator;
    for (int commandVisits : {0, 4, 1000}) {
        CAPTURE(commandVisits);
        SearchResult r = search(game, Beliefs{}, evaluator, turnSearchConfig(commandVisits), 3);
        CHECK(best(r) == step);
        double stepValue = -2;
        for (std::size_t i = 0; i < r.moves.size(); i++) {
            if (r.moves[i] == step) {
                stepValue = r.values[i];
            }
        }
        CHECK(stepValue > 0.5);
    }
}

TEST_CASE("turn search: lines reach further turns, within an evaluation budget") {
    Game game = makeGame(bigMap());
    game.players[0].stars = 30;
    ScoreEvaluator evaluator;
    SearchConfig base{.simulations = 400, .samples = 1, .horizon = 3, .fullInformation = true,
                      .gumbel = true};
    base.countEvaluations = true;
    SearchConfig turns = base;
    turns.turnSearch = true;
    turns.commandVisits = 4;
    double meanTurns[2];
    for (int k = 0; k < 2; k++) {
        BatchedSearch s({&game}, {Beliefs{}}, k ? turns : base, 9);
        run(s, evaluator);
        BatchedSearch::DepthStats d = s.takeDepthStats();
        REQUIRE(d.simulations > 0);
        meanTurns[k] = static_cast<double>(d.turns) / d.simulations;
        // the budget is in evaluations: a turn being sampled may finish past it
        CHECK(d.evaluations >= base.simulations);
        CHECK(d.evaluations < base.simulations + base.maxTurnCommands);
        SearchResult r = s.result(0);
        CHECK_FALSE(r.moves.empty());
    }
    // on average far more turns ahead (here about 0.04 against 0.7: every
    // node's first visit is still its own evaluation, in the player's turn)
    CHECK(meanTurns[0] < 0.2);
    CHECK(meanTurns[1] > 0.5);
}

TEST_CASE("turn search: tree reuse keeps the sampled turns below the played move") {
    Game game = makeGame(bigMap());
    game.players[0].stars = 30;
    ScoreEvaluator evaluator;
    SearchConfig config = turnSearchConfig(2);
    config.simulations = 150;
    BatchedSearch search({&game}, {Beliefs{}}, config, 4);
    run(search, evaluator);
    SearchResult first = search.result(0);
    int best = -1;
    for (std::size_t i = 0; i < first.moves.size(); i++) {
        if (first.moves[i].type != ENDTURN &&
            (best < 0 || first.visits[i] > first.visits[best])) {
            best = static_cast<int>(i);
        }
    }
    REQUIRE(best >= 0);
    REQUIRE(first.visits[best] > 3);
    REQUIRE(search.reuse(first.moves[best], config));
    run(search, evaluator);
    SearchResult second = search.result(0);
    int total = 0;
    for (int v : second.visits) {
        total += v;
    }
    // the new root's first visits (fewer than commandVisits) went through
    // sampled completions of the turn, not its moves
    CHECK(total <= config.simulations - 1);
    CHECK(total >= config.simulations - 1 - config.commandVisits);
}

TEST_CASE("a budget in evaluations ends even when every line ends in a known result") {
    // a capture that wins on the spot: once found, simulations down it need
    // no evaluation, so they must still count against the budget
    Game game = emptyGame();
    addUnit(game, 0, UnitTypes::WARRIOR, 6, 6);
    ScoreEvaluator evaluator;
    for (bool turns : {false, true}) {
        SearchConfig config{.simulations = 300, .samples = 1, .horizon = 3,
                            .fullInformation = true, .gumbel = true};
        config.countEvaluations = true;
        config.turnSearch = turns;
        SearchResult r = search(game, Beliefs{}, evaluator, config, 2);
        CHECK(best(r).type == CAPTURE);
    }
}

TEST_CASE("the regularized policy: the prior with no evidence, the best move with plenty") {
    std::vector<double> prior{0.5, 0.3, 0.2}, q{0.0, 0.5, -0.5};
    auto sum = [](const std::vector<double> &p) {
        double s = 0;
        for (double x : p) {
            s += x;
        }
        return s;
    };
    for (double lambda : {0.01, 0.1, 1.0, 10.0}) {
        CAPTURE(lambda);
        std::vector<double> p = regularizedPolicy(prior, q, lambda);
        CHECK(sum(p) == doctest::Approx(1));
        // better moves gain against the prior, worse ones lose
        CHECK(p[1] / prior[1] > p[0] / prior[0]);
        CHECK(p[0] / prior[0] > p[2] / prior[2]);
    }
    // equal values leave the prior as it is
    std::vector<double> even = regularizedPolicy(prior, {0.2, 0.2, 0.2}, 0.3);
    for (std::size_t a = 0; a < prior.size(); a++) {
        CHECK(even[a] == doctest::Approx(prior[a]));
    }
    // a large lambda (little evidence) stays near the prior; a small one
    // (much evidence) goes to the best move
    CHECK(regularizedPolicy(prior, q, 1000)[0] == doctest::Approx(0.5).epsilon(0.01));
    CHECK(regularizedPolicy(prior, q, 0.001)[1] > 0.99);
    // moves without prior get nothing
    CHECK(regularizedPolicy({1.0, 0.0}, {0.0, 1.0}, 0.5)[1] == 0);
}

TEST_CASE("backing up by the regularized policy leans toward the opponent's best reply") {
    // their warrior on the player's capital: of the opponent's replies, the
    // capture ends the game and the rest do nothing. With few simulations the
    // mean still counts the harmless replies; the regularized policy's value
    // is already close to the loss. (With many, it's regularized toward the
    // even prior and ends a little above the mean: here at 20 simulations
    // -0.76 against -0.54; at 160, -0.90 against -0.94.)
    Game game = emptyGame();
    addUnit(game, 1, UnitTypes::WARRIOR, 1, 1);
    ScoreEvaluator evaluator;
    double backed[3];
    int k = 0;
    for (SearchConfig::Backup backup : {SearchConfig::Backup::MEAN, SearchConfig::Backup::PBAR,
                                        SearchConfig::Backup::PBAR_OPPONENT}) {
        SearchConfig config{.simulations = 20, .samples = 1, .horizon = 1,
                            .fullInformation = true};
        config.backup = backup;
        SearchResult r = search(game, Beliefs{}, evaluator, config, 5);
        REQUIRE(r.moves.size() == r.backed.size());
        double value = -2;
        for (std::size_t i = 0; i < r.moves.size(); i++) {
            if (r.moves[i].type == ENDTURN) {
                value = r.backed[i];
                // the reported value stays the mean
                if (backup == SearchConfig::Backup::MEAN) {
                    CHECK(r.backed[i] == doctest::Approx(r.values[i]));
                }
            }
        }
        REQUIRE(value > -2);
        backed[k++] = value;
    }
    CHECK(backed[0] > -0.9);
    CHECK(backed[1] < backed[0] - 0.1);
    CHECK(backed[2] < backed[0] - 0.1);
}

TEST_CASE("the opponent's replies: the most likely first, then distinct deviations from it") {
    // their warrior on the player's capital, the capture by far the likeliest
    // command: the most likely reply is the capture alone
    Game game = emptyGame();
    addUnit(game, 1, UnitTypes::WARRIOR, 1, 1);
    CapturingEvaluator evaluator;
    Command end{ENDTURN};
    for (SearchConfig::Replies replies :
         {SearchConfig::Replies::LIKELY_FIRST, SearchConfig::Replies::DISCREPANCY}) {
        CAPTURE(static_cast<int>(replies));
        SearchConfig config = turnSearchConfig(0);
        config.simulations = 40;
        config.replies = replies;
        BatchedSearch s({&game}, {Beliefs{}}, config, 3);
        run(s, evaluator);
        std::vector<std::vector<Command>> drawn = s.replies(0, end);
        REQUIRE(drawn.size() >= 3);
        CHECK(drawn[0] == std::vector<Command>{{CAPTURE, game.index(1, 1)}});
        if (replies == SearchConfig::Replies::DISCREPANCY) {
            // every one different, each leaving the capture for another first
            // command (the only step the capture turn has)
            for (std::size_t i = 1; i < drawn.size(); i++) {
                CHECK(drawn[i].front().type != CAPTURE);
                for (std::size_t j = 0; j < i; j++) {
                    CHECK(drawn[i] != drawn[j]);
                }
            }
        }
    }
}

TEST_CASE("every way of drawing replies and backing up stays within budget, and reuses its trees") {
    Game game = makeGame(bigMap());
    game.players[0].stars = 30;
    ScoreEvaluator evaluator;
    for (SearchConfig::Replies replies :
         {SearchConfig::Replies::SAMPLED, SearchConfig::Replies::LIKELY_FIRST,
          SearchConfig::Replies::DISCREPANCY}) {
        for (SearchConfig::Backup backup :
             {SearchConfig::Backup::MEAN, SearchConfig::Backup::PBAR,
              SearchConfig::Backup::PBAR_OPPONENT}) {
            CAPTURE(static_cast<int>(replies));
            CAPTURE(static_cast<int>(backup));
            SearchConfig config = turnSearchConfig(4);
            config.simulations = 200;
            config.gumbel = true;
            config.countEvaluations = true;
            config.replies = replies;
            config.backup = backup;
            BatchedSearch s({&game}, {Beliefs{}}, config, 7);
            run(s, evaluator);
            BatchedSearch::DepthStats d = s.takeDepthStats();
            CHECK(d.evaluations >= config.simulations);
            CHECK(d.evaluations < config.simulations + config.maxTurnCommands);
            CHECK(d.reachedOpponent > 0);
            SearchResult r = s.result(0);
            REQUIRE(r.chosen >= 0);
            for (std::size_t i = 0; i < r.moves.size(); i++) {
                if (r.visits[i]) {
                    CHECK(std::abs(r.backed[i]) <= 1.0 + 1e-6);
                }
            }
            if (r.moves[r.chosen].type != ENDTURN) {
                CHECK(s.reuse(r.moves[r.chosen], config));
                run(s, evaluator);
                CHECK_FALSE(s.result(0).moves.empty());
            }
        }
    }
}
