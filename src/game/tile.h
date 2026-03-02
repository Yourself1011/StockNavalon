#ifndef TILE_H
#define TILE_H

#include <vector>
class Unit;
class Player;
class Improvement;
struct ImprovementData;

namespace TerrainTypes {
enum TerrainType { FIELD, FOREST, MOUNTAIN, WATER, OCEAN, TERRAIN_TYPE_SIZE };

const std::vector<char> terrainChars = {'F', 'T', 'M', 'W', 'O'};
} // namespace TerrainTypes

namespace ResourceTypes {
enum ResourceType {
    NONE,
    GAME,
    CROP,
    FISH,
    WHALE,
    METAL,
    FRUIT,
    SPORES,
    STARFISH,
    AQUACROP,
    RESOURCE_TYPE_SIZE
};
}

class Tile {
  public:
    int x, y;
    Unit *unit;
    Player *owner;
    Tile *capital;
    TerrainTypes::TerrainType terrainType;
    ResourceTypes::ResourceType resource;
    Improvement *improvement;
    void improve(Player *player, ImprovementData *improvementData);
    Tile(int x, int y, TerrainTypes::TerrainType terrainType);
};

#endif
