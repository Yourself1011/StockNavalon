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
    Tech(int cost, std::vector<Tech> techUnlocks,
         std::vector<Unit *(*)(Game * game, Player *player, Tile *tile)>
             unitUnlocks,
         std::vector<TerrainTypes::TerrainType> movementUnlocksVec);
    Tech();
};

namespace Techs {
extern const Tech basic;
}; // namespace Techs

#endif // !TECH_H
