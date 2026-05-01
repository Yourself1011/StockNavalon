#include "data/improvementData.h"
#include "tech.h"
#include "tile.h"
#include "utils.h"
Tech::Tech(TechTypes::TechTypes type, int cost, TechArgs args)
    : type{type}, cost{cost}, techUnlocks{},
      improvementUnlocks{args.improvementUnlocks}, unitUnlocks{},
      movementUnlocks{}, defenceBonuses{} {
    vectorToBitset(&movementUnlocks, args.movementUnlocksVec);
    vectorToBitset(&defenceBonuses, args.defenceBonusesVec);
    vectorToBitset(&techUnlocks, args.techUnlocks);
    vectorToBitset(&unitUnlocks, args.unitUnlocks);
}

const std::array<Tech, TechTypes::TECH_TYPE_SIZE> techData = {

    Tech(TechTypes::BASIC, 0,
         {.techUnlocks{TechTypes::RIDING, TechTypes::ORGANIZATION,
                       TechTypes::CLIMBING, TechTypes::FISHING,
                       TechTypes::HUNTING},
          .unitUnlocks{UnitTypes::WARRIOR, UnitTypes::GIANT, UnitTypes::DAGGER},
          .movementUnlocksVec{TerrainTypes::FIELD, TerrainTypes::FOREST}}),

    Tech(TechTypes::RIDING, 1,
         {.techUnlocks{TechTypes::ROADS, TechTypes::FREESPIRIT},
          .unitUnlocks{UnitTypes::RIDER}}),

    Tech(TechTypes::ROADS, 2,
         {.techUnlocks{TechTypes::TRADE},
          .improvementUnlocks{Improvements::road, Improvements::bridge}}),

    Tech(TechTypes::TRADE, 3, {.improvementUnlocks{Improvements::market}}),
    Tech(TechTypes::FREESPIRIT, 2,
         {.techUnlocks{TechTypes::CHIVALRY},
          .improvementUnlocks{Improvements::temple}}),
    Tech(TechTypes::CHIVALRY, 3,
         {.improvementUnlocks{Improvements::destroy},
          .unitUnlocks{UnitTypes::KNIGHT}}),

    Tech(TechTypes::ORGANIZATION, 1,
         {.techUnlocks{TechTypes::FARMING, TechTypes::STRATEGY}}),
    Tech(TechTypes::FARMING, 2,
         {.techUnlocks{TechTypes::CONSTRUCTION},
          .improvementUnlocks{Improvements::farm}}),

    Tech(TechTypes::CONSTRUCTION, 3,
         {.improvementUnlocks{Improvements::windmill,
                              Improvements::burnForest}}),

    Tech(TechTypes::STRATEGY, 2,
         {.techUnlocks{TechTypes::DIPLOMACY},
          .unitUnlocks{UnitTypes::DEFENDER}}),

    Tech(TechTypes::DIPLOMACY, 3, {.unitUnlocks{UnitTypes::CLOAK}}),

    Tech(TechTypes::CLIMBING, 1,
         {.techUnlocks{TechTypes::MEDITATION, TechTypes::MINING},
          .movementUnlocksVec{TerrainTypes::MOUNTAIN},
          .defenceBonusesVec{TerrainTypes::MOUNTAIN}}),

    Tech(TechTypes::MEDITATION, 2,
         {.techUnlocks{TechTypes::PHILOSOPHY},
          .improvementUnlocks{Improvements::mountainTemple}}),

    Tech(TechTypes::PHILOSOPHY, 3, {.unitUnlocks{UnitTypes::MINDBENDER}}),
    Tech(TechTypes::MINING, 2,
         {.techUnlocks{TechTypes::SMITHERY},
          .improvementUnlocks{Improvements::mine}}),
    Tech(TechTypes::SMITHERY, 3,
         {.improvementUnlocks{Improvements::forge},
          .unitUnlocks{UnitTypes::SWORDSMAN}}),

    Tech(TechTypes::FISHING, 1,
         {.techUnlocks{TechTypes::RAMMING, TechTypes::SAILING},
          .improvementUnlocks{Improvements::fishing, Improvements::port},
          .unitUnlocks{UnitTypes::RAFT},
          .movementUnlocksVec{TerrainTypes::WATER}}),

    Tech(TechTypes::RAMMING, 2,
         {.techUnlocks{TechTypes::AQUATISM}, .unitUnlocks{UnitTypes::RAMMER}}),

    Tech(TechTypes::AQUATISM, 3,
         {.improvementUnlocks{Improvements::waterTemple},
          .defenceBonusesVec{TerrainTypes::WATER, TerrainTypes::OCEAN}}),
    Tech(TechTypes::SAILING, 2,
         {.techUnlocks{TechTypes::NAVIGATION},
          .unitUnlocks{UnitTypes::SCOUT},
          .movementUnlocksVec{TerrainTypes::OCEAN}}),

    Tech(TechTypes::NAVIGATION, 3,
         {.improvementUnlocks{Improvements::starFishing},
          .unitUnlocks{UnitTypes::BOMBER}}),

    Tech(TechTypes::HUNTING, 1,
         {.techUnlocks{TechTypes::FORESTRY, TechTypes::ARCHERY},
          .improvementUnlocks{Improvements::hunting}}),
    Tech(TechTypes::FORESTRY, 2,
         {.techUnlocks{TechTypes::MATHEMATICS},
          .improvementUnlocks{Improvements::lumberHut}}),
    Tech(TechTypes::MATHEMATICS, 3,
         {.improvementUnlocks{Improvements::sawmill},
          .unitUnlocks{UnitTypes::CATAPULT}}),
    Tech(TechTypes::ARCHERY, 2,
         {.techUnlocks{TechTypes::SPIRITUALISM},
          .unitUnlocks{UnitTypes::ARCHER},
          .defenceBonusesVec{TerrainTypes::FOREST}}),
    Tech(TechTypes::SPIRITUALISM, 3,
         {.improvementUnlocks{Improvements::growForest,
                              Improvements::forestTemple}}),
};
