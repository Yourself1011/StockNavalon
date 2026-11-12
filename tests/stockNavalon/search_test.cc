#include "doctest/doctest.h"
#include "helpers.h"
#include "search.h"
#include "view.h"

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
            REQUIRE(!playTurn(game, evaluator, config, rng, network).empty());
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
