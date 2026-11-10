#include "doctest/doctest.h"
#include "record.h"
#include "selfPlay.h"
#include <cmath>
#include <filesystem>

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
        CHECK(sameMoves(loaded[i], records[i]));
    }
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
    encodeExamples(records, positions, enc, {},
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

TEST_CASE("the value target moves from the score to the result") {
    GameRecord decided, cut;
    decided.winner = 0;
    decided.finalScores = {1500, 1000};
    cut.winner = -1;
    cut.finalScores = {1000, 1500};
    CHECK(decided.result(1) == -1);
    CHECK(cut.result(0) == 0);
    CHECK(decided.scoreValue(0, 500) == doctest::Approx(std::tanh(1.0)));
    CHECK(cut.scoreValue(0, 500) == doctest::Approx(std::tanh(-1.0)));
}
