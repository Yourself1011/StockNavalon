#ifndef IMPROVEMENT_DATA_H
#define IMPROVEMENT_DATA_H

#include "game/tile.h"
#include <bitset>
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
    IMPROVEMENT_TYPE_SIZE
};
};

struct ImprovementData {
    ImprovementTypes::ImprovementType type;
    bool isBuilding = false, global = false, unique = false;
    int cost = 0, population = 0, maxLevel = 0, growthRate = 0, growthScore = 0;
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> *terrainRequirements = nullptr;
    std::bitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>
        *adjacencyRequirements = nullptr;
    std::bitset<ImprovementTypes::IMPROVEMENT_TYPE_SIZE>
        *adjacencyImprovements = nullptr;
    ResourceTypes::ResourceType resourceRequirement = ResourceTypes::NONE;

    void (*createEffect)(Tile &tile);
};

namespace Improvements {
extern const ImprovementData city;
extern const ImprovementData ruin;
extern const ImprovementData road;
extern const ImprovementData customHouse;
extern const ImprovementData farm;
extern const ImprovementData windmill;
extern const ImprovementData fishing;
extern const ImprovementData port;
extern const ImprovementData hunting;
extern const ImprovementData clearForest;
extern const ImprovementData burnForest;
extern const ImprovementData lumberHut;
extern const ImprovementData sawmill;
extern const ImprovementData growForest;
extern const ImprovementData harvestFruit;
// extern const ImprovementData whaleHunting;
extern const ImprovementData temple;
extern const ImprovementData forestTemple;
extern const ImprovementData waterTemple;
extern const ImprovementData mountainTemple;
extern const ImprovementData mine;
extern const ImprovementData forge;
extern const ImprovementData alterOfPeace;
extern const ImprovementData emperorsTomb;
extern const ImprovementData eyeOfGod;
extern const ImprovementData gateOfPower;
extern const ImprovementData grandBazaar;
extern const ImprovementData parkOfFortune;
extern const ImprovementData towerOfWisdom;
// extern const ImprovementData enchantAnimal;
// extern const ImprovementData enchantWhale;
// extern const ImprovementData sanctuary;
// extern const ImprovementData outpost;
// extern const ImprovementData iceBank;
// extern const ImprovementData iceTemple;
// extern const ImprovementData polarisClimate;
// extern const ImprovementData fungi;
// extern const ImprovementData algae;
// extern const ImprovementData mycelium;
// extern const ImprovementData burnsSpores;
// extern const ImprovementData clathrus;
// extern const ImprovementData hiddenSanctuary;
// extern const ImprovementData harvestSpores;
// extern const ImprovementData nullBuilding;
// extern const ImprovementData cultivate;
// extern const ImprovementData starFishing;
extern const ImprovementData lighthouse;
extern const ImprovementData bridge;
// extern const ImprovementData aquaFarm;
} // namespace Improvements

#endif // !IMPROVEMENT_DATA_H
