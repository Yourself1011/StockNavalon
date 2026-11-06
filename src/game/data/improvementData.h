#pragma once

#include "tile.h"
#include <bitset>
#include <string>
#include <unordered_map>

class Player;

namespace ImprovementTypes {
enum ImprovementType {
    NONE,
    CITY,
    RUIN,
    ROAD,
    CUSTOMSHOUSE,
    FARM,
    WINDMILL,
    FISHING,
    PORT,
    HUNTING,
    CLEARFOREST,
    BURNFOREST,
    LUMBERHUT,
    SAWMILL,
    GROWFOREST,
    HARVESTFRUIT,
    WHALEHUNTING,
    TEMPLE,
    FORESTTEMPLE,
    WATERTEMPLE,
    MOUNTAINTEMPLE,
    MINE,
    FORGE,
    ALTEROFPEACE,
    EMPERORSTOMB,
    EYEOFGOD,
    GATEOFPOWER,
    GRANDBAZAAR,
    PARKOFFORTUNE,
    TOWEROFWISDOM,
    ENCHANTANIMAL,
    ENCHANTWHALE,
    SANCTUARY,
    OUTPOST,
    ICEBANK,
    ICETEMPLE,
    POLARISCLIMATE,
    FUNGI,
    ALGAE,
    MYCELIUM,
    BURNSPORES,
    CLATHRUS,
    HIDDENSANCTUARY,
    HARVESTSPORES,
    NULLBUILDING,
    CULTIVATE,
    STARFISHING,
    LIGHTHOUSE,
    BRIDGE,
    AQUAFARM,
    MARKET,
    ATOLL,
    CANAL,
    FERTILIZE,
    LANDFILL,
    JETTY,
    DESTROY,
    IMPROVEMENT_TYPE_SIZE
};
} // namespace ImprovementTypes

struct ImprovementData {
    ImprovementTypes::ImprovementType type = ImprovementTypes::NONE;
    std::string displayName = "";
    bool isBuilding = false, global = false, unique = false,
         uniqueToCity = false;
    int cost = 0, population = 0, maxLevel = 0, growthRate = 0, growthScore = 0;
    // an empty bitset means there is no requirement
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> terrainRequirements;
    std::bitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE> adjacencyRequirements;
    std::bitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE> adjacencyImprovements;
    ResourceTypes::ResourceType resourceRequirement = ResourceTypes::NONE;

    // run on the tile when the improvement is made, by the player making it
    void (*createEffect)(Tile &tile, Player &player) = nullptr;
};

extern const std::unordered_map<ImprovementTypes::ImprovementType,
                                ImprovementData>
    ImprovementMap;

