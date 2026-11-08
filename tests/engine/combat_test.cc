#include "doctest/doctest.h"
#include "helpers.h"

using namespace UnitTypes;
using CommandTypes::ATTACK;

namespace {
void attack(Game &game, int attacker, int x, int y) {
    game.apply({ATTACK, game.units[attacker].tile, game.index(x, y)});
}
} // namespace

TEST_CASE("full-health warriors deal 5 and take 5 back") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, WARRIOR, 3, 3);
    int d = addUnit(game, 1, WARRIOR, 4, 3);
    attack(game, a, 4, 3);
    // retaliation uses the defender's HP from before the hit
    CHECK(game.units[d].hp == 50);
    CHECK(game.units[a].hp == 50);
    CHECK_FALSE(game.units[a].canMove);
    CHECK_FALSE(game.units[a].canAttack);
    CHECK_FALSE(game.units[a].canAct);
}

TEST_CASE("warrior attacking a defender") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, WARRIOR, 3, 3);
    int d = addUnit(game, 1, DEFENDER, 4, 3);
    // 2 / 5 * 2 * 4.5 = 3.6 and 3 / 5 * 3 * 4.5 = 8.1
    attack(game, a, 4, 3);
    CHECK(game.units[d].hp == 110);
    CHECK(game.units[a].hp == 20);
}

TEST_CASE("Fortify units defend 1.5x in their own city, 4x with a wall") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, WARRIOR, 5, 5);
    int d = addUnit(game, 1, WARRIOR, 6, 6);
    CHECK(game.defenceBonus(game.units[d]) == 1.5);
    CHECK(game.previewAttack(game.units[a], game.units[d]).damage == 40);
    CHECK(game.previewAttack(game.units[a], game.units[d]).retaliation == 50);

    game.cities[game.cityAt(game.index(6, 6))].wall = true;
    CHECK(game.defenceBonus(game.units[d]) == 4.0);
    CHECK(game.previewAttack(game.units[a], game.units[d]).damage == 20);
    CHECK(game.previewAttack(game.units[a], game.units[d]).retaliation == 70);

    SUBCASE("only Fortify units get the city bonus") {
        game.units[d].type = SWORDSMAN;
        CHECK(game.defenceBonus(game.units[d]) == 1.0);
    }
    SUBCASE("besiegers get no city bonus") {
        game.units[d].owner = 0;
        CHECK(game.defenceBonus(game.units[d]) == 1.0);
    }
}

TEST_CASE("terrain defence needs the tech") {
    Game game = makeGame(fieldMap({"4 3 T"}));
    clearUnits(game);
    int d = addUnit(game, 1, WARRIOR, 4, 3);
    CHECK(game.defenceBonus(game.units[d]) == 1.0);
    game.players[1].techs.set(TechTypes::ARCHERY);
    CHECK(game.defenceBonus(game.units[d]) == 1.5);
}

TEST_CASE("melee attackers move into the tile of a unit they kill") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, WARRIOR, 3, 3);
    int d = addUnit(game, 1, WARRIOR, 4, 3);
    game.units[d].hp = 30;
    attack(game, a, 4, 3);
    CHECK_FALSE(game.units[d].alive);
    CHECK(game.units[a].tile == game.index(4, 3));
    CHECK(game.tiles[game.index(3, 3)].unit == -1);
    CHECK(game.units[a].kills == 1);
}

TEST_CASE("ranged attackers stay put and get no retaliation out of range") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, ARCHER, 2, 3);
    int d = addUnit(game, 1, WARRIOR, 4, 3);
    attack(game, a, 4, 3);
    CHECK(game.units[d].hp == 50);
    CHECK(game.units[a].hp == 100);

    int c = addUnit(game, 0, CATAPULT, 1, 4);
    attack(game, c, 4, 3);
    CHECK_FALSE(game.units[d].alive);
    CHECK(game.units[c].tile == game.index(1, 4));
}

TEST_CASE("Stiff defenders and Surprise attackers get no retaliation") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, WARRIOR, 3, 3);
    int d = addUnit(game, 1, MINDBENDER, 4, 3);
    attack(game, a, 4, 3);
    CHECK(game.units[a].hp == 100);

    int dagger = addUnit(game, 0, DAGGER, 3, 2);
    int w = addUnit(game, 1, WARRIOR, 4, 2);
    attack(game, dagger, 4, 2);
    CHECK(game.units[w].hp == 50);
    CHECK(game.units[dagger].hp == 100);
    (void)d;
}

TEST_CASE("Escape lets a rider move after attacking, but not attack again") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, RIDER, 3, 3);
    addUnit(game, 1, DEFENDER, 4, 3);
    attack(game, a, 4, 3);
    CHECK(game.units[a].canMove);
    CHECK_FALSE(game.units[a].canAttack);
    game.apply({CommandTypes::MOVE, game.index(3, 3), game.index(2, 3)});
    CHECK_FALSE(game.units[a].canAttack);
}

TEST_CASE("Persist lets a knight attack again after a kill") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int k = addUnit(game, 0, KNIGHT, 3, 3);
    int d = addUnit(game, 1, WARRIOR, 4, 3);
    game.units[d].hp = 10;
    attack(game, k, 4, 3);
    CHECK(game.units[k].canAttack);
}

TEST_CASE("splash does half the rounded damage, unrounded") {
    Game game = makeGame(fieldMap({"2 4 W"}));
    clearUnits(game);
    int b = addUnit(game, 0, WARRIOR, 2, 4);
    game.units[b].type = BOMBER;
    int d = addUnit(game, 1, DEFENDER, 4, 4);
    int w = addUnit(game, 1, WARRIOR, 5, 4);
    // 3 / 6 * 3 * 4.5 = 6.75, so 7 and 3.5 splash
    attack(game, b, 4, 4);
    CHECK(game.units[d].hp == 80);
    CHECK(game.units[w].hp == 65);
}

TEST_CASE("Mind Bender converts instead of damaging") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int m = addUnit(game, 0, MINDBENDER, 3, 3);
    int d = addUnit(game, 1, WARRIOR, 4, 3);
    attack(game, m, 4, 3);
    CHECK(game.units[d].owner == 0);
    CHECK(game.units[d].hp == 100);
    CHECK(game.units[d].home == game.players[0].capital);

    SUBCASE("it goes to the capital, not the Mind Bender's city") {
        int village = game.foundCity(game.index(4, 0), 0);
        game.units[m].home = village;
        int e = addUnit(game, 1, UnitTypes::WARRIOR, 3, 4);
        game.units[m].canAttack = true;
        attack(game, m, 3, 4);
        CHECK(game.units[e].home == game.players[0].capital);
    }
    SUBCASE("without a capital, a random city") {
        int other = game.foundCity(game.index(4, 0), 0);
        game.cities[game.players[0].capital].owner = 1;
        int e = addUnit(game, 1, UnitTypes::WARRIOR, 3, 4);
        game.units[m].canAttack = true;
        attack(game, m, 3, 4);
        CHECK(game.units[e].home == other);
    }
}

TEST_CASE("Cloak infiltrates an adjacent enemy city") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int c = addUnit(game, 0, CLOAK, 5, 5);
    std::vector<int> targets = game.attackTargets(c);
    REQUIRE(targets == std::vector<int>{game.index(6, 6)});
    attack(game, c, 6, 6);
    // the Cloak is used up (its slot may be reused by a Dagger)
    CHECK_FALSE(game.unitAt(game.index(5, 5)));
    // a level-1 city: one Dagger, on the city tile
    const Unit *dagger = game.unitAt(game.index(6, 6));
    REQUIRE(dagger);
    CHECK(dagger->type == DAGGER);
    CHECK(dagger->owner == 0);
    CHECK(game.besieged(game.cityAt(game.index(6, 6))));
}

TEST_CASE("veterans") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int a = addUnit(game, 0, WARRIOR, 3, 3);
    game.units[a].kills = 3;
    game.units[a].hp = 20;
    game.apply({CommandTypes::PROMOTE, game.index(3, 3)});
    CHECK(game.units[a].veteran);
    CHECK(game.units[a].maxHp == 150);
    CHECK(game.units[a].hp == 150);
    CHECK_FALSE(hasCommand(game, {CommandTypes::PROMOTE, game.index(3, 3)}));

    int g = addUnit(game, 0, GIANT, 4, 4);
    game.units[g].kills = 5;
    CHECK_FALSE(hasCommand(game, {CommandTypes::PROMOTE, game.index(4, 4)}));
}

TEST_CASE("healing") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int inside = addUnit(game, 0, WARRIOR, 2, 2);
    int outside = addUnit(game, 0, WARRIOR, 4, 4);
    game.units[inside].hp = 30;
    game.units[outside].hp = 30;

    game.apply({CommandTypes::RECOVER, game.index(2, 2)});
    CHECK(game.units[inside].hp == 70);
    CHECK_FALSE(game.units[inside].canMove);

    // units that could still act recover at the end of the turn
    game.apply({CommandTypes::ENDTURN});
    CHECK(game.units[outside].hp == 50);
    CHECK(game.units[inside].hp == 70);
}
