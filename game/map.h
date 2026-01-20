#include "tile.h"
#include <vector>

#ifndef MAP_H
#define MAP_H

namespace MapTypes {
enum MapType { DRYLANDS, LAKES, PANGEA, CONTINENTS, ARCHIPELAGO, WATERWORLD };
}

class Map {
    MapTypes::MapType mapType;
    std::vector<Tile> tiles;

  public:
    int size;
    Map(int size, MapTypes::MapType mapType);
    Tile *at(int x, int y);
    void print();
};

#endif // !MAP_H
