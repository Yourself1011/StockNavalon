#pragma once

#include "doctest/doctest.h"
#include "game.h"
#include <sstream>
#include <string>
#include <vector>

// A game on an inline map (see map.h for the format). Every tile starts
// explored by everyone, and every tribe has met every other, so rules tests
// don't trip over fog or meeting bonuses; makeFogGame keeps the real start.
inline Game makeFogGame(const std::string &map,
                        std::vector<TribeTypes::Tribe> tribes = {
                            TribeTypes::IMPERIUS, TribeTypes::BARDUR}) {
    std::istringstream in(map);
    return Game(parseMap(in), tribes);
}

inline Game makeGame(const std::string &map,
                     std::vector<TribeTypes::Tribe> tribes = {
                         TribeTypes::IMPERIUS, TribeTypes::BARDUR}) {
    Game game = makeFogGame(map, tribes);
    for (Tile &tile : game.tiles) {
        tile.explored = ~std::uint32_t{0};
    }
    for (Player &player : game.players) {
        player.met = ~std::uint32_t{0};
    }
    return game;
}

// an 8x8 field map with capitals at (1,1) and (6,6)
inline std::string fieldMap(std::vector<std::string> overrides = {}) {
    std::vector<std::string> tokens(64, "F");
    tokens[1 * 8 + 1] = "F0";
    tokens[6 * 8 + 6] = "F1";
    // overrides are "x y token"
    for (const std::string &o : overrides) {
        std::istringstream in(o);
        int x, y;
        std::string token;
        in >> x >> y >> token;
        tokens[y * 8 + x] = token;
    }
    std::string out = "8 1\n";
    for (const std::string &t : tokens) {
        out += t + " ";
    }
    return out;
}

// a 12x12 field map with capitals at (2,2) and (9,9), so fog has room
inline std::string bigMap(std::vector<std::string> overrides = {}) {
    std::vector<std::string> tokens(144, "F");
    tokens[2 * 12 + 2] = "F0";
    tokens[9 * 12 + 9] = "F1";
    for (const std::string &o : overrides) {
        std::istringstream in(o);
        int x, y;
        std::string token;
        in >> x >> y >> token;
        tokens[y * 12 + x] = token;
    }
    std::string out = "12 1\n";
    for (const std::string &t : tokens) {
        out += t + " ";
    }
    return out;
}

// spawns a unit that can act this turn
inline int addUnit(Game &game, int player, UnitTypes::UnitType type, int x,
                   int y) {
    int unit = game.spawnUnit(player, type, game.index(x, y),
                              game.players[player].capital);
    Unit &u = game.units[unit];
    u.canMove = u.canAttack = u.canAct = true;
    return unit;
}

inline bool hasCommand(const Game &game, const Command &command) {
    for (const Command &c : game.legalCommands()) {
        if (c == command) {
            return true;
        }
    }
    return false;
}

inline void clearUnits(Game &game) {
    for (Unit &unit : game.units) {
        if (unit.alive) {
            game.tiles[unit.tile].unit = -1;
            unit.alive = false;
        }
    }
}

// the state's internal bookkeeping agrees with itself; REQUIREs, so call
// it from a test case
inline void checkInvariants(const Game &game) {
    for (std::size_t t = 0; t < game.tiles.size(); t++) {
        int unit = game.tiles[t].unit;
        if (unit >= 0) {
            REQUIRE(game.units[unit].alive);
            REQUIRE(game.units[unit].tile == static_cast<int>(t));
        }
    }
    for (std::size_t i = 0; i < game.units.size(); i++) {
        const Unit &unit = game.units[i];
        if (!unit.alive) {
            continue;
        }
        REQUIRE(game.tiles[unit.tile].unit == static_cast<int>(i));
        REQUIRE(game.explored(unit.owner, unit.tile));
        REQUIRE(unit.hp > 0);
        REQUIRE(unit.hp <= unit.maxHp);
        REQUIRE(game.players[unit.owner].alive);
        // a bridge holds land units and boats alike
        const Tile &tile = game.tiles[unit.tile];
        bool water = TerrainTypes::isWater(tile.terrain);
        if (!(water && tile.road)) {
            REQUIRE(unit.isNaval() == water);
        }
    }
    for (std::size_t i = 0; i < game.cities.size(); i++) {
        const City &city = game.cities[i];
        REQUIRE(game.tiles[city.tile].building == ImprovementTypes::CITY);
        REQUIRE(game.tiles[city.tile].territory == static_cast<int>(i));
        for (std::size_t t = 0; t < game.tiles.size(); t++) {
            if (game.tiles[t].territory == static_cast<int>(i)) {
                REQUIRE(game.explored(city.owner, static_cast<int>(t)));
            }
        }
    }
    for (const Player &player : game.players) {
        REQUIRE(player.stars >= 0);
    }
}

