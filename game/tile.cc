#include "tile.h"

Tile::Tile(int x, int y, TerrainType terrainType)
    : x{x}, y{y}, terrainType{terrainType}, unit{nullptr} {}
