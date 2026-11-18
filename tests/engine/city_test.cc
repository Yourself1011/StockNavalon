#include "doctest/doctest.h"
#include "helpers.h"

using namespace UnitTypes;
using CommandTypes::CAPTURE;
using CommandTypes::ENDTURN;
using CommandTypes::MOVE;
using CommandTypes::TRAIN;

namespace {
void passRound(Game &game) {
    game.apply({ENDTURN});
    game.apply({ENDTURN});
}
} // namespace

TEST_CASE("capturing a village needs a unit that started its turn there") {
    Game game = makeGame(fieldMap({"3 3 FV"}));
    clearUnits(game);
    int w = addUnit(game, 0, WARRIOR, 2, 3);
    game.apply({MOVE, game.index(2, 3), game.index(3, 3)});
    Command capture{CAPTURE, game.index(3, 3)};
    CHECK_FALSE(hasCommand(game, capture));

    passRound(game);
    REQUIRE(hasCommand(game, capture));
    game.apply(capture);
    int city = game.cityAt(game.index(3, 3));
    REQUIRE(city >= 0);
    CHECK(game.cities[city].owner == 0);
    CHECK(game.cities[city].level == 1);
    CHECK(game.units[w].home == city);
    CHECK(game.tileOwner(game.index(4, 4)) == 0);
    // its 3x3 borders don't take tiles another city owns
    CHECK(game.tiles[game.index(2, 2)].territory == game.players[0].capital);
    CHECK(game.techCost(0, TechTypes::RIDING) == 6);
    CHECK_FALSE(game.units[w].canMove);
}

TEST_CASE("a city supports level + 1 units") {
    Game game = makeGame(fieldMap());
    int capital = game.players[0].capital;
    game.players[0].stars = 100;
    Command train{TRAIN, game.index(1, 1), -1, WARRIOR};
    // the starting warrior blocks the city tile
    CHECK_FALSE(hasCommand(game, train));
    game.apply({MOVE, game.index(1, 1), game.index(2, 2)});
    CHECK(game.unitCount(capital) == 1);

    game.apply(train);
    CHECK(game.unitCount(capital) == 2);
    const Unit *trained = game.unitAt(game.index(1, 1));
    REQUIRE(trained);
    CHECK_FALSE(trained->canMove);
    CHECK(game.players[0].stars == 98);

    passRound(game);
    game.apply({MOVE, game.index(1, 1), game.index(0, 0)});
    CHECK_FALSE(hasCommand(game, train));
}

TEST_CASE("roads connect a city to the capital") {
    Game game = makeGame(fieldMap({"2 1 F=", "3 1 F="}));
    int capital = game.players[0].capital;
    int city = game.foundCity(game.index(4, 1), 0);
    game.updateAllConnections();
    CHECK(game.cities[city].connectedFor == 0);
    CHECK(game.cities[city].population == 1);
    CHECK(game.cities[capital].population == 1);

    // capturing the city breaks the connection on both ends
    game.cities[city].owner = 1;
    game.updateAllConnections();
    CHECK(game.cities[city].connectedFor == -1);
    CHECK(game.cities[city].population == 0);
    CHECK(game.cities[capital].population == 0);
}

TEST_CASE("ports connect over at most 5 tiles of water") {
    std::vector<std::string> water;
    for (int x = 1; x <= 7; x++) {
        water.push_back(std::to_string(x) + " 0 W");
    }
    Game game = makeGame(fieldMap(water));
    game.tiles[game.index(1, 0)].building = ImprovementTypes::PORT;

    SUBCASE("in range") {
        int city = game.foundCity(game.index(6, 1), 0);
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == 0);
    }
    SUBCASE("out of range") {
        int city = game.foundCity(game.index(7, 1), 0);
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == -1);
    }
}

TEST_CASE("capturing the enemy capital wins Might") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    addUnit(game, 0, WARRIOR, 6, 6);
    game.apply({CAPTURE, game.index(6, 6)});
    CHECK(game.winner == 0);
    CHECK(game.gameOver());
    CHECK(game.legalCommands().empty());
}

TEST_CASE("units lose their home when their city is captured") {
    Game game = makeGame(fieldMap({"4 4 FV"}));
    clearUnits(game);
    int village = game.index(4, 4);
    int city = game.foundCity(village, 1);
    int defender = game.spawnUnit(1, WARRIOR, game.index(6, 5), city);
    addUnit(game, 0, WARRIOR, 4, 4);
    game.apply({CAPTURE, village});
    CHECK(game.cities[city].owner == 0);
    CHECK(game.units[defender].home == -1);
    CHECK_FALSE(game.gameOver());
}

TEST_CASE("the super unit pushes a unit off the city") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int capital = game.players[0].capital;
    game.cities[capital].level = 4;
    int w = addUnit(game, 0, WARRIOR, 1, 1);
    game.addPopulation(capital, 5);
    REQUIRE(game.cities[capital].level == 5);
    game.apply({CommandTypes::CITYREWARD, game.index(1, 1), -1,
                CityRewards::SUPERUNIT});
    const Unit *giant = game.unitAt(game.index(1, 1));
    REQUIRE(giant);
    CHECK(giant->type == GIANT);
    CHECK(giant->hp == 400);
    // a unit that never moved goes toward the centre
    CHECK(game.units[w].alive);
    CHECK(game.units[w].tile == game.index(2, 2));
}

TEST_CASE("a game copies as a value") {
    Game game = makeGame(fieldMap());
    Game copy = game;
    copy.apply({MOVE, copy.index(1, 1), copy.index(2, 2)});
    CHECK(copy.unitAt(copy.index(2, 2)));
    CHECK(game.unitAt(game.index(1, 1)));
    CHECK_FALSE(game.unitAt(game.index(2, 2)));
}

TEST_CASE("a pushed unit with no free neighbouring tile is destroyed (the houdini)") {
    // the capital at (1,1), every neighbouring tile taken by its own units
    auto surrounded = [](bool adjacent) {
        Game game = makeGame(fieldMap());
        game.settings.rulesVersion =
            adjacent ? RuleVersions::LATEST : RuleVersions::ADJACENT_PUSHES - 1;
        clearUnits(game);
        int capital = game.players[0].capital;
        game.cities[capital].level = 4;
        int w = addUnit(game, 0, WARRIOR, 1, 1);
        for (int y = 0; y <= 2; y++) {
            for (int x = 0; x <= 2; x++) {
                if (x != 1 || y != 1) {
                    addUnit(game, 0, WARRIOR, x, y);
                }
            }
        }
        game.addPopulation(capital, 5);
        game.apply({CommandTypes::CITYREWARD, game.index(1, 1), -1, CityRewards::SUPERUNIT});
        REQUIRE(game.unitAt(game.index(1, 1)));
        CHECK(game.unitAt(game.index(1, 1))->type == GIANT);
        (void)w;
        return game;
    };
    // a destroyed unit's slot can go to the Giant, so count warriors
    auto warriors = [](const Game &game, int within) {
        int n = 0;
        for (const Unit &u : game.units) {
            n += u.alive && u.type == WARRIOR &&
                 game.distance(u.tile, game.index(1, 1)) <= within;
        }
        return n;
    };
    Game now = surrounded(true);
    CHECK(warriors(now, 10) == 8);
    // games recorded before the rule replay as they were played: further out
    Game before = surrounded(false);
    CHECK(warriors(before, 10) == 9);
    CHECK(warriors(before, 1) == 8);
}

TEST_CASE("a pushed unit tries the push direction, then one step counter-clockwise, one clockwise, two each way...") {
    // a friendly warrior at the capital (1,1) that last moved east: pushed
    // onward east, then around the ring of neighbours (north is up)
    const std::vector<std::pair<int, int>> order = {
        {2, 1}, {2, 0}, {2, 2}, {1, 0}, {1, 2}, {0, 0}, {0, 2}, {0, 1}};
    for (std::size_t blocked = 0; blocked < order.size(); blocked++) {
        CAPTURE(blocked);
        Game game = makeGame(fieldMap());
        clearUnits(game);
        int capital = game.players[0].capital;
        game.cities[capital].level = 4;
        int w = addUnit(game, 0, WARRIOR, 1, 1);
        game.units[w].lastDx = 1;
        game.units[w].lastDy = 0;
        game.units[w].hp = 77;
        for (std::size_t k = 0; k < blocked; k++) {
            addUnit(game, 0, DEFENDER, order[k].first, order[k].second);
        }
        game.addPopulation(capital, 5);
        game.apply({CommandTypes::CITYREWARD, game.index(1, 1), -1, CityRewards::SUPERUNIT});
        const Unit *pushed = game.unitAt(game.index(order[blocked].first, order[blocked].second));
        REQUIRE(pushed);
        CHECK(pushed->type == WARRIOR);
        CHECK(pushed->hp == 77);
    }
}
