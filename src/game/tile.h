#pragma once

#include "data/improvementData.h"
#include "data/terrain.h"
#include <cstdint>

// Plain value: everything else is referred to by index, so a Game can be
// copied as a whole.
struct Tile {
    TerrainTypes::TerrainType terrain = TerrainTypes::FIELD;
    ResourceTypes::ResourceType resource = ResourceTypes::NONE;
    // a map feature or a building, never an action
    ImprovementTypes::ImprovementType building = ImprovementTypes::NONE;
    // Windmill, Sawmill and Forge level
    int level = 0;
    // the turn a temple was built, for its level
    int builtTurn = 0;
    // road or bridge
    bool road = false;
    // index into Game::units, -1 if empty
    int unit = -1;
    // index into Game::cities of the city whose borders contain this tile, -1
    // if none. A city's own tile is inside its borders.
    int territory = -1;
    // bit p is set once player p has explored the tile. Explored tiles stay
    // explored and show their live contents.
    std::uint32_t explored = 0;

    bool exploredBy(int player) const { return explored >> player & 1; }
    bool operator==(const Tile &) const = default;
};
