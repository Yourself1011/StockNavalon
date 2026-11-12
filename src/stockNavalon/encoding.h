#pragma once

// Game states and commands as arrays, for stockNavalon's network. The spec,
// with every plane and slot, is docs/encoding.md; ENCODING_VERSION goes up
// whenever their meaning changes.

#include "command.h"
#include "determinize.h"
#include <cstdint>
#include <string>
#include <utility>
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

constexpr int ENCODING_VERSION = 6;
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
                 float *planes, float *globals, bool fullInformation = false);
// fullInformation (for training without fog, docs/encoding.md section 10):
// from the full view instead, every tile's contents and every opponent's
// techs, stars and tribe, with `explored` still the tiles the player has
// actually explored.

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
    // per-tile heads: how many [N][N] layers they are; 0 for the rest
    int tileLayers = 0;
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
                   float *targets, std::uint8_t *masks, bool fullInformation = false);
// with fullInformation nothing is hidden, so every mask is 0

// The map-wide belief head, of the unexplored tiles (opponent 0 in
// targetHeads): per tile, a softmax over terrains, one over resources (NONE
// first), and one over features (TileFeature), as [LAYERS][N][N]
namespace UnexploredHead {
constexpr int TERRAIN = 0;
constexpr int RESOURCE = TERRAIN + TerrainTypes::TERRAIN_TYPE_SIZE;
constexpr int FEATURE = RESOURCE + ResourceTypes::RESOURCE_TYPE_SIZE;
constexpr int LAYERS = FEATURE + static_cast<int>(TileFeature::FEATURE_SIZE);
} // namespace UnexploredHead

// ---- reading the belief heads back (docs/encoding.md section 8.4) ----

// the stars head: a mixture of this many negative binomials, each with a
// weight, a mean and a spread (raw, before softmax and softplus)
constexpr int STAR_COMPONENTS = 3;
// the most stars a sample can have
constexpr int STAR_COUNT_CAP = 60;

// The network's raw belief outputs as one flat array per position: the same
// heads and order as targetHeads, logits where those have probabilities,
// and for the stars the mixture's 3 x STAR_COMPONENTS raw parameters
std::vector<TargetHead> beliefOutputHeads(const EncodingConfig &config);
int beliefOutputCount(const EncodingConfig &config);

// The beliefs those outputs describe, for `view`'s player (Beliefs is by
// seat; the player's own entry stays empty): softmaxes and sigmoids applied,
// the stars mixture turned into a weight per count. What's impossible is
// left to determinize(), which samples within the possible.
Beliefs decodeBeliefs(const PlayerView &view, const EncodingConfig &config,
                      const float *outputs);

// ---- side targets (docs/encoding.md section 8.5) ----

// Targets for side heads the search never reads, from how the game went: the
// score lead a few turns later and at the end, squashed, and each tile's
// owner when the game ended (mine, an opponent's, nobody's), as [3][N][N].
namespace SideTargets {
constexpr int SCORE_SOON = 0;
constexpr int SCORE_END = 1;
constexpr int TERRITORY = 2;
constexpr int TERRITORY_CLASSES = 3;
// how far ahead the first score lead looks
constexpr int SOON_TURNS = 5;
// the score lead's squash: tanh(lead / SCORE_SCALE)
constexpr double SCORE_SCALE = 500;
} // namespace SideTargets
int sideTargetCount(const EncodingConfig &config);

// ---- symmetries (docs/encoding.md section 9) ----

// The square map's 8 symmetries, rotations and reflections; 0 is the
// identity. The rules don't depend on direction, so a position and its
// transform are equally good to train on.
constexpr int SYMMETRIES = 8;

// Where symmetry s takes tile (x, y) of a mapSize x mapSize map, and an
// offset (dx, dy) between tiles
std::pair<int, int> transformTile(int s, int x, int y, int mapSize);
std::pair<int, int> transformOffset(int s, int dx, int dy);

// Applies symmetry s in place to one encoded position, all of it seen
// through the transformed map: the planes [C][N][N], any [A] command arrays
// (the legal mask, the policy: tile slots move with their tile, and target
// offsets turn with it), and the belief targets and masks [T]. The map is
// mapSize x mapSize in the top-left corner; padding stays as it is. Any
// pointer may be null.
void applySymmetry(int s, int mapSize, const EncodingConfig &config,
                   float *planes, std::uint8_t *legal, float *policy,
                   float *targets, std::uint8_t *targetMasks);
// The same for `layers` consecutive [N][N] layers of any per-tile array
void applySymmetry(int s, int mapSize, const EncodingConfig &config, int layers,
                   float *data);
void applySymmetry(int s, int mapSize, const EncodingConfig &config, int layers,
                   std::uint8_t *data);
