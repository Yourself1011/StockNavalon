#include "game.h"

using namespace ImprovementTypes;

namespace {
bool isMapFeature(ImprovementType type) {
    return type == CITY || type == VILLAGE || type == RUIN ||
           type == LIGHTHOUSE;
}
} // namespace

bool Game::canBuild(int player, int tile, ImprovementType type) const {
    if (type < FARM || type >= IMPROVEMENT_TYPE_SIZE) {
        return false;
    }
    const ImprovementData &data = improvementInfo(type);
    const Tile &t = tiles[tile];
    const Player &p = players[player];

    if (!explored(player, tile) || !p.hasTech(data.tech) ||
        p.stars < data.cost || !data.terrain.test(t.terrain)) {
        return false;
    }
    int owner = tileOwner(tile);
    if (data.freelance ? (owner >= 0 && owner != player) : owner != player) {
        return false;
    }
    const Unit *unit = unitAt(tile);
    if (unit && hostile(player, unit->owner) && canSee(player, *unit)) {
        return false;
    }
    if (data.resource != ResourceTypes::NONE && t.resource != data.resource) {
        return false;
    }
    // a monument needs its task done, and is built once
    if (isMonument(type)) {
        int task = TaskTypes::taskFor(type);
        if (!p.tasksDone.test(task) || p.monumentsBuilt.test(task)) {
            return false;
        }
    }

    if (type == ROAD) {
        // a road can share its tile with a building, but not a map feature
        if (t.road || isMapFeature(t.building)) {
            return false;
        }
    } else if (t.building != NONE || (t.road && TerrainTypes::isWater(t.terrain))) {
        return false;
    }

    if (data.adjacency.any()) {
        bool found = false;
        forEachNeighbour(tile, [&](int n) {
            found |= data.adjacency.test(tiles[n].building) &&
                     tileOwner(n) == player;
        });
        if (!found) {
            return false;
        }
    }

    if (data.limited) {
        int city = t.territory;
        bool exists = false;
        forEachInRadius(cities[city].tile, Rules::MAX_BORDER_RADIUS, [&](int n) {
            exists |= tiles[n].territory == city && tiles[n].building == type;
        });
        if (exists) {
            return false;
        }
    }
    return true;
}

bool Game::canDestroy(int player, int tile) const {
    const Tile &t = tiles[tile];
    if (!players[player].hasTech(TechTypes::CHIVALRY) ||
        tileOwner(tile) != player) {
        return false;
    }
    // monuments can't be destroyed
    return t.building == RUIN ||
           (t.building >= FARM && t.building <= WATERTEMPLE);
}

void Game::refreshProducer(int tile) {
    Tile &t = tiles[tile];
    const ImprovementData &data = improvementInfo(t.building);
    int owner = tileOwner(tile);
    int count = 0;
    forEachNeighbour(tile, [&](int n) {
        count += data.adjacency.test(tiles[n].building) && tileOwner(n) == owner;
    });
    int level = std::min(count, MAX_BUILDING_LEVEL);
    int change = level - t.level;
    t.level = level;
    if (change != 0 && t.territory >= 0) {
        addPopulation(t.territory, change * data.population);
    }
}

void Game::refreshProducersAround(int tile) {
    forEachNeighbour(tile, [&](int n) {
        if (isProducer(tiles[n].building)) {
            refreshProducer(n);
        }
    });
}

void Game::build(int tile, ImprovementType type) {
    const ImprovementData &data = improvementInfo(type);
    Tile &t = tiles[tile];
    Player &player = players[currentPlayer];
    player.stars -= data.cost;

    switch (type) {
    case HARVESTFRUIT:
    case HUNTING:
    case FISHING:
        t.resource = ResourceTypes::NONE;
        addPopulation(t.territory, data.population);
        return;
    case CLEARFOREST:
        t.terrain = TerrainTypes::FIELD;
        t.resource = ResourceTypes::NONE;
        player.stars += Rules::CLEAR_FOREST_REFUND;
        return;
    case BURNFOREST:
        t.terrain = TerrainTypes::FIELD;
        t.resource = ResourceTypes::CROP;
        return;
    case GROWFOREST:
        t.terrain = TerrainTypes::FOREST;
        t.resource = ResourceTypes::NONE;
        return;
    case ROAD:
    case BRIDGE:
        t.road = true;
        updateAllConnections();
        return;
    default:
        break;
    }

    // a building
    if (isMonument(type)) {
        player.monumentsBuilt.set(TaskTypes::taskFor(type));
    }
    t.building = type;
    t.resource = ResourceTypes::NONE;
    t.level = 0;
    t.builtTurn = turn;
    if (isProducer(type)) {
        refreshProducer(tile);
    } else {
        addPopulation(t.territory, data.population);
    }
    refreshProducersAround(tile);
    if (type == PORT) {
        updateAllConnections();
    }
}

void Game::destroy(int tile) {
    Tile &t = tiles[tile];
    ImprovementType type = t.building;
    const ImprovementData &data = improvementInfo(type);
    if (type != RUIN && t.territory >= 0) {
        // you lose what it gave
        int population = isProducer(type) ? t.level * data.population
                                          : data.population;
        addPopulation(t.territory, -population);
    }
    t.building = NONE;
    t.level = 0;
    refreshProducersAround(tile);
    if (type == PORT) {
        updateAllConnections();
    }
}
