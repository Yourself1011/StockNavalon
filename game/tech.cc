#include "tech.h"
#include "tile.h"
#include "units.h"

Tech::Tech(
    int cost, std::vector<Tech> techUnlocks,
    std::vector<Unit *(*)(Game * game, Player *player, Tile *tile)> unitUnlocks,
    std::vector<TerrainTypes::TerrainType> movementUnlocks)
    : cost{cost}, techUnlocks{techUnlocks}, unitUnlocks{unitUnlocks},
      movementUnlocks{movementUnlocks} {}
