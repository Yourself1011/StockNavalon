#pragma once

#include "data/tribeData.h"
#include "map.h"
#include <cstdint>
#include <vector>

// GAME.md section 18, every map type.
struct MapGenSettings {
    // Tiny 11, Small 14, Normal 16, Large 18, Huge 20, Massive 30
    int size = 14;
    MapTypes::MapType type = MapTypes::LAKES;
    // one per player, in seat order; climate regions use their modifiers
    std::vector<TribeTypes::Tribe> tribes;
    std::uint64_t seed = 0;

    // Defaults until verified (GAME.md section 18), all tunable. A negative
    // value means the map type's own default.
    // candidate maps; the fairest is kept
    int candidates = 10;
    // share of water tiles: Drylands 0, Lakes 25-30%, Pangea and Continents
    // 45-55%, Archipelago 60-80%, Water World 90-100%
    double minWater = -1, maxWater = -1;
    // pre-terrain villages: Lakes and Archipelago 0.3, Water World 0.1,
    // otherwise none
    double preTerrainDensity = -1;
    // Lakes and Archipelago only
    int suburbsPerCapital = 2;
    // suburbs go this far from their capital
    int minSuburbDistance = 3, maxSuburbDistance = 4;
    // capitals stay at least this far from the map edge (0 is the edge)
    int capitalEdgeDistance = 2;
    // villages and capitals stay at least this far apart
    int settlementSpacing = 3;
    // ruins; -1 uses the map size table
    int ruins = -1;
    // lakes are grown this many tiles at a time
    int minLake = 4, maxLake = 12;
    // fish chance on ocean next to a village, and two tiles from one
    double oceanFishInner = 0.5, oceanFishOuter = 0.25;
    // Climate regions grow from the capitals, so they're Voronoi-like; this
    // is the chance a tile's claim waits a round, giving irregular borders
    // like the real game's
    double climateIrregularity = 0.2;
};

int ruinCountForSize(int size);

// ---- placement rules, also read by stockNavalon's determinization ----

// Each resource the generator can put on a terrain, with its share of
// tiles next to a village (inner) and two away (outer). Starfish come
// separately: any water tile can have one.
struct ResourceRate {
    ResourceTypes::ResourceType resource;
    double inner, outer;
};
std::vector<ResourceRate> resourceRates(TerrainTypes::TerrainType terrain,
                                        const MapGenSettings &settings = {});
// whether a map can have this resource on this terrain (NONE fits anything)
bool resourceFitsTerrain(ResourceTypes::ResourceType resource,
                         TerrainTypes::TerrainType terrain);
// corners are always a field with a lighthouse
bool isCornerTile(int x, int y, int size);
// ruins: never on shallow water, at least this far from a village or capital
// on land and on ocean, and at least RUIN_SPACING from another ruin
constexpr int RUIN_LAND_SETTLEMENT_DISTANCE = 2;
constexpr int RUIN_OCEAN_SETTLEMENT_DISTANCE = 3;
constexpr int RUIN_SPACING = 2;

// villages on 1-tile islands: Continents, Pangea and Water World
int tinyIslandCountForSize(int size);

MapData generateMap(const MapGenSettings &settings);
