#pragma once

// Games for training, the search playing both seats (docs/stockNavalon.md
// section 5)

#include "parallel.h"
#include "record.h"
#include "plans.h"
#include <cmath>
#include "search.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// Who plays a side of a game: the caller's network, the search with the
// score evaluator (in C++), or the random bot (no search)
enum class SideKind { NETWORK, SCORE, RANDOM };

struct SelfPlayConfig {
    SearchConfig search{.simulations = 100, .samples = 2, .horizon = 1,
                        .rootNoise = true};
    // Tiny maps, of every type, with random tribes
    int mapSize = 11;
    // Weak players may never finish a game, so games stop here; such a game
    // is truncated, not drawn: its value target is each seat's last search
    // value (GameRecord::finalValues)
    int maxTurns = 50;
    // A stalemate cutoff, the same kind of truncation: once anyone has
    // attacked or captured, the game ends after this many turns with no
    // attack or capture by anyone (0 for none). Quiet openings are left
    // alone: the clock starts at the first fight.
    int stallTurns = 0;
    // The share of games played with fog (docs/stockNavalon.md section 4).
    // The rest are played with full information: both players see
    // everything, the search runs on the real game, and the network's input
    // is the full view. The rules don't change.
    double fogShare = 1.0;
    // Peace treaties in these games (GameSettings::peaceTreaties): off while
    // training a bot that can't use them yet, which otherwise makes and
    // breaks them every few turns, freezing its own army
    bool peaceTreaties = true;
    // Tree reuse within a turn: the next decision continues from the played
    // move's subtree, its visits counting towards the next search's
    // simulations, when the player's view of the tree's position is exactly
    // the real one (with fog, a move that revealed something breaks that, as
    // does chance going differently)
    bool treeReuse = true;
    // With fog, a network side's searches sample the hidden state from the
    // network's own belief heads (decodeBeliefs), asked once per turn before
    // its first search, instead of priorBeliefs
    bool networkBeliefs = false;
    // each player's first turns pick commands in proportion to visits, for
    // varied openings; after that, the most visited
    int temperatureTurns = 3;
    // Playout cap randomization (KataGo): a random share of decisions get the
    // full search, with root noise, and are recorded as policy targets; the
    // rest get a quick search without noise, only to play the game on. Most
    // of a game's commands needn't be training-quality.
    double fullShare = 0.25;
    int quickSimulations = 25;
    // Threads for a session's own work: advancing its games and encoding
    // their positions, kept waiting between batches (a ThreadPool)
    int threads = 1;

    // ---- matches ----
    // Who plays each of the two sides. Self-play is the same network on both.
    std::array<SideKind, 2> sides{SideKind::NETWORK, SideKind::NETWORK};
    // Games in pairs: the same map and tribes twice, the sides swapping
    // seats, so neither the map nor moving first favours a side
    bool paired = false;
    // Opponent-pool games: side 1 is an earlier network, at a random seat;
    // only the sides with trainSides true record policy targets (training
    // only samples moves with targets, so the other side's never train), and
    // records mark the pool opponent's seat (GameRecord::poolOpponentSeat)
    bool randomSeats = false;
    std::array<bool, 2> trainSides{true, true};
    // Handicap games: this share of games give one player, at random, an
    // edge, so the bot sees games where it's behind and can come back. Most
    // (handicapIncomeShare) as the real game handicaps its bots: an extra
    // capital income of one of handicapIncome stars a turn
    // (GameSettings::capitalIncome); the rest as a head start of one of
    // handicapStars stars (GameSettings::headStart).
    double handicapShare = 0, handicapIncomeShare = 0.75;
    std::vector<int> handicapIncome{1, 2};
    std::vector<int> handicapStars{3, 6, 10};
    // Of handicap games, this share are catch-up games instead: from turn
    // catchUpFromTurn on, once both players have met, the first side whose
    // search value falls below catchUpBelow gets one of handicapIncome more
    // capital income from its next turn on (GameSettings::incomeBonuses), so
    // a side behind in the mid or late game can come back
    double handicapCatchUpShare = 0.5;
    int catchUpFromTurn = 10;
    double catchUpBelow = -0.5;
    // in handicap games, a side whose search value is below catchUpBelow
    // searches with this many times the simulations, for better moves where
    // the bot tends to give up
    int trailingSearchFactor = 2;
    // on full searches only, this many of the root's Gumbel candidates are
    // random legal moves (SearchConfig::exploreCandidates)
    int exploreCandidates = 2;
    // If set, a game whose search finds nothing to do (BatchedSearch::result
    // throws) is appended here as its record so far, to replay the position
    std::string failuresPath;
    // per side, a forced plan (plans.h) for planTurns turns: from the start,
    // or with planWhenAhead (not NaN) from the side's first decision with its
    // own search value at least that (its previous one's, a network side's).
    // It chooses only among the commands the plan allows, played at once
    // when there's one, and none of its decisions then record policy
    // targets.
    std::array<Plan, 2> sidePlans{Plan::NONE, Plan::NONE};
    int planTurns = 20;
    double planWhenAhead = std::nan("");
    // the map types games are drawn from, evenly; empty for every type
    std::vector<MapTypes::MapType> mapTypes;
    // per side, a whole search instead of `search`, for a match between two
    // searches; self-play's own adjustments (quick searches, root noise,
    // exploration candidates, a trailing side's budget) apply on top as for
    // `search`
    std::array<std::optional<SearchConfig>, 2> sideSearch;
};

// One game. The evaluator scores positions for the search.
GameRecord playGame(const SelfPlayConfig &config, Evaluator &evaluator,
                    std::uint64_t seed);

// `count` games over `threads` threads, each with its own score evaluator
// (the bootstrap); without final values, since the score evaluator's values
// are score leads
std::vector<GameRecord> playGames(const SelfPlayConfig &config, int count,
                                  std::uint64_t seed, int threads);

// ---- self-play with a network ----

// The inputs for a batch of search requests: planes [B][C][N][N], globals
// [B][G] and legal masks [B][A] (skipped if null), from each request's
// player's view
void encodeRequests(const std::vector<EvalRequest> &requests,
                    const EncodingConfig &config, float *planes, float *globals,
                    std::uint8_t *legal, int threads = 1);
void encodeRequests(const std::vector<EvalRequest> &requests,
                    const EncodingConfig &config, float *planes, float *globals,
                    std::uint8_t *legal, ThreadPool &pool);
// The command slots of each request's moves, flattened: for the k-th move
// overall, the request it belongs to and its slot (-1 if it has none). The
// network's output can then be cut down to these before leaving the GPU.
void requestSlots(const std::vector<EvalRequest> &requests,
                  const EncodingConfig &config, std::vector<int> &rows,
                  std::vector<int> &slots);
// Evaluations from the logits of those slots alone ([K], in that order) and
// the values [B]: each request's priors are the softmax of its moves' logits.
// With `tieBreaks` [B][2], each position's predicted score lead and turns left
// (Evaluation::scoreLead, turnsLeft).
void evaluationsFromMoveLogits(const std::vector<EvalRequest> &requests,
                               const std::vector<int> &slots,
                               const float *moveLogits, const float *values,
                               std::vector<Evaluation> &results,
                               const float *tieBreaks = nullptr);
// The network's raw belief outputs ([R][beliefOutputCount], one row per
// request that wants beliefs, in order), decoded into those requests'
// results
void attachBeliefs(const std::vector<EvalRequest> &requests,
                   const EncodingConfig &config, const float *outputs,
                   std::vector<Evaluation> &results);
// The same from full policy logits [B][A]
void evaluationsFromNetwork(const std::vector<EvalRequest> &requests,
                            const EncodingConfig &config, const float *logits,
                            const float *values,
                            std::vector<Evaluation> &results);

// Many self-play games at once, each searching its current decision, all of
// their waiting positions evaluated together. The caller only evaluates:
//
//   SelfPlaySession session(config, 64, 1000, seed);
//   while (!session.done()) {
//       session.submit(evaluate(session.pending()));
//   }
//   std::vector<GameRecord> games = session.takeFinished();
//
// Moves, records and new games are handled here.
class SelfPlaySession {
  public:
    // games numbered from `first`: with the same seed, sessions of games
    // [0, n) and [n, m) play the same games as one session of [0, m) (n even
    // for paired games)
    SelfPlaySession(const SelfPlayConfig &config, int parallel, int games,
                    std::uint64_t seed, int first = 0);
    ~SelfPlaySession();
    SelfPlaySession(const SelfPlaySession &) = delete;
    SelfPlaySession &operator=(const SelfPlaySession &) = delete;

    bool done() const;
    // every waiting position, across all games; valid until submit(). Empty
    // once advancing the games finished the last of them.
    const std::vector<EvalRequest> &pending();
    // the last pending() again, without advancing any game; after
    // prepare(), only the positions the cache couldn't answer
    const std::vector<EvalRequest> &waiting() const { return prepared ? served : requests; }
    // for each of those, the side whose search it's for (only NETWORK sides'
    // positions are pending; the others are played in C++)
    const std::vector<int> &waitingSides() const {
        return prepared ? servedSides : requestSides;
    }
    void submit(const std::vector<Evaluation> &results);

    // ---- the evaluation cache ----
    // Between pending() and the network: encodes every pending position (as
    // encodeRequests does) and answers those whose exact input this game's
    // search already had evaluated this turn, by the same side's network.
    // waiting() is then only the rest, in order, and copyPrepared gives their
    // inputs; returns how many. A position a later search asks about again,
    // when tree reuse couldn't continue, or moves in a different order, cost
    // a fifth to a quarter of all evaluations.
    std::size_t prepare(const EncodingConfig &config);
    // pending() then prepare(), over again while the cache answers every
    // waiting position (its answers submitted each time): how many positions
    // need the network, 0 only once nothing is pending. What a caller that
    // takes an empty batch as the end should use.
    std::size_t pendingForNetwork(const EncodingConfig &config);
    // the inputs of the waiting() positions: planes [B][C][N][N], globals [B][G]
    void copyPrepared(float *planes, float *globals) const;
    // The network's answers for the waiting() positions (as
    // evaluationsFromMoveLogits takes them; tieBreaks and beliefs may be
    // null), kept in the cache, then with the cache's answers submitted
    void submitMoveLogits(const EncodingConfig &config, const float *moveLogits,
                          const float *values, const float *tieBreaks,
                          const float *beliefs);
    // evaluations for the waiting() positions, with the cache's answers
    // submitted (nothing kept in the cache)
    void submitServed(const std::vector<Evaluation> &results);
    // forget every game's cached evaluations: after the network changes
    void clearCache();
    struct CacheStats {
        std::int64_t lookups = 0, hits = 0;
    };
    CacheStats takeCacheStats() { return std::exchange(cacheStats, {}); }
    // the games finished since the last call
    std::vector<GameRecord> takeFinished();
    int finishedGames() const { return finished; }
    // Ends every game still being played, as if the turn limit had come
    // (scored by the lead now), and starts no more. Call between submit()
    // and the next pending(). For cutting short a match's last stragglers.
    void finishNow();
    // Gives every game still being played `moves` more commands, then ends
    // it the same way; starts no more games. A gentler cut than finishNow().
    void finishAfter(int moves);
    // the share of games started from now on that have fog (a schedule
    // raising it during training)
    void setFogShare(double share) { config.fogShare = share; }
    int threads() const { return config.threads; }
    // per side, how deep its searches have gone so far (BatchedSearch::DepthStats)
    const std::array<BatchedSearch::DepthStats, 2> &depthStats() const { return depth; }
    // the session's threads, for encoding its pending positions
    ThreadPool &threadPool() { return *pool; }

  private:
    struct Slot;
    SelfPlayConfig config;
    int total;
    std::uint64_t seed;
    int first;
    // games are numbered as they start, and each one's seed comes from its
    // number, so the games don't depend on which thread starts them
    std::atomic<int> started{0}, finished{0};
    std::vector<std::unique_ptr<Slot>> slots;
    std::unique_ptr<ThreadPool> pool;
    std::array<BatchedSearch::DepthStats, 2> depth;
    std::mutex depthMutex;
    std::mutex doneMutex;
    std::vector<GameRecord> done_;
    std::vector<EvalRequest> requests;
    std::vector<int> requestSides;
    // per slot with requests: the slot, and how many
    std::vector<std::pair<int, int>> owners;
    // after prepare(): the pending positions encoded, their keys and slots,
    // the ones the network still has to answer (and where they came from),
    // and the cache's answers for the rest
    bool prepared = false;
    std::vector<float> encodedPlanes, encodedGlobals;
    std::size_t planeSize = 0, globalSize = 0;
    struct CacheKey {
        std::uint64_t a = 0, b = 0;
        int side = 0;
        bool operator==(const CacheKey &) const = default;
    };
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey &k) const {
            return static_cast<std::size_t>(k.a ^ (k.b * 31) ^ static_cast<std::uint64_t>(k.side));
        }
    };
    struct CachedEvaluation {
        float value = 0, scoreLead = 0, turnsLeft = 0;
        // the logit of each move slot asked about, by slot
        std::vector<std::pair<int, float>> logits;
    };
    using Cache = std::unordered_map<CacheKey, CachedEvaluation, CacheKeyHash>;
    std::vector<CacheKey> keys;
    std::vector<int> requestSlot, servedFrom;
    std::vector<EvalRequest> served;
    std::vector<int> servedSides;
    std::vector<char> answered;
    std::vector<Evaluation> answers;
    CacheStats cacheStats;

    void startGame(Slot &slot);
    void noteEconomy(Slot &slot);
    void maybeCatchUp(Slot &slot, float value);
    // records the slot's game as it stands
    void finishGame(Slot &slot);
    // plays the slot's decisions until its search wants evaluations
    void advance(Slot &slot);
    void noteCombat(Slot &slot, const Command &command);
};

