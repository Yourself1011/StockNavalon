#include "doctest/doctest.h"
#include "helpers.h"
#include "view.h"
#include <algorithm>

using namespace UnitTypes;
using CommandTypes::ATTACK;
using CommandTypes::ENDTURN;
using CommandTypes::MOVE;

namespace {
bool seen(const Game &game, int player, int x, int y) {
    return game.explored(player, game.index(x, y));
}

bool canReach(const Game &game, int unit, int x, int y) {
    std::vector<int> moves = game.moveTargets(unit);
    return std::find(moves.begin(), moves.end(), game.index(x, y)) !=
           moves.end();
}
} // namespace

TEST_CASE("each player starts with the 5x5 around their capital explored") {
    Game game = makeFogGame(bigMap());
    int count = 0;
    for (int t = 0; t < game.size * game.size; t++) {
        count += game.explored(0, t);
    }
    CHECK(count == 25);
    CHECK(seen(game, 0, 4, 4));
    CHECK_FALSE(seen(game, 0, 5, 2));
    CHECK_FALSE(seen(game, 0, 9, 9));
    CHECK(seen(game, 1, 9, 9));
}

TEST_CASE("units reveal what they can see") {
    Game game = makeFogGame(bigMap({"6 2 M"}));
    clearUnits(game);
    CHECK_FALSE(seen(game, 0, 5, 2));
    int w = addUnit(game, 0, WARRIOR, 4, 2);
    CHECK(seen(game, 0, 5, 2));
    CHECK_FALSE(seen(game, 0, 6, 2));

    SUBCASE("moving reveals around the new tile") {
        game.apply({MOVE, game.index(4, 2), game.index(5, 2)});
        CHECK(seen(game, 0, 6, 2));
        CHECK_FALSE(seen(game, 0, 7, 2));
    }
    SUBCASE("units on mountains see 2 tiles") {
        game.players[0].techs.set(TechTypes::CLIMBING);
        game.apply({MOVE, game.index(4, 2), game.index(5, 2)});
        game.apply({ENDTURN});
        game.apply({ENDTURN});
        game.apply({MOVE, game.index(5, 2), game.index(6, 2)});
        CHECK(seen(game, 0, 8, 2));
        CHECK(seen(game, 0, 8, 4));
    }
    SUBCASE("Scout units see 2 tiles") {
        addUnit(game, 0, CLOAK, 4, 4);
        CHECK(seen(game, 0, 6, 5));
    }
    (void)w;
}

TEST_CASE("moves and builds never touch unexplored tiles") {
    Game game = makeFogGame(bigMap());
    clearUnits(game);
    int r = addUnit(game, 0, RIDER, 4, 2);
    CHECK(canReach(game, r, 5, 2));
    CHECK_FALSE(canReach(game, r, 6, 2));

    game.players[0].techs.set(TechTypes::ROADS);
    CHECK_FALSE(game.canBuild(0, game.index(7, 7), ImprovementTypes::ROAD));
    game.reveal(0, game.index(7, 7));
    CHECK(game.canBuild(0, game.index(7, 7), ImprovementTypes::ROAD));

    Game open = makeGame(bigMap());
    clearUnits(open);
    int r2 = addUnit(open, 0, RIDER, 4, 2);
    CHECK(canReach(open, r2, 6, 2));
}

TEST_CASE("everything inside your borders is revealed") {
    Game game = makeFogGame(bigMap());
    int city = game.foundCity(game.index(7, 2), 0);
    CHECK(seen(game, 0, 8, 3));
    CHECK_FALSE(seen(game, 0, 9, 2));

    game.cities[city].level = 3;
    game.addPopulation(city, 4);
    game.apply({CommandTypes::CITYREWARD, game.index(7, 2), -1,
                CityRewards::BORDERGROWTH});
    CHECK(seen(game, 0, 9, 2));
    CHECK(seen(game, 0, 9, 4));
}

TEST_CASE("finding a lighthouse gives the capital a population, once") {
    Game game = makeFogGame(bigMap({"4 4 FL", "6 2 FL"}));
    // a lighthouse in the starting 5x5 still has to be found
    CHECK_FALSE(seen(game, 0, 4, 4));
    clearUnits(game);
    int capital = game.players[0].capital;

    addUnit(game, 0, WARRIOR, 5, 2);
    CHECK(game.players[0].lighthousesFound == 1);
    CHECK(game.cities[capital].population == 1);
    addUnit(game, 0, WARRIOR, 5, 3);
    CHECK(game.players[0].lighthousesFound == 2);
    CHECK(game.cities[capital].population == 0);
    CHECK(game.cities[capital].level == 2);

    game.reveal(0, game.index(6, 2));
    CHECK(game.players[0].lighthousesFound == 2);
}

TEST_CASE("resources show once their tech is researchable") {
    Game game = makeGame(bigMap({"3 3 Fc", "3 1 Mm"}),
                         {TribeTypes::IMPERIUS, TribeTypes::BARDUR});
    // Imperius owns Organization, so Farming is researchable
    CHECK(makeView(game, 0).tiles[game.index(3, 3)].resource ==
          ResourceTypes::CROP);
    CHECK(makeView(game, 1).tiles[game.index(3, 3)].resource ==
          ResourceTypes::NONE);
    // Mining needs Climbing first
    CHECK(makeView(game, 0).tiles[game.index(3, 1)].resource ==
          ResourceTypes::NONE);
    CHECK(makeView(game, 0, true).tiles[game.index(3, 1)].resource ==
          ResourceTypes::METAL);
    game.players[0].techs.set(TechTypes::CLIMBING);
    CHECK(makeView(game, 0).tiles[game.index(3, 1)].resource ==
          ResourceTypes::METAL);
}

TEST_CASE("an attacker the defender can't see isn't hit back") {
    Game game = makeFogGame(bigMap());
    clearUnits(game);
    int a = addUnit(game, 0, ARCHER, 5, 6);
    int d = addUnit(game, 1, ARCHER, 7, 6);
    game.reveal(0, game.index(7, 6));

    SUBCASE("in fog") {
        REQUIRE_FALSE(game.explored(1, game.index(5, 6)));
        game.apply({ATTACK, game.index(5, 6), game.index(7, 6)});
        CHECK(game.units[d].hp == 40);
        CHECK(game.units[a].hp == 100);
    }
    SUBCASE("seen") {
        game.reveal(1, game.index(5, 6));
        game.apply({ATTACK, game.index(5, 6), game.index(7, 6)});
        CHECK(game.units[d].hp == 40);
        CHECK(game.units[a].hp == 80);
    }
}

TEST_CASE("hidden units") {
    Game game = makeGame(bigMap());
    clearUnits(game);
    int cloak = addUnit(game, 0, CLOAK, 4, 5);
    // visible until it moves; there is no Hide command
    CHECK_FALSE(game.units[cloak].hidden);
    game.apply({MOVE, game.index(4, 5), game.index(5, 5)});
    CHECK(game.units[cloak].hidden);
    int w = addUnit(game, 1, WARRIOR, 6, 5);
    game.apply({ENDTURN});
    REQUIRE(game.currentPlayer == 1);

    SUBCASE("enemies can't see, target or be stopped by it") {
        PlayerView view = makeView(game, 1);
        CHECK(view.tiles[game.index(5, 5)].unit == -1);
        CHECK(view.units[view.tiles[game.index(6, 5)].unit].hiddenEnemyNear);
        CHECK(game.attackTargets(w).empty());
        CHECK(makeView(game, 0).tiles[game.index(5, 5)].unit >= 0);
    }
    SUBCASE("an enemy moving onto it reveals it and keeps its move") {
        REQUIRE(canReach(game, w, 5, 5));
        game.apply({MOVE, game.index(6, 5), game.index(5, 5)});
        CHECK_FALSE(game.units[cloak].hidden);
        CHECK(game.units[cloak].tile == game.index(5, 5));
        CHECK(game.units[w].tile == game.index(6, 5));
        CHECK(game.units[w].canMove);
        CHECK(game.units[w].canAttack);
        CHECK(game.attackTargets(w) == std::vector<int>{game.index(5, 5)});
    }
}

TEST_CASE("Hide units move through enemies") {
    // (5, 5) is the only way from (4, 5) to (6, 5)
    Game game = makeGame(bigMap({"5 4 M", "5 6 M"}));
    clearUnits(game);
    int cloak = addUnit(game, 0, CLOAK, 4, 5);
    addUnit(game, 1, WARRIOR, 5, 5);
    CHECK(canReach(game, cloak, 6, 5));
    // a rider, with the same movement, can't
    game.units[cloak].type = RIDER;
    CHECK_FALSE(canReach(game, cloak, 6, 5));
}

TEST_CASE("a Cloak stays hidden on a village until it captures it") {
    Game game = makeGame(bigMap({"5 5 FV"}));
    clearUnits(game);
    int cloak = addUnit(game, 0, CLOAK, 4, 5);
    REQUIRE(canReach(game, cloak, 5, 5));
    game.apply({MOVE, game.index(4, 5), game.index(5, 5)});
    CHECK(game.units[cloak].hidden);
    CHECK(makeView(game, 1).tiles[game.index(5, 5)].unit == -1);

    game.apply({ENDTURN});
    game.apply({ENDTURN});
    game.apply({CommandTypes::CAPTURE, game.index(5, 5)});
    CHECK_FALSE(game.units[cloak].hidden);
    CHECK(makeView(game, 1).tiles[game.index(5, 5)].unit >= 0);

    // and it hides again once it moves
    game.apply({ENDTURN});
    game.apply({ENDTURN});
    game.apply({MOVE, game.index(5, 5), game.index(6, 5)});
    CHECK(game.units[cloak].hidden);
}

TEST_CASE("villages can't be infiltrated") {
    Game game = makeGame(bigMap({"5 5 FV"}));
    clearUnits(game);
    int cloak = addUnit(game, 0, CLOAK, 4, 5);
    CHECK(game.attackTargets(cloak).empty());
}

TEST_CASE("a Cloak passes through an enemy city but can't stop on it") {
    // (9, 9) is the enemy capital and the only way from (8, 9) to (10, 9)
    Game game = makeGame(bigMap({"9 8 M", "9 10 M"}));
    clearUnits(game);
    int cloak = addUnit(game, 0, CLOAK, 8, 9);
    CHECK_FALSE(canReach(game, cloak, 9, 9));
    CHECK(canReach(game, cloak, 10, 9));
    CHECK(game.attackTargets(cloak) == std::vector<int>{game.index(9, 9)});
}

TEST_CASE("going from visible to hidden ends a Cloak's attack") {
    Game game = makeGame(bigMap());
    clearUnits(game);
    int cloak = addUnit(game, 0, CLOAK, 6, 7);
    REQUIRE_FALSE(game.units[cloak].hidden);
    game.apply({MOVE, game.index(6, 7), game.index(8, 8)});
    CHECK(game.units[cloak].hidden);
    CHECK_FALSE(game.units[cloak].canAttack);
    CHECK_FALSE(hasCommand(game, {ATTACK, game.index(8, 8), game.index(9, 9)}));

    // already hidden, it keeps Dash
    game.apply({ENDTURN});
    game.apply({ENDTURN});
    game.apply({MOVE, game.index(8, 8), game.index(8, 9)});
    CHECK(game.units[cloak].canAttack);
    CHECK(hasCommand(game, {ATTACK, game.index(8, 9), game.index(9, 9)}));
}

TEST_CASE("no connection through unexplored roads") {
    Game game = makeFogGame(
        bigMap({"3 2 F=", "4 2 F=", "5 2 F=", "6 2 F="}));
    int city = game.foundCity(game.index(7, 2), 0);
    game.updateAllConnections();
    REQUIRE_FALSE(seen(game, 0, 5, 2));
    CHECK(game.cities[city].connectedFor == -1);

    game.reveal(0, game.index(5, 2));
    game.updateAllConnections();
    CHECK(game.cities[city].connectedFor == 0);
}

namespace {
// levels the capital at (2, 2) up and picks the Explorer
void explore(Game &game) {
    int capital = game.players[0].capital;
    game.addPopulation(capital, 2);
    game.apply({CommandTypes::CITYREWARD, game.cities[capital].tile, -1,
                CityRewards::EXPLORER});
}

int exploredCount(const Game &game, int player) {
    int n = 0;
    for (int t = 0; t < game.size * game.size; t++) {
        n += game.explored(player, t);
    }
    return n;
}
} // namespace

TEST_CASE("the Explorer reveals fog, the same way in every copy") {
    Game game = makeFogGame(bigMap());
    Game copy = game;
    int before = exploredCount(game, 0);
    explore(game);
    explore(copy);
    CHECK(exploredCount(game, 0) > before + 12);
    for (int t = 0; t < game.size * game.size; t++) {
        CHECK(game.explored(0, t) == copy.explored(0, t));
    }
}

TEST_CASE("the Explorer walks a corridor to the end and back") {
    // mountains everywhere except row 2; no Climbing
    std::vector<std::string> mountains;
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 12; x++) {
            if (y != 2 && !(x == 9 && y == 9)) {
                mountains.push_back(std::to_string(x) + " " +
                                    std::to_string(y) + " M");
            }
        }
    }
    Game game = makeFogGame(bigMap(mountains));
    explore(game);
    CHECK(seen(game, 0, 11, 2));
    CHECK(seen(game, 0, 11, 3));
    CHECK_FALSE(seen(game, 0, 11, 4));
}

TEST_CASE("the Explorer needs Fishing for shallow water, like units") {
    // land is only the explored 5x5 around the capital, so all fog is water
    std::vector<std::string> water;
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 12; x++) {
            if ((x > 4 || y > 4) && !(x == 9 && y == 9)) {
                water.push_back(std::to_string(x) + " " + std::to_string(y) +
                                " W");
            }
        }
    }
    Game game = makeFogGame(bigMap(water));
    Game fishing = game;
    fishing.players[0].techs.set(TechTypes::FISHING);

    // without Fishing it can only see one tile out from the shore
    auto beyondShore = [](const Game &g) {
        int n = 0;
        for (int t = 0; t < g.size * g.size; t++) {
            n += g.explored(0, t) && (g.tileX(t) > 5 || g.tileY(t) > 5);
        }
        return n;
    };
    explore(game);
    CHECK(beyondShore(game) == 0);
    explore(fishing);
    CHECK(beyondShore(fishing) > 0);
}

TEST_CASE("moving reveals along the path, not just at the end") {
    Game game = makeFogGame(bigMap());
    clearUnits(game);
    int rider = addUnit(game, 0, RIDER, 2, 5);
    // explore just the path: (3,6) then (4,7)
    game.reveal(0, game.index(3, 6));
    game.reveal(0, game.index(4, 7));
    // (2,7) is next to the middle of the path only
    REQUIRE_FALSE(seen(game, 0, 2, 7));
    REQUIRE(canReach(game, rider, 4, 7));
    game.apply({MOVE, game.index(2, 5), game.index(4, 7)});
    CHECK(seen(game, 0, 2, 7));
    CHECK(seen(game, 0, 5, 8));
}
