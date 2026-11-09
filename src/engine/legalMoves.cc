#include "legalMoves.h"
#include "game.h"

using namespace CommandTypes;

namespace {

// commands whose source is the current player's unit on the from tile
bool isUnitCommand(CommandType type) {
    return (type >= MOVE && type <= UPGRADE) || type == HARVEST ||
           type == CAPTURE || type == EXAMINERUINS;
}

// what the command costs, for the commands kept regardless of stars
int starCost(const Game &game, const Command &command) {
    switch (command.type) {
    case BUILD:
        return improvementInfo(
                   static_cast<ImprovementTypes::ImprovementType>(command.arg))
            .cost;
    case UPGRADE:
        return std::max(
            0, unitInfo(static_cast<UnitTypes::UnitType>(command.arg)).cost -
                   game.unitAt(command.from)->data().cost);
    default:
        return 0;
    }
}

// How far from its tile a change can affect a unit's commands: road steps
// cost half, so a move reaches twice its movement, plus a tile for zone of
// control and landing; attacks reach their range.
int reach(const Unit &unit) {
    return std::max(2 * unit.data().movement + 1, unit.data().range);
}

// Changes that touch everything: a new turn or player, the end of the game,
// or any change to techs, tasks, monuments, peace or who's alive (these feed
// into unit and tile commands all over the map).
bool needsRebuild(const Game &before, const Game &after) {
    if (before.currentPlayer != after.currentPlayer ||
        before.turn != after.turn || before.winner != after.winner ||
        before.tiles.size() != after.tiles.size()) {
        return true;
    }
    for (std::size_t i = 0; i < after.players.size(); i++) {
        const Player &a = before.players[i], &b = after.players[i];
        if (a.techs != b.techs || a.tasksDone != b.tasksDone ||
            a.monumentsBuilt != b.monumentsBuilt || a.peace != b.peace ||
            a.alive != b.alive) {
            return true;
        }
    }
    return false;
}

} // namespace

LegalMoves::LegalMoves(const Game &game) { rebuild(game); }

void LegalMoves::rebuild(const Game &game) {
    structural.clear();
    std::vector<char> all(game.tiles.size(), 1);
    generate(game, all, all);
    assemble(game);
}

void LegalMoves::generate(const Game &game, const std::vector<char> &unitTiles,
                          const std::vector<char> &tiles) {
    auto any = [](const std::vector<char> &v) {
        return std::any_of(v.begin(), v.end(), [](char c) { return c; });
    };
    if (game.gameOver() || (!any(tiles) && !any(unitTiles))) {
        return;
    }
    // the same generators, on a copy where everything is affordable
    Game rich = game;
    rich.players[rich.currentPlayer].stars = 1 << 30;
    for (std::size_t t = 0; t < tiles.size(); t++) {
        int unit = game.tiles[t].unit;
        if (unitTiles[t] && unit >= 0 &&
            game.units[unit].owner == game.currentPlayer) {
            rich.addUnitCommands(unit, structural);
        }
    }
    for (std::size_t t = 0; t < tiles.size(); t++) {
        if (tiles[t]) {
            rich.addTileCommands(static_cast<int>(t), structural);
        }
    }
}

void LegalMoves::assemble(const Game &game) {
    list.clear();
    if (game.gameOver()) {
        return;
    }
    // a waiting level-up reward is the only legal choice
    if (game.pendingRewardCity() >= 0) {
        list = game.legalCommands();
        return;
    }
    game.addResearchCommands(list);
    game.addDiplomacyCommands(list);
    for (std::size_t i = 0; i < game.cities.size(); i++) {
        if (game.cities[i].owner == game.currentPlayer) {
            game.addCityCommands(static_cast<int>(i), list);
        }
    }
    int stars = game.players[game.currentPlayer].stars;
    for (const Command &command : structural) {
        if (starCost(game, command) <= stars) {
            list.push_back(command);
        }
    }
    list.push_back({ENDTURN});
}

void LegalMoves::apply(Game &game, const Command &command) {
    Game before = game;
    game.applyUnchecked(command);
    update(before, game);
}

void LegalMoves::update(const Game &before, const Game &after) {
    if (needsRebuild(before, after)) {
        rebuild(after);
        return;
    }
    const int count = static_cast<int>(after.tiles.size());

    // tiles whose contents changed
    std::vector<char> dirty(count, 0);
    std::vector<int> dirtyList;
    auto mark = [&](int t) {
        if (!dirty[t]) {
            dirty[t] = 1;
            dirtyList.push_back(t);
        }
    };
    for (int t = 0; t < count; t++) {
        if (!(before.tiles[t] == after.tiles[t])) {
            mark(t);
        }
    }
    // units that changed, where they were and where they are
    std::size_t units = std::max(before.units.size(), after.units.size());
    for (std::size_t i = 0; i < units; i++) {
        const Unit *a = i < before.units.size() ? &before.units[i] : nullptr;
        const Unit *b = i < after.units.size() ? &after.units[i] : nullptr;
        if (a && b && *a == *b) {
            continue;
        }
        if (a && a->alive) {
            mark(a->tile);
        }
        if (b && b->alive) {
            mark(b->tile);
        }
    }
    // cities that changed; a new owner changes every tile in its borders
    for (std::size_t i = 0; i < after.cities.size(); i++) {
        const City &b = after.cities[i];
        bool added = i >= before.cities.size();
        if (!added && before.cities[i] == b) {
            continue;
        }
        mark(b.tile);
        if (added || before.cities[i].owner != b.owner) {
            int city = static_cast<int>(i);
            after.forEachInRadius(b.tile, Rules::MAX_BORDER_RADIUS, [&](int t) {
                if (after.tiles[t].territory == city ||
                    before.tiles[t].territory == city) {
                    mark(t);
                }
            });
        }
    }

    // build and destroy commands depend on the tile, its neighbours
    // (adjacency) and the rest of its city (one Windmill per city, ...)
    std::vector<char> tileSources(count, 0);
    for (int t : dirtyList) {
        tileSources[t] = 1;
        after.forEachNeighbour(t, [&](int n) { tileSources[n] = 1; });
        for (int city : {before.tiles[t].territory, after.tiles[t].territory}) {
            if (city < 0 || city >= static_cast<int>(after.cities.size())) {
                continue;
            }
            int centre = after.cities[city].tile;
            tileSources[centre] = 1;
            after.forEachInRadius(centre, Rules::MAX_BORDER_RADIUS, [&](int n) {
                if (after.tiles[n].territory == city) {
                    tileSources[n] = 1;
                }
            });
        }
    }
    // a unit's commands depend on everything within its reach
    std::vector<char> unitSources(count, 0);
    for (const Unit &unit : after.units) {
        if (!unit.alive || unit.owner != after.currentPlayer) {
            continue;
        }
        int range = reach(unit);
        for (int t : dirtyList) {
            if (after.distance(t, unit.tile) <= range) {
                unitSources[unit.tile] = 1;
                break;
            }
        }
    }

    std::erase_if(structural, [&](const Command &command) {
        return isUnitCommand(command.type)
                   ? unitSources[command.from] || dirty[command.from]
                   : tileSources[command.from];
    });
    generate(after, unitSources, tileSources);
    assemble(after);
}
