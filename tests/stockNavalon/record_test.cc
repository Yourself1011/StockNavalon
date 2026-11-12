#include "doctest/doctest.h"
#include "encoding.h"
#include "record.h"
#include "selfPlay.h"
#include "view.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
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
        CHECK(with < without * 0.9);
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
