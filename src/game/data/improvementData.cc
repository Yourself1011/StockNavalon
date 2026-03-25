#include "improvementData.h"
#include "player.h"
#include "tile.h"
#include "utils.h"

namespace Improvements {

const ImprovementData city = {
    .type = ImprovementTypes::CITY, .isBuilding = true, .cost = 0};
const ImprovementData ruin = {
    .type = ImprovementTypes::RUIN, .isBuilding = true, .cost = 0};
const ImprovementData road = {
    .type = ImprovementTypes::ROAD,
    .isBuilding = true,
    .global = true,
    .cost = 3,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::FOREST})};
const ImprovementData farm = {
    .type = ImprovementTypes::FARM,
    .isBuilding = true,
    .cost = 5,
    .population = 2,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::FIELD}),
    .resourceRequirement = ResourceTypes::CROP};
const ImprovementData fishing = {
    .type = ImprovementTypes::FISHING,
    .isBuilding = false,
    .cost = 2,
    .population = 1,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::WATER, TerrainTypes::OCEAN}),
    .resourceRequirement = ResourceTypes::FISH,
    .createEffect = [](Tile &tile) { tile.resource = ResourceTypes::NONE; }};
const ImprovementData port = {
    .type = ImprovementTypes::PORT,
    .isBuilding = true,
    .cost = 7,
    .population = 1,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::WATER})};
const ImprovementData hunting = {
    .type = ImprovementTypes::HUNTING,
    .isBuilding = false,
    .cost = 2,
    .population = 1,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::FOREST}),
    .resourceRequirement = ResourceTypes::GAME,
    .createEffect = [](Tile &tile) { tile.resource = ResourceTypes::NONE; }};
const ImprovementData clearForest = {
    .type = ImprovementTypes::CLEARFOREST,
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
    .isBuilding = true,
    .cost = 3,
    .population = 1,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FOREST})};
const ImprovementData sawMill = {
    .type = ImprovementTypes::SAWMILL,
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
    .cost = 5,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::FIELD}),
    .createEffect = [](Tile &tile) {
        tile.resource = ResourceTypes::NONE;
        tile.terrainType = TerrainTypes::FOREST;
    }};
const ImprovementData harvestFruit = {
    .type = ImprovementTypes::HARVESTFRUIT,
    .cost = 2,
    .population = 1,
    .resourceRequirement = ResourceTypes::FRUIT,
    .createEffect = [](Tile &tile) { tile.resource = ResourceTypes::NONE; }};
const ImprovementData temple = {
    .type = ImprovementTypes::TEMPLE,
    .cost = 20,
    .population = 1,
    .maxLevel = 3,
    .growthRate = 3,
    .growthScore = 100,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::FIELD})};
const ImprovementData forestTemple = {
    .type = ImprovementTypes::FORESTTEMPLE,
    .cost = 20,
    .population = 1,
    .maxLevel = 3,
    .growthRate = 3,
    .growthScore = 100,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FOREST})};
const ImprovementData waterTemple = {
    .type = ImprovementTypes::WATERTEMPLE,
    .cost = 20,
    .population = 1,
    .maxLevel = 3,
    .growthRate = 3,
    .growthScore = 100,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::WATER})};
const ImprovementData mountainTemple = {
    .type = ImprovementTypes::MOUNTAINTEMPLE,
    .cost = 20,
    .population = 1,
    .maxLevel = 3,
    .growthRate = 3,
    .growthScore = 100,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::MOUNTAIN})};
const ImprovementData mine = {
    .type = ImprovementTypes::MINE,
    .isBuilding = true,
    .cost = 5,
    .population = 2,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::MOUNTAIN}),
    .resourceRequirement = ResourceTypes::METAL};
const ImprovementData forge = {
    .type = ImprovementTypes::FORGE,
    .isBuilding = true,
    .cost = 5,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::MOUNTAIN}),
    .adjacencyRequirements =
        vectorToBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
            {ImprovementTypes::MINE}),
    .adjacencyImprovements =
        vectorToBitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>(
            {ImprovementTypes::MINE})};
const ImprovementData alterOfPeace = {
    .type = ImprovementTypes::ALTEROFPEACE,
    .isBuilding = true,
    .unique = true,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::WATER})};
const ImprovementData emperorsTomb = {
    .type = ImprovementTypes::EMPERORSTOMB,
    .isBuilding = true,
    .unique = true,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::WATER})};
const ImprovementData eyeOfGod = {
    .type = ImprovementTypes::EYEOFGOD,
    .isBuilding = true,
    .unique = true,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::WATER})};
const ImprovementData gateOfPower = {
    .type = ImprovementTypes::EYEOFGOD,
    .isBuilding = true,
    .unique = true,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::WATER})};
const ImprovementData grandBazaar = {
    .type = ImprovementTypes::EYEOFGOD,
    .isBuilding = true,
    .unique = true,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::WATER})};
const ImprovementData towerOfWisdom = {
    .type = ImprovementTypes::TOWEROFWISDOM,
    .isBuilding = true,
    .unique = true,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::WATER})};
const ImprovementData lighthouse = {
    .type = ImprovementTypes::LIGHTHOUSE,
    .isBuilding = true,
    .terrainRequirements = vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>(
        {TerrainTypes::FIELD, TerrainTypes::WATER})};
const ImprovementData bridge = {
    .type = ImprovementTypes::BRIDGE,
    .isBuilding = true,
    .global = true,
    .cost = 5,
    .terrainRequirements =
        vectorToBitset<TerrainTypes::TERRAIN_TYPE_SIZE>({TerrainTypes::WATER})};
} // namespace Improvements
