#pragma once

// Which tribes an unmet opponent could be, from public information only
// (docs/tribeInference.md). Exact and sound: the opponent's real tribe is
// always in the set.
//
// Three filters: the starting score, climates seen, and a search over the
// opponent's opening score changes.

#include "data/tribeData.h"
#include <bitset>
#include <vector>

class Game;
struct PlayerView;

using TribeSet = std::bitset<TribeTypes::TRIBE_SIZE>;

struct TribeInferenceConfig {
    // How many of the opponent's score changes the search follows. Only
    // their turn 0 narrowed anything in measurements (docs/tribeInference.md
    // section 9), so that's the default.
    int turns = 1;
    // States kept per turn, and the work (states made, at every stage) one
    // turn may take. Past either, the search stops reading and the tribe stays
    // possible: the limits bound the time, never the soundness.
    int maxStates = 5000;
    int maxWork = 50000;
};

// The tribes `opponent` could be, as `view.player` sees it. A met opponent's
// set (and the player's own) is just their tribe.
TribeSet possibleTribes(const PlayerView &view, int opponent,
                        const TribeInferenceConfig &config = {});

// The tribes whose climate the player has seen on some explored tile
TribeSet climatesSeen(const PlayerView &view);

// The tribes whose starting score could be exactly `score`: a tribe's fixed
// starting score (start tech, start unit, capital and its 9 border tiles)
// plus 5 for each of the 9 to 25 tiles it starts with explored.
TribeSet tribesWithStartingScore(int score);

// How many tiles a player of `tribe` with this starting score started with
// explored (9 to 25), or -1 if no number fits. It says where their capital
// is: 25 at least 2 tiles from every edge, fewer near an edge or a corner.
int startingExplored(TribeTypes::Tribe tribe, int score);

// Where the opponent's capital could be, if they're this tribe: tiles the
// player hasn't explored whose distances to the edges fit the starting
// square their starting score implies (empty if the score doesn't fit the
// tribe)
std::vector<int> possibleCapitalTiles(const PlayerView &view, int opponent,
                                      TribeTypes::Tribe tribe);

// The search alone: could a player of `tribe` have produced the opponent's
// published score changes so far? Only for two-player games; otherwise, and
// past the first `config.turns` changes, it doesn't rule anything out.
bool scoreChangesFit(const PlayerView &view, int opponent,
                     TribeTypes::Tribe tribe,
                     const TribeInferenceConfig &config = {});

// The coverage test's link to the engine: `player`'s score in `game` as the
// search models it, from the true state. It must always equal
// `game.score(player)`; a score source the search doesn't know would break
// that.
int searchModelScore(const Game &game, int player);
