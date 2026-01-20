#ifndef TECH_H
#define TECH_H

#include "tile.h"
#include "units.h"
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
    std::vector<TerrainTypes::TerrainType> movementUnlocks;
    Tech(int cost, std::vector<Tech> techUnlocks,
         std::vector<Unit *(*)(Game * game, Player *player, Tile *tile)>
             unitUnlocks,
         std::vector<TerrainTypes::TerrainType> movementUnlocks);
    Tech();
};

namespace Techs {
const Tech basic =
    Tech(0, {}, {*Units::warrior}, {TerrainTypes::FIELD, TerrainTypes::FOREST});
}; // namespace Techs

#endif // !TECH_H
