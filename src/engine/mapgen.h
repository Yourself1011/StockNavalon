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
    // up to this many candidate maps; the fairest is kept, and the first at
    // most equalityLimit ends the search (game: the generator's settings)
    int candidates = 50;
    double equalityLimit = 1.01;
    // share of water tiles on the noise path: Drylands 0, Lakes 25.5-28.1%,
    // Archipelago 61.2-64.3%, Water World 84.7-85.7% (Continents and Pangea
    // get theirs from how their continents grow)
    double minWater = -1, maxWater = -1;
    // the noise path (Lakes, Archipelago, Water World): smoothing passes
    // over the noise (Lakes 2, Archipelago 0, Water World 2); the wetness,
    // which the noise beside a capital is lifted to at least (Lakes 0.3,
    // Archipelago 0.65, Water World 0.85); the noise counted outside the map
    // when smoothing (Lakes 1, Continents 1, Pangea 0, others 0.5)
    int smoothing = -1;
    double wetness = -1, edgeNoise = -1;
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
// ruins (game: the generator's own rules): never on shallow water or a
// corner, at least this far from a village on land and from any settlement
// on ocean, from a capital, and from another ruin; on a resource or not
constexpr int RUIN_LAND_SETTLEMENT_DISTANCE = 2;
constexpr int RUIN_OCEAN_SETTLEMENT_DISTANCE = 3;
constexpr int RUIN_CAPITAL_DISTANCE = 3;
constexpr int RUIN_SPACING = 3;


// How unequal the players' starts are, as the real game scores it (GAME.md
// section 18): the best territory's score over the worst's, 1 when equal.
double startInequality(const MapData &map);

MapData generateMap(const MapGenSettings &settings);
