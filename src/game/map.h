#pragma once

#include "tile.h"
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

class Map {
    MapTypes::MapType mapType;
    std::vector<Tile> tiles;

  public:
    Game *game;
    int size;
    Map(Game *game, int size, MapTypes::MapType mapType);
    Map(Game *game, const std::string &fileName);

    bool inBounds(int x, int y) const;
    // throws std::out_of_range if (x, y) is off the map
    Tile *at(int x, int y);
    const Tile *at(int x, int y) const;
    void print() const;
};
