#include "doctest/doctest.h"
#include "helpers.h"
#include "mapgen.h"
#include <random>

namespace {
void checkInvariants(const Game &game) {
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
} // namespace

TEST_CASE("random games keep the state consistent") {
    MapData fixed = loadMap("src/engine/map.txt");
    for (unsigned seed = 0; seed < 40; seed++) {
        CAPTURE(seed);
        std::mt19937 rng(seed);
        auto tribe = [&] {
            return static_cast<TribeTypes::Tribe>(rng() %
                                                  TribeTypes::TRIBE_SIZE);
        };
        std::vector<TribeTypes::Tribe> tribes = {tribe(), tribe()};
        // a quarter on the fixed map, the rest on generated maps of every
        // type
        auto type = static_cast<MapTypes::MapType>(seed % MapTypes::MAP_TYPE_SIZE);
        MapData map =
            seed % 4 == 0
                ? fixed
                : generateMap({.type = type, .tribes = tribes, .seed = seed});
        Game game(map, tribes, {.seed = seed});
        // rich games reach more of the rules
        bool rich = seed % 2 == 1;

        for (int step = 0; step < 4000 && !game.gameOver(); step++) {
            std::vector<Command> legal = game.legalCommands();
            REQUIRE_FALSE(legal.empty());
            const Command &command = legal[rng() % legal.size()];
            REQUIRE(game.isLegal(command));
            // commands never act on tiles the player hasn't explored
            for (int t : {command.from, command.to}) {
                if (t >= 0) {
                    REQUIRE(game.explored(game.currentPlayer, t));
                }
            }
            int turn = game.turn;
            game.apply(command);
            if (rich && game.turn != turn) {
                for (Player &player : game.players) {
                    player.stars += 10;
                }
            }
            checkInvariants(game);
        }
    }
}
