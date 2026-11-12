#pragma once

// Determinization (docs/stockNavalon.md section 4): from what one player
// knows, plus beliefs about the rest, a complete game to search in. Every
// sample agrees exactly with the player's view; everything hidden is
// sampled, never copied.

#include "data/terrain.h"
#include "data/tribeData.h"
#include "data/unitData.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

class Game;
struct PlayerView;

// What the player believes about one opponent, in engine terms. The
// network's belief heads give these (docs/encoding.md section 8); until
// there's a network, priorBeliefs does. Every entry is a weight, so they
// needn't sum to 1.
struct OpponentBeliefs {
    // their tribe, if not met: sampled among the tribes still possible
    std::array<float, TribeTypes::TRIBE_SIZE> tribe{};
    // each tech owned, as a probability
    std::array<float, TechTypes::TECH_TYPE_SIZE> tech{};
    // their stars: a weight for each count from 0
    std::vector<float> stars;
    // how many of their units the player can't see: a weight per count
    std::vector<float> hiddenUnitCount;
    // per tile: how likely a hidden unit of theirs is there, and if so of
    // which type
    std::vector<float> unitHere;
    std::vector<std::array<float, UnitTypes::UNIT_TYPE_SIZE>> unitType;
};

// what an unexplored tile holds besides its terrain and resource
enum class TileFeature { NONE, VILLAGE, RUIN, FEATURE_SIZE };

struct Beliefs {
    // by seat; the player's own entry is unused
    std::vector<OpponentBeliefs> players;
    // The map's unexplored tiles, per tile: weights for its terrain, its
    // resource and its feature. Empty without a network: unexplored tiles
    // then come from a freshly generated map.
    std::vector<std::array<float, TerrainTypes::TERRAIN_TYPE_SIZE>> terrain;
    std::vector<std::array<float, ResourceTypes::RESOURCE_TYPE_SIZE>> resource;
    std::vector<std::array<float, static_cast<int>(TileFeature::FEATURE_SIZE)>> feature;
};

// Beliefs with no model behind them: any possible tribe, a few techs, some
// stars and a few hidden units, anywhere. For tests and for the first games,
// before there's a network.
Beliefs priorBeliefs(const PlayerView &view);

enum class Detail {
    // the tiles and hidden units: enough for a search within the player's
    // own turn. The opponent's stars stay at 0 and their score isn't matched.
    TILES_AND_UNITS,
    // everything, with the opponent's published score matched exactly
    EVERYTHING,
};

struct DeterminizeStats {
    // samples drawn, and rejected: they couldn't match an opponent's score,
    // or had nowhere to put a city the player has seen the borders of
    int tries = 0, rejected = 0;
};

// A game that agrees with everything `player` knows of `real`, with the rest
// sampled from `beliefs` using `seed`. Samples that can't match an
// opponent's published score are rejected and drawn again, up to
// `maxTries`; nullopt if none fit. Two-player games only, for now.
std::optional<Game> determinize(const Game &real, int player,
                                const Beliefs &beliefs, std::uint64_t seed,
                                Detail detail = Detail::EVERYTHING,
                                int maxTries = 20,
                                DeterminizeStats *stats = nullptr);
