#pragma once

#include <array>
#include <memory>

class Unit;
class Player;
class Improvement;
class Game;
struct ImprovementData;
struct UnitData;

namespace TerrainTypes {
enum TerrainType { FIELD, FOREST, MOUNTAIN, WATER, OCEAN, TERRAIN_TYPE_SIZE };

const std::array<char, TERRAIN_TYPE_SIZE> terrainChars = {'F', 'T', 'M', 'W',
                                                          'O'};
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
} // namespace ResourceTypes

class Tile {
  public:
    int x, y;
    Game *game;
    Unit *unit = nullptr; // owned by the unit's player
    Player *owner = nullptr;
    Tile *city = nullptr; // the city tile whose borders contain this tile
    TerrainTypes::TerrainType terrainType;
    ResourceTypes::ResourceType resource = ResourceTypes::NONE;
    std::unique_ptr<Improvement> improvement;
    bool hasRoad = false;

    Tile(Game *game, int x, int y, TerrainTypes::TerrainType terrainType);
    // defined in tile.cc, where Improvement is a complete type
    ~Tile();
    Tile(Tile &&) noexcept;
    Tile &operator=(Tile &&) noexcept;

    void improve(Player &player, const ImprovementData &improvementData);
    Unit &spawnUnit(Player &player, const UnitData &unitData);
};
