#include "tech.h"
#include "utils.h"

Tech::Tech(TechTypes::TechTypes type, int cost, const TechArgs &args)
    : type{type}, cost{cost}, techUnlocks{},
      improvementUnlocks{args.improvementUnlocks}, unitUnlocks{},
      movementUnlocks{}, defenceBonuses{} {
    setBits(movementUnlocks, args.movementUnlocksVec);
    setBits(defenceBonuses, args.defenceBonusesVec);
    setBits(techUnlocks, args.techUnlocks);
    setBits(unitUnlocks, args.unitUnlocks);
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
          .improvementUnlocks{ImprovementTypes::ROAD,
                              ImprovementTypes::BRIDGE}}),

    Tech(TechTypes::TRADE, 3, {.improvementUnlocks{ImprovementTypes::MARKET}}),
    Tech(TechTypes::FREESPIRIT, 2,
         {.techUnlocks{TechTypes::CHIVALRY},
          .improvementUnlocks{ImprovementTypes::TEMPLE}}),
    Tech(TechTypes::CHIVALRY, 3,
         {.improvementUnlocks{ImprovementTypes::DESTROY},
          .unitUnlocks{UnitTypes::KNIGHT}}),

    Tech(TechTypes::ORGANIZATION, 1,
         {.techUnlocks{TechTypes::FARMING, TechTypes::STRATEGY}}),
    Tech(TechTypes::FARMING, 2,
         {.techUnlocks{TechTypes::CONSTRUCTION},
          .improvementUnlocks{ImprovementTypes::FARM}}),

    Tech(TechTypes::CONSTRUCTION, 3,
         {.improvementUnlocks{ImprovementTypes::WINDMILL,
                              ImprovementTypes::BURNFOREST}}),

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
          .improvementUnlocks{ImprovementTypes::MOUNTAINTEMPLE}}),

    Tech(TechTypes::PHILOSOPHY, 3, {.unitUnlocks{UnitTypes::MINDBENDER}}),
    Tech(TechTypes::MINING, 2,
         {.techUnlocks{TechTypes::SMITHERY},
          .improvementUnlocks{ImprovementTypes::MINE}}),
    Tech(TechTypes::SMITHERY, 3,
         {.improvementUnlocks{ImprovementTypes::FORGE},
          .unitUnlocks{UnitTypes::SWORDSMAN}}),

    Tech(
        TechTypes::FISHING, 1,
        {.techUnlocks{TechTypes::RAMMING, TechTypes::SAILING},
         .improvementUnlocks{ImprovementTypes::FISHING, ImprovementTypes::PORT},
         .unitUnlocks{UnitTypes::RAFT},
         .movementUnlocksVec{TerrainTypes::WATER}}),

    Tech(TechTypes::RAMMING, 2,
         {.techUnlocks{TechTypes::AQUATISM}, .unitUnlocks{UnitTypes::RAMMER}}),

    Tech(TechTypes::AQUATISM, 3,
         {.improvementUnlocks{ImprovementTypes::WATERTEMPLE},
          .defenceBonusesVec{TerrainTypes::WATER, TerrainTypes::OCEAN}}),
    Tech(TechTypes::SAILING, 2,
         {.techUnlocks{TechTypes::NAVIGATION},
          .unitUnlocks{UnitTypes::SCOUT},
          .movementUnlocksVec{TerrainTypes::OCEAN}}),

    Tech(TechTypes::NAVIGATION, 3,
         {.improvementUnlocks{ImprovementTypes::STARFISHING},
          .unitUnlocks{UnitTypes::BOMBER}}),

    Tech(TechTypes::HUNTING, 1,
         {.techUnlocks{TechTypes::FORESTRY, TechTypes::ARCHERY},
          .improvementUnlocks{ImprovementTypes::HUNTING}}),
    Tech(TechTypes::FORESTRY, 2,
         {.techUnlocks{TechTypes::MATHEMATICS},
          .improvementUnlocks{ImprovementTypes::LUMBERHUT}}),
    Tech(TechTypes::MATHEMATICS, 3,
         {.improvementUnlocks{ImprovementTypes::SAWMILL},
          .unitUnlocks{UnitTypes::CATAPULT}}),
    Tech(TechTypes::ARCHERY, 2,
         {.techUnlocks{TechTypes::SPIRITUALISM},
          .unitUnlocks{UnitTypes::ARCHER},
          .defenceBonusesVec{TerrainTypes::FOREST}}),
    Tech(TechTypes::SPIRITUALISM, 3,
         {.improvementUnlocks{ImprovementTypes::GROWFOREST,
                              ImprovementTypes::FORESTTEMPLE}}),
};
