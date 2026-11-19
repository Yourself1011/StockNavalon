#include "selfPlay.h"
#include "profile.h"
#include <cstring>
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
    record.map.type =
        config.mapTypes.empty()
            ? static_cast<MapTypes::MapType>(type(rng))
            : config.mapTypes[std::uniform_int_distribution<std::size_t>(
                  0, config.mapTypes.size() - 1)(rng)];
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
    // policy, and whether it's in a forced opening (SelfPlayConfig::sidePlans)
    bool full = true, forced = false;
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
    // each player's root values in each turn, summed and counted, turns x
    // players (GameRecord::turnValues)
    std::vector<double> valueSums;
    std::vector<int> valueCounts;
    // the turn of the latest attack or capture, -1 before the first
    int lastCombat = -1;
    // a handicap game, and for a catch-up one, whether the boost is still to
    // give (SelfPlayConfig::handicapCatchUpShare)
    bool handicap = false, catchUpPending = false;
    // this turn's evaluations, by their exact input (prepare())
    Cache cache;
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
                                 int games, std::uint64_t seed, int first)
    : config(config), total(games), seed(seed), first(first),
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
    int index = started++;
    slot.active = index < total;
    if (!slot.active) {
        slot.game.reset();
        return;
    }
    int number = first + index;
    slot.number = number;
    // a pair's two games share their setup
    int setup = config.paired ? number / 2 : number;
    std::seed_seq sequence{static_cast<std::uint32_t>(seed),
                           static_cast<std::uint32_t>(seed >> 32),
                           static_cast<std::uint32_t>(setup)};
    slot.rng.seed(sequence);
    slot.record = newRecord(config, slot.rng);
    slot.record.seatZeroSide = config.paired        ? number % 2
                               : config.randomSeats ? static_cast<int>(slot.rng() & 1)
                                                    : 0;
    // side 1's seat, when it's a pool opponent that doesn't train
    slot.record.poolOpponentSeat =
        config.trainSides[1] ? -1 : 1 - slot.record.seatZeroSide;
    // a pair's games share their fog setting too
    slot.record.fog = std::bernoulli_distribution(config.fogShare)(slot.rng);
    slot.record.peaceTreaties = config.peaceTreaties;
    slot.handicap = slot.catchUpPending = false;
    slot.cache.clear();
    if (config.handicapShare > 0 &&
        std::bernoulli_distribution(config.handicapShare)(slot.rng)) {
        slot.handicap = true;
        const std::size_t players = slot.record.map.tribes.size();
        std::uniform_int_distribution<std::size_t> who(0, players - 1);
        std::size_t leader = who(slot.rng);
        bool catchUp = !config.handicapIncome.empty() &&
                       std::bernoulli_distribution(config.handicapCatchUpShare)(slot.rng);
        bool income = !config.handicapIncome.empty() &&
                      (config.handicapStars.empty() ||
                       std::bernoulli_distribution(config.handicapIncomeShare)(slot.rng));
        const std::vector<int> &amounts = income ? config.handicapIncome : config.handicapStars;
        int amount = amounts[std::uniform_int_distribution<std::size_t>(
            0, amounts.size() - 1)(slot.rng)];
        if (catchUp) {
            // given later, to whoever falls behind
            slot.catchUpPending = true;
            slot.record.catchUp = true;
        } else if (income) {
            slot.record.capitalIncome.assign(players, Rules::CAPITAL_INCOME);
            slot.record.capitalIncome[leader] += amount;
        } else {
            slot.record.headStart.assign(players, 0);
            slot.record.headStart[leader] = amount;
        }
    }
    // the rest of the game's randomness differs between the pair's games
    slot.rng.seed(slot.rng() ^ static_cast<std::uint64_t>(number));
    slot.randomBot.emplace(slot.rng());
    slot.game.emplace(slot.record.start());
    slot.lastValues.assign(slot.game->players.size(), std::nanf(""));
    slot.valueSums.clear();
    slot.valueCounts.clear();
    slot.lastCombat = -1;
    slot.beliefs.reset();
    slot.beliefsTurn = slot.beliefsPlayer = -1;
    slot.wantsBeliefs = false;
    slot.commandsThisTurn = 0;
    slot.turn = slot.mover = -1;
    slot.moveLimit = -1;
}

// A catch-up game's boost: once the side to move has fallen behind in the
// mid or late game, and both players have met (so neither infers the other's
// tribe from its income), more capital income from its next turn on
void SelfPlaySession::maybeCatchUp(Slot &slot, float value) {
    Game &game = *slot.game;
    int p = game.currentPlayer, q = 1 - p;
    if (!slot.catchUpPending || game.turn < config.catchUpFromTurn ||
        value >= config.catchUpBelow || game.players.size() != 2 ||
        !game.players[p].hasMet(q) || !game.players[q].hasMet(p)) {
        return;
    }
    const std::vector<int> &amounts = config.handicapIncome;
    int extra = amounts[std::uniform_int_distribution<std::size_t>(0, amounts.size() - 1)(slot.rng)];
    IncomeBonus bonus{.player = p, .fromTurn = game.turn + 1, .extra = extra};
    game.settings.incomeBonuses.push_back(bonus);
    slot.record.incomeBonuses.push_back(bonus);
    slot.catchUpPending = false;
}

// the mover's stars and income as their turn starts, for the side targets
void SelfPlaySession::noteEconomy(Slot &slot) {
    const Game &game = *slot.game;
    const std::size_t players = game.players.size(),
                      need = static_cast<std::size_t>(game.turn + 1) * players;
    if (slot.record.turnStars.size() < need) {
        slot.record.turnStars.resize(need, -1);
        slot.record.turnIncome.resize(need, -1);
    }
    std::size_t at = static_cast<std::size_t>(game.turn) * players + game.currentPlayer;
    slot.record.turnStars[at] = game.players[game.currentPlayer].stars;
    slot.record.turnIncome[at] = game.income(game.currentPlayer);
}

// an attack or a capture restarts the stalemate clock
void SelfPlaySession::noteCombat(Slot &slot, const Command &command) {
    if (command.type == CommandTypes::ATTACK || command.type == CommandTypes::CAPTURE) {
        slot.lastCombat = slot.game->turn;
    }
}

void SelfPlaySession::advance(Slot &slot) {
    profiling::Timer timer(profiling::ADVANCE);
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
                noteEconomy(slot);
                // repeats come within a turn: keep the cache small
                slot.cache.clear();
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
            // this side's search (a match's may differ by side)
            const SearchConfig &base =
                config.sideSearch[slot.side] ? *config.sideSearch[slot.side] : config.search;
            SearchConfig search = base;
            search.fullInformation = !slot.record.fog;
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
            } else if (search.rootNoise) {
                // training's full searches explore; matches search without
                // root noise, and so without this
                search.exploreCandidates = config.exploreCandidates;
            }
            // a side behind in a handicap game searches harder
            if (slot.handicap && config.trailingSearchFactor > 1 &&
                slot.lastValues[game.currentPlayer] < config.catchUpBelow) {
                search.simulations *= config.trailingSearchFactor;
            }
            // Gumbel search picks its move by its noise: on in the opening
            // turns, for varied openings, as the temperature is otherwise
            if (search.gumbel && base.rootNoise &&
                game.turn < config.temperatureTurns) {
                search.rootNoise = true;
            }
            // a forced opening: only the plan's commands, played at once
            // when there's one
            std::vector<Command> allowed;
            slot.forced =
                config.sidePlans[slot.side] != Plan::NONE && game.turn < config.planTurns;
            bool restricted = false;
            if (slot.forced) {
                std::vector<Command> legal;
                for (const Command &c : game.legalCommands()) {
                    if (c.type != CommandTypes::RESIGN && c.type != CommandTypes::STAY) {
                        legal.push_back(c);
                    }
                }
                allowed = planAllowed(config.sidePlans[slot.side], game, legal);
                restricted = allowed.size() < legal.size();
                if (allowed.size() == 1) {
                    noteCombat(slot, allowed[0]);
                    slot.lastTurn = -1;
                    game.apply(allowed[0]);
                    slot.record.moves.push_back({.player = slot.mover, .command = allowed[0]});
                    slot.commandsThisTurn++;
                    slot.previous.reset();
                    continue;
                }
            }
            // reuse the last decision's tree when it's still this player's
            // turn and the tree's position looks exactly like the real one
            // (not under a plan: its root had other moves)
            bool reused;
            {
                profiling::Timer reuseTimer(profiling::REUSE);
                profiling::Laps laps;
                reused = config.treeReuse && !slot.forced && slot.previous &&
                         slot.lastTurn == game.turn &&
                         slot.lastPlayer == game.currentPlayer &&
                         slot.previous->reuse(slot.lastPlayed, search);
                laps.mark(profiling::REUSE_TREE);
                reused = reused && sameView(*slot.previous, game, search.fullInformation);
                laps.mark(profiling::REUSE_CHECK);
            }
            if (reused) {
                slot.search = std::move(slot.previous);
            } else {
                profiling::Timer startTimer(profiling::START_SEARCH);
                slot.search = std::make_unique<BatchedSearch>(
                    std::vector<const Game *>{&game}, std::vector<Beliefs>{beliefs},
                    search, slot.rng());
                if (restricted) {
                    slot.search->restrictRoot(allowed);
                }
            }
            slot.previous.reset();
        }
        if (!slot.search->done()) {
            return;
        }
        // the decision is made: play it and record it
        SearchResult result = slot.search->result(0);
        {
            std::lock_guard<std::mutex> lock(depthMutex);
            depth[slot.side] += slot.search->takeDepthStats();
        }
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
                maybeCatchUp(slot, rootValue);
                const std::size_t players = game.players.size(),
                                  at = static_cast<std::size_t>(game.turn) * players +
                                       game.currentPlayer;
                if (slot.valueSums.size() <= at) {
                    slot.valueSums.resize((game.turn + 1) * players, 0);
                    slot.valueCounts.resize((game.turn + 1) * players, 0);
                }
                slot.valueSums[at] += rootValue;
                slot.valueCounts[at]++;
            }
        }
        double temperature = game.turn < config.temperatureTurns ? 1.0 : 0.0;
        RecordedMove move{.player = game.currentPlayer,
                          .command = result.choose(temperature, slot.rng),
                          .rootValue = rootValue};
        // only a full search's visits are worth training the policy on
        // and none of a forced opening's: the plan chose, not the search
        for (std::size_t i = 0; slot.full && !slot.forced && config.trainSides[slot.side] &&
                                i < result.moves.size();
             i++) {
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
    // a player who's gone has nothing from then on
    slot.record.turnStars.resize(static_cast<std::size_t>(game.turn + 1) * players, -1);
    slot.record.turnIncome.resize(slot.record.turnStars.size(), -1);
    for (int p = 0; p < players; p++) {
        std::size_t at = static_cast<std::size_t>(game.turn) * players + p;
        if (!game.players[p].alive) {
            slot.record.turnStars[at] = slot.record.turnIncome[at] = 0;
        }
    }
    slot.record.turnValues.assign(static_cast<std::size_t>(game.turn + 1) * players,
                                  std::nanf(""));
    for (std::size_t i = 0; i < slot.valueSums.size() && i < slot.record.turnValues.size(); i++) {
        if (slot.valueCounts[i] > 0) {
            slot.record.turnValues[i] =
                static_cast<float>(slot.valueSums[i] / slot.valueCounts[i]);
        }
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
    // the last batch's cache state goes: nothing of it may be copied again
    prepared = false;
    served.clear();
    servedSides.clear();
    servedFrom.clear();
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
        profiling::Timer timer(profiling::EXPAND);
        s.search->submit(part);
    });
    requests.clear();
    requestSides.clear();
    owners.clear();
}

namespace {
// A 128-bit fingerprint of a block of floats: two 64-bit mixes, each over
// four interleaved lanes so the multiplications don't wait on each other
// (one chain of them took a sixth of self-play's CPU time), the lanes folded
// together at the end
void fingerprint(const float *data, std::size_t count, std::uint64_t &a, std::uint64_t &b) {
    std::uint64_t la[4] = {0x243f6a8885a308d3ULL, 0x13198a2e03707344ULL,
                           0xa4093822299f31d0ULL, 0x082efa98ec4e6c89ULL};
    std::uint64_t lb[4] = {0x452821e638d01377ULL, 0xbe5466cf34e90c6cULL,
                           0xc0ac29b7c97c50ddULL, 0x3f84d5b5b5470917ULL};
    const auto *bytes = reinterpret_cast<const unsigned char *>(data);
    std::size_t n = count * sizeof(float), i = 0;
    for (; i + 32 <= n; i += 32) {
        for (int k = 0; k < 4; k++) {
            std::uint64_t w;
            std::memcpy(&w, bytes + i + 8 * k, 8);
            la[k] = (la[k] ^ w) * 0x9e3779b97f4a7c15ULL;
            la[k] ^= la[k] >> 29;
            lb[k] = (lb[k] + w) * 0xbf58476d1ce4e5b9ULL;
            lb[k] ^= lb[k] >> 31;
        }
    }
    a = la[0];
    b = lb[0];
    for (int k = 1; k < 4; k++) {
        a = (a ^ la[k]) * 0x9e3779b97f4a7c15ULL;
        a ^= a >> 29;
        b = (b + lb[k]) * 0xbf58476d1ce4e5b9ULL;
        b ^= b >> 31;
    }
    for (; i < n; i++) {
        a = (a ^ bytes[i]) * 0x9e3779b97f4a7c15ULL;
        b = (b + bytes[i]) * 0xbf58476d1ce4e5b9ULL;
    }
    // the length too, so blocks of zeros of different lengths differ
    a ^= n * 0x94d049bb133111ebULL;
    b ^= n;
}
} // namespace

std::size_t SelfPlaySession::prepare(const EncodingConfig &config) {
    const std::size_t n = requests.size(), N = config.mapSize;
    planeSize = planeCount(config) * N * N;
    globalSize = globalCount(config);
    encodedPlanes.resize(n * planeSize);
    encodedGlobals.resize(n * globalSize);
    keys.resize(n);
    // which game each request is for
    requestSlot.clear();
    for (auto [slot, count] : owners) {
        requestSlot.insert(requestSlot.end(), count, slot);
    }
    pool->run(static_cast<int>(n), [&](int i) {
        const EvalRequest &r = requests[i];
        float *planes = encodedPlanes.data() + i * planeSize;
        float *globals = encodedGlobals.data() + i * globalSize;
        {
            profiling::Timer timer(profiling::ENCODE);
            encodeState(*r.game, r.player, config, planes, globals, r.fullInformation);
        }
        profiling::Timer timer(profiling::FINGERPRINT);
        std::uint64_t pa, pb, ga, gb;
        fingerprint(planes, planeSize, pa, pb);
        fingerprint(globals, globalSize, ga, gb);
        keys[i] = {.a = pa ^ (ga * 0x9e3779b97f4a7c15ULL), .b = pb ^ (gb * 0xc2b2ae3d27d4eb4fULL),
                   .side = requestSides[i]};
    });
    // answer what the cache can: every move asked about must be there
    profiling::Timer cacheTimer(profiling::CACHE);
    answered.assign(n, 0);
    answers.assign(n, Evaluation{});
    served.clear();
    servedSides.clear();
    servedFrom.clear();
    std::vector<int> rows, slotsOf;
    for (std::size_t i = 0; i < n; i++) {
        const EvalRequest &r = requests[i];
        bool hit = false;
        if (!r.wantsBeliefs) {
            cacheStats.lookups++;
            const Cache &cache = slots[requestSlot[i]]->cache;
            auto found = cache.find(keys[i]);
            if (found != cache.end()) {
                // the logits of this request's moves, from the cached slots
                const CachedEvaluation &c = found->second;
                std::vector<EvalRequest> one{r};
                requestSlots(one, config, rows, slotsOf);
                std::vector<float> logits(slotsOf.size(), 0.0f);
                hit = true;
                for (std::size_t m = 0; m < slotsOf.size() && hit; m++) {
                    if (slotsOf[m] < 0) {
                        continue;
                    }
                    auto at = std::lower_bound(c.logits.begin(), c.logits.end(),
                                               std::make_pair(slotsOf[m], -1e30f));
                    if (at == c.logits.end() || at->first != slotsOf[m]) {
                        hit = false;
                    } else {
                        logits[m] = at->second;
                    }
                }
                if (hit) {
                    float value = c.value, ties[2] = {c.scoreLead, c.turnsLeft};
                    std::vector<Evaluation> result;
                    evaluationsFromMoveLogits(one, slotsOf, logits.data(), &value, result, ties);
                    answers[i] = std::move(result[0]);
                    answered[i] = 1;
                    cacheStats.hits++;
                }
            }
        }
        if (!hit) {
            served.push_back(r);
            servedSides.push_back(requestSides[i]);
            servedFrom.push_back(static_cast<int>(i));
        }
    }
    prepared = true;
    return served.size();
}

std::size_t SelfPlaySession::pendingForNetwork(const EncodingConfig &config) {
    while (true) {
        if (pending().empty()) {
            return 0;
        }
        std::size_t b = prepare(config);
        if (b > 0) {
            return b;
        }
        // all answered: hand them over, and on to the next positions
        submitServed({});
    }
}

void SelfPlaySession::copyPrepared(float *planes, float *globals) const {
    if (!prepared) {
        return; // nothing pending: an empty batch
    }
    for (std::size_t j = 0; j < servedFrom.size(); j++) {
        std::size_t i = static_cast<std::size_t>(servedFrom[j]);
        std::copy_n(encodedPlanes.data() + i * planeSize, planeSize, planes + j * planeSize);
        std::copy_n(encodedGlobals.data() + i * globalSize, globalSize, globals + j * globalSize);
    }
}

void SelfPlaySession::submitServed(const std::vector<Evaluation> &results) {
    if (!prepared) {
        submit(results);
        return;
    }
    std::vector<Evaluation> all = std::move(answers);
    for (std::size_t j = 0; j < servedFrom.size() && j < results.size(); j++) {
        all[servedFrom[j]] = results[j];
    }
    prepared = false;
    submit(all);
}

void SelfPlaySession::submitMoveLogits(const EncodingConfig &config, const float *moveLogits,
                                       const float *values, const float *tieBreaks,
                                       const float *beliefs) {
    std::vector<int> rows, slotsOf;
    requestSlots(waiting(), config, rows, slotsOf);
    std::vector<Evaluation> results;
    evaluationsFromMoveLogits(waiting(), slotsOf, moveLogits, values, results, tieBreaks);
    if (beliefs) {
        attachBeliefs(waiting(), config, beliefs, results);
    }
    if (prepared) {
        // keep each answer for the rest of the turn
        profiling::Timer cacheTimer(profiling::CACHE);
        std::size_t k = 0;
        for (std::size_t j = 0; j < served.size(); j++) {
            const EvalRequest &r = served[j];
            std::size_t moves = r.moves ? r.moves->size() : 0;
            if (!r.wantsBeliefs) {
                std::size_t i = static_cast<std::size_t>(servedFrom[j]);
                CachedEvaluation c{.value = values[j],
                                   .scoreLead = tieBreaks ? tieBreaks[2 * j] : 0.0f,
                                   .turnsLeft = tieBreaks ? tieBreaks[2 * j + 1] : 0.0f};
                for (std::size_t m = 0; m < moves; m++) {
                    if (slotsOf[k + m] >= 0) {
                        c.logits.push_back({slotsOf[k + m], moveLogits[k + m]});
                    }
                }
                std::sort(c.logits.begin(), c.logits.end());
                c.logits.erase(std::unique(c.logits.begin(), c.logits.end(),
                                           [](auto &x, auto &y) { return x.first == y.first; }),
                               c.logits.end());
                slots[requestSlot[i]]->cache.insert_or_assign(keys[i], std::move(c));
            }
            k += moves;
        }
    }
    submitServed(results);
}

void SelfPlaySession::clearCache() {
    for (std::unique_ptr<Slot> &slot : slots) {
        slot->cache.clear();
    }
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
            records[i].turnValues.clear();
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
    profiling::Timer timer(profiling::MOVE_SLOTS);
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
                               std::vector<Evaluation> &results, const float *tieBreaks) {
    profiling::Timer timer(profiling::PRIORS);
    results.resize(requests.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < requests.size(); i++) {
        const EvalRequest &r = requests[i];
        Evaluation &e = results[i];
        e.value = std::clamp(values[i], -1.0f, 1.0f);
        e.scoreLead = tieBreaks ? std::clamp(tieBreaks[2 * i], -1.0f, 1.0f) : 0.0f;
        e.turnsLeft = tieBreaks ? std::max(0.0f, tieBreaks[2 * i + 1]) : 0.0f;
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
