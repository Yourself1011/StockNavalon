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

TEST_CASE("connections as the game finds them: villages link, a capture waits, other players' land needs peace") {
    SUBCASE("a village is a link, as a city is") {
        Game game = makeGame(fieldMap({"2 1 F=", "3 1 FV", "4 1 F="}));
        int city = game.foundCity(game.index(5, 1), 0);
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == 0);
        game.settings.rulesVersion = RuleVersions::GAME_CONNECTIONS - 1;
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == -1);
    }
    SUBCASE("a captured village connects at the end of the capture; breaking peace waits") {
        Game game = makeGame(fieldMap({"2 1 F=", "3 1 F=", "4 1 FV"}));
        clearUnits(game);
        addUnit(game, 0, WARRIOR, 4, 1);
        game.apply({CAPTURE, game.index(4, 1)});
        int city = game.cityAt(game.index(4, 1));
        CHECK(game.cities[city].connectedFor == 0);
    }
    SUBCASE("a step from the player's land into another's needs peace between them") {
        Game game = makeGame(fieldMap({"2 1 F=", "3 1 F=", "4 1 F="}));
        int theirs = game.foundCity(game.index(3, 2), 1);
        REQUIRE(game.tileOwner(game.index(3, 1)) == 1);
        REQUIRE(game.tileOwner(game.index(2, 1)) == 0);
        int city = game.foundCity(game.index(5, 1), 0);
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == -1);
        game.players[0].peace = game.players[1].peace = ~std::uint32_t{0};
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == 0);
        CHECK(game.cities[theirs].connectedFor == -1);
    }
}

TEST_CASE("ports connect to ports at most 5 tiles away, over water the player's techs open") {
    std::vector<std::string> water;
    for (int x = 1; x <= 7; x++) {
        water.push_back(std::to_string(x) + " 0 W");
    }
    Game game = makeGame(fieldMap(water));
    game.players[0].techs.set(TechTypes::FISHING);
    game.tiles[game.index(1, 0)].building = ImprovementTypes::PORT;

    SUBCASE("in range") {
        int city = game.foundCity(game.index(6, 1), 0);
        game.tiles[game.index(6, 0)].building = ImprovementTypes::PORT;
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == 0);
        // the route is marked, ports included
        CHECK(game.waterRoute[game.index(1, 0)]);
        CHECK(game.waterRoute[game.index(6, 0)]);
    }
    SUBCASE("not without the water's tech") {
        game.players[0].techs.reset(TechTypes::FISHING);
        int city = game.foundCity(game.index(6, 1), 0);
        game.tiles[game.index(6, 0)].building = ImprovementTypes::PORT;
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == -1);
    }
    SUBCASE("out of range") {
        int city = game.foundCity(game.index(7, 1), 0);
        game.tiles[game.index(7, 0)].building = ImprovementTypes::PORT;
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == -1);
    }
    SUBCASE("a port alone doesn't reach a city (it did under older rules)") {
        int city = game.foundCity(game.index(6, 1), 0);
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == -1);
        game.settings.rulesVersion = RuleVersions::GAME_CONNECTIONS - 1;
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == 0);
    }
}

TEST_CASE("connections give the city its point before the capital, last city first; a level-up holds back the rest (game)") {
    // a city east of the capital (1,1) and one south of it, both on roads
    Game game = makeGame(fieldMap({"2 1 F=", "3 1 F=", "1 2 F=", "1 3 F="}));
    int capital = game.players[0].capital;
    int east = game.foundCity(game.index(4, 1), 0);
    int south = game.foundCity(game.index(1, 4), 0);
    game.cities[capital].population = 1;
    game.updateAllConnections();
    // the southern city (the later tile) connects first; the capital's
    // point levels it up, and the eastern city's waits
    CHECK(game.cities[south].population == 1);
    CHECK(game.cities[capital].level == 2);
    CHECK(game.cities[capital].population == 0);
    CHECK(game.cities[east].population == 0);
    CHECK(game.cities[east].connectedFor == 0);
    game.apply({CommandTypes::CITYREWARD, game.cities[capital].tile, -1, CityRewards::WORKSHOP});
    CHECK(game.cities[east].population == 1);
    CHECK(game.cities[capital].population == 1);
}

TEST_CASE("water routes: bridges end them, and they leave land only onto a port or a bridge (game)") {
    // water, a field with a city, water, along the top row
    Game game = makeGame(fieldMap({"1 0 W", "2 0 W", "4 0 W", "5 0 W"}));
    game.players[0].techs.set(TechTypes::FISHING);
    game.foundCity(game.index(3, 1), 0);
    game.foundCity(game.index(5, 1), 0);
    game.tiles[game.index(1, 0)].building = ImprovementTypes::PORT;
    game.tiles[game.index(5, 0)].building = ImprovementTypes::PORT;
    // the way from (1,0) to (5,0) crosses the city tile (3,0) and steps off
    // it onto open water: not a route
    game.foundCity(game.index(3, 0), 0);
    game.updateAllConnections();
    CHECK_FALSE(game.waterRoute[game.index(4, 0)]);
    game.settings.rulesVersion = RuleVersions::ROUTE_LANDINGS - 1;
    game.updateAllConnections();
    CHECK(game.waterRoute[game.index(4, 0)]);
    // a bridge at (4,0) is a router, and a step off land onto it is allowed
    game.settings.rulesVersion = RuleVersions::LATEST;
    game.tiles[game.index(4, 0)].road = true;
    game.updateAllConnections();
    CHECK(game.waterRoute[game.index(4, 0)]);
    CHECK(game.waterRoute[game.index(3, 0)]);
}

TEST_CASE("a road built by another player connects your cities only once you recompute yours (game)") {
    // the capital (1,1) and a city at (5,1), with neutral land at (3,1)
    Game game = makeGame(fieldMap({"2 1 F=", "4 1 F="}));
    int city = game.foundCity(game.index(5, 1), 0);
    REQUIRE(game.tileOwner(game.index(3, 1)) < 0);
    game.updateAllConnections();
    REQUIRE(game.cities[city].connectedFor == -1);
    game.currentPlayer = 1;
    game.players[1].techs.set(TechTypes::ROADS);
    game.players[1].stars = 10;
    game.tiles[game.index(3, 1)].explored |= 2;
    game.applyUnchecked({CommandTypes::BUILD, game.index(3, 1), -1, ImprovementTypes::ROAD});
    CHECK(game.cities[city].connectedFor == -1);
    // something of player 0's that recomputes
    game.currentPlayer = 0;
    game.connectionsChanged();
    game.applyUnchecked({ENDTURN});
    CHECK(game.cities[city].connectedFor == 0);
}

TEST_CASE("a captured city's connection moves from the old owner's capital to the capturer's (game)") {
    // player 1's city at (4,4) on a road to its capital (6,6), and on a road
    // to player 0's capital (1,1) too
    Game game = makeGame(fieldMap({"5 5 F=", "2 2 F=", "3 3 F=", "4 4 FV"}));
    clearUnits(game);
    int city = game.foundCity(game.index(4, 4), 1);
    int mine = game.players[0].capital, theirs = game.players[1].capital;
    game.updateAllConnections();
    REQUIRE(game.cities[city].connectedFor == 1);
    int cityPop = game.cities[city].population;
    int minePop = game.cities[mine].population, theirsPop = game.cities[theirs].population;
    addUnit(game, 0, WARRIOR, 4, 4);
    game.apply({CAPTURE, game.index(4, 4)});
    CHECK(game.cities[city].connectedFor == 0);
    CHECK(game.cities[city].population == cityPop);
    CHECK(game.cities[theirs].population == theirsPop - 1);
    CHECK(game.cities[mine].population == minePop + 1);
}

TEST_CASE("a capture that explores still moves the connection: the old owner loses it first") {
    // in the fog, the city's grown borders exploring new tiles
    for (bool first : {true, false}) {
        CAPTURE(first);
        // capitals at (2,2) and (9,9), a road between them through (4,4)
        Game game = makeFogGame(bigMap({"3 3 F=", "4 4 FV", "5 5 F=", "6 6 F=", "7 7 F=",
                                        "8 8 F="}));
        game.settings.rulesVersion =
            first ? RuleVersions::LATEST : RuleVersions::OLD_OWNER_DISCONNECTS_FIRST - 1;
        clearUnits(game);
        int city = game.foundCity(game.index(4, 4), 1);
        game.cities[city].borderGrown = true;
        int theirs = game.players[1].capital;
        game.updateAllConnections();
        REQUIRE(game.cities[city].connectedFor == 1);
        int cityPop = game.cities[city].population;
        int theirsPop = game.cities[theirs].population;
        addUnit(game, 0, WARRIOR, 4, 4);
        REQUIRE_FALSE(game.explored(0, game.index(6, 6)));
        game.apply({CAPTURE, game.index(4, 4)});
        REQUIRE(game.explored(0, game.index(6, 6)));
        CHECK(game.cities[city].connectedFor == 0);
        // before, the capturer took the connection over first, and the old
        // owner's population was never taken away: the city's extra point
        // levelled it up
        CHECK(game.cities[theirs].population == theirsPop - (first ? 1 : 0));
        if (first) {
            CHECK(game.cities[city].population == cityPop);
        } else {
            CHECK(game.cities[city].level == 2);
        }
    }
}

TEST_CASE("a captured city the capturer doesn't reach loses its connection: -1 there and at the old capital (game)") {
    // player 1's city at (4,4) on a road to its capital (6,6), none to
    // player 0's capital (1,1)
    Game game = makeGame(fieldMap({"5 5 F=", "4 4 FV"}));
    clearUnits(game);
    int city = game.foundCity(game.index(4, 4), 1);
    int mine = game.players[0].capital, theirs = game.players[1].capital;
    game.updateAllConnections();
    REQUIRE(game.cities[city].connectedFor == 1);
    int cityPop = game.cities[city].population;
    int minePop = game.cities[mine].population, theirsPop = game.cities[theirs].population;
    addUnit(game, 0, WARRIOR, 4, 4);
    game.apply({CAPTURE, game.index(4, 4)});
    CHECK(game.cities[city].connectedFor == -1);
    CHECK(game.cities[city].population == cityPop - 1);
    CHECK(game.cities[theirs].population == theirsPop - 1);
    CHECK(game.cities[mine].population == minePop);
}

TEST_CASE("no capture of a city of a player either side is cooling down from (game)") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int w = addUnit(game, 0, WARRIOR, 6, 6);
    game.players[0].peace = game.players[1].peace = 0;
    game.players[0].coolDown = std::uint32_t{1} << 1;
    CHECK_FALSE(hasCommand(game, {CAPTURE, game.index(6, 6)}));
    game.players[0].coolDown = 0;
    CHECK(hasCommand(game, {CAPTURE, game.index(6, 6)}));
    (void)w;
}

TEST_CASE("a capture explores the city's border radius and re-checks the old owner's producers next to it (game)") {
    // player 1's city at (4,4) with a mine at (4,5) on its land; player 1's
    // capital (6,6) has a forge at (5,5) next to that mine
    Game game = makeGame(fieldMap({"4 4 FV"}));
    clearUnits(game);
    int city = game.foundCity(game.index(4, 4), 1);
    int capital = game.players[1].capital;
    REQUIRE(game.tiles[game.index(4, 5)].territory == city);
    REQUIRE(game.tiles[game.index(5, 5)].territory == capital);
    game.tiles[game.index(4, 5)].building = ImprovementTypes::MINE;
    game.placeBuilding(1, game.index(5, 5), ImprovementTypes::FORGE);
    REQUIRE(game.tiles[game.index(5, 5)].level >= 1);
    int popBefore = game.cities[capital].population;
    int level = game.tiles[game.index(5, 5)].level;
    addUnit(game, 0, WARRIOR, 4, 4);
    game.apply({CAPTURE, game.index(4, 4)});
    // the mine is player 0's now: the forge loses that level
    CHECK(game.tiles[game.index(5, 5)].level == level - 1);
    CHECK(game.cities[capital].population < popBefore);
    // and every tile within a tile of the city is explored, the capital's
    // land beside it too
    CHECK(game.tiles[game.index(5, 5)].exploredBy(0));
}

TEST_CASE("a captured city's producer counts its new owner's buildings next to it (game)") {
    // player 1's city (3,4) with a forge at (4,4); player 0's city (6,4)
    // with mines at (5,4) and (5,3)
    Game game = makeGame(fieldMap({"3 4 FV", "6 4 FV"}));
    clearUnits(game);
    int theirs = game.foundCity(game.index(3, 4), 1);
    game.foundCity(game.index(6, 4), 0);
    REQUIRE(game.tileOwner(game.index(4, 4)) == 1);
    REQUIRE(game.tileOwner(game.index(5, 4)) == 0);
    game.placeBuilding(1, game.index(4, 4), ImprovementTypes::FORGE);
    game.tiles[game.index(5, 4)].building = ImprovementTypes::MINE;
    game.tiles[game.index(5, 3)].building = ImprovementTypes::MINE;
    REQUIRE(game.tileOwner(game.index(5, 3)) == 0);
    REQUIRE(game.tiles[game.index(4, 4)].level == 0);
    int pop = game.cities[theirs].population, level = game.cities[theirs].level;
    addUnit(game, 0, WARRIOR, 3, 4);
    game.apply({CAPTURE, game.index(3, 4)});
    CHECK(game.tiles[game.index(4, 4)].level == 2);
    // 4 population: from level 1, 2 levels up (2 then 3 needed), 1 left
    CHECK(game.cities[theirs].level * 10 + game.cities[theirs].population > level * 10 + pop);
}

TEST_CASE("a wiped out player gets its units' disband refund (game)") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    addUnit(game, 0, WARRIOR, 6, 6);
    int rider = addUnit(game, 1, RIDER, 3, 5);
    int stars = game.players[1].stars;
    game.apply({CAPTURE, game.index(6, 6)});
    CHECK_FALSE(game.units[rider].alive);
    CHECK(game.players[1].stars == stars + Rules::disbandRefund(unitInfo(RIDER).cost));
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

TEST_CASE("a captured city's units lose it as their home, but not under 77-78") {
    for (bool keeps : {false, true}) {
        CAPTURE(keeps);
        Game game = makeGame(fieldMap({"4 4 FV"}));
        game.settings.rulesVersion =
            keeps ? RuleVersions::CAPTURE_SPARES_PASSENGERS - 1 : RuleVersions::LATEST;
        clearUnits(game);
        int village = game.index(4, 4);
        int city = game.foundCity(village, 1);
        int defender = game.spawnUnit(1, WARRIOR, game.index(6, 5), city);
        addUnit(game, 0, WARRIOR, 4, 4);
        game.apply({CAPTURE, village});
        CHECK(game.cities[city].owner == 0);
        CHECK(game.units[defender].home == (keeps ? city : -1));
        // the capturer and the defender
        CHECK(game.unitCount(city) == (keeps ? 2 : 1));
        CHECK_FALSE(game.gameOver());
    }
}

TEST_CASE("a boat's passenger keeps its home through a capture, and lands with it (game)") {
    Game game = makeGame(fieldMap({"4 4 FV", "7 5 W"}));
    clearUnits(game);
    int village = game.index(4, 4);
    int city = game.foundCity(village, 1);
    int boat = game.spawnUnit(1, WARRIOR, game.index(7, 5), city);
    game.units[boat].type = RAFT;
    addUnit(game, 0, WARRIOR, 4, 4);
    game.apply({CAPTURE, village});
    // the boat is on the map: it loses the city, so it doesn't count
    CHECK(game.units[boat].home == -1);
    CHECK(game.unitCount(city) == 1);
    game.apply({ENDTURN});
    Unit &b = game.units[boat];
    b.canMove = b.canAct = true;
    game.apply({CommandTypes::DISEMBARK, game.index(7, 5), game.index(6, 5)});
    CHECK(game.units[boat].type == WARRIOR);
    CHECK(game.units[boat].home == city);
    CHECK(game.unitCount(city) == 2);
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

TEST_CASE("a pushed unit goes the way it faces, then turning further each way: first toward lower directions") {
    // a friendly warrior at the capital (1,1) facing east (direction 5,
    // (+1, 0)): the game's directions are SW, W, NW, N, NE, E, SE, S with N
    // as +y, so it tries E, NE, SE, N, S, NW, SW, W
    const std::vector<std::pair<int, int>> order = {
        {2, 1}, {2, 2}, {2, 0}, {1, 2}, {1, 0}, {0, 2}, {0, 0}, {0, 1}};
    for (std::size_t blocked = 0; blocked < order.size(); blocked++) {
        CAPTURE(blocked);
        Game game = makeGame(fieldMap());
        clearUnits(game);
        int capital = game.players[0].capital;
        game.cities[capital].level = 4;
        int w = addUnit(game, 0, WARRIOR, 1, 1);
        game.units[w].facing = 5;
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

TEST_CASE("units face the map's centre when they appear, and each other when they fight") {
    CHECK(Directions::nearest(5, 1) == 5);
    CHECK(Directions::nearest(1, 1) == 4);
    CHECK(Directions::nearest(0, 0) == 0);
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, WARRIOR, 1, 1);
    CHECK(game.units[a].facing == Directions::nearest(game.size / 2 - 1, game.size / 2 - 1));
}

TEST_CASE("under older rules, a pushed unit tries the push direction, then one step counter-clockwise, one clockwise, two each way...") {
    // a friendly warrior at the capital (1,1) that last moved east: pushed
    // onward east, then around the ring of neighbours (north is up)
    const std::vector<std::pair<int, int>> order = {
        {2, 1}, {2, 0}, {2, 2}, {1, 0}, {1, 2}, {0, 0}, {0, 2}, {0, 1}};
    for (std::size_t blocked = 0; blocked < order.size(); blocked++) {
        CAPTURE(blocked);
        Game game = makeGame(fieldMap());
        game.settings.rulesVersion = RuleVersions::GAME_PUSH_ORDER - 1;
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

TEST_CASE("a Giant pushed onto a port becomes a Juggernaut and stomps (game)") {
    Game game = makeGame(fieldMap({"2 1 W"}));
    clearUnits(game);
    game.players[0].techs.set(TechTypes::FISHING);
    game.tiles[game.index(2, 1)].building = ImprovementTypes::PORT;
    int capital = game.players[0].capital;
    game.cities[capital].level = 4;
    int giant = addUnit(game, 0, GIANT, 1, 1);
    game.units[giant].facing = 5;  // east, toward the port
    int enemy = addUnit(game, 1, WARRIOR, 3, 1);
    game.addPopulation(capital, 5);
    game.apply({CommandTypes::CITYREWARD, game.index(1, 1), -1, CityRewards::SUPERUNIT});
    REQUIRE(game.units[giant].tile == game.index(2, 1));
    CHECK(game.units[giant].type == JUGGERNAUT);
    CHECK(game.units[enemy].hp < 100);
}

TEST_CASE("a pushed land unit can go onto a port, as a boat, not onto open water (game)") {
    for (bool port : {true, false}) {
        CAPTURE(port);
        Game game = makeGame(fieldMap({"2 1 W"}));
        clearUnits(game);
        game.players[0].techs.set(TechTypes::FISHING);
        if (port) {
            game.tiles[game.index(2, 1)].building = ImprovementTypes::PORT;
        }
        int capital = game.players[0].capital;
        game.cities[capital].level = 4;
        int w = addUnit(game, 0, WARRIOR, 1, 1);
        game.units[w].facing = 5;  // east, toward the water
        game.addPopulation(capital, 5);
        game.apply({CommandTypes::CITYREWARD, game.index(1, 1), -1, CityRewards::SUPERUNIT});
        const Unit *pushed = game.unitAt(game.index(2, 1));
        if (port) {
            REQUIRE(pushed);
            CHECK(pushed->type == RAFT);
            CHECK(pushed->passenger == WARRIOR);
        } else {
            // the next way round: north-east, (2, 2)
            CHECK_FALSE(pushed);
            REQUIRE(game.unitAt(game.index(2, 2)));
            CHECK(game.unitAt(game.index(2, 2))->type == WARRIOR);
        }
    }
}

TEST_CASE("with a reward waiting at its turn's start, the income comes once it's picked (game)") {
    Game game = makeGame(fieldMap());
    int capital = game.players[1].capital;
    game.apply({CommandTypes::ENDTURN});
    game.apply({CommandTypes::ENDTURN});
    // during player 0's turn, player 1's capital grows a level
    game.addPopulation(capital, 2);
    REQUIRE(game.cities[capital].pendingRewards == 1);
    int stars = game.players[1].stars;
    game.apply({CommandTypes::ENDTURN});
    CHECK(game.players[1].stars == stars);
    game.apply({CommandTypes::CITYREWARD, game.cities[capital].tile, -1, CityRewards::WORKSHOP});
    // the income with the Workshop in it
    CHECK(game.players[1].stars == stars + game.income(1));
    CHECK(game.cities[capital].workshop);
}
