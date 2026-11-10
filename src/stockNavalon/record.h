#pragma once

// Game records for training (docs/stockNavalon.md section 7). A record is
// the game's setup and its commands, not encoded positions: the engine is
// deterministic, so replaying the commands rebuilds every position exactly.
// That keeps a game to a few KB, and the records outlive encoding changes:
// positions are encoded when training reads them.

#include "command.h"
#include "encoding.h"
#include "game.h"
#include "mapgen.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct RecordedMove {
    int player;
    Command command;
    // the search's root visits, for the policy target; empty for a command
    // the search didn't choose (a random bot's)
    std::vector<std::pair<Command, int>> visits;
};

struct GameRecord {
    MapGenSettings map;
    std::uint64_t gameSeed = 0;
    std::vector<RecordedMove> moves;
    // how it ended: the winner (-1 if the turn limit came first), the turn,
    // and each player's score
    int winner = -1, turns = 0;
    std::vector<int> finalScores;
    // in a match, the side that played seat 0 (the other side played seat
    // 1); not saved with the record
    int seatZeroSide = 0;

    // the game before any command
    Game start() const;
    // +1 or -1 for a win or loss, 0 for a game cut off by the turn limit
    float result(int player) const;
    // the final score lead, squashed into [-1, 1]
    float scoreValue(int player, double scale) const;
};

// Records as text, one game after another; appends to the file, or replaces it
void saveRecords(const std::vector<GameRecord> &records, const std::string &path,
                 bool append = true);
std::vector<GameRecord> loadRecords(const std::string &path);

// The value target (docs/stockNavalon.md section 7): mostly the result, with
// a small score term,
//   (1 - scoreWeight) x the result + scoreWeight x the score value,
// where a game cut off by the turn limit has a result of 0, a draw. Kept
// small, the score term breaks ties without making a lead at the limit look
// nearly as good as a win, so winning stays worth playing for.
struct ValueTarget {
    double scoreWeight = 0.1;
    double scale = 500;
};

// Training examples: positions picked as (record, move) pairs, each the
// position just before that move, for the player to move there. Arrays as
// in docs/encoding.md: planes [B][C][N][N], globals [B][G], legal [B][A],
// policy [B][A] (the visits, normalized), value and its weight [B], and the
// belief targets and masks [B][T].
struct ExampleArrays {
    float *planes, *globals;
    std::uint8_t *legal;
    float *policy, *value, *valueWeight, *targets;
    std::uint8_t *targetMasks;
};
// Each position's game is replayed to it, so the games are spread over
// `threads` threads.
void encodeExamples(const std::vector<GameRecord> &records,
                    const std::vector<std::pair<int, int>> &positions,
                    const EncodingConfig &config, const ValueTarget &value,
                    const ExampleArrays &out, int threads = 1);

// The recent games training draws from (docs/stockNavalon.md section 7.1):
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
                const EncodingConfig &config, const ValueTarget &value,
                const ExampleArrays &out, int threads = 1) const;
    // writes exactly the buffer's games to `path`, replacing it
    void save(const std::string &path) const;
    // sample() and encode() together, with no add() in between
    void encodeSample(int count, std::uint64_t seed, int perGame,
                      const EncodingConfig &config, const ValueTarget &value,
                      const ExampleArrays &out, int threads = 1) const;

  private:
    std::vector<std::pair<int, int>> sampleUnlocked(int count, std::uint64_t seed,
                                                    int perGame) const;
    mutable std::mutex mutex;
    std::size_t maxPositions, searched = 0, total = 0;
    std::vector<GameRecord> records;
};

