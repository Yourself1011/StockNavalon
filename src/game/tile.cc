#include "tile.h"
#include "data/improvementData.h"
#include "improvement.h"
#include "player.h"
#include "unit.h"

Tile::Tile(Game *game, int x, int y, TerrainTypes::TerrainType terrainType)
    : x{x}, y{y}, game{game}, terrainType{terrainType} {}

Tile::~Tile() = default;
Tile::Tile(Tile &&) noexcept = default;
Tile &Tile::operator=(Tile &&) noexcept = default;

void Tile::improve(Player &player, const ImprovementData &improvementData) {
    if (improvementData.createEffect != nullptr) {
        improvementData.createEffect(*this, player);
    }
    if (improvementData.isBuilding) {
        improvement =
            std::make_unique<Improvement>(player, *this, improvementData);
        resource = ResourceTypes::NONE;
    }
    if (improvementData.type == ImprovementTypes::ROAD) {
        hasRoad = true;
    }
    if (improvementData.population && city) {
        city->improvement->addPopulation(improvementData.population);
    }
    player.stars -= improvementData.cost;
}

Unit &Tile::spawnUnit(Player &player, const UnitData &unitData) {
    unit = &player.addUnit(std::make_unique<Unit>(game, &player, this, unitData));
    return *unit;
}
