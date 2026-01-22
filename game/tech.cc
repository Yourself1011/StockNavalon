#include "tech.h"
#include "tile.h"
#include "units.h"
#include "utils.h"

Tech::Tech(
    int cost, std::vector<Tech> techUnlocks,
    std::vector<Unit *(*)(Game * game, Player *player, Tile *tile)> unitUnlocks,
    std::vector<TerrainTypes::TerrainType> movementUnlocksVec)
    : cost{cost}, techUnlocks{techUnlocks}, unitUnlocks{unitUnlocks},
      movementUnlocks{} {
    vectorToBitset(&movementUnlocks, movementUnlocksVec);
}

const Tech Techs::basic =
    Tech(0, {}, {*Units::warrior}, {TerrainTypes::FIELD, TerrainTypes::FOREST});
