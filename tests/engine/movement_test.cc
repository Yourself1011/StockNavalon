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

TEST_CASE("any movement left allows one more step") {
    // a warrior (movement 1) takes a road step, and with half a step left
    // can still step off the road; without the road step it couldn't go 2
    Game game = makeGame(fieldMap({"1 3 F=", "2 3 F="}));
    clearUnits(game);
    int w = addUnit(game, 0, WARRIOR, 1, 3);
    CHECK(canReach(game, w, 2, 3));
    CHECK(canReach(game, w, 3, 3));
    CHECK(canReach(game, w, 3, 4));
    // the last step uses up what's left
    CHECK_FALSE(canReach(game, w, 4, 3));
    CHECK_FALSE(canReach(game, w, 4, 4));
    // and off the road, one step as before
    int other = addUnit(game, 0, WARRIOR, 1, 6);
    CHECK(canReach(game, other, 2, 7));
    CHECK_FALSE(canReach(game, other, 3, 6));
}

TEST_CASE("roads in enemy territory don't help") {
    Game game = makeGame(fieldMap({"3 6 F=", "4 6 F=", "5 6 F=", "6 7 F=",
                                   "7 7 F="}));
    clearUnits(game);
    int r = addUnit(game, 0, RIDER, 3, 6);
    CHECK(canReach(game, r, 5, 6));
    // the step into their borders costs a whole step, so only the half step
    // left takes it on to (6, 7); with their roads it would reach (7, 7)
    CHECK(canReach(game, r, 6, 7));
    CHECK_FALSE(canReach(game, r, 7, 7));
}

TEST_CASE("embarking and disembarking") {
    Game game = makeGame(fieldMap({"2 2 W", "3 0 W", "3 1 W", "3 2 W", "3 3 W",
                                   "3 4 W", "3 5 W"}));
    clearUnits(game);
    int port = game.index(2, 2);
    game.tiles[port].building = ImprovementTypes::PORT;
    game.players[0].techs.set(TechTypes::FISHING);

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

TEST_CASE("a boat stops at zone of control, and only lands from water it could move on from") {
    // a water column at x = 3; an enemy at (2,3) next to (3,2), (3,3), (3,4)
    std::vector<std::string> water;
    for (int y = 0; y < 8; y++) {
        water.push_back("3 " + std::to_string(y) + " W");
    }
    Game game = makeGame(fieldMap(water));
    clearUnits(game);
    int raft = addUnit(game, 0, WARRIOR, 3, 1);
    game.units[raft].type = RAFT;
    addUnit(game, 1, WARRIOR, 2, 3);
    // the step next to the enemy ends the move: no further down the water
    CHECK(canReach(game, raft, 3, 2));
    CHECK_FALSE(canReach(game, raft, 3, 3));
    // landings only next to where it could have moved on from: its start
    std::vector<int> landings = game.disembarkTargets(raft);
    auto lands = [&](int x, int y) {
        return std::find(landings.begin(), landings.end(), game.index(x, y)) != landings.end();
    };
    CHECK(lands(2, 2));
    CHECK(lands(4, 1));
    CHECK_FALSE(lands(4, 3));
    CHECK_FALSE(lands(2, 4));
    // landing ends its turn: no move, attack or action after
    game.apply({DISEMBARK, game.index(3, 1), game.index(2, 2)});
    REQUIRE(game.units[raft].type == WARRIOR);
    CHECK_FALSE(game.units[raft].canMove);
    CHECK_FALSE(game.units[raft].canAttack);
    CHECK_FALSE(game.units[raft].canAct);
    for (const Command &c : game.legalCommands()) {
        CHECK(c.from != game.index(2, 2));
    }
}

TEST_CASE("an enemy in fog still exerts zone of control, a hidden one doesn't") {
    // the rider's route from (1,3) to (3,3) passes (2,3), next to an enemy
    // at (2,4); mountains close the way round (Climbing not owned)
    Game game = makeGame(fieldMap({"1 2 M", "2 2 M", "3 2 M", "1 4 M", "3 4 M"}));
    clearUnits(game);
    int r = addUnit(game, 0, RIDER, 1, 3);
    REQUIRE(canReach(game, r, 3, 3));
    addUnit(game, 1, WARRIOR, 2, 4);
    CHECK_FALSE(canReach(game, r, 3, 3));
    // the same with the enemy's tile unexplored: player 0 can't see it
    game.tiles[game.index(2, 4)].explored &= ~std::uint32_t{1};
    REQUIRE_FALSE(game.canSee(0, *game.unitAt(game.index(2, 4))));
    CHECK(canReach(game, r, 2, 3));
    CHECK_FALSE(canReach(game, r, 3, 3));
    // a hidden unit (Hide) exerts none, explored tile or not
    game.unitAt(game.index(2, 4))->hidden = true;
    CHECK(canReach(game, r, 3, 3));
    game.tiles[game.index(2, 4)].explored = ~std::uint32_t{0};
    CHECK(canReach(game, r, 3, 3));
}

TEST_CASE("a Juggernaut stomps whenever it moves: embarking, at sea, and disembarking") {
    // water down x = 3 and at (2,2), a port at (2,2) inside player 0's
    // borders (capital at (1,1))
    std::vector<std::string> water{"2 2 W"};
    for (int y = 0; y < 8; y++) {
        water.push_back("3 " + std::to_string(y) + " W");
    }
    for (bool embarkStomp : {true, false}) {
        CAPTURE(embarkStomp);
        Game game = makeGame(fieldMap(water));
        game.settings.rulesVersion =
            embarkStomp ? RuleVersions::LATEST : RuleVersions::EMBARK_STOMP - 1;
        clearUnits(game);
        game.tiles[game.index(2, 2)].building = ImprovementTypes::PORT;
        game.players[0].techs.set(TechTypes::FISHING);
        int giant = addUnit(game, 0, GIANT, 1, 2);
        int next = addUnit(game, 1, WARRIOR, 1, 3);
        int far = addUnit(game, 1, WARRIOR, 5, 5);
        int hp = game.units[next].hp;
        // embarking: the Giant becomes a Juggernaut on the port
        game.apply({MOVE, game.index(1, 2), game.index(2, 2)});
        REQUIRE(game.units[giant].type == JUGGERNAUT);
        if (embarkStomp) {
            CHECK(game.units[next].hp < hp);
        } else {
            CHECK(game.units[next].hp == hp);
        }
        CHECK(game.units[far].hp == game.units[far].maxHp);
    }
    // at sea and disembarking
    Game game = makeGame(fieldMap(water));
    clearUnits(game);
    int jug = addUnit(game, 0, GIANT, 3, 3);
    game.units[jug].type = JUGGERNAUT;
    int beside = addUnit(game, 1, WARRIOR, 2, 4);
    int landing = addUnit(game, 1, WARRIOR, 5, 4);
    game.apply({MOVE, game.index(3, 3), game.index(3, 4)});
    CHECK(game.units[beside].hp < game.units[beside].maxHp);
    CHECK(game.units[landing].hp == game.units[landing].maxHp);
    game.apply({CommandTypes::ENDTURN});
    game.apply({CommandTypes::ENDTURN});
    REQUIRE(hasCommand(game, {DISEMBARK, game.index(3, 4), game.index(4, 4)}));
    game.apply({DISEMBARK, game.index(3, 4), game.index(4, 4)});
    REQUIRE(game.units[jug].type == GIANT);
    CHECK(game.units[landing].hp < game.units[landing].maxHp);
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
    // the upgrade is final: the Rammer can't become another boat
    game.players[0].techs.set(TechTypes::SAILING);
    game.players[0].techs.set(TechTypes::NAVIGATION);
    game.players[0].stars = 100;
    for (UnitTypes::UnitType boat : {UnitTypes::SCOUT, RAMMER, BOMBER}) {
        CHECK_FALSE(hasCommand(game, {CommandTypes::UPGRADE, game.index(2, 2), -1, boat}));
    }
}

TEST_CASE("units embark at a peace partner's port too, with Fishing (game)") {
    Game game = makeGame(fieldMap({"5 6 W"}));
    clearUnits(game);
    game.tiles[game.index(5, 6)].building = ImprovementTypes::PORT;
    game.players[0].peace = game.players[1].peace = ~std::uint32_t{0};
    int w = addUnit(game, 0, WARRIOR, 4, 6);
    CHECK_FALSE(canReach(game, w, 5, 6));
    game.settings.rulesVersion = RuleVersions::EMBARK_NEEDS_PORT_TECH - 1;
    CHECK(canReach(game, w, 5, 6));
    game.settings.rulesVersion = RuleVersions::LATEST;
    game.players[0].techs.set(TechTypes::FISHING);
    CHECK(canReach(game, w, 5, 6));
}

TEST_CASE("a Raft that recovered gets no attack back by upgrading; one that moved onto a port does (game)") {
    Game game = makeGame(fieldMap({"2 2 W", "2 1 W"}));
    clearUnits(game);
    game.tiles[game.index(2, 1)].building = ImprovementTypes::PORT;
    int raft = addUnit(game, 0, WARRIOR, 2, 2);
    game.units[raft].type = RAFT;
    game.units[raft].hp = 50;
    game.players[0].techs.set(TechTypes::SAILING);
    game.players[0].stars = 10;
    Game moved = game;
    game.apply({CommandTypes::RECOVER, game.index(2, 2)});
    game.apply({CommandTypes::UPGRADE, game.index(2, 2), -1, UnitTypes::SCOUT});
    CHECK_FALSE(game.units[raft].canAttack);
    moved.apply({CommandTypes::MOVE, moved.index(2, 2), moved.index(2, 1)});
    moved.apply({CommandTypes::UPGRADE, moved.index(2, 1), -1, UnitTypes::SCOUT});
    CHECK(moved.units[raft].canAttack);
}

TEST_CASE("a Raft that moved, upgraded to a Dash boat, can still attack (game)") {
    Game game = makeGame(fieldMap({"2 2 W", "2 1 W"}));
    clearUnits(game);
    int raft = addUnit(game, 0, WARRIOR, 2, 2);
    game.units[raft].type = RAFT;
    game.players[0].techs.set(TechTypes::SAILING);
    game.players[0].stars = 10;
    game.apply({CommandTypes::MOVE, game.index(2, 2), game.index(2, 1)});
    CHECK_FALSE(game.units[raft].canAttack);
    game.apply({CommandTypes::UPGRADE, game.index(2, 1), -1, UnitTypes::SCOUT});
    CHECK(game.units[raft].canAttack);

    // embarking ends the turn, attacks too, upgrade or not (game)
    Game port = makeGame(fieldMap({"2 2 W"}));
    clearUnits(port);
    port.tiles[port.index(2, 2)].building = ImprovementTypes::PORT;
    port.players[0].techs.set(TechTypes::FISHING);
    port.players[0].techs.set(TechTypes::SAILING);
    port.players[0].stars = 10;
    int w = addUnit(port, 0, WARRIOR, 1, 2);
    port.apply({CommandTypes::MOVE, port.index(1, 2), port.index(2, 2)});
    port.apply({CommandTypes::UPGRADE, port.index(2, 2), -1, UnitTypes::SCOUT});
    CHECK_FALSE(port.units[w].canAttack);
}

TEST_CASE("a Raft upgrades in a peace partner's borders too (game)") {
    Game game = makeGame(fieldMap({"5 6 W"}));
    clearUnits(game);
    int raft = addUnit(game, 0, WARRIOR, 5, 6);
    game.units[raft].type = RAFT;
    game.players[0].techs.set(TechTypes::RAMMING);
    REQUIRE(game.tileOwner(game.index(5, 6)) == 1);
    Command upgrade{CommandTypes::UPGRADE, game.index(5, 6), -1, RAMMER};
    CHECK_FALSE(hasCommand(game, upgrade));
    game.players[0].peace = game.players[1].peace = ~std::uint32_t{0};
    CHECK(hasCommand(game, upgrade));
}

TEST_CASE("a Creep unit gets nothing from roads (game), unlike under older rules") {
    Game game = makeGame(fieldMap({"2 3 F=", "3 3 F=", "4 3 F=", "5 3 F=", "6 3 F="}));
    clearUnits(game);
    // a Rider (movement 2) goes 4 road steps, a Cloak (movement 2) only 2
    int rider = addUnit(game, 0, RIDER, 2, 3);
    CHECK(canReach(game, rider, 6, 3));
    game.units[rider].alive = false;
    game.tiles[game.index(2, 3)].unit = -1;
    int cloak = addUnit(game, 0, CLOAK, 2, 3);
    CHECK(canReach(game, cloak, 4, 3));
    CHECK_FALSE(canReach(game, cloak, 5, 3));
    game.settings.rulesVersion = RuleVersions::CREEP_NO_ROADS - 1;
    CHECK(canReach(game, cloak, 6, 3));
}

TEST_CASE("without Fishing, a bridge is entered along it or diagonally, not straight from its side (game)") {
    // an east-west bridge at (3, 3): land west and east, water north and south
    Game game = makeGame(fieldMap({"3 2 W", "3 3 W=", "3 4 W"}));
    clearUnits(game);
    int north = addUnit(game, 0, WARRIOR, 3, 1);
    CHECK_FALSE(canReach(game, north, 3, 2));
    int west = addUnit(game, 0, WARRIOR, 2, 3);
    CHECK(canReach(game, west, 3, 3));
    int diagonal = addUnit(game, 0, WARRIOR, 2, 2);
    CHECK(canReach(game, diagonal, 3, 3));
    // a warrior on water would be a boat, so for the side: (3, 2) is water,
    // and no unit stands there; check the step itself
    CHECK(game.bridgeSideStep(0, game.index(3, 2), game.index(3, 3)));
    CHECK_FALSE(game.bridgeSideStep(0, game.index(2, 2), game.index(3, 3)));
    game.players[0].techs.set(TechTypes::FISHING);
    CHECK_FALSE(game.bridgeSideStep(0, game.index(3, 2), game.index(3, 3)));
    game.players[0].techs.reset(TechTypes::FISHING);
    game.settings.rulesVersion = RuleVersions::BRIDGE_SIDES - 1;
    CHECK_FALSE(game.bridgeSideStep(0, game.index(3, 2), game.index(3, 3)));
}

TEST_CASE("a boat on a bridge can disembark where it is (game)") {
    Game game = makeGame(fieldMap({"2 0 W", "2 1 W=", "2 2 W"}));
    clearUnits(game);
    int w = addUnit(game, 0, WARRIOR, 2, 1);
    game.units[w].type = RAFT;
    REQUIRE(hasCommand(game, {DISEMBARK, game.index(2, 1), game.index(2, 1)}));
    game.apply({DISEMBARK, game.index(2, 1), game.index(2, 1)});
    CHECK(game.units[w].type == WARRIOR);
    CHECK(game.units[w].tile == game.index(2, 1));
    game.settings.rulesVersion = RuleVersions::DISEMBARK_ON_BRIDGE - 1;
    game.units[w].type = RAFT;
    game.units[w].canMove = true;
    CHECK_FALSE(hasCommand(game, {DISEMBARK, game.index(2, 1), game.index(2, 1)}));
}
