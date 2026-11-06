#include "data/improvementData.h"
#include "improvement.h"
#include "player.h"
#include "tile.h"
#include "utils.h"

const std::unordered_map<ImprovementTypes::ImprovementType, ImprovementData>
    ImprovementMap = {
        {ImprovementTypes::CITY,
         {.type = ImprovementTypes::CITY,
          .displayName = "ci",
          .isBuilding = true,
          .cost = 0}},

        {ImprovementTypes::RUIN,
         {.type = ImprovementTypes::RUIN,
          .displayName = "ru",
          .isBuilding = true,
          .cost = 0}},

        {ImprovementTypes::ROAD,
         {.type = ImprovementTypes::ROAD,
          .displayName = "ro",
          .global = true,
          .cost = 3,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::FOREST})}},

        {ImprovementTypes::FARM,
         {.type = ImprovementTypes::FARM,
          .displayName = "fa",
          .isBuilding = true,
          .cost = 5,
          .population = 2,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD}),
          .resourceRequirement = ResourceTypes::CROP}},

        {ImprovementTypes::WINDMILL,
         {
             .type = ImprovementTypes::WINDMILL,
             .displayName = "wm",
             .isBuilding = true,
             .uniqueToCity = true,
             .cost = 5,
             .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
                 {TerrainTypes::FIELD}),
             .adjacencyRequirements =
                 makeBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
                     {ImprovementTypes::FARM}),
             .adjacencyImprovements =
                 makeBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
                     {ImprovementTypes::FARM}),
         }},

        {ImprovementTypes::FISHING,
         {.type = ImprovementTypes::FISHING,
          .isBuilding = false,
          .cost = 2,
          .population = 1,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::WATER, TerrainTypes::OCEAN}),
          .resourceRequirement = ResourceTypes::FISH,
          .createEffect =
              [](Tile &tile, Player &) {
                  tile.resource = ResourceTypes::NONE;
              }}},

        {ImprovementTypes::PORT,
         {.type = ImprovementTypes::PORT,
          .displayName = "po",
          .isBuilding = true,
          .cost = 7,
          .population = 1,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::WATER})}},

        {ImprovementTypes::HUNTING,
         {.type = ImprovementTypes::HUNTING,
          .isBuilding = false,
          .cost = 2,
          .population = 1,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FOREST}),
          .resourceRequirement = ResourceTypes::GAME,
          .createEffect =
              [](Tile &tile, Player &) {
                  tile.resource = ResourceTypes::NONE;
              }}},

        {ImprovementTypes::CLEARFOREST,
         {.type = ImprovementTypes::CLEARFOREST,
          .isBuilding = false,
          .cost = 0,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FOREST}),
          .createEffect =
              [](Tile &tile, Player &player) {
                  tile.resource = ResourceTypes::NONE;
                  tile.terrainType = TerrainTypes::FIELD;
                  player.stars += 1;
              }}},

        {ImprovementTypes::BURNFOREST,
         {.type = ImprovementTypes::BURNFOREST,
          .isBuilding = false,
          .cost = 5,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FOREST}),
          .createEffect =
              [](Tile &tile, Player &) {
                  tile.resource = ResourceTypes::CROP;
                  tile.terrainType = TerrainTypes::FIELD;
              }}},

        {ImprovementTypes::LUMBERHUT,
         {.type = ImprovementTypes::LUMBERHUT,
          .displayName = "lh",
          .isBuilding = true,
          .cost = 3,
          .population = 1,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FOREST})}},

        {ImprovementTypes::SAWMILL,
         {.type = ImprovementTypes::SAWMILL,
          .displayName = "sm",
          .isBuilding = true,
          .uniqueToCity = true,
          .cost = 5,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::FOREST}),
          .adjacencyRequirements =
              makeBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
                  {ImprovementTypes::LUMBERHUT}),
          .adjacencyImprovements =
              makeBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
                  {ImprovementTypes::LUMBERHUT})}},

        {ImprovementTypes::GROWFOREST,
         {.type = ImprovementTypes::GROWFOREST,
          .cost = 5,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD}),
          .createEffect =
              [](Tile &tile, Player &) {
                  tile.resource = ResourceTypes::NONE;
                  tile.terrainType = TerrainTypes::FOREST;
              }}},

        {ImprovementTypes::HARVESTFRUIT,
         {.type = ImprovementTypes::HARVESTFRUIT,
          .cost = 2,
          .population = 1,
          .resourceRequirement = ResourceTypes::FRUIT,
          .createEffect =
              [](Tile &tile, Player &) {
                  tile.resource = ResourceTypes::NONE;
              }}},

        {ImprovementTypes::TEMPLE,
         {.type = ImprovementTypes::TEMPLE,
          .displayName = "tm",
          .isBuilding = true,
          .cost = 20,
          .population = 1,
          .maxLevel = 3,
          .growthRate = 3,
          .growthScore = 100,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD})}},

        {ImprovementTypes::FORESTTEMPLE,
         {.type = ImprovementTypes::FORESTTEMPLE,
          .displayName = "ft",
          .isBuilding = true,
          .cost = 20,
          .population = 1,
          .maxLevel = 3,
          .growthRate = 3,
          .growthScore = 100,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FOREST})}},

        {ImprovementTypes::WATERTEMPLE,
         {.type = ImprovementTypes::WATERTEMPLE,
          .displayName = "wt",
          .isBuilding = true,
          .cost = 20,
          .population = 1,
          .maxLevel = 3,
          .growthRate = 3,
          .growthScore = 100,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::WATER})}},

        {ImprovementTypes::MOUNTAINTEMPLE,
         {.type = ImprovementTypes::MOUNTAINTEMPLE,
          .displayName = "mt",
          .isBuilding = true,
          .cost = 20,
          .population = 1,
          .maxLevel = 3,
          .growthRate = 3,
          .growthScore = 100,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::MOUNTAIN})}},

        {ImprovementTypes::MINE,
         {.type = ImprovementTypes::MINE,
          .displayName = "mi",
          .isBuilding = true,
          .cost = 5,
          .population = 2,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::MOUNTAIN}),
          .resourceRequirement = ResourceTypes::METAL}},

        {ImprovementTypes::FORGE,
         {.type = ImprovementTypes::FORGE,
          .displayName = "fo",
          .isBuilding = true,
          .uniqueToCity = true,
          .cost = 5,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::MOUNTAIN}),
          .adjacencyRequirements =
              makeBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
                  {ImprovementTypes::MINE}),
          .adjacencyImprovements =
              makeBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
                  {ImprovementTypes::MINE})}},

        {ImprovementTypes::ALTEROFPEACE,
         {.type = ImprovementTypes::ALTEROFPEACE,
          .displayName = "ap",
          .isBuilding = true,
          .unique = true,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::WATER})}},

        {ImprovementTypes::EMPERORSTOMB,
         {.type = ImprovementTypes::EMPERORSTOMB,
          .displayName = "et",
          .isBuilding = true,
          .unique = true,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::WATER})}},

        {ImprovementTypes::EYEOFGOD,
         {.type = ImprovementTypes::EYEOFGOD,
          .displayName = "eg",
          .isBuilding = true,
          .unique = true,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::WATER})}},

        {ImprovementTypes::GATEOFPOWER,
         {.type = ImprovementTypes::GATEOFPOWER,
          .displayName = "gp",
          .isBuilding = true,
          .unique = true,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::WATER})}},

        {ImprovementTypes::GRANDBAZAAR,
         {.type = ImprovementTypes::GRANDBAZAAR,
          .displayName = "gb",
          .isBuilding = true,
          .unique = true,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::WATER})}},

        {ImprovementTypes::TOWEROFWISDOM,
         {.type = ImprovementTypes::TOWEROFWISDOM,
          .displayName = "tw",
          .isBuilding = true,
          .unique = true,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::WATER})}},

        // TODO: make this work
        {ImprovementTypes::STARFISHING,
         {.type = ImprovementTypes::STARFISHING,
          .resourceRequirement = ResourceTypes::STARFISH,
          .createEffect =
              [](Tile &tile, Player &) {
                  tile.resource = ResourceTypes::NONE;
              }}},

        {ImprovementTypes::LIGHTHOUSE,
         {.type = ImprovementTypes::LIGHTHOUSE,
          .displayName = "li",
          .isBuilding = true,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD, TerrainTypes::WATER})}},

        {ImprovementTypes::BRIDGE,
         {.type = ImprovementTypes::BRIDGE,
          .displayName = "br",
          .isBuilding = true,
          .global = true,
          .cost = 5,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::WATER})}},

        {ImprovementTypes::MARKET,
         {.type = ImprovementTypes::MARKET,
          .displayName = "ma",
          .isBuilding = true,
          .uniqueToCity = true,
          .cost = 5,
          .terrainRequirements = makeBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
              {TerrainTypes::FIELD})}},

        {ImprovementTypes::DESTROY,
         {.type = ImprovementTypes::DESTROY,
          .createEffect = [](Tile &tile,
                             Player &) { tile.improvement.reset(); }}},
};
