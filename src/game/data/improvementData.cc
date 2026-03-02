#include "improvementData.h"
#include "player.h"
#include "tile.h"
#include "utils.h"

namespace Improvements {

const ImprovementData city = {.type = ImprovementTypes::CITY,
                              .hidden = true,
                              .isBuilding = true,
                              .cost = 0};
const ImprovementData ruin = {.type = ImprovementTypes::RUIN,
                              .hidden = true,
                              .isBuilding = true,
                              .cost = 0};
const ImprovementData road = {
    .type = ImprovementTypes::ROAD,
    .hidden = false,
    .isBuilding = true,
    .global = true,
    .cost = 3,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::FOREST})};
const ImprovementData farm = {
    .type = ImprovementTypes::FARM,
    .hidden = false,
    .isBuilding = true,
    .cost = 5,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::FIELD}),
    .resourceRequirement = ResourceTypes::CROP,
    .createEffect = [](Tile &tile) { tile.resource = ResourceTypes::NONE; }};
const ImprovementData fishing = {
    .type = ImprovementTypes::FISHING,
    .hidden = false,
    .isBuilding = false,
    .cost = 2,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::WATER, TerrainTypes::OCEAN}),
    .resourceRequirement = ResourceTypes::FISH,
    .createEffect = [](Tile &tile) { tile.resource = ResourceTypes::NONE; }};
const ImprovementData port = {
    .type = ImprovementTypes::PORT,
    .hidden = false,
    .isBuilding = true,
    .cost = 7,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::WATER})};
const ImprovementData hunting = {
    .type = ImprovementTypes::HUNTING,
    .hidden = false,
    .isBuilding = false,
    .cost = 2,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::FOREST}),
    .resourceRequirement = ResourceTypes::GAME,
    .createEffect = [](Tile &tile) { tile.resource = ResourceTypes::NONE; }};
const ImprovementData clearForest = {
    .type = ImprovementTypes::CLEARFOREST,
    .hidden = false,
    .isBuilding = false,
    .cost = 0,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::FOREST}),
    .createEffect = [](Tile &tile) {
        tile.resource = ResourceTypes::NONE;
        tile.terrainType = TerrainTypes::FIELD;
        tile.owner->stars += 1;
    }};
const ImprovementData burnForest = {
    .type = ImprovementTypes::BURNFOREST,
    .hidden = false,
    .isBuilding = false,
    .cost = 5,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::FOREST}),
    .createEffect = [](Tile &tile) {
        tile.resource = ResourceTypes::CROP;
        tile.terrainType = TerrainTypes::FIELD;
    }};
const ImprovementData lumberHut = {
    .type = ImprovementTypes::LUMBERHUT,
    .hidden = false,
    .isBuilding = true,
    .cost = 3,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FOREST})};
const ImprovementData sawMill = {
    .type = ImprovementTypes::SAWMILL,
    .hidden = false,
    .isBuilding = true,
    .cost = 5,
    .adjacencyRequirements =
        vectorToBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
            {ImprovementTypes::LUMBERHUT}),
    .adjacencyImprovements =
        vectorToBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
            {ImprovementTypes::LUMBERHUT})};
const ImprovementData growForest = {
    .type = ImprovementTypes::GROWFOREST,

};
} // namespace Improvements
