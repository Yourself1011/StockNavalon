#pragma once

#include "data/techData.h"
#include "data/terrain.h"
#include <array>
#include <bitset>
#include <string_view>

namespace ImprovementTypes {
enum ImprovementType {
    NONE,
    // map features, never built
    CITY,
    VILLAGE,
    RUIN,
    LIGHTHOUSE,
    // buildings, which stay on their tile
    FARM,
    MINE,
    LUMBERHUT,
    WINDMILL,
    SAWMILL,
    FORGE,
    MARKET,
    PORT,
    TEMPLE,
    FORESTTEMPLE,
    MOUNTAINTEMPLE,
    WATERTEMPLE,
    // actions, which change the tile and leave nothing behind
    HARVESTFRUIT,
    HUNTING,
    FISHING,
    CLEARFOREST,
    BURNFOREST,
    GROWFOREST,
    ROAD,
    BRIDGE,
    IMPROVEMENT_TYPE_SIZE
};
} // namespace ImprovementTypes

using ImprovementSet = std::bitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>;

// GAME.md section 6
struct ImprovementData {
    ImprovementTypes::ImprovementType type;
    std::string_view name;
    // two characters, for the map display; empty for actions
    std::string_view id;
    bool isBuilding;
    int cost;
    // population given to the tile's city; for Windmill, Sawmill and Forge,
    // population per level
    int population;
    TechTypes::TechType tech;
    TerrainSet terrain;
    ResourceTypes::ResourceType resource;
    // at least one neighbouring tile must have one of these. For Windmill,
    // Sawmill and Forge each such neighbour is also one level.
    ImprovementSet adjacency;
    // one per city
    bool limited;
    // can be built on tiles outside your borders
    bool freelance;
};

extern const std::array<ImprovementData, ImprovementTypes::IMPROVEMENT_TYPE_SIZE>
    improvementData;

inline const ImprovementData &
improvementInfo(ImprovementTypes::ImprovementType type) {
    return improvementData[type];
}

// Windmill, Sawmill and Forge: buildings that level up with their neighbours
bool isProducer(ImprovementTypes::ImprovementType type);

// the highest level of Windmill, Sawmill, Forge and Market
constexpr int MAX_BUILDING_LEVEL = 8;
constexpr int MAX_TEMPLE_LEVEL = 5;
