#include "tile.h"
#include <vector>

#ifndef MAP_H
#define MAP_H

enum MapType { DRYLANDS, LAKES, PANGEA, CONTINENTS, ARCHIPELAGO, WATERWORLD };

class Map {
    int size;
    MapType mapType;
    std::vector<Tile> tiles;

  public:
    Map(int size, MapType mapType);
    Tile *at(int x, int y);
    void print();
};

#endif // !MAP_H
