#ifndef TILE_H
#define TILE_H

class Unit;

enum TerrainType {
    FIELD = 'F',
    FOREST = 'T',
    MOUNTAIN = 'M',
    WATER = 'W',
    OCEAN = 'O'
};

class Tile {
  public:
    int x, y;
    Unit *unit;
    TerrainType terrainType;
    Tile(int x, int y, TerrainType terrainType);
};

#endif
