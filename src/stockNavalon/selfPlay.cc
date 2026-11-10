#include "selfPlay.h"
#include "bots/randomBot.h"
#include "parallel.h"
#include "view.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <thread>

namespace {
// a new game's setup: a Tiny map of a random type, two random tribes
GameRecord newRecord(const SelfPlayConfig &config, std::mt19937_64 &rng) {
    GameRecord record;
    std::uniform_int_distribution<int> tribe(0, TribeTypes::TRIBE_SIZE - 1);
    std::uniform_int_distribution<int> type(0, MapTypes::MAP_TYPE_SIZE - 1);
    record.map.size = config.mapSize;
    record.map.type = static_cast<MapTypes::MapType>(type(rng));
    record.map.seed = rng();
    record.map.tribes = {static_cast<TribeTypes::Tribe>(tribe(rng)),
                         static_cast<TribeTypes::Tribe>(tribe(rng))};
    record.gameSeed = rng();
    return record;
}
} // namespace


struct SelfPlaySession::Slot {
    GameRecord record;
    std::optional<Game> game;
    std::unique_ptr<BatchedSearch> search;
    std::mt19937_64 rng;
    int commandsThisTurn = 0, turn = -1, mover = -1;
    bool active = false;
    // whether the decision being searched is a full one, recorded for the
    // policy
    bool full = true;
    // the game's number, and the side to move at the search's root
    int number = 0, side = 0;
    std::optional<RandomBot> randomBot;
    // its waiting positions, gathered by pending()
    std::vector<EvalRequest> waiting;
};

SelfPlaySession::SelfPlaySession(const SelfPlayConfig &config, int parallel,
                                 int games, std::uint64_t seed)
    : config(config), total(games), seed(seed) {
    for (int i = 0; i < std::max(1, parallel); i++) {
        slots.push_back(std::make_unique<Slot>());
        startGame(*slots.back());
    }
}

SelfPlaySession::~SelfPlaySession() = default;

void SelfPlaySession::startGame(Slot &slot) {
    slot.search.reset();
    int number = started++;
    slot.active = number < total;
    if (!slot.active) {
        slot.game.reset();
        return;
    }
    slot.number = number;
    // a pair's two games share their setup
    int setup = config.paired ? number / 2 : number;
    std::seed_seq sequence{static_cast<std::uint32_t>(seed),
                           static_cast<std::uint32_t>(seed >> 32),
                           static_cast<std::uint32_t>(setup)};
    slot.rng.seed(sequence);
    slot.record = newRecord(config, slot.rng);
    slot.record.seatZeroSide = config.paired ? number % 2 : 0;
    // the rest of the game's randomness differs between the pair's games
    slot.rng.seed(slot.rng() ^ static_cast<std::uint64_t>(number));
    slot.randomBot.emplace(slot.rng());
    slot.game.emplace(slot.record.start());
    slot.commandsThisTurn = 0;
    slot.turn = slot.mover = -1;
}

void SelfPlaySession::advance(Slot &slot) {
    while (slot.active) {
        Game &game = *slot.game;
        if (!slot.search) {
            if (game.gameOver() || game.turn >= config.maxTurns) {
                slot.record.winner = game.winner;
                slot.record.turns = game.turn;
                slot.record.finalScores.clear();
                for (std::size_t p = 0; p < game.players.size(); p++) {
                    slot.record.finalScores.push_back(game.score(static_cast<int>(p)));
                }
                {
                    std::lock_guard<std::mutex> lock(doneMutex);
                    done_.push_back(std::move(slot.record));
                }
                finished++;
                startGame(slot);
                continue;
            }
            if (game.turn != slot.turn || game.currentPlayer != slot.mover) {
                slot.turn = game.turn;
                slot.mover = game.currentPlayer;
                slot.commandsThisTurn = 0;
            }
            // a turn always ends, but cap it in case
            if (slot.commandsThisTurn >= 200) {
                Command end{CommandTypes::ENDTURN};
                if (!game.isLegal(end)) {
                    end = game.legalCommands().front();
                }
                game.apply(end);
                slot.record.moves.push_back({.player = slot.mover, .command = end});
                slot.commandsThisTurn++;
                continue;
            }
            // which side moves: seat 0 is seatZeroSide's
            slot.side = (game.currentPlayer + slot.record.seatZeroSide) % 2;
            if (config.sides[slot.side] == SideKind::RANDOM) {
                Command command = slot.randomBot->choose(game);
                game.apply(command);
                slot.record.moves.push_back({.player = slot.mover, .command = command});
                slot.commandsThisTurn++;
                continue;
            }
            Beliefs beliefs = priorBeliefs(makeView(game, game.currentPlayer));
            SearchConfig search = config.search;
            slot.full = std::bernoulli_distribution(config.fullShare)(slot.rng);
            if (!slot.full) {
                search.simulations = config.quickSimulations;
                search.rootNoise = false;
            }
            slot.search = std::make_unique<BatchedSearch>(
                std::vector<const Game *>{&game}, std::vector<Beliefs>{beliefs},
                search, slot.rng());
        }
        if (!slot.search->done()) {
            return;
        }
        // the decision is made: play it and record it
        SearchResult result = slot.search->result(0);
        slot.search.reset();
        double temperature = game.turn < config.temperatureTurns ? 1.0 : 0.0;
        RecordedMove move{.player = game.currentPlayer,
                          .command = result.choose(temperature, slot.rng)};
        // only a full search's visits are worth training the policy on
        for (std::size_t i = 0; slot.full && i < result.moves.size(); i++) {
            move.visits.push_back({result.moves[i], result.visits[i]});
        }
        if (result.moves.empty() || !game.isLegal(move.command)) {
            move.command = game.legalCommands().front();
            move.visits.clear();
        }
        game.apply(move.command);
        slot.record.moves.push_back(std::move(move));
        slot.commandsThisTurn++;
    }
}

bool SelfPlaySession::done() const { return finished >= total; }

const std::vector<EvalRequest> &SelfPlaySession::pending() {
    // each game on its own, in parallel: they share nothing
    parallelFor(static_cast<int>(slots.size()), config.threads, [&](int i) {
        Slot &slot = *slots[i];
        slot.waiting.clear();
        while (true) {
            advance(slot);
            if (!slot.active) {
                break;
            }
            const std::vector<EvalRequest> &r = slot.search->pending();
            if (!r.empty() && config.sides[slot.side] == SideKind::SCORE) {
                // the score evaluator's side is evaluated here, at once
                ScoreEvaluator evaluator;
                std::vector<Evaluation> results;
                evaluator.evaluate(r, results);
                slot.search->submit(results);
                continue;
            }
            if (!r.empty()) {
                slot.waiting = r;
                break;
            }
            // its simulations ended without needing an evaluation
        }
    });
    requests.clear();
    requestSides.clear();
    owners.clear();
    for (std::size_t i = 0; i < slots.size(); i++) {
        const std::vector<EvalRequest> &r = slots[i]->waiting;
        if (!r.empty()) {
            requests.insert(requests.end(), r.begin(), r.end());
            requestSides.insert(requestSides.end(), r.size(), slots[i]->side);
            owners.push_back({static_cast<int>(i), static_cast<int>(r.size())});
        }
    }
    return requests;
}

void SelfPlaySession::submit(const std::vector<Evaluation> &results) {
    std::vector<std::size_t> starts;
    std::size_t at = 0;
    for (auto [slot, count] : owners) {
        starts.push_back(at);
        at += count;
    }
    parallelFor(static_cast<int>(owners.size()), config.threads, [&](int k) {
        auto [slot, count] = owners[k];
        std::vector<Evaluation> part(results.begin() + starts[k],
                                     results.begin() + starts[k] + count);
        slots[slot]->search->submit(part);
    });
    requests.clear();
    requestSides.clear();
    owners.clear();
}

std::vector<GameRecord> SelfPlaySession::takeFinished() {
    std::lock_guard<std::mutex> lock(doneMutex);
    return std::exchange(done_, {});
}

GameRecord playGame(const SelfPlayConfig &config, Evaluator &evaluator,
                    std::uint64_t seed) {
    SelfPlaySession session(config, 1, 1, seed);
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    return session.takeFinished().at(0);
}

std::vector<GameRecord> playGames(const SelfPlayConfig &config, int count,
                                  std::uint64_t seed, int threads) {
    std::vector<GameRecord> records(count);
    std::atomic<int> next{0};
    auto worker = [&] {
        ScoreEvaluator evaluator;
        for (int i = next++; i < count; i = next++) {
            records[i] = playGame(config, evaluator, seed + i);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < std::max(1, threads); t++) {
        pool.emplace_back(worker);
    }
    for (std::thread &t : pool) {
        t.join();
    }
    return records;
}

void encodeRequests(const std::vector<EvalRequest> &requests,
                    const EncodingConfig &config, float *planes, float *globals,
                    std::uint8_t *legal, int threads) {
    const std::size_t n = config.mapSize, p = planeCount(config) * n * n,
                      g = globalCount(config), a = actionCount(config);
    parallelFor(static_cast<int>(requests.size()), threads, [&](int i) {
        const EvalRequest &r = requests[i];
        encodeState(*r.game, r.player, config, planes + i * p, globals + i * g);
        if (legal) {
            legalMask(*r.game, config, legal + i * a);
        }
    });
}

void requestSlots(const std::vector<EvalRequest> &requests,
                  const EncodingConfig &config, std::vector<int> &rows,
                  std::vector<int> &slots) {
    rows.clear();
    slots.clear();
    for (std::size_t i = 0; i < requests.size(); i++) {
        const EvalRequest &r = requests[i];
        if (!r.moves) {
            continue;
        }
        for (const Command &c : *r.moves) {
            rows.push_back(static_cast<int>(i));
            slots.push_back(encodeCommand(*r.game, c, config));
        }
    }
}

void evaluationsFromMoveLogits(const std::vector<EvalRequest> &requests,
                               const std::vector<int> &slots,
                               const float *moveLogits, const float *values,
                               std::vector<Evaluation> &results) {
    results.resize(requests.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < requests.size(); i++) {
        const EvalRequest &r = requests[i];
        Evaluation &e = results[i];
        e.value = std::clamp(values[i], -1.0f, 1.0f);
        e.priors.clear();
        if (!r.moves) {
            continue;
        }
        // a softmax over the node's moves, those without a slot left out
        std::size_t n = r.moves->size();
        float most = -1e30f;
        for (std::size_t m = 0; m < n; m++) {
            if (slots[k + m] >= 0) {
                most = std::max(most, moveLogits[k + m]);
            }
        }
        double sum = 0;
        for (std::size_t m = 0; m < n; m++) {
            float p = slots[k + m] >= 0 ? std::exp(moveLogits[k + m] - most) : 0.0f;
            e.priors.push_back(p);
            sum += p;
        }
        for (float &p : e.priors) {
            p = sum > 0 ? static_cast<float>(p / sum) : 1.0f / static_cast<float>(n);
        }
        k += n;
    }
}

void evaluationsFromNetwork(const std::vector<EvalRequest> &requests,
                            const EncodingConfig &config, const float *logits,
                            const float *values,
                            std::vector<Evaluation> &results) {
    std::vector<int> rows, slots;
    requestSlots(requests, config, rows, slots);
    const std::size_t a = actionCount(config);
    std::vector<float> moveLogits(slots.size(), 0);
    for (std::size_t k = 0; k < slots.size(); k++) {
        if (slots[k] >= 0) {
            moveLogits[k] = logits[rows[k] * a + slots[k]];
        }
    }
    evaluationsFromMoveLogits(requests, slots, moveLogits.data(), values, results);
}
