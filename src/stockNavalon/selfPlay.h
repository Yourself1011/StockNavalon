#pragma once

// Games for training, the search playing both seats (docs/stockNavalon.md
// section 5)

#include "parallel.h"
#include "record.h"
#include "search.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
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
    // Gumbel search at the root for each side (SearchConfig::gumbel), so a
    // match can put the two searches against each other
    std::array<bool, 2> gumbel{false, false};
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
// the values [B]: each request's priors are the softmax of its moves' logits
void evaluationsFromMoveLogits(const std::vector<EvalRequest> &requests,
                               const std::vector<int> &slots,
                               const float *moveLogits, const float *values,
                               std::vector<Evaluation> &results);
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
    SelfPlaySession(const SelfPlayConfig &config, int parallel, int games,
                    std::uint64_t seed);
    ~SelfPlaySession();
    SelfPlaySession(const SelfPlaySession &) = delete;
    SelfPlaySession &operator=(const SelfPlaySession &) = delete;

    bool done() const;
    // every waiting position, across all games; valid until submit(). Empty
    // once advancing the games finished the last of them.
    const std::vector<EvalRequest> &pending();
    // the last pending() again, without advancing any game
    const std::vector<EvalRequest> &waiting() const { return requests; }
    // for each of those, the side whose search it's for (only NETWORK sides'
    // positions are pending; the others are played in C++)
    const std::vector<int> &waitingSides() const { return requestSides; }
    void submit(const std::vector<Evaluation> &results);
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
    // the session's threads, for encoding its pending positions
    ThreadPool &threadPool() { return *pool; }

  private:
    struct Slot;
    SelfPlayConfig config;
    int total;
    std::uint64_t seed;
    // games are numbered as they start, and each one's seed comes from its
    // number, so the games don't depend on which thread starts them
    std::atomic<int> started{0}, finished{0};
    std::vector<std::unique_ptr<Slot>> slots;
    std::unique_ptr<ThreadPool> pool;
    std::mutex doneMutex;
    std::vector<GameRecord> done_;
    std::vector<EvalRequest> requests;
    std::vector<int> requestSides;
    // per slot with requests: the slot, and how many
    std::vector<std::pair<int, int>> owners;

    void startGame(Slot &slot);
    // records the slot's game as it stands
    void finishGame(Slot &slot);
    // plays the slot's decisions until its search wants evaluations
    void advance(Slot &slot);
    void noteCombat(Slot &slot, const Command &command);
};

