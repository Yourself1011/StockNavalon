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
// Whether every tree's root looks to the player to move exactly as the real
// game does: the condition for reusing a search's trees
bool sameView(const BatchedSearch &search, const Game &real, bool fullInformation) {
    EncodingConfig config{.mapSize = real.size,
                          .players = static_cast<int>(real.players.size())};
    const std::size_t planes = planeCount(config) * real.size * real.size,
                      globals = globalCount(config);
    std::vector<float> realPlanes(planes), realGlobals(globals), planesOf(planes),
        globalsOf(globals);
    encodeState(real, real.currentPlayer, config, realPlanes.data(), realGlobals.data(),
                fullInformation);
    for (int t = 0; t < search.treeCount(); t++) {
        encodeState(search.rootGame(t), real.currentPlayer, config, planesOf.data(),
                    globalsOf.data(), fullInformation);
        if (planesOf != realPlanes || globalsOf != realGlobals) {
            return false;
        }
    }
    return true;
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
    // the move count at which the game is cut short, -1 for none
    int moveLimit = -1;
    std::optional<RandomBot> randomBot;
    // its waiting positions, gathered by pending()
    std::vector<EvalRequest> waiting;
    // each seat's root value at its latest decision with the network, for a
    // game the turn limit cuts off (GameRecord::finalValues)
    std::vector<float> lastValues;
    // the turn of the latest attack or capture, -1 before the first
    int lastCombat = -1;
    // the network's beliefs for this turn's player, asked for once per turn
    // (networkBeliefs), and whether the slot is waiting for them
    std::optional<Beliefs> beliefs;
    int beliefsTurn = -1, beliefsPlayer = -1;
    bool wantsBeliefs = false;
    // the last decision's search, kept for tree reuse, and what it played
    std::unique_ptr<BatchedSearch> previous;
    Command lastPlayed{CommandTypes::ENDTURN};
    int lastTurn = -1, lastPlayer = -1;
};

SelfPlaySession::SelfPlaySession(const SelfPlayConfig &config, int parallel,
                                 int games, std::uint64_t seed)
    : config(config), total(games), seed(seed),
      pool(std::make_unique<ThreadPool>(std::max(1, config.threads))) {
    for (int i = 0; i < std::max(1, parallel); i++) {
        slots.push_back(std::make_unique<Slot>());
        startGame(*slots.back());
    }
}

SelfPlaySession::~SelfPlaySession() = default;

void SelfPlaySession::startGame(Slot &slot) {
    slot.search.reset();
    slot.previous.reset();
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
    // a pair's games share their fog setting too
    slot.record.fog = std::bernoulli_distribution(config.fogShare)(slot.rng);
    slot.record.peaceTreaties = config.peaceTreaties;
    // the rest of the game's randomness differs between the pair's games
    slot.rng.seed(slot.rng() ^ static_cast<std::uint64_t>(number));
    slot.randomBot.emplace(slot.rng());
    slot.game.emplace(slot.record.start());
    slot.lastValues.assign(slot.game->players.size(), std::nanf(""));
    slot.lastCombat = -1;
    slot.beliefs.reset();
    slot.beliefsTurn = slot.beliefsPlayer = -1;
    slot.wantsBeliefs = false;
    slot.commandsThisTurn = 0;
    slot.turn = slot.mover = -1;
    slot.moveLimit = -1;
}

// an attack or a capture restarts the stalemate clock
void SelfPlaySession::noteCombat(Slot &slot, const Command &command) {
    if (command.type == CommandTypes::ATTACK || command.type == CommandTypes::CAPTURE) {
        slot.lastCombat = slot.game->turn;
    }
}

void SelfPlaySession::advance(Slot &slot) {
    while (slot.active) {
        Game &game = *slot.game;
        if (!slot.search) {
            bool stalled = config.stallTurns > 0 && slot.lastCombat >= 0 &&
                           game.turn - slot.lastCombat >= config.stallTurns;
            if (game.gameOver() || game.turn >= config.maxTurns || stalled) {
                slot.record.stalled = stalled && !game.gameOver();
                finishGame(slot);
                startGame(slot);
                continue;
            }
            if (slot.moveLimit >= 0 &&
                static_cast<int>(slot.record.moves.size()) >= slot.moveLimit) {
                // cut short (finishAfter): scored by the lead now
                finishGame(slot);
                slot.game.reset();
                slot.active = false;
                return;
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
                noteCombat(slot, command);
                game.apply(command);
                slot.record.moves.push_back({.player = slot.mover, .command = command});
                slot.commandsThisTurn++;
                continue;
            }
            SearchConfig search = config.search;
            search.fullInformation = !slot.record.fog;
            search.gumbel = config.gumbel[slot.side];
            // with fog, the network's beliefs, once per turn: wait for them
            bool fromNetwork = !search.fullInformation && config.networkBeliefs &&
                               config.sides[slot.side] == SideKind::NETWORK;
            bool current = slot.beliefs && slot.beliefsTurn == game.turn &&
                           slot.beliefsPlayer == game.currentPlayer;
            if (fromNetwork && !current) {
                slot.wantsBeliefs = true;
                return;
            }
            // without fog the real game is searched: no beliefs needed
            Beliefs beliefs = search.fullInformation ? Beliefs{}
                              : fromNetwork          ? *slot.beliefs
                                         : priorBeliefs(makeView(game, game.currentPlayer));
            slot.full = std::bernoulli_distribution(config.fullShare)(slot.rng);
            if (!slot.full) {
                search.simulations = config.quickSimulations;
                search.rootNoise = false;
            }
            // Gumbel search picks its move by its noise: on in the opening
            // turns, for varied openings, as the temperature is otherwise
            if (search.gumbel && config.search.rootNoise &&
                game.turn < config.temperatureTurns) {
                search.rootNoise = true;
            }
            // reuse the last decision's tree when it's still this player's
            // turn and the tree's position looks exactly like the real one
            bool reused = config.treeReuse && slot.previous &&
                          slot.lastTurn == game.turn &&
                          slot.lastPlayer == game.currentPlayer &&
                          slot.previous->reuse(slot.lastPlayed, search) &&
                          sameView(*slot.previous, game, search.fullInformation);
            if (reused) {
                slot.search = std::move(slot.previous);
            } else {
                slot.search = std::make_unique<BatchedSearch>(
                    std::vector<const Game *>{&game}, std::vector<Beliefs>{beliefs},
                    search, slot.rng());
            }
            slot.previous.reset();
        }
        if (!slot.search->done()) {
            return;
        }
        // the decision is made: play it and record it
        SearchResult result = slot.search->result(0);
        // kept for the next decision's tree reuse
        slot.previous = std::move(slot.search);
        // the search's value of the position for the player to move: the
        // root moves' values, weighted by visits. Only the network's: the
        // score evaluator's would make the target a score lead.
        float rootValue = std::nanf("");
        if (config.sides[slot.side] == SideKind::NETWORK) {
            double total = 0, visits = 0;
            for (std::size_t i = 0; i < result.moves.size(); i++) {
                total += result.values[i] * result.visits[i];
                visits += result.visits[i];
            }
            if (visits > 0) {
                rootValue = static_cast<float>(total / visits);
                slot.lastValues[game.currentPlayer] = rootValue;
            }
        }
        double temperature = game.turn < config.temperatureTurns ? 1.0 : 0.0;
        RecordedMove move{.player = game.currentPlayer,
                          .command = result.choose(temperature, slot.rng),
                          .rootValue = rootValue};
        // only a full search's visits are worth training the policy on
        for (std::size_t i = 0; slot.full && i < result.moves.size(); i++) {
            int weight = result.chosen >= 0
                             ? static_cast<int>(std::lround(result.policy[i] * POLICY_WEIGHT_SCALE))
                             : result.visits[i];
            move.visits.push_back({result.moves[i], weight});
        }
        if (result.moves.empty() || !game.isLegal(move.command)) {
            move.command = game.legalCommands().front();
            move.visits.clear();
        }
        noteCombat(slot, move.command);
        slot.lastPlayed = move.command;
        slot.lastTurn = game.turn;
        slot.lastPlayer = game.currentPlayer;
        game.apply(move.command);
        slot.record.moves.push_back(std::move(move));
        slot.commandsThisTurn++;
    }
}

void SelfPlaySession::finishGame(Slot &slot) {
    const Game &game = *slot.game;
    slot.record.winner = game.winner;
    slot.record.turns = game.turn;
    slot.record.finalValues.clear();
    if (game.winner < 0 &&
        std::any_of(slot.lastValues.begin(), slot.lastValues.end(),
                    [](float v) { return !std::isnan(v); })) {
        slot.record.finalValues = slot.lastValues;
    }
    slot.record.finalScores.clear();
    for (std::size_t p = 0; p < game.players.size(); p++) {
        slot.record.finalScores.push_back(game.score(static_cast<int>(p)));
    }
    // the city counts, for the value target's early city term
    slot.record.finalCities.assign(game.players.size(), 0);
    for (const City &city : game.cities) {
        if (city.owner >= 0) {
            slot.record.finalCities[city.owner]++;
        }
    }
    // for the side targets: who owns each tile now, and each player's
    // latest published score at each turn
    const int players = static_cast<int>(game.players.size());
    slot.record.finalTerritory.resize(game.size * game.size);
    for (int t = 0; t < game.size * game.size; t++) {
        slot.record.finalTerritory[t] = game.tileOwner(t);
    }
    slot.record.turnScores.assign(static_cast<std::size_t>(game.turn + 1) * players, 0);
    std::vector<int> latest(players, 0);
    std::size_t next = 0;
    for (int turn = 0; turn <= game.turn; turn++) {
        for (; next < game.scoreHistory.size() && game.scoreHistory[next].turn <= turn;
             next++) {
            latest[game.scoreHistory[next].player] = game.scoreHistory[next].score;
        }
        std::copy(latest.begin(), latest.end(),
                  slot.record.turnScores.begin() + static_cast<std::size_t>(turn) * players);
    }
    {
        std::lock_guard<std::mutex> lock(doneMutex);
        done_.push_back(std::move(slot.record));
    }
    finished++;
}

void SelfPlaySession::finishNow() {
    // no more games: the ones started are all there will be
    total = std::min<int>(total, started);
    for (std::unique_ptr<Slot> &slot : slots) {
        if (slot->active) {
            finishGame(*slot);
            slot->search.reset();
            slot->game.reset();
            slot->active = false;
        }
    }
}

void SelfPlaySession::finishAfter(int moves) {
    total = std::min<int>(total, started);
    for (std::unique_ptr<Slot> &slot : slots) {
        if (slot->active && slot->moveLimit < 0) {
            slot->moveLimit = static_cast<int>(slot->record.moves.size()) + moves;
        }
    }
}

bool SelfPlaySession::done() const { return finished >= total; }

const std::vector<EvalRequest> &SelfPlaySession::pending() {
    // each game on its own, in parallel: they share nothing
    pool->run(static_cast<int>(slots.size()), [&](int i) {
        Slot &slot = *slots[i];
        slot.waiting.clear();
        while (true) {
            advance(slot);
            if (!slot.active) {
                break;
            }
            if (slot.wantsBeliefs) {
                // one request: the decision's position, for its beliefs
                slot.waiting = {EvalRequest{.game = &*slot.game,
                                            .player = slot.game->currentPlayer,
                                            .moves = nullptr,
                                            .wantsBeliefs = true}};
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
    pool->run(static_cast<int>(owners.size()), [&](int k) {
        auto [slot, count] = owners[k];
        std::vector<Evaluation> part(results.begin() + starts[k],
                                     results.begin() + starts[k] + count);
        Slot &s = *slots[slot];
        if (s.wantsBeliefs) {
            // the beliefs for this turn; the prior if the caller gave none
            const Game &game = *s.game;
            s.beliefs = part.at(0).beliefs ? *part[0].beliefs
                                           : priorBeliefs(makeView(game, game.currentPlayer));
            s.beliefsTurn = game.turn;
            s.beliefsPlayer = game.currentPlayer;
            s.wantsBeliefs = false;
            return;
        }
        s.search->submit(part);
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
            // its values are score leads, not chances of winning: a game cut
            // off here is a draw, and no position has a search value
            records[i].finalValues.clear();
            for (RecordedMove &m : records[i].moves) {
                m.rootValue = std::nanf("");
            }
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
    ThreadPool pool(threads);
    encodeRequests(requests, config, planes, globals, legal, pool);
}

void encodeRequests(const std::vector<EvalRequest> &requests,
                    const EncodingConfig &config, float *planes, float *globals,
                    std::uint8_t *legal, ThreadPool &pool) {
    const std::size_t n = config.mapSize, p = planeCount(config) * n * n,
                      g = globalCount(config), a = actionCount(config);
    pool.run(static_cast<int>(requests.size()), [&](int i) {
        const EvalRequest &r = requests[i];
        encodeState(*r.game, r.player, config, planes + i * p, globals + i * g,
                    r.fullInformation);
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

void attachBeliefs(const std::vector<EvalRequest> &requests,
                   const EncodingConfig &config, const float *outputs,
                   std::vector<Evaluation> &results) {
    const std::size_t size = beliefOutputCount(config);
    std::size_t row = 0;
    for (std::size_t i = 0; i < requests.size(); i++) {
        if (requests[i].wantsBeliefs) {
            const EvalRequest &r = requests[i];
            results.at(i).beliefs =
                decodeBeliefs(makeView(*r.game, r.player), config, outputs + row * size);
            row++;
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
