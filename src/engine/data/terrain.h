#pragma once

#include <array>

namespace TerrainTypes {
// WATER is shallow water, OCEAN is deep water
enum TerrainType { FIELD, FOREST, MOUNTAIN, WATER, OCEAN, TERRAIN_TYPE_SIZE };

const std::array<char, TERRAIN_TYPE_SIZE> terrainChars = {'F', 'T', 'M', 'W',
                                                          'O'};

inline bool isWater(TerrainType terrain) {
    return terrain == WATER || terrain == OCEAN;
}
} // namespace TerrainTypes

namespace ResourceTypes {
enum ResourceType {
    NONE,
    FRUIT,
    CROP,
    GAME,
    METAL,
    FISH,
    STARFISH,
    RESOURCE_TYPE_SIZE
};

// map file and display characters; NONE has none
const std::array<char, RESOURCE_TYPE_SIZE> resourceChars = {' ', 'f', 'c', 'g',
                                                            'm', 'h', 's'};
} // namespace ResourceTypes
