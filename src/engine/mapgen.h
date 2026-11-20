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
    // share of water tiles: Drylands 0, Lakes 25.5-28.1%, Pangea and
    // Continents 45-55%, Archipelago 61.2-64.3%, Water World 84.7-85.7%
    double minWater = -1, maxWater = -1;
    // the noise path (Lakes, Archipelago, Water World): smoothing passes
    // over the noise (Lakes 2, Archipelago 0, Water World 2); the wetness,
    // which the noise beside a capital is lifted to at least (Lakes 0.3,
    // Archipelago 0.65, Water World 0.85); the noise counted outside the map
    // when smoothing (Lakes 1, Continents 1, others 0.5)
    int smoothing = -1;
    double wetness = -1, edgeNoise = -1;
    // Continents: one per player, or one fewer or one more this often
    // (game: measured on 2-player maps)
    double continentFewer = 0.19, continentMore = 0.28;
    // Continents: how much a tile's land neighbours on the continent count
    // against its noise when the continent grows, for compact coasts
    double continentCompactness = 0.05;
    // pre-terrain villages: Lakes and Archipelago 0.3, Water World 0.1,
    // otherwise none
    double preTerrainDensity = -1;
    // suburbs per capital, Lakes and Archipelago only (game: the map
    // generator's settings)
    int minSuburbs = 1, maxSuburbs = 2;
    // suburbs go this far from their capital (game: measured)
    int minSuburbDistance = 3, maxSuburbDistance = 3;
    // capitals are at least this far in from their domain's sides (game:
    // measured), and at least capitalEdgeDistance from the map edge (0 is
    // the edge)
    int capitalDomainMargin = 2;
    int capitalEdgeDistance = 2;
    // villages and capitals stay at least this far apart
    int settlementSpacing = 3;
    // ruins; -1 uses the map size table
    int ruins = -1;
    // a tile two from a settlement gets a resource drawn as next to one this
    // often, and none otherwise (game: measured, 0.32-0.35 of the rate next
    // to a settlement for every tribe and resource)
    double outerShare = 0.32;
};

int ruinCountForSize(int size);

// ---- placement rules, also read by stockNavalon's determinization ----

// Each resource the generator can put on a terrain, with the chance it
// tries for a tile next to a settlement, before the tribe's modifier. Every
// resource tries on its own; if several succeed, one of them is picked
// (game: measured, tribe by tribe). Starfish come separately: any water
// tile can have one.
struct ResourceRate {
    ResourceTypes::ResourceType resource;
    double chance;
};
std::vector<ResourceRate> resourceRates(TerrainTypes::TerrainType terrain,
                                        const MapGenSettings &settings = {});
// whether a map can have this resource on this terrain (NONE fits anything)
bool resourceFitsTerrain(ResourceTypes::ResourceType resource,
                         TerrainTypes::TerrainType terrain);
// corners always have a lighthouse: on field, or on the water already there
bool isCornerTile(int x, int y, int size);
// ruins: never on shallow water, at least this far from a village or capital
// on land and on ocean, and at least RUIN_SPACING from another ruin
constexpr int RUIN_LAND_SETTLEMENT_DISTANCE = 2;
constexpr int RUIN_OCEAN_SETTLEMENT_DISTANCE = 3;
constexpr int RUIN_SPACING = 2;

// villages on 1-tile islands: Continents, Pangea and Water World
int tinyIslandCountForSize(int size);

MapData generateMap(const MapGenSettings &settings);
