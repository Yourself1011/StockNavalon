#pragma once

// Game records for training (docs/stockNavalon.md section 5). A record is
// the game's setup and its commands, not encoded positions: the engine is
// deterministic, so replaying the commands rebuilds every position exactly.
// That keeps a game to a few KB, and the records outlive encoding changes:
// positions are encoded when training reads them.

#include "command.h"
#include "encoding.h"
#include "game.h"
#include "mapgen.h"
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Gumbel search's improved policy, stored as whole weights
constexpr int POLICY_WEIGHT_SCALE = 10000;

struct RecordedMove {
    int player;
    Command command;
    // the policy target's weights per root move, normalized when encoded:
    // the search's root visits, or with Gumbel search the improved policy in
    // units of 1 / POLICY_WEIGHT_SCALE; empty for a command the search didn't
    // choose (a random bot's)
    std::vector<std::pair<Command, int>> visits;
    // the search's value of the position for the player to move (its root
    // moves' values, weighted by visits), for the value target's search
    // term; NaN without one (a score evaluator's search, older records)
    float rootValue = std::nanf("");
};

struct GameRecord {
    MapGenSettings map;
    std::uint64_t gameSeed = 0;
    std::vector<RecordedMove> moves;
    // how it ended: the winner (-1 if the turn limit came first), the turn,
    // and each player's score
    int winner = -1, turns = 0;
    std::vector<int> finalScores;
    // For a game cut off by the turn limit, each seat's estimate of its
    // chances at the end: the root value of the search at its last decision
    // with the network, in [-1, 1]. NaN for a seat without one (a score
    // evaluator's searches estimate the score lead, not winning), and empty
    // for a decided game or an older record.
    std::vector<float> finalValues;
    // For the side targets (docs/encoding.md section 8.5), filled in when a
    // game ends; empty in older records:
    // the owner of each tile when the game ended, -1 for nobody (size x size)
    std::vector<int> finalTerritory;
    // each player's latest published score at each turn, turns x players
    std::vector<int> turnScores;
    // whether the game was played with fog; without it, its positions are
    // encoded with full information (SelfPlayConfig::fogShare)
    bool fog = true;
    // whether peace treaties were allowed (GameSettings::peaceTreaties), so
    // a replay has the same legal commands
    bool peaceTreaties = true;
    // whether diplomacy introduced the sender (GameSettings::diplomacyMeets):
    // false for games recorded before the engine had the rule
    bool diplomacyMeets = true;
    // each player's cities when the game ended, for the early city term of
    // the value target; empty in older records
    std::vector<int> finalCities;
    // in a match, the side that played seat 0 (the other side played seat
    // 1); not saved with the record
    int seatZeroSide = 0;
    // ended by the stalemate cutoff (SelfPlayConfig::stallTurns) rather than
    // the turn limit; not saved with the record
    bool stalled = false;

    // the game before any command
    Game start() const;
    // whether every command is legal when replayed, and no player's stars go
    // below zero: a check that the record replays as it was played
    bool replaysLegally() const;
    // +1 or -1 for a win or loss, 0 for a game cut off by the turn limit
    float result(int player) const;
    // The value target: the result if the game was decided; if it was cut
    // off, the player's final value, as a truncated game rather than a draw
    // (or 0 without one)
    float value(int player) const;
    // the final score lead, squashed into [-1, 1]
    float scoreValue(int player, double scale) const;
    // the final city lead: (the player's cities - the best opponent's) /
    // (their sum), in [-1, 1]; 0 without the counts
    float cityValue(int player) const;
};

// Keeps the search's visits (the policy target, and what makes a move a
// training position) on a random `share` of each record's moves, and drops
// them on the rest: a match's games, where every move had a full search, at
// self-play's density (SelfPlayConfig::fullShare)
void thinPolicyTargets(std::vector<GameRecord> &records, double share,
                       std::uint64_t seed);

// Records as text, one game after another; appends to the file, or replaces it
void saveRecords(const std::vector<GameRecord> &records, const std::string &path,
                 bool append = true);
std::vector<GameRecord> loadRecords(const std::string &path);

// How the value target and its weight are made (docs/stockNavalon.md section
// 6). The target is about winning, GameRecord::value, blended with the final
// city lead only while `cityWeight` is above 0 (early in a run, while true
// results are scarce; cities are what a capture takes):
//   (1 - cityWeight) x value + cityWeight x GameRecord::cityValue.
// A position of a game cut off by the turn limit or a stalemate weighs
// `cutOffWeight`, a decided game's 1: cut-off targets carry little
// information but far outnumber real results.
struct ValueTarget {
    double cityWeight = 0;
    // the share of the target from the position's own search value
    // (RecordedMove::rootValue), where there is one: (1 - searchWeight) x
    // the result + searchWeight x the search value, before the city term
    double searchWeight = 0;
    double cutOffWeight = 1;
};

// Training examples: positions picked as (record, move) pairs, each the
// position just before that move, for the player to move there. Arrays as
// in docs/encoding.md: planes [B][C][N][N], globals [B][G], legal [B][A],
// policy [B][A] (the visits, normalized), the value (GameRecord::value; about
// winning only, never the score: docs/stockNavalon.md section 6) and its
// weight [B], and the
// belief targets and masks [B][T].
struct ExampleArrays {
    float *planes, *globals;
    std::uint8_t *legal;
    float *policy, *value, *valueWeight, *targets;
    std::uint8_t *targetMasks;
    // the side targets and their masks [B][S]; skipped if null
    float *sideTargets = nullptr;
    std::uint8_t *sideMasks = nullptr;
};

// The side targets for `player` at game turn `turn` of a recorded game
// ([sideTargetCount] each): the score leads from its score table and final
// scores, the territory from its final map. Masked where the record has none.
void encodeSideTargets(const GameRecord &record, int turn, int player,
                       const EncodingConfig &config, float *targets,
                       std::uint8_t *masks);
// Each position's game is replayed to it, so the games are spread over
// `threads` threads. With `symmetries` (one per position), each example is
// seen through that symmetry of the map (applySymmetry).
void encodeExamples(const std::vector<GameRecord> &records,
                    const std::vector<std::pair<int, int>> &positions,
                    const EncodingConfig &config, const ExampleArrays &out,
                    int threads = 1,
                    const std::vector<int> &symmetries = {},
                    const ValueTarget &value = {});

// The recent games training draws from (docs/stockNavalon.md section 5):
// positions are sampled across all of them, so a minibatch isn't dominated
// by the neighbouring positions of one game, and the oldest games drop out
// as new ones come in.
//
// Safe to use from several threads (self-play adding while training reads).
// Indices from sample() only hold until the next add(), which can drop the
// oldest games, so a training thread uses encodeSample(), which does both at
// once.
class ReplayBuffer {
  public:
    explicit ReplayBuffer(std::size_t maxPositions) : maxPositions(maxPositions) {}

    void add(std::vector<GameRecord> games);
    // not while another thread adds
    const std::vector<GameRecord> &games() const { return records; }
    std::size_t gameCount() const;
    // positions with a policy target: moves the search chose
    std::size_t positions() const;
    // positions ever added, dropped ones included: the training throttle
    // counts against it
    std::size_t added() const;
    // the share of games decided by a capture rather than the turn limit
    double decidedShare() const;

    // `count` random positions with a policy target, in groups of `perGame`
    // from the same game: a game is replayed once for its whole group, and
    // positions far apart in a long game are rarely alike
    std::vector<std::pair<int, int>> sample(int count, std::uint64_t seed,
                                            int perGame = 1) const;
    void encode(const std::vector<std::pair<int, int>> &positions,
                const EncodingConfig &config, const ExampleArrays &out,
                int threads = 1, const ValueTarget &value = {}) const;
    // writes exactly the buffer's games to `path`, replacing it
    void save(const std::string &path) const;
    // sample() and encode() together, with no add() in between; with
    // `augment`, each example through a random symmetry of the map. With
    // `drawn`, also which positions they were, as (game seed, move): stable
    // while games come and go, so the same positions can be encoded again.
    void encodeSample(int count, std::uint64_t seed, int perGame,
                      const EncodingConfig &config, const ExampleArrays &out,
                      int threads = 1, bool augment = false,
                      const ValueTarget &value = {},
                      std::vector<std::pair<std::uint64_t, int>> *drawn = nullptr) const;

  private:
    std::vector<std::pair<int, int>> sampleUnlocked(int count, std::uint64_t seed,
                                                    int perGame) const;
    mutable std::mutex mutex;
    std::size_t maxPositions, searched = 0, total = 0;
    std::vector<GameRecord> records;
};

