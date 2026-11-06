#pragma once

#include "data/improvementData.h"
#include "data/unitData.h"
#include "tile.h"
#include <array>
#include <bitset>
#include <vector>

namespace TechTypes {
enum TechTypes {
    BASIC,
    RIDING,
    ROADS,
    TRADE,
    FREESPIRIT,
    CHIVALRY,
    ORGANIZATION,
    FARMING,
    CONSTRUCTION,
    STRATEGY,
    DIPLOMACY,
    CLIMBING,
    MEDITATION,
    PHILOSOPHY,
    MINING,
    SMITHERY,
    FISHING,
    RAMMING,
    AQUATISM,
    SAILING,
    NAVIGATION,
    HUNTING,
    FORESTRY,
    MATHEMATICS,
    ARCHERY,
    SPIRITUALISM,
    TECH_TYPE_SIZE
};
}

struct TechArgs {
    std::vector<TechTypes::TechTypes> techUnlocks;
    std::vector<ImprovementTypes::ImprovementType> improvementUnlocks;
    std::vector<UnitTypes::UnitType> unitUnlocks;
    std::vector<TerrainTypes::TerrainType> movementUnlocksVec;
    std::vector<TerrainTypes::TerrainType> defenceBonusesVec;
};

struct Tech {
    TechTypes::TechTypes type;
    int cost;
    std::bitset<TechTypes::TECH_TYPE_SIZE> techUnlocks;
    std::vector<ImprovementTypes::ImprovementType> improvementUnlocks;
    std::bitset<UnitTypes::UNIT_TYPE_SIZE> unitUnlocks;
    // abilityUnlocks
    // taskUnlocks
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> movementUnlocks;
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> defenceBonuses;
    Tech(TechTypes::TechTypes type, int cost, const TechArgs &args);
};

extern const std::array<Tech, TechTypes::TECH_TYPE_SIZE> techData;

struct TechDataAccessor {
    const Tech *operator()(std::size_t pos) const { return &techData.at(pos); }
};
