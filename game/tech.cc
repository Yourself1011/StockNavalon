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
    Tech(0,
         {Techs::riding, Techs::organization, Techs::climbing, Techs::fishing,
          Techs::hunting},
         {*Units::warrior, *Units::giant, *Units::dagger},
         {TerrainTypes::FIELD, TerrainTypes::FOREST});

const Tech Techs::riding =
    Tech(1, {Techs::roads, Techs::freeSpirit}, {*Units::rider}, {});
const Tech Techs::roads = Tech(2, {Techs::trade}, {}, {});
const Tech Techs::trade = Tech(3, {}, {}, {});
const Tech Techs::freeSpirit = Tech(2, {Techs::chivlary}, {}, {});
const Tech Techs::chivlary = Tech(3, {}, {*Units::knight}, {});

const Tech Techs::organization =
    Tech(1, {Techs::farming, Techs::strategy}, {}, {});
const Tech Techs::farming = Tech(2, {Techs::construction}, {}, {});
const Tech Techs::construction = Tech(3, {}, {}, {});
const Tech Techs::strategy =
    Tech(2, {Techs::diplomacy}, {*Units::defender}, {});
const Tech Techs::diplomacy = Tech(3, {}, {*Units::cloak}, {});

const Tech Techs::climbing =
    Tech(1, {Techs::meditation, Techs::mining}, {}, {TerrainTypes::MOUNTAIN});
const Tech Techs::meditation = Tech(2, {Techs::philosophy}, {}, {});
const Tech Techs::philosophy = Tech(3, {}, {*Units::mindBender}, {});
const Tech Techs::mining = Tech(2, {Techs::smithery}, {}, {});
const Tech Techs::smithery = Tech(3, {}, {*Units::swordsman}, {});

const Tech Techs::fishing = Tech(1, {Techs::ramming, Techs::sailing},
                                 {*Units::raft}, {TerrainTypes::WATER});
const Tech Techs::ramming = Tech(2, {Techs::aquatism}, {*Units::rammer}, {});
const Tech Techs::aquatism = Tech(3, {}, {}, {});
const Tech Techs::sailing =
    Tech(2, {Techs::navagation}, {*Units::scout}, {TerrainTypes::OCEAN});
const Tech Techs::navagation = Tech(3, {}, {Units::bomber}, {});

const Tech Techs::hunting = Tech(1, {Techs::forestry, Techs::archery}, {}, {});
const Tech Techs::forestry = Tech(2, {Techs::mathematics}, {}, {});
const Tech Techs::mathematics = Tech(3, {}, {*Units::catapult}, {});
const Tech Techs::archery =
    Tech(2, {Techs::spiritualism}, {*Units::archer}, {});
const Tech Techs::spiritualism = Tech(3, {}, {}, {});
