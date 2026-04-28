#ifndef TECH_H
#define TECH_H

#include "data/improvementData.h"
#include "improvement.h"
#include "tile.h"
#include "units.h"
#include "utils.h"
#include <bitset>
#include <string>
#include <vector>

class Unit;
class Player;
class Tech;

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
    std::vector<ImprovementData> improvementUnlocks;
    std::vector<Unit *(*)(Game * game, Player *player, Tile *tile)> unitUnlocks;
    std::vector<TerrainTypes::TerrainType> movementUnlocksVec;
    std::vector<TerrainTypes::TerrainType> defenceBonusesVec;
};

struct Tech {
    int cost;
    TechTypes::TechTypes type;
    std::bitset<TechTypes::TECH_TYPE_SIZE> techUnlocks;
    std::vector<ImprovementData> improvementUnlocks;
    // a vector of pointers to unit factory functions
    std::vector<Unit *(*)(Game * game, Player *player, Tile *tile)> unitUnlocks;
    // abilityUnlocks
    // taskUnlocks
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> movementUnlocks;
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> defenceBonuses;
    Tech(TechTypes::TechTypes type, int cost, TechArgs args);
    Tech();
};

extern const std::array<Tech, TechTypes::TECH_TYPE_SIZE> techData;

struct TechDataAccessor {
    const Tech *operator()(int pos) const { return &techData.at(pos); }
};

#endif // !TECH_H
