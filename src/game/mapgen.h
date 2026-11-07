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
    // ruins; -1 uses the map size table
    int ruins = -1;
    // lakes are grown this many tiles at a time
    int minLake = 4, maxLake = 12;
    // fish chance on ocean next to a village, and two tiles from one
    double oceanFishInner = 0.5, oceanFishOuter = 0.25;
};

int ruinCountForSize(int size);

// villages on 1-tile islands: Continents, Pangea and Water World
int tinyIslandCountForSize(int size);

MapData generateMap(const MapGenSettings &settings);
