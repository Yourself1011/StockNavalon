#pragma once

// Game states and commands as arrays, for stockNavalon's network. The spec,
// with every plane and slot, is docs/encoding.md; ENCODING_VERSION goes up
// whenever their meaning changes.

#include "command.h"
#include <cstdint>
#include <string>
#include <vector>

class Game;
struct PlayerView;

struct EncodingConfig {
    // every map is padded to mapSize x mapSize
    int mapSize = 11;
    // seats encoded, the player to move first
    int players = 2;

    bool operator==(const EncodingConfig &) const = default;
};

constexpr int ENCODING_VERSION = 4;
// how many of each opponent's first score changes are encoded
constexpr int OPENING_CHANGES = 6;

// the per-tile command slots (docs/encoding.md section 5)
namespace EncodingSlots {
constexpr int OFFSET_RADIUS = 6;
constexpr int OFFSET_SIDE = 2 * OFFSET_RADIUS + 1;
// Move, Attack and Disembark, by target offset
constexpr int TARGET = 0;
// Recover, Heal Others, Promote, Disband, Capture, Examine Ruins, Harvest
constexpr int UNIT_ACTION = TARGET + OFFSET_SIDE * OFFSET_SIDE;
// to Scout, Rammer, Bomber
constexpr int UPGRADE = UNIT_ACTION + 7;
// farm ... bridge, in ImprovementType order
constexpr int BUILD = UPGRADE + 3;
constexpr int DESTROY = BUILD + 27;
// warrior ... cloak, in UnitType order
constexpr int TRAIN = DESTROY + 1;
// the first or second option for the level being rewarded
constexpr int REWARD = TRAIN + 9;
constexpr int PER_TILE = REWARD + 2;

// global slots, after the per-tile ones
constexpr int RESEARCH = 25;
constexpr int DIPLOMACY_PER_OPPONENT = 6;
} // namespace EncodingSlots

// C, G and A: the planes, global features and command slots
int planeCount(const EncodingConfig &config);
int globalCount(const EncodingConfig &config);
int actionCount(const EncodingConfig &config);

// Writes the player's input: planes as [C][N][N] (row y, column x) and the
// global vector as [G]. Reads only what the player knows: their view, the
// public score history, attack previews between units they can see, and
// their own units' moves. Throws std::invalid_argument if the game doesn't
// fit the config.
void encodeState(const Game &game, int player, const EncodingConfig &config,
                 float *planes, float *globals);

// The slot of a command of the current player; -1 if it has none (Resign,
// Stay, or a tile outside the config's map size).
int encodeCommand(const Game &game, const Command &command,
                  const EncodingConfig &config);
// The legal command in that slot. Throws std::invalid_argument if none is.
Command decodeCommand(const Game &game, int index, const EncodingConfig &config);
// Writes [A]: 1 for each legal command's slot, 0 elsewhere.
void legalMask(const Game &game, const EncodingConfig &config,
               std::uint8_t *mask);
// The same from an already known legal list, e.g. LegalMoves::commands().
void legalMask(const std::vector<Command> &legal, const Game &game,
               const EncodingConfig &config, std::uint8_t *mask);

// The part of a player's published score that the viewer can account for
// from what they see (docs/encoding.md section 4.1).
int explainedScore(const PlayerView &view, int player);

// ---- training targets for the belief heads (docs/encoding.md section 8) ----

// the hidden unit count head's last class: this many or more
constexpr int HIDDEN_UNIT_COUNT_CAP = 20;

// One belief head's part of the target array, for one opponent (`opponent`
// counts from 1, relative to the player)
struct TargetHead {
    std::string name;
    int opponent, offset, size;
};
// every head, for every opponent, in order
std::vector<TargetHead> targetHeads(const EncodingConfig &config);
int targetCount(const EncodingConfig &config);

// Writes the belief targets for `player` from the true game, and with them a
// mask: 1 where the target is trained, 0 where the player already knows the
// answer or there's nothing to learn. Both are [targetCount]. The value
// target is the game's result, so whoever records the game adds it at the
// end. Throws std::invalid_argument if the game doesn't fit the config.
void encodeTargets(const Game &game, int player, const EncodingConfig &config,
                   float *targets, std::uint8_t *masks);
