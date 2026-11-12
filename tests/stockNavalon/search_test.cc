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
