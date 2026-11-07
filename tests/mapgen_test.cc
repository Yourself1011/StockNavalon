#include "doctest/doctest.h"
#include "helpers.h"
#include "mapgen.h"
#include <cstdlib>

namespace {
int dist(const MapData &map, int a, int b) {
    return std::max(std::abs(a % map.size - b % map.size),
                    std::abs(a / map.size - b / map.size));
}
int edgeDistance(const MapData &map, int t) {
    int x = t % map.size, y = t / map.size, n = map.size;
    return std::min({x, y, n - 1 - x, n - 1 - y});
}
bool isLand(const MapData &map, int t) {
    return !TerrainTypes::isWater(map.tiles[t].terrain);
}
template <typename F> void forNeighbours(const MapData &map, int t, F f) {
    for (int u = 0; u < map.size * map.size; u++) {
        if (dist(map, t, u) == 1) {
            f(u);
        }
    }
}

std::vector<int> settlements(const MapData &map) {
    std::vector<int> out = map.capitals;
    for (int t = 0; t < map.size * map.size; t++) {
        if (map.tiles[t].building == ImprovementTypes::VILLAGE) {
            out.push_back(t);
        }
    }
    return out;
}

void checkMap(const MapData &map, int players) {
    int n = map.size;
    REQUIRE(static_cast<int>(map.capitals.size()) == players);
    std::vector<int> towns = settlements(map);

    for (int c : map.capitals) {
        CHECK(isLand(map, c));
        CHECK(edgeDistance(map, c) >= 2);
    }
    // all villages: land, off the edge, at least 3 apart
    for (std::size_t i = 0; i < towns.size(); i++) {
        CHECK(isLand(map, towns[i]));
        CHECK(edgeDistance(map, towns[i]) > 0);
        for (std::size_t j = i + 1; j < towns.size(); j++) {
            CHECK(dist(map, towns[i], towns[j]) >= 3);
        }
    }

    int water = 0;
    for (int t = 0; t < n * n; t++) {
        const Tile &tile = map.tiles[t];
        // the outer ring is land
        if (edgeDistance(map, t) == 0) {
            CHECK(isLand(map, t));
        }
        // shallow water touches land, ocean doesn't
        if (!isLand(map, t)) {
            water++;
            bool coast = false;
            forNeighbours(map, t, [&](int u) { coast |= isLand(map, u); });
            CHECK(coast == (tile.terrain == TerrainTypes::WATER));
        }
        // resources only within 2 tiles of a village
        if (tile.resource != ResourceTypes::NONE &&
            tile.resource != ResourceTypes::STARFISH) {
            int nearest = n;
            for (int v : towns) {
                nearest = std::min(nearest, dist(map, t, v));
            }
            CHECK(nearest <= 2);
        }
    }
    // a little under the 25% target once starting resources turn some water
    // back into land
    CHECK(water >= n * n * 0.18);
    CHECK(water <= n * n * 0.36);

    for (int corner : {0, n - 1, n * (n - 1), n * n - 1}) {
        CHECK(map.tiles[corner].building == ImprovementTypes::LIGHTHOUSE);
    }

    std::vector<int> ruins, starfish;
    for (int t = 0; t < n * n; t++) {
        if (map.tiles[t].building == ImprovementTypes::RUIN) {
            ruins.push_back(t);
        }
        if (map.tiles[t].resource == ResourceTypes::STARFISH) {
            starfish.push_back(t);
        }
    }
    CHECK(static_cast<int>(ruins.size()) == ruinCountForSize(n));
    int waterRuins = 0;
    for (int r : ruins) {
        CHECK(map.tiles[r].terrain != TerrainTypes::WATER);
        waterRuins += map.tiles[r].terrain == TerrainTypes::OCEAN;
        for (int v : towns) {
            CHECK(dist(map, r, v) >= 2);
        }
        for (int other : ruins) {
            CHECK((other == r || dist(map, r, other) >= 2));
        }
    }
    CHECK(waterRuins <= ruinCountForSize(n) / 3);
    for (int f : starfish) {
        for (int v : towns) {
            CHECK(dist(map, f, v) >= 2);
        }
        for (int other : starfish) {
            CHECK((other == f || dist(map, f, other) >= 2));
        }
    }

    // every capital reaches at least 2 villages over land, where there are 2
    int need = std::min<int>(
        2, static_cast<int>(towns.size() - map.capitals.size()));
    for (int c : map.capitals) {
        std::vector<char> seen(n * n, 0);
        std::vector<int> stack{c};
        seen[c] = 1;
        int villages = 0;
        while (!stack.empty()) {
            int t = stack.back();
            stack.pop_back();
            villages += map.tiles[t].building == ImprovementTypes::VILLAGE;
            forNeighbours(map, t, [&](int u) {
                if (!seen[u] && isLand(map, u)) {
                    seen[u] = 1;
                    stack.push_back(u);
                }
            });
        }
        CHECK(villages >= need);
    }
}
} // namespace

TEST_CASE("generated Lakes maps follow GAME.md section 18") {
    using namespace TribeTypes;
    std::vector<std::vector<Tribe>> lineups = {
        {IMPERIUS, BARDUR}, {LUXIDOOR, KICKOO}, {XINXI, VENGIR},
        {OUMAJI, HOODRICK, ZEBASI}, {AIMO, QUETZALI, YADAKK, IMPERIUS}};
    for (int size : {11, 14, 16}) {
        for (std::size_t l = 0; l < lineups.size(); l++) {
            for (std::uint64_t seed = 0; seed < 4; seed++) {
                CAPTURE(size);
                CAPTURE(l);
                CAPTURE(seed);
                MapGenSettings settings{.size = size,
                                        .tribes = lineups[l],
                                        .seed = seed};
                MapData map = generateMap(settings);
                checkMap(map, static_cast<int>(lineups[l].size()));
                // and it makes a game
                Game game(map, lineups[l]);
                CHECK(game.cities.size() == lineups[l].size());
            }
        }
    }
}

TEST_CASE("a seed always gives the same map") {
    MapGenSettings settings{.tribes = {TribeTypes::IMPERIUS, TribeTypes::BARDUR},
                            .seed = 42};
    MapData a = generateMap(settings), b = generateMap(settings);
    settings.seed = 43;
    MapData c = generateMap(settings);
    bool same = a.capitals == b.capitals, different = a.capitals != c.capitals;
    for (std::size_t t = 0; t < a.tiles.size(); t++) {
        same &= a.tiles[t].terrain == b.tiles[t].terrain &&
                a.tiles[t].resource == b.tiles[t].resource &&
                a.tiles[t].building == b.tiles[t].building;
        different |= a.tiles[t].terrain != c.tiles[t].terrain;
    }
    CHECK(same);
    CHECK(different);
}

TEST_CASE("tribes get their starting resources next to the capital") {
    MapGenSettings settings{
        .tribes = {TribeTypes::LUXIDOOR, TribeTypes::KICKOO}, .seed = 7};
    MapData map = generateMap(settings);
    auto around = [&](int capital, ResourceTypes::ResourceType resource) {
        int count = 0;
        forNeighbours(map, capital, [&](int t) {
            count += map.tiles[t].resource == resource;
        });
        return count;
    };
    CHECK(around(map.capitals[0], ResourceTypes::GAME) >= 2);
    CHECK(around(map.capitals[0], ResourceTypes::FISH) >= 2);
    CHECK(around(map.capitals[0], ResourceTypes::METAL) >= 1);
    CHECK(around(map.capitals[1], ResourceTypes::FISH) >= 2);
}

TEST_CASE("other map types aren't implemented yet") {
    MapGenSettings settings{.type = MapTypes::PANGEA,
                            .tribes = {TribeTypes::IMPERIUS}};
    CHECK_THROWS_AS(generateMap(settings), std::invalid_argument);
}
