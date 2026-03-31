#include "data/improvementData.h"
#include "tech.h"
#include "tile.h"
#include "units.h"
#include "utils.h"

Tech::Tech(int cost, TechArgs args)
    : cost{cost}, techUnlocks{args.techUnlocks},
      improvementUnlocks{args.improvementUnlocks},
      unitUnlocks{args.unitUnlocks}, movementUnlocks{}, defenceBonuses{} {
    vectorToBitset(&movementUnlocks, args.movementUnlocksVec);
    vectorToBitset(&defenceBonuses, args.defenceBonusesVec);
}

namespace Techs {

const Tech basic =
    Tech(0, {.techUnlocks{riding, organization, climbing, fishing, hunting},
             .unitUnlocks{*Units::warrior, *Units::giant, *Units::dagger},
             .movementUnlocksVec{TerrainTypes::FIELD, TerrainTypes::FOREST}});

const Tech riding = Tech(
    1, {.techUnlocks{roads, Techs::freeSpirit}, .unitUnlocks{*Units::rider}});
const Tech roads =
    Tech(2, {.techUnlocks{Techs::trade},
             .improvementUnlocks{Improvements::road, Improvements::bridge}});
const Tech trade = Tech(3, {.improvementUnlocks{Improvements::market}});
const Tech freeSpirit = Tech(2, {.techUnlocks{Techs::chivalry},
                                 .improvementUnlocks{Improvements::temple}});
const Tech chivalry = Tech(3, {.improvementUnlocks{Improvements::destroy},
                               .unitUnlocks{*Units::knight}});

const Tech organization = Tech(1, {.techUnlocks{farming, strategy}});
const Tech farming = Tech(
    2, {.techUnlocks{construction}, .improvementUnlocks{Improvements::farm}});
const Tech construction = Tech(
    3, {.improvementUnlocks{Improvements::windmill, Improvements::burnForest}});
const Tech strategy =
    Tech(2, {.techUnlocks{diplomacy}, .unitUnlocks{*Units::defender}});
const Tech diplomacy = Tech(3, {.unitUnlocks{*Units::cloak}});

const Tech climbing = Tech(1, {.techUnlocks{meditation, mining},
                               .movementUnlocksVec{TerrainTypes::MOUNTAIN},
                               .defenceBonusesVec{TerrainTypes::MOUNTAIN}});
const Tech meditation =
    Tech(2, {.techUnlocks{philosophy},
             .improvementUnlocks{Improvements::mountainTemple}});
const Tech philosophy = Tech(3, {.unitUnlocks{*Units::mindBender}});
const Tech mining =
    Tech(2, {.techUnlocks{smithery}, .improvementUnlocks{Improvements::mine}});
const Tech smithery = Tech(3, {.improvementUnlocks{Improvements::forge},
                               .unitUnlocks{*Units::swordsman}});

const Tech fishing =
    Tech(1, {.techUnlocks{ramming, sailing},
             .improvementUnlocks{Improvements::fishing, Improvements::port},
             .unitUnlocks{*Units::raft},
             .movementUnlocksVec{TerrainTypes::WATER}});
const Tech ramming =
    Tech(2, {.techUnlocks{aquatism}, .unitUnlocks{*Units::rammer}});
const Tech aquatism =
    Tech(3, {.improvementUnlocks{Improvements::waterTemple},
             .defenceBonusesVec{TerrainTypes::WATER, TerrainTypes::OCEAN}});
const Tech sailing = Tech(2, {.techUnlocks{Techs::navigation},
                              .unitUnlocks{*Units::scout},
                              .movementUnlocksVec{TerrainTypes::OCEAN}});
const Tech navigation = Tech(3, {.improvementUnlocks{Improvements::starFishing},
                                 .unitUnlocks{Units::bomber}});

const Tech hunting = Tech(1, {.techUnlocks{forestry, archery},
                              .improvementUnlocks{Improvements::hunting}});
const Tech forestry = Tech(2, {.techUnlocks{mathematics},
                               .improvementUnlocks{Improvements::lumberHut}});
const Tech mathematics = Tech(3, {.improvementUnlocks{Improvements::sawmill},
                                  .unitUnlocks{*Units::catapult}});
const Tech archery = Tech(2, {.techUnlocks{spiritualism},
                              .unitUnlocks{*Units::archer},
                              .defenceBonusesVec{TerrainTypes::FOREST}});
const Tech spiritualism =
    Tech(3, {.improvementUnlocks{Improvements::growForest,
                                 Improvements::forestTemple}});
} // namespace Techs
