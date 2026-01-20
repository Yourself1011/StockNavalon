#ifndef TILE_H
#define TILE_H

class Unit;

namespace TerrainTypes {
enum TerrainType {
    FIELD = 'F',
    FOREST = 'T',
    MOUNTAIN = 'M',
    WATER = 'W',
    OCEAN = 'O'
};
}

class Tile {
  public:
    int x, y;
    Unit *unit;
    TerrainTypes::TerrainType terrainType;
    Tile(int x, int y, TerrainTypes::TerrainType terrainType);
};

#endif
