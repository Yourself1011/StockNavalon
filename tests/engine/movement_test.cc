#include "doctest/doctest.h"
#include "helpers.h"
#include <algorithm>

using namespace UnitTypes;
using CommandTypes::DISEMBARK;
using CommandTypes::MOVE;

namespace {
bool canReach(const Game &game, int unit, int x, int y) {
    std::vector<int> moves = game.moveTargets(unit);
    return std::find(moves.begin(), moves.end(), game.index(x, y)) !=
           moves.end();
}
} // namespace

TEST_CASE("a warrior moves one tile in any direction") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int w = addUnit(game, 0, WARRIOR, 3, 3);
    CHECK(game.moveTargets(w).size() == 8);
    CHECK(canReach(game, w, 4, 4));
    CHECK_FALSE(canReach(game, w, 5, 3));
}

TEST_CASE("units can pass through friendly units but not enemies") {
    // (3, 3) is the only way from (2, 3) to (4, 3)
    Game game = makeGame(fieldMap({"3 2 M", "3 4 M"}));
    clearUnits(game);
    int r = addUnit(game, 0, RIDER, 2, 3);
    int w = addUnit(game, 0, WARRIOR, 3, 3);
    CHECK_FALSE(canReach(game, r, 3, 3));
    CHECK(canReach(game, r, 4, 3));

    game.units[w].owner = 1;
    CHECK_FALSE(canReach(game, r, 4, 3));
}

TEST_CASE("forests and mountains end movement; mountains need Climbing") {
    Game game = makeGame(fieldMap({"4 0 T", "4 1 T", "4 2 T", "4 3 T",
                                   "4 4 T", "4 5 T", "4 6 T", "4 7 T",
                                   "2 4 M"}));
    clearUnits(game);
    int r = addUnit(game, 0, RIDER, 3, 3);
    CHECK(canReach(game, r, 4, 3));
    CHECK_FALSE(canReach(game, r, 5, 3));
    CHECK_FALSE(canReach(game, r, 2, 4));
    game.players[0].techs.set(TechTypes::CLIMBING);
    CHECK(canReach(game, r, 2, 4));
    CHECK_FALSE(canReach(game, r, 1, 5));
}

TEST_CASE("zone of control") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int r = addUnit(game, 0, RIDER, 2, 3);
    CHECK(canReach(game, r, 4, 4));
    addUnit(game, 1, WARRIOR, 4, 3);
    // both ways to (4, 4) step next to the enemy first
    CHECK_FALSE(canReach(game, r, 4, 4));
    CHECK(canReach(game, r, 3, 4));
    CHECK(canReach(game, r, 2, 5));
}

TEST_CASE("road steps cost half") {
    Game game = makeGame(fieldMap({"1 3 F=", "2 3 F=", "3 3 F=", "4 3 F=",
                                   "5 3 F=", "6 3 F="}));
    clearUnits(game);
    int r = addUnit(game, 0, RIDER, 1, 3);
    CHECK(canReach(game, r, 5, 3));
    CHECK_FALSE(canReach(game, r, 6, 3));
}

TEST_CASE("roads in enemy territory don't help") {
    Game game = makeGame(fieldMap({"3 6 F=", "4 6 F=", "5 6 F=", "6 7 F=",
                                   "7 7 F="}));
    clearUnits(game);
    int r = addUnit(game, 0, RIDER, 3, 6);
    CHECK(canReach(game, r, 5, 6));
    CHECK_FALSE(canReach(game, r, 6, 7));
}

TEST_CASE("embarking and disembarking") {
    Game game = makeGame(fieldMap({"2 2 W", "3 0 W", "3 1 W", "3 2 W", "3 3 W",
                                   "3 4 W", "3 5 W"}));
    clearUnits(game);
    int port = game.index(2, 2);
    game.tiles[port].building = ImprovementTypes::PORT;

    int w = addUnit(game, 0, WARRIOR, 1, 2);
    game.units[w].kills = 2;
    game.units[w].hp = 70;
    REQUIRE(canReach(game, w, 2, 2));
    game.apply({MOVE, game.index(1, 2), port});
    CHECK(game.units[w].type == RAFT);
    CHECK(game.units[w].passenger == WARRIOR);
    CHECK_FALSE(game.units[w].canMove);

    game.apply({CommandTypes::ENDTURN});
    game.apply({CommandTypes::ENDTURN});
    CHECK(game.units[w].canMove);
    // the raft moves on water only, and can land next to it
    CHECK(canReach(game, w, 3, 3));
    CHECK_FALSE(canReach(game, w, 1, 2));
    std::vector<int> landings = game.disembarkTargets(w);
    CHECK(std::find(landings.begin(), landings.end(), game.index(4, 3)) !=
          landings.end());

    game.apply({DISEMBARK, port, game.index(4, 3)});
    CHECK(game.units[w].type == WARRIOR);
    CHECK(game.units[w].hp == 70);
    CHECK(game.units[w].kills == 2);
    CHECK_FALSE(game.units[w].canMove);
}

TEST_CASE("Raft upgrades inside your borders") {
    Game game = makeGame(fieldMap({"2 2 W"}));
    clearUnits(game);
    int raft = addUnit(game, 0, WARRIOR, 2, 2);
    game.units[raft].type = RAFT;
    Command upgrade{CommandTypes::UPGRADE, game.index(2, 2), -1, RAMMER};
    CHECK_FALSE(hasCommand(game, upgrade));
    game.players[0].techs.set(TechTypes::RAMMING);
    REQUIRE(hasCommand(game, upgrade));
    game.apply(upgrade);
    CHECK(game.units[raft].type == RAMMER);
    CHECK(game.players[0].stars == 0);
    CHECK(game.units[raft].canMove);
}
