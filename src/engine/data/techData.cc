#include "data/techData.h"
#include "utils.h"

namespace {
using namespace TechTypes;
using namespace TerrainTypes;

TechData tech(TechType type, std::string_view name, int tier, TechType parent,
              TerrainSet movement = {}, TerrainSet defence = {}) {
    return {type, name, tier, parent, movement, defence};
}
} // namespace

const std::array<TechData, TECH_TYPE_SIZE> techData = {
    tech(BASIC, "basic", 0, BASIC,
         makeBitset<TERRAIN_TYPE_SIZE>({FIELD, FOREST})),

    tech(RIDING, "riding", 1, BASIC),
    tech(ROADS, "roads", 2, RIDING),
    tech(TRADE, "trade", 3, ROADS),
    tech(FREESPIRIT, "free spirit", 2, RIDING),
    tech(CHIVALRY, "chivalry", 3, FREESPIRIT),

    tech(ORGANIZATION, "organization", 1, BASIC),
    tech(FARMING, "farming", 2, ORGANIZATION),
    tech(CONSTRUCTION, "construction", 3, FARMING),
    tech(STRATEGY, "strategy", 2, ORGANIZATION),
    tech(DIPLOMACY, "diplomacy", 3, STRATEGY),

    tech(CLIMBING, "climbing", 1, BASIC, makeBitset<TERRAIN_TYPE_SIZE>({MOUNTAIN}),
         makeBitset<TERRAIN_TYPE_SIZE>({MOUNTAIN})),
    tech(MEDITATION, "meditation", 2, CLIMBING),
    tech(PHILOSOPHY, "philosophy", 3, MEDITATION),
    tech(MINING, "mining", 2, CLIMBING),
    tech(SMITHERY, "smithery", 3, MINING),

    tech(FISHING, "fishing", 1, BASIC, makeBitset<TERRAIN_TYPE_SIZE>({WATER})),
    tech(RAMMING, "ramming", 2, FISHING),
    tech(AQUATISM, "aquatism", 3, RAMMING, {},
         makeBitset<TERRAIN_TYPE_SIZE>({WATER, OCEAN})),
    tech(SAILING, "sailing", 2, FISHING, makeBitset<TERRAIN_TYPE_SIZE>({OCEAN})),
    tech(NAVIGATION, "navigation", 3, SAILING),

    tech(HUNTING, "hunting", 1, BASIC),
    tech(FORESTRY, "forestry", 2, HUNTING),
    tech(MATHEMATICS, "mathematics", 3, FORESTRY),
    tech(ARCHERY, "archery", 2, HUNTING, {},
         makeBitset<TERRAIN_TYPE_SIZE>({FOREST})),
    tech(SPIRITUALISM, "spiritualism", 3, ARCHERY),
};
