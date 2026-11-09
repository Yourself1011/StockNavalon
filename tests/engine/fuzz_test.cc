#include "doctest/doctest.h"
#include "helpers.h"
#include "mapgen.h"
#include <random>


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
