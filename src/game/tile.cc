#include "data/improvementData.h"
#include "improvement.h"
#include "player.h"
#include "tile.h"

Tile::Tile(int x, int y, TerrainTypes::TerrainType terrainType)
    : x{x}, y{y}, unit{nullptr}, terrainType{terrainType},
      resource(ResourceTypes::NONE) {}

void Tile::improve(Player *player, ImprovementData *improvementData) {
    if (improvementData->createEffect != nullptr) {
        improvementData->createEffect(*this);
    }
    if (improvementData->isBuilding) {
        improvement = new Improvement(player, this, *improvementData);
        resource = ResourceTypes::NONE;
    }
}
