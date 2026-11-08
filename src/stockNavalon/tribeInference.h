#pragma once

// Which tribes an unmet opponent could be, from public information only
// (docs/tribeInference.md). Exact and sound: the opponent's real tribe is
// always in the set.
//
// Three filters: the starting score, climates seen, and a search over the
// opponent's opening score changes.

#include "data/tribeData.h"
#include <bitset>

struct PlayerView;

using TribeSet = std::bitset<TribeTypes::TRIBE_SIZE>;

struct TribeInferenceConfig {
    // how many of the opponent's score changes the search follows
    int turns = 5;
    // states kept per turn; past this the search stops, and the tribe stays
    // possible
    int maxStates = 5000;
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

// The search alone: could a player of `tribe` have produced the opponent's
// published score changes so far? Only for two-player games; otherwise, and
// past the first `config.turns` changes, it doesn't rule anything out.
bool scoreChangesFit(const PlayerView &view, int opponent,
                     TribeTypes::Tribe tribe,
                     const TribeInferenceConfig &config = {});
