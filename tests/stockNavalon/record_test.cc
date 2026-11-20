#include "doctest/doctest.h"
#include "encoding.h"
#include "record.h"
#include "selfPlay.h"
#include "view.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <set>
#include <tuple>

namespace {
bool sameMoves(const GameRecord &a, const GameRecord &b) {
    if (a.moves.size() != b.moves.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.moves.size(); i++) {
        if (a.moves[i].player != b.moves[i].player ||
            !(a.moves[i].command == b.moves[i].command) ||
            a.moves[i].visits != b.moves[i].visits) {
            return false;
        }
    }
    return true;
}
// the moves the search chose, which are training positions
std::vector<int> searchedMovesOf(const GameRecord &r) {
    std::vector<int> out;
    for (std::size_t m = 0; m < r.moves.size(); m++) {
        if (!r.moves[m].visits.empty()) {
            out.push_back(static_cast<int>(m));
        }
    }
    return out;
}
} // namespace

TEST_CASE("recorded games replay exactly, and survive a save and load") {
    SelfPlayConfig config;
    config.search.simulations = 20;
    config.maxTurns = 4;
    std::vector<GameRecord> records = playGames(config, 3, 11, 2);

    for (const GameRecord &r : records) {
        REQUIRE(!r.moves.empty());
        Game game = r.start();
        for (const RecordedMove &m : r.moves) {
            REQUIRE(game.currentPlayer == m.player);
            REQUIRE(game.isLegal(m.command));
            game.apply(m.command);
        }
        CHECK(game.turn == r.turns);
        CHECK(game.winner == r.winner);
        for (std::size_t p = 0; p < r.finalScores.size(); p++) {
            CHECK(game.score(static_cast<int>(p)) == r.finalScores[p]);
        }
    }

    // bootstrap games have no final values; one with them, a seat's missing
    for (const GameRecord &r : records) {
        CHECK(r.finalValues.empty());
    }
    records[0].finalValues = {0.25f, std::nanf("")};

    auto path = std::filesystem::temp_directory_path() / "stocknavalon_records_test.txt";
    std::filesystem::remove(path);
    saveRecords(records, path.string());
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    REQUIRE(loaded.size() == records.size());
    for (std::size_t i = 0; i < records.size(); i++) {
        CHECK(loaded[i].map.seed == records[i].map.seed);
        CHECK(loaded[i].map.tribes == records[i].map.tribes);
        CHECK(loaded[i].gameSeed == records[i].gameSeed);
        CHECK(loaded[i].finalScores == records[i].finalScores);
        CHECK(loaded[i].finalValues.size() == records[i].finalValues.size());
        CHECK(loaded[i].finalTerritory == records[i].finalTerritory);
        CHECK(loaded[i].turnScores == records[i].turnScores);
        CHECK(sameMoves(loaded[i], records[i]));
    }
    CHECK(loaded[0].finalValues[0] == doctest::Approx(0.25));
    CHECK(std::isnan(loaded[0].finalValues[1]));
    // games recorded now keep the current rules
    for (const GameRecord &r : loaded) {
        CHECK(r.rulesVersion == RuleVersions::LATEST);
    }
}

TEST_CASE("a record saved before diplomacy introduced the sender replays without it") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 2;
    std::vector<GameRecord> records = playGames(config, 1, 4, 1);
    // no tables, so the header ends with the rule flags, three empty tables
    // and no head start
    records[0].turnStars.clear();
    records[0].turnIncome.clear();
    records[0].turnValues.clear();
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_old_records_test.txt";
    saveRecords(records, path.string(), false);
    // drop the header's tokens from the rule flag on, as files written
    // before them were
    std::string text;
    {
        std::ifstream in(path);
        std::stringstream all;
        all << in.rdbuf();
        text = all.str();
    }
    std::size_t end = text.find('\n');
    REQUIRE(text.compare(end - 31, 31, " 1 1 0 0 0 1 0 0 0 0 -1 rules 4") == 0);
    // the formats before: no version (the flags it has say version 2), and
    // the third and fourth fixes as flags
    for (auto [tail, version] : std::vector<std::pair<std::string, int>>{
             {"", RuleVersions::ADJACENT_PUSHES},
             {" 0", RuleVersions::ADJACENT_PUSHES},
             {" 1", RuleVersions::UNSEEN_ZONE_OF_CONTROL},
             {" 1 0", RuleVersions::UNSEEN_ZONE_OF_CONTROL},
             {" 1 1", RuleVersions::EMBARK_STOMP}}) {
        CAPTURE(tail);
        std::string older = text;
        older.replace(end - 8, 8, tail);
        {
            std::ofstream out(path, std::ios::trunc);
            out << older;
        }
        CHECK(loadRecords(path.string()).at(0).rulesVersion == version);
    }
    text.erase(end - 29, 29);
    {
        std::ofstream out(path, std::ios::trunc);
        out << text;
    }
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].rulesVersion == 0);
    CHECK(loaded[0].start().settings.rulesVersion == 0);
    CHECK_FALSE(loaded[0].start().settings.follows(RuleVersions::DIPLOMACY_MEETS));
    CHECK(sameMoves(loaded[0], records[0]));
}

TEST_CASE("training examples from records") {
    SelfPlayConfig config;
    config.search.simulations = 20;
    config.maxTurns = 3;
    std::vector<GameRecord> records = playGames(config, 2, 5, 2);
    EncodingConfig enc;
    std::vector<std::pair<int, int>> positions;
    for (int r = 0; r < 2; r++) {
        for (int m = 0; m < static_cast<int>(records[r].moves.size()); m += 3) {
            positions.push_back({r, m});
        }
    }
    const std::size_t b = positions.size(), n = enc.mapSize,
                      a = actionCount(enc), t = targetCount(enc);
    std::vector<float> planes(b * planeCount(enc) * n * n), globals(b * globalCount(enc)),
        policy(b * a), value(b), weight(b), targets(b * t);
    std::vector<std::uint8_t> legal(b * a), masks(b * t);
    encodeExamples(records, positions, enc,
                   {planes.data(), globals.data(), legal.data(), policy.data(),
                    value.data(), weight.data(), targets.data(), masks.data()});
    for (std::size_t i = 0; i < b; i++) {
        double sum = 0;
        for (std::size_t s = 0; s < a; s++) {
            float p = policy[i * a + s];
            sum += p;
            // the search only visits legal commands
            if (p > 0) {
                CHECK(legal[i * a + s] == 1);
            }
        }
        const RecordedMove &m = records[positions[i].first].moves[positions[i].second];
        CHECK(sum == doctest::Approx(m.visits.empty() ? 0 : 1));
        CHECK(std::abs(value[i]) <= 1);
    }
}

TEST_CASE("a self-play session runs many games, replayable, until it has them all") {
    SelfPlayConfig config;
    config.search.simulations = 10;
    config.maxTurns = 3;
    SelfPlaySession session(config, 3, 5, 9);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    std::size_t largest = 0;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        // advancing can finish the last games, leaving nothing to evaluate
        if (requests.empty()) {
            CHECK(session.done());
            break;
        }
        largest = std::max(largest, requests.size());
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 5);
    // several games' positions go out together
    CHECK(largest > 2);
    for (const GameRecord &r : records) {
        Game game = r.start();
        for (const RecordedMove &m : r.moves) {
            REQUIRE(game.isLegal(m.command));
            game.apply(m.command);
        }
        CHECK(game.turn == r.turns);
    }
}

TEST_CASE("network output becomes the search's priors and values") {
    SelfPlayConfig config;
    config.search.simulations = 10;
    SelfPlaySession session(config, 2, 2, 4);
    EncodingConfig enc;
    const std::vector<EvalRequest> &requests = session.pending();
    REQUIRE(!requests.empty());
    const std::size_t b = requests.size(), a = actionCount(enc);
    std::vector<float> logits(b * a, 0), values(b, 0.25f);
    // raise one move's logit in the first request
    const EvalRequest &first = requests[0];
    REQUIRE(first.moves);
    REQUIRE(first.moves->size() > 1);
    int slot = encodeCommand(*first.game, (*first.moves)[1], enc);
    REQUIRE(slot >= 0);
    logits[slot] = 3;
    std::vector<Evaluation> results;
    evaluationsFromNetwork(requests, enc, logits.data(), values.data(), results);
    REQUIRE(results.size() == b);
    double sum = 0;
    for (float p : results[0].priors) {
        sum += p;
    }
    CHECK(sum == doctest::Approx(1));
    CHECK(results[0].priors[1] ==
          *std::max_element(results[0].priors.begin(), results[0].priors.end()));
    CHECK(results[0].priors[1] > results[0].priors[0]);
    CHECK(results[0].value == doctest::Approx(0.25));
}

TEST_CASE("the value target is about winning, and a cut-off game is truncated, not drawn") {
    GameRecord decided, cut, older;
    decided.winner = 0;
    decided.finalScores = {1000, 1500};
    // a decided game's result wins over any final value
    decided.finalValues = {-0.5f, 0.5f};
    cut.winner = -1;
    cut.finalScores = {1000, 1500};
    cut.finalValues = {0.4f, std::nanf("")};
    older.winner = -1;
    older.finalScores = {1000, 1500};

    CHECK(decided.value(0) == 1);
    CHECK(decided.value(1) == -1);
    // the score lead never enters: player 0 is behind, but expected to win
    CHECK(cut.value(0) == doctest::Approx(0.4));
    // no value for that seat, or none at all: a draw
    CHECK(cut.value(1) == 0);
    CHECK(older.value(0) == 0);
}

TEST_CASE("self-play keeps each seat's last network value for a game the limit cuts off") {
    SelfPlayConfig config;
    config.search.simulations = 10;
    config.maxTurns = 2;
    // a stand-in network: uniform priors, and a value it believes
    struct Fixed : Evaluator {
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                std::size_t n = requests[i].moves ? requests[i].moves->size() : 0;
                results[i].priors.assign(n, n ? 1.0f / n : 0.0f);
                results[i].value = 0.3f;
            }
        }
    } network;
    auto play = [&](const SelfPlayConfig &c) {
        SelfPlaySession session(c, 2, 2, 4);
        std::vector<Evaluation> results;
        while (!session.done()) {
            const std::vector<EvalRequest> &requests = session.pending();
            if (requests.empty()) {
                break;
            }
            network.evaluate(requests, results);
            session.submit(results);
        }
        return session.takeFinished();
    };
    for (const GameRecord &r : play(config)) {
        REQUIRE(r.winner < 0);
        REQUIRE(r.finalValues.size() == 2);
        for (float v : r.finalValues) {
            CHECK(v >= -1);
            CHECK(v <= 1);
        }
    }
    // the score evaluator's side has none: its values are score leads
    SelfPlayConfig score = config;
    score.sides = {SideKind::SCORE, SideKind::SCORE};
    for (const GameRecord &r : play(score)) {
        CHECK(r.finalValues.empty());
    }
}

TEST_CASE("a stalemate ends a game once the fighting has stopped long enough") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 60;
    config.stallTurns = 4;
    // the random bot attacks often, then stops for a while sooner or later
    config.sides = {SideKind::RANDOM, SideKind::RANDOM};
    SelfPlaySession session(config, 8, 8, 21);
    while (!session.done()) {
        session.pending();
    }
    int stalled = 0;
    for (const GameRecord &r : session.takeFinished()) {
        if (!r.stalled) {
            continue;
        }
        stalled++;
        CHECK(r.winner < 0);
        CHECK(r.turns < config.maxTurns);
        // the last attack or capture came exactly stallTurns turns before the end
        Game game = r.start();
        int lastCombat = -1;
        for (const RecordedMove &m : r.moves) {
            if (m.command.type == CommandTypes::ATTACK ||
                m.command.type == CommandTypes::CAPTURE) {
                lastCombat = game.turn;
            }
            game.apply(m.command);
        }
        CHECK(lastCombat >= 0);
        CHECK(r.turns - lastCombat == config.stallTurns);
    }
    CHECK(stalled > 0);
}

TEST_CASE("side targets: the score lead soon and at the end, and the final territory") {
    using namespace SideTargets;
    SelfPlayConfig config;
    config.search.simulations = 10;
    config.maxTurns = 12;
    std::vector<GameRecord> records = playGames(config, 2, 31, 2);
    EncodingConfig enc;
    const int n = enc.mapSize, count = sideTargetCount(enc);
    std::vector<float> t(count);
    std::vector<std::uint8_t> m(count);
    for (const GameRecord &r : records) {
        // replay to the end, keeping the published scores
        Game game = r.start();
        for (const RecordedMove &move : r.moves) {
            game.apply(move.command);
        }
        REQUIRE(r.finalTerritory.size() == static_cast<std::size_t>(game.size * game.size));
        REQUIRE(r.turnScores.size() == static_cast<std::size_t>((game.turn + 1) * 2));
        // a player's latest published score at a turn
        auto published = [&](int player, int turn) {
            int score = 0;
            for (const ScoreRecord &s : game.scoreHistory) {
                if (s.player == player && s.turn <= turn) {
                    score = s.score;
                }
            }
            return score;
        };
        for (int player = 0; player < 2; player++) {
            for (int turn : {0, 3, game.turn}) {
                encodeSideTargets(r, turn, player, enc, t.data(), m.data());
                int q = 1 - player;
                CHECK(t[SCORE_END] == doctest::Approx(std::tanh(
                    (r.finalScores[player] - r.finalScores[q]) / SCORE_SCALE)));
                int soon = turn + SOON_TURNS;
                float expected = soon <= game.turn
                    ? static_cast<float>(std::tanh((published(player, soon) -
                                                    published(q, soon)) / SCORE_SCALE))
                    : t[SCORE_END];
                CHECK(t[SCORE_SOON] == doctest::Approx(expected));
                CHECK(m[SCORE_SOON] == 1);
                // one class per map tile: mine, theirs, nobody's
                for (int tile = 0; tile < game.size * game.size; tile++) {
                    int x = game.tileX(tile), y = game.tileY(tile), owner = game.tileOwner(tile);
                    int cls = owner == player ? 0 : owner < 0 ? 2 : 1;
                    for (int c = 0; c < TERRITORY_CLASSES; c++) {
                        CHECK(t[TERRITORY + c * n * n + y * n + x] == (c == cls));
                        CHECK(m[TERRITORY + c * n * n + y * n + x] == 1);
                    }
                }
            }
        }
    }
    // an older record has only the final score lead
    GameRecord older = records[0];
    older.finalTerritory.clear();
    older.turnScores.clear();
    encodeSideTargets(older, 2, 0, enc, t.data(), m.data());
    CHECK(m[SCORE_END] == 1);
    CHECK(m[SCORE_SOON] == 0);
    CHECK(std::count(m.begin() + TERRITORY, m.end(), 1) == 0);
}

TEST_CASE("without fog, self-play searches the real game and records it as fog-free") {
    std::vector<TribeTypes::Tribe> lineup = {TribeTypes::OUMAJI, TribeTypes::XINXI};
    Game game(generateMap({.size = 11, .type = MapTypes::PANGEA, .tribes = lineup, .seed = 8}),
              lineup, {.seed = 8});
    SearchConfig search{.simulations = 6, .samples = 3, .fullInformation = true};
    BatchedSearch batched({&game}, {Beliefs{}}, search, 3);
    const std::vector<EvalRequest> &requests = batched.pending();
    REQUIRE(!requests.empty());
    // one tree, on the real game (not three samples), its future dice reseeded
    CHECK(requests.size() == 1);
    CHECK(requests[0].fullInformation);
    CHECK(requests[0].game->units.size() == game.units.size());
    CHECK(requests[0].game->rng != game.rng);

    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 3;
    config.fogShare = 0;
    std::vector<GameRecord> records = playGames(config, 2, 6, 2);
    for (const GameRecord &r : records) {
        CHECK_FALSE(r.fog);
    }
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_fog_test.txt";
    std::filesystem::remove(path);
    saveRecords(records, path.string());
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    CHECK_FALSE(loaded[0].fog);

    // training examples see a fog-free game the way it was played
    EncodingConfig enc;
    const std::size_t c = planeCount(enc) * enc.mapSize * enc.mapSize;
    std::vector<float> planes(c), globals(globalCount(enc)), policy(actionCount(enc)),
        value(1), weight(1), targets(targetCount(enc));
    std::vector<std::uint8_t> legal(actionCount(enc)), masks(targetCount(enc));
    int move = static_cast<int>(records[0].moves.size()) - 1;
    encodeExamples(records, {{0, move}}, enc,
                   {planes.data(), globals.data(), legal.data(), policy.data(),
                    value.data(), weight.data(), targets.data(), masks.data()});
    Game replay = records[0].start();
    for (int m = 0; m < move; m++) {
        replay.apply(records[0].moves[m].command);
    }
    std::vector<float> expected(c), expectedGlobals(globalCount(enc));
    encodeState(replay, replay.currentPlayer, enc, expected.data(), expectedGlobals.data(), true);
    CHECK(planes == expected);
    CHECK(std::count(masks.begin(), masks.end(), 1) == 0);
}

TEST_CASE("with network beliefs, each player's turn asks for them once, before searching") {
    SelfPlayConfig config;
    config.search.simulations = 6;
    config.maxTurns = 4;
    config.networkBeliefs = true;
    SelfPlaySession session(config, 2, 2, 12);
    int beliefRequests = 0;
    std::set<std::tuple<const Game *, int, int>> asked;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        results.assign(requests.size(), {});
        for (std::size_t i = 0; i < requests.size(); i++) {
            const EvalRequest &r = requests[i];
            std::size_t n = r.moves ? r.moves->size() : 0;
            results[i].priors.assign(n, n ? 1.0f / n : 0.0f);
            if (r.wantsBeliefs) {
                // a belief request evaluates nothing, and comes alone
                CHECK(r.moves == nullptr);
                beliefRequests++;
                results[i].beliefs = priorBeliefs(makeView(*r.game, r.player));
            }
        }
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    // one per player turn played with a search: every turn of these games
    int turns = 0;
    for (const GameRecord &r : records) {
        Game game = r.start();
        std::set<std::pair<int, int>> seen;
        for (const RecordedMove &m : r.moves) {
            seen.insert({game.turn, game.currentPlayer});
            game.apply(m.command);
        }
        turns += static_cast<int>(seen.size());
    }
    CHECK(beliefRequests == turns);
}

TEST_CASE("the value target can blend in the city lead, and cut-off games weigh less") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 3;
    std::vector<GameRecord> records = playGames(config, 1, 14, 1);
    // one decided and one cut off, from the same game
    GameRecord decided = records[0];
    decided.winner = 0;
    GameRecord cut = records[0];
    cut.winner = -1;
    cut.finalValues = {0.4f, -0.4f};
    std::vector<GameRecord> both = {decided, cut};
    EncodingConfig enc;
    const std::size_t c = planeCount(enc) * enc.mapSize * enc.mapSize, a = actionCount(enc),
                      t = targetCount(enc);
    std::vector<float> planes(2 * c), globals(2 * globalCount(enc)), policy(2 * a), value(2),
        weight(2), targets(2 * t);
    std::vector<std::uint8_t> legal(2 * a), masks(2 * t);
    ExampleArrays out{planes.data(), globals.data(), legal.data(), policy.data(),
                      value.data(), weight.data(), targets.data(), masks.data()};
    // move 0 is player 0's
    encodeExamples(both, {{0, 0}, {1, 0}}, enc, out, 1, {},
                   {.cityWeight = 0.5, .cutOffWeight = 0.3});
    float lead = decided.cityValue(0);
    CHECK(value[0] == doctest::Approx(0.5 * 1 + 0.5 * lead));
    CHECK(value[1] == doctest::Approx(0.5 * 0.4 + 0.5 * lead));
    CHECK(weight[0] == 1);
    CHECK(weight[1] == doctest::Approx(0.3));
    // by default: wins only, every position weighing the same
    encodeExamples(both, {{0, 0}, {1, 0}}, enc, out);
    CHECK(value[0] == 1);
    CHECK(value[1] == doctest::Approx(0.4));
    CHECK(weight[1] == 1);
}

TEST_CASE("examples count the positions in their player's turn") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 4;
    config.fullShare = 0.5;
    std::vector<GameRecord> records = playGames(config, 1, 23, 1);
    const GameRecord &r = records[0];
    // the expected counts: by (turn, player), replaying the game
    std::map<std::pair<int, int>, int> perTurn;
    std::vector<std::pair<int, int>> positions;
    std::vector<std::pair<int, int>> turnOf;
    Game game = r.start();
    for (std::size_t i = 0; i < r.moves.size(); i++) {
        if (!r.moves[i].visits.empty()) {
            perTurn[{game.turn, game.currentPlayer}]++;
            positions.push_back({0, static_cast<int>(i)});
            turnOf.push_back({game.turn, game.currentPlayer});
        }
        game.apply(r.moves[i].command);
    }
    REQUIRE(positions.size() > 4);
    EncodingConfig enc;
    const std::size_t b = positions.size(), c = planeCount(enc) * enc.mapSize * enc.mapSize,
                      a = actionCount(enc), t = targetCount(enc);
    std::vector<float> planes(b * c), globals(b * globalCount(enc)), policy(b * a), value(b),
        weight(b), targets(b * t), turnPositions(b);
    std::vector<std::uint8_t> legal(b * a), masks(b * t);
    ExampleArrays out{planes.data(), globals.data(), legal.data(), policy.data(),
                      value.data(),  weight.data(),  targets.data(), masks.data()};
    out.turnPositions = turnPositions.data();
    encodeExamples(records, positions, enc, out);
    bool someLong = false;
    for (std::size_t i = 0; i < b; i++) {
        CHECK(turnPositions[i] == perTurn[turnOf[i]]);
        someLong |= turnPositions[i] > 1;
    }
    CHECK(someLong);
}

TEST_CASE("the value target can be a lambda-return of the network's values of later turns") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 4;
    std::vector<GameRecord> records = playGames(config, 1, 14, 1);
    GameRecord &r = records[0];
    r.winner = 0;
    const int players = 2;
    REQUIRE(r.turns >= 3);
    // known estimates: player 0's turn t is worth 0.1 x t, player 1's -0.2;
    // a turn with none passes the return on
    r.valueEstimates.assign(static_cast<std::size_t>(r.turns + 1) * players, 0.0f);
    for (int t = 0; t <= r.turns; t++) {
        r.valueEstimates[t * players] = 0.1f * t;
        r.valueEstimates[t * players + 1] = -0.2f;
    }
    r.valueEstimates[2 * players] = std::nanf("");
    Game game = r.start();
    REQUIRE(r.moves[0].player == 0);
    const int turn = game.turn;
    // by hand: from the result back to the turn after the position's
    const double lambda = 0.5;
    double expected = 1.0;
    for (int t = r.turns; t > turn; t--) {
        float v = r.valueEstimates[t * players];
        if (!std::isnan(v)) {
            expected = (1 - lambda) * v + lambda * expected;
        }
    }
    CHECK(lambdaReturn(r, 0, turn + 1, lambda, r.valueEstimates) == doctest::Approx(expected));
    CHECK(lambdaReturn(r, 0, turn + 1, 1.0, r.valueEstimates) == doctest::Approx(1.0));
    EncodingConfig enc;
    const std::size_t c = planeCount(enc) * enc.mapSize * enc.mapSize, a = actionCount(enc),
                      t = targetCount(enc);
    std::vector<float> planes(c), globals(globalCount(enc)), policy(a), value(1), weight(1),
        targets(t);
    std::vector<std::uint8_t> legal(a), masks(t);
    ExampleArrays out{planes.data(), globals.data(), legal.data(), policy.data(),
                      value.data(),  weight.data(),  targets.data(), masks.data()};
    encodeExamples(records, {{0, 0}}, enc, out, 1, {}, {.lambda = lambda});
    CHECK(value[0] == doctest::Approx(expected));
    encodeExamples(records, {{0, 0}}, enc, out);
    CHECK(value[0] == 1);
    // without estimates, the result: never the recorded search values,
    // which carry the biases of the network that played
    r.valueEstimates.clear();
    encodeExamples(records, {{0, 0}}, enc, out, 1, {}, {.lambda = lambda});
    CHECK(value[0] == 1);
}

TEST_CASE("reanalysis: the buffer encodes each player turn's start and keeps the network's values") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 4;
    std::vector<GameRecord> games = playGames(config, 2, 31, 1);
    std::uint64_t seed0 = games[0].gameSeed, seed1 = games[1].gameSeed;
    REQUIRE(seed0 != seed1);
    const GameRecord copy = games[0];
    ReplayBuffer buffer(1'000'000);
    buffer.add(games);
    CHECK(buffer.gameSeeds(true).size() == 2);
    EncodingConfig enc;
    std::vector<float> planes, globals;
    // an unknown seed is skipped
    std::vector<ReplayBuffer::TurnStart> starts =
        buffer.encodeTurnStarts({seed0, 12345}, enc, planes, globals, 2);
    REQUIRE(!starts.empty());
    const std::size_t p = planeCount(enc) * enc.mapSize * enc.mapSize, g = globalCount(enc);
    CHECK(planes.size() == starts.size() * p);
    CHECK(globals.size() == starts.size() * g);
    // the same as encoding each turn's first position directly
    Game game = copy.start();
    std::size_t k = 0;
    for (std::size_t m = 0; m < copy.moves.size(); m++) {
        if (m == 0 || copy.moves[m].player != copy.moves[m - 1].player) {
            REQUIRE(k < starts.size());
            CHECK(starts[k].gameSeed == seed0);
            CHECK(starts[k].turn == game.turn);
            CHECK(starts[k].player == game.currentPlayer);
            std::vector<float> pl(p), gl(g);
            encodeState(game, game.currentPlayer, enc, pl.data(), gl.data(), !copy.fog);
            CHECK(std::equal(pl.begin(), pl.end(), planes.begin() + k * p));
            CHECK(std::equal(gl.begin(), gl.end(), globals.begin() + k * g));
            k++;
        }
        game.applyUnchecked(copy.moves[m].command);
    }
    CHECK(k == starts.size());
    std::vector<float> values(starts.size(), 0.25f);
    buffer.setValueEstimates(starts, values);
    // only the other game is left to reanalyse
    CHECK(buffer.gameSeeds(true) == std::vector<std::uint64_t>{seed1});
    CHECK(buffer.gameSeeds().size() == 2);
    const GameRecord &held = buffer.games()[0];
    REQUIRE(held.gameSeed == seed0);
    CHECK(held.valueEstimates[starts[0].turn * 2 + starts[0].player] == doctest::Approx(0.25));
}

TEST_CASE("records keep the final cities and whether peace treaties were allowed") {
    SelfPlayConfig config;
    config.search.simulations = 6;
    config.maxTurns = 3;
    config.peaceTreaties = false;
    std::vector<GameRecord> records = playGames(config, 1, 19, 1);
    GameRecord &r = records[0];
    CHECK_FALSE(r.peaceTreaties);
    // the replay has the same rules, and the counts match it
    Game game = r.start();
    CHECK_FALSE(game.settings.peaceTreaties);
    for (const RecordedMove &m : r.moves) {
        REQUIRE(game.isLegal(m.command));
        game.apply(m.command);
    }
    std::vector<int> cities(2, 0);
    for (const City &c : game.cities) {
        if (c.owner >= 0) {
            cities[c.owner]++;
        }
    }
    CHECK(r.finalCities == cities);
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_cities_test.txt";
    std::filesystem::remove(path);
    saveRecords(records, path.string());
    GameRecord loaded = loadRecords(path.string())[0];
    std::filesystem::remove(path);
    CHECK_FALSE(loaded.peaceTreaties);
    CHECK(loaded.finalCities == cities);
    // the city lead: 3 cities against 1 is +0.5 for the first, -0.5 for the other
    loaded.finalCities = {3, 1};
    CHECK(loaded.cityValue(0) == doctest::Approx(0.5));
    CHECK(loaded.cityValue(1) == doctest::Approx(-0.5));
}

TEST_CASE("thinning a match's games keeps a share of their training positions") {
    SelfPlayConfig config;
    config.search.simulations = 6;
    config.maxTurns = 4;
    config.fullShare = 1.0;
    std::vector<GameRecord> records = playGames(config, 2, 23, 2);
    auto searched = [](const std::vector<GameRecord> &rs) {
        int n = 0, all = 0;
        for (const GameRecord &r : rs) {
            for (const RecordedMove &m : r.moves) {
                n += !m.visits.empty();
                all++;
            }
        }
        return std::pair{n, all};
    };
    auto [before, all] = searched(records);
    REQUIRE(before > 30);
    thinPolicyTargets(records, 0.5, 4);
    auto [after, same] = searched(records);
    CHECK(same == all);
    CHECK(after > before * 0.35);
    CHECK(after < before * 0.65);
}

TEST_CASE("tree reuse saves evaluations, and games still replay") {
    struct Counting : Evaluator {
        int positions = 0;
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            positions += static_cast<int>(requests.size());
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                std::size_t n = requests[i].moves ? requests[i].moves->size() : 0;
                results[i].priors.assign(n, n ? 1.0f / n : 0.0f);
                results[i].value = 0.1f;
            }
        }
    };
    auto play = [](bool reuse, double fog) {
        SelfPlayConfig config;
        config.search.simulations = 32;
        config.quickSimulations = 8;
        config.fullShare = 0.5;
        config.maxTurns = 6;
        config.fogShare = fog;
        config.treeReuse = reuse;
        SelfPlaySession session(config, 4, 4, 13);
        Counting network;
        std::vector<Evaluation> results;
        while (!session.done()) {
            const std::vector<EvalRequest> &requests = session.pending();
            if (requests.empty()) {
                break;
            }
            network.evaluate(requests, results);
            session.submit(results);
        }
        std::vector<GameRecord> records = session.takeFinished();
        int moves = 0;
        for (const GameRecord &r : records) {
            Game game = r.start();
            for (const RecordedMove &m : r.moves) {
                REQUIRE(game.isLegal(m.command));
                game.apply(m.command);
            }
            moves += static_cast<int>(r.moves.size());
        }
        return static_cast<double>(network.positions) / moves;
    };
    for (double fog : {0.0, 1.0}) {
        CAPTURE(fog);
        double without = play(false, fog), with = play(true, fog);
        MESSAGE("evaluations per move with fog " << fog << ": " << without << " without reuse, "
                                                   << with << " with");
        // about 10% fewer, but a handful of games is noisy: the maps alone
        // move it between 7% and 14%
        CHECK(with < without * 0.95);
    }
}

TEST_CASE("each searched move keeps its search value, and the value target can blend it in") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 3;
    // a stand-in network: every position worth 0.2 to its player
    struct Fixed : Evaluator {
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                std::size_t n = requests[i].moves ? requests[i].moves->size() : 0;
                results[i].priors.assign(n, n ? 1.0f / n : 0.0f);
                results[i].value = 0.2f;
            }
        }
    } network;
    GameRecord record = playGame(config, network, 7);
    int withValue = 0;
    for (const RecordedMove &m : record.moves) {
        withValue += !std::isnan(m.rootValue);
        if (!std::isnan(m.rootValue)) {
            CHECK(m.rootValue >= -1);
            CHECK(m.rootValue <= 1);
        }
    }
    CHECK(withValue > 0);
    // the score evaluator's searches have none
    GameRecord scored = playGames(config, 1, 7, 1)[0];
    CHECK(std::all_of(scored.moves.begin(), scored.moves.end(),
                      [](const RecordedMove &m) { return std::isnan(m.rootValue); }));
    // they survive a save and load
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_roots_test.txt";
    std::filesystem::remove(path);
    saveRecords({record}, path.string());
    GameRecord loaded = loadRecords(path.string())[0];
    std::filesystem::remove(path);
    for (std::size_t i = 0; i < record.moves.size(); i++) {
        CHECK((std::isnan(loaded.moves[i].rootValue) ? std::isnan(record.moves[i].rootValue)
                                                      : loaded.moves[i].rootValue ==
                                                            doctest::Approx(record.moves[i].rootValue)));
    }
    // the blend: a quarter of the target from the position's own search value
    int move = 0;
    while (std::isnan(record.moves[move].rootValue)) {
        move++;
    }
    record.winner = record.moves[move].player;
    EncodingConfig enc;
    const std::size_t c = planeCount(enc) * enc.mapSize * enc.mapSize, a = actionCount(enc),
                      t = targetCount(enc);
    std::vector<float> planes(c), globals(globalCount(enc)), policy(a), value(1), weight(1),
        targets(t);
    std::vector<std::uint8_t> legal(a), masks(t);
    encodeExamples({record}, {{0, move}}, enc,
                   {planes.data(), globals.data(), legal.data(), policy.data(), value.data(),
                    weight.data(), targets.data(), masks.data()},
                   1, {}, {.searchWeight = 0.25});
    CHECK(value[0] == doctest::Approx(0.75 * 1 + 0.25 * record.moves[move].rootValue));
}

TEST_CASE("a session's games don't depend on its thread count") {
    auto play = [](int threads) {
        SelfPlayConfig config;
        config.search.simulations = 16;
        config.quickSimulations = 4;
        config.fullShare = 0.5;
        config.maxTurns = 5;
        config.fogShare = 0.5;
        config.threads = threads;
        SelfPlaySession session(config, 6, 9, 31);
        ScoreEvaluator evaluator;
        std::vector<Evaluation> results;
        while (!session.done()) {
            const std::vector<EvalRequest> &requests = session.pending();
            if (requests.empty()) {
                break;
            }
            evaluator.evaluate(requests, results);
            session.submit(results);
        }
        std::vector<GameRecord> records = session.takeFinished();
        // games finish in whatever order the threads get to them
        std::sort(records.begin(), records.end(),
                  [](const GameRecord &a, const GameRecord &b) {
                      return a.gameSeed < b.gameSeed;
                  });
        return records;
    };
    std::vector<GameRecord> one = play(1), four = play(4);
    REQUIRE(one.size() == 9);
    REQUIRE(four.size() == one.size());
    for (std::size_t i = 0; i < one.size(); i++) {
        CHECK(one[i].gameSeed == four[i].gameSeed);
        CHECK(sameMoves(one[i], four[i]));
        CHECK(one[i].fog == four[i].fog);
    }
}

TEST_CASE("a session numbered from a later game plays that share of a larger session's games") {
    // the games [first, first + games) of seed 31, sorted
    auto play = [](int games, int first) {
        SelfPlayConfig config;
        config.search.simulations = 16;
        config.quickSimulations = 4;
        config.fullShare = 0.5;
        config.maxTurns = 5;
        config.fogShare = 0.5;
        config.paired = true;
        SelfPlaySession session(config, 4, games, 31, first);
        ScoreEvaluator evaluator;
        std::vector<Evaluation> results;
        while (!session.done()) {
            const std::vector<EvalRequest> &requests = session.pending();
            if (requests.empty()) {
                break;
            }
            evaluator.evaluate(requests, results);
            session.submit(results);
        }
        return session.takeFinished();
    };
    std::vector<GameRecord> whole = play(8, 0), split = play(4, 0), rest = play(4, 4);
    split.insert(split.end(), rest.begin(), rest.end());
    auto order = [](const GameRecord &a, const GameRecord &b) {
        return std::pair(a.gameSeed, a.seatZeroSide) < std::pair(b.gameSeed, b.seatZeroSide);
    };
    std::sort(whole.begin(), whole.end(), order);
    std::sort(split.begin(), split.end(), order);
    REQUIRE(whole.size() == 8);
    REQUIRE(split.size() == whole.size());
    for (std::size_t i = 0; i < whole.size(); i++) {
        CHECK(whole[i].gameSeed == split[i].gameSeed);
        CHECK(whole[i].seatZeroSide == split[i].seatZeroSide);
        CHECK(sameMoves(whole[i], split[i]));
        CHECK(whole[i].fog == split[i].fog);
    }
}

TEST_CASE("self-play with Gumbel search records the improved policy as the target") {
    SelfPlayConfig config;
    config.search.simulations = 16;
    config.quickSimulations = 4;
    config.fullShare = 0.5;
    config.maxTurns = 4;
    config.search.rootNoise = true;
    config.search.gumbel = true;
    SelfPlaySession session(config, 3, 3, 8);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 3);
    int targets = 0;
    for (const GameRecord &r : records) {
        Game game = r.start();
        for (const RecordedMove &m : r.moves) {
            REQUIRE(game.isLegal(m.command));
            if (!m.visits.empty()) {
                targets++;
                int sum = 0;
                for (const auto &[command, weight] : m.visits) {
                    CHECK(weight >= 0);
                    sum += weight;
                }
                // whole weights, so a little rounding
                CHECK(std::abs(sum - POLICY_WEIGHT_SCALE) <= static_cast<int>(m.visits.size()));
            }
            game.apply(m.command);
        }
    }
    CHECK(targets > 0);
    // they encode into a distribution like visit counts do
    std::vector<std::pair<int, int>> positions;
    for (std::size_t m = 0; m < records[0].moves.size(); m++) {
        if (!records[0].moves[m].visits.empty()) {
            positions.push_back({0, static_cast<int>(m)});
        }
    }
    REQUIRE(!positions.empty());
    EncodingConfig enc;
    const std::size_t b = positions.size(), a = actionCount(enc), n = enc.mapSize;
    std::vector<float> planes(b * planeCount(enc) * n * n), globals(b * globalCount(enc)),
        policy(b * a), value(b), weight(b), targets_(b * targetCount(enc)),
        side(b * sideTargetCount(enc));
    std::vector<std::uint8_t> legal(b * a), masks(b * targetCount(enc)),
        sideMasks(b * sideTargetCount(enc));
    encodeExamples(records, positions, enc,
                   {planes.data(), globals.data(), legal.data(), policy.data(), value.data(),
                    weight.data(), targets_.data(), masks.data(), side.data(), sideMasks.data()});
    for (std::size_t i = 0; i < b; i++) {
        double sum = 0;
        for (std::size_t s = 0; s < a; s++) {
            sum += policy[i * a + s];
        }
        CHECK(sum == doctest::Approx(1).epsilon(0.01));
    }
}

TEST_CASE("a record replays legally only under the rules it was played with") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.maxTurns = 3;
    std::vector<GameRecord> records = playGames(config, 2, 6, 1);
    for (GameRecord &r : records) {
        CHECK(r.replaysLegally());
        // a command that was never legal
        r.moves.insert(r.moves.begin(), {.player = 0, .command = {CommandTypes::CAPTURE, 0}});
        CHECK_FALSE(r.replaysLegally());
    }
}

TEST_CASE("side targets: the player's own stars and income some turns ahead") {
    using namespace SideTargets;
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.quickSimulations = 4;
    config.maxTurns = 14;
    SelfPlaySession session(config, 2, 2, 17);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 2);
    // kept through a save and load
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_economy_test.txt";
    saveRecords(records, path.string(), false);
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    EncodingConfig enc;
    const int count = sideTargetCount(enc);
    for (std::size_t g = 0; g < records.size(); g++) {
        const GameRecord &r = records[g];
        CHECK(loaded[g].turnStars == r.turnStars);
        CHECK(loaded[g].turnIncome == r.turnIncome);
        REQUIRE(r.turnStars.size() == static_cast<std::size_t>(r.turns + 1) * 2);
        // replayed, each player's stars and income as their turn starts are
        // what was recorded
        Game game = r.start();
        int turn = -1, mover = -1;
        for (const RecordedMove &m : r.moves) {
            if (game.turn != turn || game.currentPlayer != mover) {
                turn = game.turn;
                mover = game.currentPlayer;
                std::size_t at = static_cast<std::size_t>(turn) * 2 + mover;
                CHECK(r.turnStars[at] == game.players[mover].stars);
                CHECK(r.turnIncome[at] == game.income(mover));
            }
            game.apply(m.command);
        }
        // the target at turn 1 is the player's from turn 11
        std::vector<float> t(count);
        std::vector<std::uint8_t> mask(count);
        encodeSideTargets(r, 1, 0, enc, t.data(), mask.data());
        REQUIRE(mask[STARS_AHEAD]);
        CHECK(t[STARS_AHEAD] == doctest::Approx(r.turnStars[11 * 2] / STARS_SCALE));
        CHECK(t[INCOME_AHEAD] == doctest::Approx(r.turnIncome[11 * 2] / INCOME_SCALE));
    }
    // an older record has none
    GameRecord older = records[0];
    older.turnStars.clear();
    older.turnIncome.clear();
    std::vector<float> t(count);
    std::vector<std::uint8_t> mask(count);
    encodeSideTargets(older, 1, 0, enc, t.data(), mask.data());
    CHECK_FALSE(mask[STARS_AHEAD]);
    CHECK_FALSE(mask[INCOME_AHEAD]);
    // after a capture, the loser has nothing
    GameRecord lost = records[0];
    lost.winner = 1;
    std::fill(lost.turnStars.end() - 2, lost.turnStars.end() - 1, 0);
    std::fill(lost.turnIncome.end() - 2, lost.turnIncome.end() - 1, 0);
    encodeSideTargets(lost, lost.turns - 1, 0, enc, t.data(), mask.data());
    CHECK(t[STARS_AHEAD] == 0);
    CHECK(t[INCOME_AHEAD] == 0);
}

TEST_CASE("side targets: short-term values are lambda-returns of the player's search values") {
    using namespace SideTargets;
    EncodingConfig enc;
    const int count = sideTargetCount(enc);
    GameRecord r;
    r.map.size = 11;
    r.winner = 0;
    r.turns = 4;
    r.finalScores = {1000, 900};
    // turns x players; player 1 has no value in turn 2
    const float nan = std::nanf("");
    r.turnValues = {0.2f, -0.2f, 0.4f, -0.4f, 0.6f, nan, 0.8f, -0.8f};
    std::vector<float> t(count);
    std::vector<std::uint8_t> mask(count);
    for (int player : {0, 1}) {
        CAPTURE(player);
        encodeSideTargets(r, 1, player, enc, t.data(), mask.data());
        for (std::size_t h = 0; h < SHORT_TERM_HORIZONS.size(); h++) {
            double lambda = 1 - 1 / SHORT_TERM_HORIZONS[h];
            // from the result back to turn 1, skipping the missing value
            double ret = player == 0 ? 1 : -1;
            for (int turn = 3; turn >= 1; turn--) {
                float v = r.turnValues[turn * 2 + player];
                if (!std::isnan(v)) {
                    ret = (1 - lambda) * v + lambda * ret;
                }
            }
            REQUIRE(mask[SHORT_TERM_VALUE + h]);
            CHECK(t[SHORT_TERM_VALUE + h] == doctest::Approx(ret));
        }
    }
    // the shortest follows the coming turns, the longest the result
    encodeSideTargets(r, 1, 1, enc, t.data(), mask.data());
    CHECK(t[SHORT_TERM_VALUE] > t[SHORT_TERM_VALUE + 2]);
    // an older record has none
    r.turnValues.clear();
    encodeSideTargets(r, 1, 0, enc, t.data(), mask.data());
    CHECK_FALSE(mask[SHORT_TERM_VALUE]);
}

TEST_CASE("self-play records each player's search values per turn, and they survive a save") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.quickSimulations = 4;
    config.maxTurns = 5;
    SelfPlaySession session(config, 2, 2, 23);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 2);
    for (const GameRecord &r : records) {
        REQUIRE(r.turnValues.size() == static_cast<std::size_t>(r.turns + 1) * 2);
        // every turn played has a value for its player
        for (int turn = 0; turn < r.turns; turn++) {
            CHECK_FALSE(std::isnan(r.turnValues[turn * 2]));
            CHECK(std::abs(r.turnValues[turn * 2]) <= 1);
        }
    }
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_values_test.txt";
    saveRecords(records, path.string(), false);
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    for (std::size_t g = 0; g < records.size(); g++) {
        REQUIRE(loaded[g].turnValues.size() == records[g].turnValues.size());
        for (std::size_t i = 0; i < records[g].turnValues.size(); i++) {
            float a = records[g].turnValues[i], b = loaded[g].turnValues[i];
            CHECK((std::isnan(a) ? std::isnan(b) : b == doctest::Approx(a).epsilon(1e-4)));
        }
    }
    // bootstrap games (score leads, not chances of winning) keep none
    for (const GameRecord &r : playGames(config, 1, 3, 1)) {
        CHECK(r.turnValues.empty());
    }
}

TEST_CASE("side targets: turns left, only for games that ended in a capture") {
    using namespace SideTargets;
    EncodingConfig enc;
    const int count = sideTargetCount(enc);
    GameRecord r;
    r.map.size = 11;
    r.finalScores = {1000, 900};
    r.turns = 40;
    r.winner = 1;
    std::vector<float> t(count);
    std::vector<std::uint8_t> mask(count);
    encodeSideTargets(r, 10, 0, enc, t.data(), mask.data());
    REQUIRE(mask[TURNS_LEFT]);
    CHECK(t[TURNS_LEFT] == doctest::Approx(30 / TURNS_LEFT_SCALE));
    // capped
    r.turns = 150;
    encodeSideTargets(r, 0, 0, enc, t.data(), mask.data());
    CHECK(t[TURNS_LEFT] == doctest::Approx(TURNS_LEFT_CAP / TURNS_LEFT_SCALE));
    // a cut-off game says nothing about when its end would have come
    r.winner = -1;
    encodeSideTargets(r, 10, 0, enc, t.data(), mask.data());
    CHECK_FALSE(mask[TURNS_LEFT]);
}

TEST_CASE("handicap games: one side's head start, recorded, replayed and encoded") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.quickSimulations = 4;
    config.maxTurns = 3;
    config.handicapShare = 1;
    // head starts only, here; the other kinds have their own tests
    config.handicapIncomeShare = 0;
    config.handicapCatchUpShare = 0;
    SelfPlaySession session(config, 4, 6, 3);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 6);
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_handicap_test.txt";
    saveRecords(records, path.string(), false);
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    EncodingConfig enc;
    const int G = globalCount(enc);
    for (std::size_t g = 0; g < records.size(); g++) {
        const GameRecord &r = records[g];
        REQUIRE(r.headStart.size() == 2);
        // exactly one side ahead, by one of the configured amounts
        int ahead = r.headStart[0] > 0 ? 0 : 1;
        CHECK(r.headStart[1 - ahead] == 0);
        CHECK(std::find(config.handicapStars.begin(), config.handicapStars.end(),
                        r.headStart[ahead]) != config.handicapStars.end());
        CHECK(loaded[g].headStart == r.headStart);
        CHECK(r.replaysLegally());
        // the start has it, and each player's input shows it, relative
        Game game = r.start();
        CHECK(game.players[ahead].stars ==
              tribeData[r.map.tribes[ahead]].startStars + r.headStart[ahead]);
        std::vector<float> planes(planeCount(enc) * enc.mapSize * enc.mapSize), globals(G);
        encodeState(game, ahead, enc, planes.data(), globals.data());
        CHECK(globals[G - 4] == doctest::Approx(r.headStart[ahead] / 50.0f));
        CHECK(globals[G - 3] == 0);
        encodeState(game, 1 - ahead, enc, planes.data(), globals.data());
        CHECK(globals[G - 4] == 0);
        CHECK(globals[G - 3] == doctest::Approx(r.headStart[ahead] / 50.0f));
    }
    // ordinary games have none
    for (const GameRecord &r : playGames(SelfPlayConfig{.maxTurns = 2}, 1, 1, 1)) {
        CHECK(r.headStart.empty());
    }
}

TEST_CASE("handicap games by income: one side's capital earns more, recorded and encoded") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.quickSimulations = 4;
    config.maxTurns = 3;
    config.handicapShare = 1;
    config.handicapIncomeShare = 1;
    config.handicapCatchUpShare = 0;
    SelfPlaySession session(config, 4, 4, 5);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 4);
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_income_test.txt";
    saveRecords(records, path.string(), false);
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    EncodingConfig enc;
    const int G = globalCount(enc);
    for (std::size_t g = 0; g < records.size(); g++) {
        const GameRecord &r = records[g];
        REQUIRE(r.capitalIncome.size() == 2);
        CHECK(r.headStart.empty());
        int ahead = r.capitalIncome[0] > Rules::CAPITAL_INCOME ? 0 : 1;
        CHECK(r.capitalIncome[1 - ahead] == Rules::CAPITAL_INCOME);
        int extra = r.capitalIncome[ahead] - Rules::CAPITAL_INCOME;
        CHECK((extra == 1 || extra == 2));
        CHECK(loaded[g].capitalIncome == r.capitalIncome);
        CHECK(r.replaysLegally());
        Game game = r.start();
        CHECK(game.capitalIncome(ahead) == Rules::CAPITAL_INCOME + extra);
        std::vector<float> planes(planeCount(enc) * enc.mapSize * enc.mapSize), globals(G);
        encodeState(game, 1 - ahead, enc, planes.data(), globals.data());
        // from the trailing side's seat, the opponent is relative player 1
        CHECK(globals[G - 2] == 0);
        CHECK(globals[G - 1] == doctest::Approx(extra / 30.0f));
    }
}

TEST_CASE("catch-up games: the side that falls behind after meeting gets more income from then on") {
    // a network that always calls player 1 lost
    struct Grim : Evaluator {
        void evaluate(const std::vector<EvalRequest> &requests,
                      std::vector<Evaluation> &results) override {
            results.resize(requests.size());
            for (std::size_t i = 0; i < requests.size(); i++) {
                std::size_t n = requests[i].moves ? requests[i].moves->size() : 0;
                results[i].priors.assign(n, n ? 1.0f / static_cast<float>(n) : 0.0f);
                results[i].value = requests[i].player == 1 ? -0.9f : 0.9f;
            }
        }
    } network;
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.quickSimulations = 4;
    config.maxTurns = 25;
    config.handicapShare = 1;
    config.handicapCatchUpShare = 1;
    config.catchUpFromTurn = 3;
    SelfPlaySession session(config, 6, 6, 9);
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        network.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 6);
    int boosted = 0;
    for (const GameRecord &r : records) {
        // no edge at the start
        CHECK(r.headStart.empty());
        CHECK(r.capitalIncome.empty());
        CHECK(r.replaysLegally());
        if (r.incomeBonuses.empty()) {
            continue;
        }
        boosted++;
        REQUIRE(r.incomeBonuses.size() == 1);
        const IncomeBonus &b = r.incomeBonuses[0];
        CHECK(b.player == 1);
        CHECK(b.fromTurn > config.catchUpFromTurn);
        CHECK((b.extra == 1 || b.extra == 2));
        // replayed: nothing extra before its turn; from then on it pays, and
        // the players had met (it's given during the turn before, after
        // meeting)
        Game game = r.start();
        CHECK(game.capitalIncome(1) == Rules::CAPITAL_INCOME);
        for (std::size_t m = 0; m < r.moves.size() && game.turn < b.fromTurn; m++) {
            game.applyUnchecked(r.moves[m].command);
        }
        if (game.turn >= b.fromTurn) {
            CHECK(game.players[0].hasMet(1));
            CHECK(game.players[1].hasMet(0));
            CHECK(game.capitalIncome(1) == Rules::CAPITAL_INCOME + b.extra);
        }
    }
    CHECK(boosted > 0);
    // kept through a save and load
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_catchup_test.txt";
    saveRecords(records, path.string(), false);
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    for (std::size_t g = 0; g < records.size(); g++) {
        CHECK(loaded[g].incomeBonuses == records[g].incomeBonuses);
        CHECK(loaded[g].catchUp);
    }
    // the value trains only from the boost on; a catch-up game without one
    // never trains it, an ordinary game always does
    EncodingConfig enc;
    for (const GameRecord &r : records) {
        std::vector<std::pair<int, int>> positions;
        for (int m : searchedMovesOf(r)) {
            positions.push_back({0, m});
        }
        if (positions.empty()) {
            continue;
        }
        const std::size_t n = positions.size(), a = actionCount(enc);
        std::vector<float> planes(n * planeCount(enc) * enc.mapSize * enc.mapSize),
            globals(n * globalCount(enc)), policy(n * a), value(n), weight(n),
            targets(n * targetCount(enc)), side(n * sideTargetCount(enc));
        std::vector<std::uint8_t> legal(n * a), masks(n * targetCount(enc)),
            sideMasks(n * sideTargetCount(enc));
        encodeExamples({r}, positions, enc,
                       {planes.data(), globals.data(), legal.data(), policy.data(), value.data(),
                        weight.data(), targets.data(), masks.data(), side.data(),
                        sideMasks.data()});
        Game game = r.start();
        std::size_t k = 0;
        for (std::size_t m = 0; m < r.moves.size() && k < n; m++) {
            if (static_cast<int>(m) == positions[k].second) {
                CHECK((weight[k] > 0) == r.valueCounts(game.turn));
                const std::size_t S = sideTargetCount(enc);
                bool anySide = std::any_of(sideMasks.begin() + k * S, sideMasks.begin() + (k + 1) * S,
                                           [](std::uint8_t x) { return x != 0; });
                CHECK(anySide == r.valueCounts(game.turn));
                if (r.incomeBonuses.empty()) {
                    CHECK(weight[k] == 0);
                }
                k++;
            }
            game.applyUnchecked(r.moves[m].command);
        }
    }
    GameRecord ordinary;
    CHECK(ordinary.valueCounts(0));
}

TEST_CASE("the evaluation cache answers repeats exactly as the network would") {
    // a stand-in network whose answers depend only on its input
    EncodingConfig enc;
    const std::size_t P = planeCount(enc) * enc.mapSize * enc.mapSize, G = globalCount(enc);
    auto summary = [&](const float *planes, const float *globals) {
        double s = 0;
        for (std::size_t k = 0; k < P; k++) {
            s += planes[k] * static_cast<double>(k % 97 + 1);
        }
        for (std::size_t k = 0; k < G; k++) {
            s += globals[k] * static_cast<double>(k % 89 + 3);
        }
        return s;
    };
    auto play = [&](bool cached, SelfPlaySession::CacheStats &stats, int parallel) {
        SelfPlayConfig config;
        config.search.simulations = 24;
        config.quickSimulations = 8;
        config.fullShare = 0.5;
        config.maxTurns = 6;
        config.fogShare = 0.5;
        config.search.gumbel = false;
        SelfPlaySession session(config, parallel, 4, 17);
        while (!session.done()) {
            // as the Python side does: arrays for the batch, copied into
            // even when it's empty, and an empty batch is the end
            std::size_t b = cached ? session.pendingForNetwork(enc) : session.pending().size();
            const std::vector<EvalRequest> &requests = session.waiting();
            std::vector<float> planes(b * P), globals(b * G);
            if (cached) {
                session.copyPrepared(planes.data(), globals.data());
            } else {
                encodeRequests(requests, enc, planes.data(), globals.data(), nullptr);
            }
            if (b == 0) {
                break;
            }
            std::vector<int> rows, slots;
            requestSlots(requests, enc, rows, slots);
            std::vector<float> logits(slots.size()), values(b), ties(2 * b);
            std::vector<double> sums(b);
            for (std::size_t i = 0; i < b; i++) {
                sums[i] = summary(planes.data() + i * P, globals.data() + i * G);
                values[i] = static_cast<float>(std::tanh(sums[i] * 1e-3));
                ties[2 * i] = static_cast<float>(std::sin(sums[i]));
                ties[2 * i + 1] = static_cast<float>(std::fabs(std::cos(sums[i])));
            }
            for (std::size_t k = 0; k < slots.size(); k++) {
                logits[k] = static_cast<float>(std::sin(sums[rows[k]] * 0.01 + slots[k] * 0.37));
            }
            if (cached) {
                session.submitMoveLogits(enc, logits.data(), values.data(), ties.data(), nullptr);
            } else {
                std::vector<Evaluation> results;
                evaluationsFromMoveLogits(requests, slots, logits.data(), values.data(), results,
                                          ties.data());
                session.submit(results);
            }
        }
        stats = session.takeCacheStats();
        std::vector<GameRecord> records = session.takeFinished();
        std::sort(records.begin(), records.end(),
                  [](const GameRecord &a, const GameRecord &b) { return a.gameSeed < b.gameSeed; });
        return records;
    };
    // one game at a time, where whole batches come from the cache, and four
    for (int parallel : {1, 4}) {
        CAPTURE(parallel);
        SelfPlaySession::CacheStats none, stats;
        std::vector<GameRecord> plain = play(false, none, parallel),
                                cached = play(true, stats, parallel);
        // every game finished: a batch the cache answered whole doesn't end it
        REQUIRE(plain.size() == 4);
        REQUIRE(plain.size() == cached.size());
        for (std::size_t g = 0; g < plain.size(); g++) {
            CHECK(sameMoves(plain[g], cached[g]));
        }
        MESSAGE("cache, " << parallel << " at a time: " << stats.hits << " hits of "
                          << stats.lookups << " lookups");
        CHECK(none.lookups == 0);
        CHECK(stats.hits > 0);
    }
}

TEST_CASE("the evaluation cache with belief requests and two networks, as Python drives it") {
    EncodingConfig enc;
    const std::size_t P = planeCount(enc) * enc.mapSize * enc.mapSize, G = globalCount(enc),
                      B = beliefOutputCount(enc);
    for (bool match : {false, true}) {
        CAPTURE(match);
        SelfPlayConfig config;
        config.search.simulations = 16;
        config.quickSimulations = 4;
        config.maxTurns = 6;
        config.fogShare = 1;
        config.networkBeliefs = true;
        config.search.gumbel = true;
        if (match) {
            config.paired = true;
        }
        config.handicapShare = 0.5;
        SelfPlaySession session(config, 4, 6, 29);
        // past the end too, as Python's loop asks once more before stopping
        for (int extra = 0; extra < 2;) {
            std::size_t b = session.pendingForNetwork(enc);
            const std::vector<EvalRequest> &requests = session.waiting();
            REQUIRE(requests.size() == b);
            REQUIRE(session.waitingSides().size() == b);
            std::vector<float> planes(b * P), globals(b * G);
            session.copyPrepared(planes.data(), globals.data());
            if (b == 0) {
                extra++;
                continue;
            }
            std::vector<int> rows, slots;
            requestSlots(requests, enc, rows, slots);
            std::size_t wanted = 0;
            for (const EvalRequest &r : requests) {
                wanted += r.wantsBeliefs;
            }
            std::vector<float> logits(slots.size(), 0.1f), values(b), ties(2 * b, 0.0f),
                beliefs(wanted * B, 0.0f);
            // an untrained network's belief outputs: anything
            std::mt19937_64 noise(b * 7919 + wanted);
            std::normal_distribution<float> normal(0.0f, 2.0f);
            for (float &x : beliefs) {
                x = normal(noise);
            }
            for (std::size_t i = 0; i < b; i++) {
                values[i] = session.waitingSides()[i] == 0 ? 0.2f : -0.1f;
            }
            session.submitMoveLogits(enc, logits.data(), values.data(), ties.data(),
                                     wanted ? beliefs.data() : nullptr);
        }
        CHECK(session.takeFinished().size() == 6);
        SelfPlaySession::CacheStats stats = session.takeCacheStats();
        CHECK(stats.lookups > 0);
    }
    // and games on threads afterwards still play
    SelfPlayConfig plain;
    plain.search.simulations = 10;
    plain.maxTurns = 4;
    CHECK(playGames(plain, 4, 5, 2).size() == 4);
}

TEST_CASE("each side can search with its own number of simulations") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.fullShare = 1;
    config.maxTurns = 3;
    config.treeReuse = false;
    // simulations count per determinized game: one each
    config.search.samples = 1;
    for (int side : {0, 1}) {
        config.sideSearch[side] = config.search;
        config.sideSearch[side]->simulations = side ? 32 : 4;
    }
    SelfPlaySession session(config, 2, 2, 43);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 2);
    // side 0 plays seat 0 (not paired): simulations per searched move
    std::array<int, 2> searched{0, 0};
    for (const GameRecord &r : records) {
        for (const RecordedMove &m : r.moves) {
            searched[m.player] += !m.visits.empty();
        }
    }
    REQUIRE(searched[0] > 0);
    REQUIRE(searched[1] > 0);
    const auto &depth = session.depthStats();
    double per0 = static_cast<double>(depth[0].simulations) / searched[0],
           per1 = static_cast<double>(depth[1].simulations) / searched[1];
    CHECK(per0 <= 4);
    CHECK(per1 > 8);
    CHECK(per1 <= 32);
}

TEST_CASE("opponent-pool games: only the current network's moves train, at either seat") {
    SelfPlayConfig config;
    config.search.simulations = 8;
    config.quickSimulations = 4;
    config.fullShare = 1;
    config.maxTurns = 4;
    config.randomSeats = true;
    config.trainSides = {true, false};
    SelfPlaySession session(config, 4, 8, 41);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 8);
    std::set<int> seats;
    for (const GameRecord &r : records) {
        REQUIRE((r.poolOpponentSeat == 0 || r.poolOpponentSeat == 1));
        CHECK(r.poolOpponentSeat == 1 - r.seatZeroSide);
        seats.insert(r.poolOpponentSeat);
        CHECK(r.replaysLegally());
        int trained = 0;
        for (const RecordedMove &m : r.moves) {
            if (m.player == r.poolOpponentSeat) {
                CHECK(m.visits.empty());
            } else {
                trained += !m.visits.empty();
            }
        }
        CHECK(trained > 0);
    }
    // both seats come up
    CHECK(seats.size() == 2);
    // kept through a save and load
    auto path = std::filesystem::temp_directory_path() / "stocknavalon_pool_test.txt";
    saveRecords(records, path.string(), false);
    std::vector<GameRecord> loaded = loadRecords(path.string());
    std::filesystem::remove(path);
    for (std::size_t g = 0; g < records.size(); g++) {
        CHECK(loaded[g].poolOpponentSeat == records[g].poolOpponentSeat);
        // new records play by the current zone of control rule
        CHECK(loaded[g].rulesVersion == RuleVersions::LATEST);
    }
}
