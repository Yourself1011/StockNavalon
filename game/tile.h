#ifndef TILE_H
#define TILE_H

#include <vector>
class Unit;

namespace TerrainTypes {
enum TerrainType { FIELD, FOREST, MOUNTAIN, WATER, OCEAN, TERRAIN_TYPE_SIZE };

const std::vector<char> terrainChars = {'F', 'T', 'M', 'W', 'O'};
} // namespace TerrainTypes

class Tile {
  public:
    int x, y;
    Unit *unit;
    TerrainTypes::TerrainType terrainType;
    Tile(int x, int y, TerrainTypes::TerrainType terrainType);
};

#endif
