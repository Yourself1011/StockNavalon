#pragma once

#include "tile.h"
#include <istream>
#include <string>
#include <vector>

namespace MapTypes {
enum MapType {
    DRYLANDS,
    LAKES,
    PANGEA,
    CONTINENTS,
    ARCHIPELAGO,
    WATERWORLD,
    MAP_TYPE_SIZE
};
} // namespace MapTypes

// A map before the game starts: terrain, resources and features, plus the
// capital tile of each player in seat order.
struct MapData {
    int size = 0;
    MapTypes::MapType type = MapTypes::LAKES;
    // index y * size + x
    std::vector<Tile> tiles;
    std::vector<int> capitals;
};

// Map file format: "<size> <mapType>" followed by size * size tokens, row by
// row from the top. Each token is a terrain character (F field, T forest,
// M mountain, W shallow water, O ocean) followed by any of:
//   f c g m h s  fruit, crop, game, metal, fish, starfish
//   V R L        village, ruin, lighthouse
//   0-9          the capital of that player
//   =            road (a bridge on shallow water)
// e.g. "Tg" is a forest with game, "F0" player 0's capital.
MapData parseMap(std::istream &in);
MapData loadMap(const std::string &fileName);
