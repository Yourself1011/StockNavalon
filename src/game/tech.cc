#include "tech.h"
#include "tile.h"
#include "units.h"
#include "utils.h"

Tech::Tech(int cost, TechArgs args)
    : cost{cost}, techUnlocks{args.techUnlocks}, unitUnlocks{args.unitUnlocks},
      movementUnlocks{}, defenceBonuses{} {
    vectorToBitset(&movementUnlocks, args.movementUnlocksVec);
    vectorToBitset(&defenceBonuses, args.defenceBonusesVec);
}

const Tech Techs::basic =
    Tech(0, {.techUnlocks{Techs::riding, Techs::organization, Techs::climbing,
                          Techs::fishing, Techs::hunting},
             .unitUnlocks{*Units::warrior, *Units::giant, *Units::dagger},
             .movementUnlocksVec{TerrainTypes::FIELD, TerrainTypes::FOREST}});

const Tech Techs::riding =
    Tech(1, {.techUnlocks{Techs::roads, Techs::freeSpirit},
             .unitUnlocks{*Units::rider}});
const Tech Techs::roads = Tech(2, {.techUnlocks{Techs::trade}});
const Tech Techs::trade = Tech(3, {});
const Tech Techs::freeSpirit = Tech(2, {.techUnlocks{Techs::chivalry}});
const Tech Techs::chivalry = Tech(3, {.unitUnlocks{*Units::knight}});

const Tech Techs::organization =
    Tech(1, {.techUnlocks{Techs::farming, Techs::strategy}});
const Tech Techs::farming = Tech(2, {.techUnlocks{Techs::construction}});
const Tech Techs::construction = Tech(3, {});
const Tech Techs::strategy =
    Tech(2, {.techUnlocks{Techs::diplomacy}, .unitUnlocks{*Units::defender}});
const Tech Techs::diplomacy = Tech(3, {.unitUnlocks{*Units::cloak}});

const Tech Techs::climbing =
    Tech(1, {.techUnlocks{Techs::meditation, Techs::mining},
             .movementUnlocksVec{TerrainTypes::MOUNTAIN},
             .defenceBonusesVec{TerrainTypes::MOUNTAIN}});
const Tech Techs::meditation = Tech(2, {.techUnlocks{Techs::philosophy}});
const Tech Techs::philosophy = Tech(3, {.unitUnlocks{*Units::mindBender}});
const Tech Techs::mining = Tech(2, {.techUnlocks{Techs::smithery}});
const Tech Techs::smithery = Tech(3, {.unitUnlocks{*Units::swordsman}});

const Tech Techs::fishing =
    Tech(1, {.techUnlocks{Techs::ramming, Techs::sailing},
             .unitUnlocks{*Units::raft},
             .movementUnlocksVec{TerrainTypes::WATER}});
const Tech Techs::ramming =
    Tech(2, {.techUnlocks{Techs::aquatism}, .unitUnlocks{*Units::rammer}});
const Tech Techs::aquatism =
    Tech(3, {.defenceBonusesVec{TerrainTypes::WATER, TerrainTypes::OCEAN}});
const Tech Techs::sailing = Tech(2, {.techUnlocks{Techs::navigation},
                                     .unitUnlocks{*Units::scout},
                                     .movementUnlocksVec{TerrainTypes::OCEAN}});
const Tech Techs::navigation = Tech(3, {.unitUnlocks{Units::bomber}});

const Tech Techs::hunting =
    Tech(1, {.techUnlocks{Techs::forestry, Techs::archery}});
const Tech Techs::forestry = Tech(2, {.techUnlocks{Techs::mathematics}});
const Tech Techs::mathematics = Tech(3, {.unitUnlocks{*Units::catapult}});
const Tech Techs::archery = Tech(2, {.techUnlocks{Techs::spiritualism},
                                     .unitUnlocks{*Units::archer},
                                     .defenceBonusesVec{TerrainTypes::FOREST}});
const Tech Techs::spiritualism = Tech(3, {});
