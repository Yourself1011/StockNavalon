#include "tile.h"

Tile::Tile(int x, int y, TerrainTypes::TerrainType terrainType)
    : x{x}, y{y}, unit{nullptr}, terrainType{terrainType} {}
