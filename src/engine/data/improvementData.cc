#include "data/improvementData.h"
#include "utils.h"

namespace {
using namespace ImprovementTypes;
using namespace TerrainTypes;
using ResourceTypes::ResourceType;
using TechTypes::TechType;

struct Args {
    std::string_view id;
    bool isBuilding = false;
    int cost = 0, population = 0;
    TechType tech = TechTypes::BASIC;
    std::initializer_list<int> terrain;
    ResourceType resource = ResourceTypes::NONE;
    std::initializer_list<int> adjacency;
    bool limited = false, freelance = false;
};

ImprovementData improvement(ImprovementType type, std::string_view name,
                            const Args &args) {
    return {type,
            name,
            args.id,
            args.isBuilding,
            args.cost,
            args.population,
            args.tech,
            makeBitset<TERRAIN_TYPE_SIZE>(args.terrain),
            args.resource,
            makeBitset<IMPROVEMENT_TYPE_SIZE>(args.adjacency),
            args.limited,
            args.freelance};
}
// free, +3 population, on field or shallow water; unlocked by a task, not a
// tech. Built here so the terrain list outlives its use.
ImprovementData monument(ImprovementType type, std::string_view name,
                         std::string_view id) {
    return improvement(type, name,
                       {.id = id,
                        .isBuilding = true,
                        .population = 3,
                        .terrain = {FIELD, WATER}});
}
} // namespace

const std::array<ImprovementData, IMPROVEMENT_TYPE_SIZE> improvementData = {
    improvement(NONE, "none", {}),
    improvement(CITY, "city", {.id = "ci", .isBuilding = true}),
    improvement(VILLAGE, "village", {.id = "vi", .isBuilding = true}),
    improvement(RUIN, "ruin", {.id = "ru", .isBuilding = true}),
    improvement(LIGHTHOUSE, "lighthouse", {.id = "li", .isBuilding = true}),

    improvement(FARM, "farm",
                {.id = "fa",
                 .isBuilding = true,
                 .cost = 5,
                 .population = 2,
                 .tech = TechTypes::FARMING,
                 .terrain = {FIELD},
                 .resource = ResourceTypes::CROP}),
    improvement(MINE, "mine",
                {.id = "mi",
                 .isBuilding = true,
                 .cost = 5,
                 .population = 2,
                 .tech = TechTypes::MINING,
                 .terrain = {MOUNTAIN},
                 .resource = ResourceTypes::METAL}),
    improvement(LUMBERHUT, "lumber hut",
                {.id = "lh",
                 .isBuilding = true,
                 .cost = 3,
                 .population = 1,
                 .tech = TechTypes::FORESTRY,
                 .terrain = {FOREST}}),
    improvement(WINDMILL, "windmill",
                {.id = "wm",
                 .isBuilding = true,
                 .cost = 5,
                 .population = 1,
                 .tech = TechTypes::CONSTRUCTION,
                 .terrain = {FIELD},
                 .adjacency = {FARM},
                 .limited = true}),
    improvement(SAWMILL, "sawmill",
                {.id = "sm",
                 .isBuilding = true,
                 .cost = 5,
                 .population = 1,
                 .tech = TechTypes::MATHEMATICS,
                 .terrain = {FIELD},
                 .adjacency = {LUMBERHUT},
                 .limited = true}),
    improvement(FORGE, "forge",
                {.id = "fo",
                 .isBuilding = true,
                 .cost = 5,
                 .population = 2,
                 .tech = TechTypes::SMITHERY,
                 .terrain = {FIELD, FOREST},
                 .adjacency = {MINE},
                 .limited = true}),
    improvement(MARKET, "market",
                {.id = "ma",
                 .isBuilding = true,
                 .cost = 5,
                 .tech = TechTypes::TRADE,
                 .terrain = {FIELD},
                 .adjacency = {WINDMILL, SAWMILL, FORGE},
                 .limited = true}),
    improvement(PORT, "port",
                {.id = "po",
                 .isBuilding = true,
                 .cost = 7,
                 .population = 1,
                 .tech = TechTypes::FISHING,
                 .terrain = {WATER}}),
    improvement(TEMPLE, "temple",
                {.id = "te",
                 .isBuilding = true,
                 .cost = 20,
                 .population = 1,
                 .tech = TechTypes::FREESPIRIT,
                 .terrain = {FIELD}}),
    improvement(FORESTTEMPLE, "forest temple",
                {.id = "ft",
                 .isBuilding = true,
                 .cost = 15,
                 .population = 1,
                 .tech = TechTypes::SPIRITUALISM,
                 .terrain = {FOREST}}),
    improvement(MOUNTAINTEMPLE, "mountain temple",
                {.id = "mt",
                 .isBuilding = true,
                 .cost = 20,
                 .population = 1,
                 .tech = TechTypes::MEDITATION,
                 .terrain = {MOUNTAIN}}),
    improvement(WATERTEMPLE, "water temple",
                {.id = "wt",
                 .isBuilding = true,
                 .cost = 20,
                 .population = 1,
                 .tech = TechTypes::AQUATISM,
                 .terrain = {WATER, OCEAN}}),

    monument(ALTAROFPEACE, "altar of peace", "ap"),
    monument(EMPERORSTOMB, "emperor's tomb", "et"),
    monument(EYEOFGOD, "eye of god", "eg"),
    monument(GATEOFPOWER, "gate of power", "gp"),
    monument(GRANDBAZAAR, "grand bazaar", "gb"),
    monument(PARKOFFORTUNE, "park of fortune", "pf"),
    monument(TOWEROFWISDOM, "tower of wisdom", "tw"),

    improvement(HARVESTFRUIT, "harvest fruit",
                {.cost = 2,
                 .population = 1,
                 .tech = TechTypes::ORGANIZATION,
                 .terrain = {FIELD},
                 .resource = ResourceTypes::FRUIT}),
    improvement(HUNTING, "hunting",
                {.cost = 2,
                 .population = 1,
                 .tech = TechTypes::HUNTING,
                 .terrain = {FOREST},
                 .resource = ResourceTypes::GAME}),
    improvement(FISHING, "fishing",
                {.cost = 2,
                 .population = 1,
                 .tech = TechTypes::FISHING,
                 .terrain = {WATER, OCEAN},
                 .resource = ResourceTypes::FISH}),
    improvement(CLEARFOREST, "clear forest",
                {.tech = TechTypes::FORESTRY, .terrain = {FOREST}}),
    improvement(BURNFOREST, "burn forest",
                {.cost = 3,
                 .tech = TechTypes::CONSTRUCTION,
                 .terrain = {FOREST}}),
    improvement(GROWFOREST, "grow forest",
                {.cost = 5,
                 .tech = TechTypes::SPIRITUALISM,
                 .terrain = {FIELD}}),
    improvement(ROAD, "road",
                {.cost = 3,
                 .tech = TechTypes::ROADS,
                 .terrain = {FIELD, FOREST},
                 .freelance = true}),
    improvement(BRIDGE, "bridge",
                {.cost = 5,
                 .tech = TechTypes::ROADS,
                 .terrain = {WATER},
                 .freelance = true}),
};

bool isProducer(ImprovementTypes::ImprovementType type) {
    return type == WINDMILL || type == SAWMILL || type == FORGE;
}

bool isTemple(ImprovementTypes::ImprovementType type) {
    return type >= TEMPLE && type <= WATERTEMPLE;
}

bool isMonument(ImprovementTypes::ImprovementType type) {
    return type >= ALTAROFPEACE && type <= TOWEROFWISDOM;
}
