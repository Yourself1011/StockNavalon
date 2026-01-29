#ifndef TECH_H
#define TECH_H

#include "tile.h"
#include "units.h"
#include "utils.h"
#include <bitset>
#include <string>
#include <vector>

class Unit;
class Player;
class Tech;

struct TechArgs {
    std::vector<Tech> techUnlocks;
    std::vector<Unit *(*)(Game * game, Player *player, Tile *tile)> unitUnlocks;
    std::vector<TerrainTypes::TerrainType> movementUnlocksVec;
    std::vector<TerrainTypes::TerrainType> defenceBonusesVec;
};

class Tech {
  public:
    int cost;
    std::vector<Tech> techUnlocks;
    // improvementUnlocks
    // a vector of pointers to unit factory functions
    std::vector<Unit *(*)(Game * game, Player *player, Tile *tile)> unitUnlocks;
    // abilityUnlocks
    // taskUnlocks
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> movementUnlocks;
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> defenceBonuses;
    Tech(int cost, TechArgs args);
    Tech();
};

namespace Techs {
extern const Tech basic;

extern const Tech riding;
extern const Tech roads;
extern const Tech trade;
extern const Tech freeSpirit;
extern const Tech chivlary;

extern const Tech organization;
extern const Tech farming;
extern const Tech construction;
extern const Tech strategy;
extern const Tech diplomacy;

extern const Tech climbing;
extern const Tech meditation;
extern const Tech philosophy;
extern const Tech mining;
extern const Tech smithery;

extern const Tech fishing;
extern const Tech ramming;
extern const Tech aquatism;
extern const Tech sailing;
extern const Tech navagation;

extern const Tech hunting;
extern const Tech forestry;
extern const Tech mathematics;
extern const Tech archery;
extern const Tech spiritualism;

}; // namespace Techs

#endif // !TECH_H
