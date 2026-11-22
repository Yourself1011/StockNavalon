#include "doctest/doctest.h"
#include "helpers.h"

using namespace UnitTypes;
using CommandTypes::ATTACK;

namespace {
void attack(Game &game, int attacker, int x, int y) {
    game.apply({ATTACK, game.units[attacker].tile, game.index(x, y)});
}
} // namespace

TEST_CASE("damage in the game's integer arithmetic: a retaliation of 1.5 is 1.4999, so 1 (game)") {
    // a veteran Archer on 13 of 15 against a Scout carrying a Defender on 13
    // of 15, seen in a game against the real Crazy bot: 6 dealt, 1 back
    Rules::Damage d = Rules::damage(20, 130, 150, 10, 130, 150, 1.0);
    CHECK(d.dealt == 60);
    CHECK(d.returned == 10);
    // exactly, 1.5 rounds up
    CHECK(Rules::damage(20, 130, 150, 10, 130, 150, 1.0, false).returned == 20);
}

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

TEST_CASE("Persist lets a knight attack again after a kill, with another target in range") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int k = addUnit(game, 0, KNIGHT, 3, 3);
    int d = addUnit(game, 1, WARRIOR, 4, 3);
    game.units[d].hp = 10;
    int other = addUnit(game, 1, WARRIOR, 5, 3);
    attack(game, k, 4, 3);
    CHECK(game.units[k].canAttack);
    // none left beside it: no more attacks (PERSIST_NEEDS_TARGET)
    game.units[other].hp = 10;
    attack(game, k, 5, 3);
    CHECK_FALSE(game.units[k].canAttack);
}

TEST_CASE("a melee unit that kills a unit on a port moves on and embarks only where it could embark") {
    for (bool own : {true, false}) {
        CAPTURE(own);
        // a port at (2,2) in player 0's borders, or at (5,6) in player 1's
        Game game = makeGame(fieldMap({"2 2 W", "5 6 W"}));
        clearUnits(game);
        game.players[0].techs.set(TechTypes::FISHING);
        int px = own ? 2 : 5, py = own ? 2 : 6;
        game.tiles[game.index(px, py)].building = ImprovementTypes::PORT;
        int w = addUnit(game, 0, WARRIOR, px - 1, py);
        int d = addUnit(game, 1, WARRIOR, px, py);
        game.units[d].type = RAFT;
        game.units[d].hp = 10;
        attack(game, w, px, py);
        CHECK_FALSE(game.units[d].alive);
        CHECK((game.units[w].tile == game.index(px, py)) == own);
        CHECK(game.units[w].isNaval() == own);
    }
}

TEST_CASE("Persist counts the targets seen from the dead unit's tile before moving in, none if it embarks (game)") {
    Game game = makeGame(fieldMap({"5 3 W"}));
    clearUnits(game);
    game.players[0].techs.set(TechTypes::FISHING);
    game.tiles[game.index(5, 3)].building = ImprovementTypes::PORT;
    game.tiles[game.index(5, 3)].territory = game.players[0].capital;
    int k = addUnit(game, 0, KNIGHT, 4, 3);
    int d = addUnit(game, 1, WARRIOR, 5, 3);
    game.units[d].hp = 10;
    addUnit(game, 1, WARRIOR, 6, 3);
    attack(game, k, 5, 3);
    // it embarked on the port: its turn is over, Persist or not
    CHECK(game.units[k].isNaval());
    CHECK_FALSE(game.units[k].canAttack);
}

TEST_CASE("a melee boat that kills a unit on land lets its passenger off there (game)") {
    Game game = makeGame(fieldMap({"3 3 W"}));
    clearUnits(game);
    int boat = addUnit(game, 0, WARRIOR, 3, 3);
    game.units[boat].type = RAMMER;
    int d = addUnit(game, 1, WARRIOR, 4, 3);
    game.units[d].hp = 10;
    attack(game, boat, 4, 3);
    CHECK(game.units[boat].tile == game.index(4, 3));
    CHECK(game.units[boat].type == WARRIOR);
    CHECK_FALSE(game.units[boat].canMove);
    CHECK(game.tiles[game.index(3, 3)].unit < 0);
}

TEST_CASE("splash does half of an attack on each victim, floored") {
    Game game = makeGame(fieldMap({"2 4 W"}));
    clearUnits(game);
    int b = addUnit(game, 0, WARRIOR, 2, 4);
    game.units[b].type = BOMBER;
    int d = addUnit(game, 1, DEFENDER, 4, 4);
    int w = addUnit(game, 1, WARRIOR, 5, 4);
    // 3 / 6 * 3 * 4.5 = 6.75, so 7 to the Defender; an attack on the
    // Warrior would deal 3 / 5 * 3 * 4.5 = 8.1, so 8, half of it 4
    // (AREA_DAMAGE_FLOORED)
    attack(game, b, 4, 4);
    CHECK(game.units[d].hp == 80);
    CHECK(game.units[w].hp == 60);
    // the Bomber ends facing its last splash victim
    CHECK(game.units[b].facing == Directions::nearest(3, 0));
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

TEST_CASE("no infiltrating a city one's own unit stands on (game)") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int c = addUnit(game, 0, CLOAK, 5, 5);
    addUnit(game, 0, WARRIOR, 6, 6);
    CHECK(game.attackTargets(c).empty());
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

TEST_CASE("infiltration as the game does it: rebellion stars, a robbed city, Daggers placed and the defender hit (game)") {
    SUBCASE("an empty city: one Dagger pushed off by the next") {
        Game game = makeGame(fieldMap());
        clearUnits(game);
        int city = game.cityAt(game.index(6, 6));
        game.cities[city].level = 2;
        int stars = game.players[0].stars;
        int c = addUnit(game, 0, CLOAK, 5, 5);
        attack(game, c, 6, 6);
        // level 2 + the original capital's 1
        CHECK(game.players[0].stars == stars + 3);
        CHECK(game.cities[city].robbed);
        CHECK(game.cityIncome(city) == 0);
        const Unit *onCity = game.unitAt(game.index(6, 6));
        REQUIRE(onCity);
        CHECK(onCity->type == DAGGER);
        int daggers = 0;
        for (const Unit &u : game.units) {
            daggers += u.alive && u.type == DAGGER;
        }
        CHECK(daggers == 2);
        // a robbed city can't be infiltrated again until its owner's turn ends
        int again = addUnit(game, 0, CLOAK, 7, 7);
        CHECK(game.attackTargets(again).empty());
        game.apply({CommandTypes::ENDTURN});
        game.apply({CommandTypes::ENDTURN});
        CHECK_FALSE(game.cities[city].robbed);
    }
    SUBCASE("a defended city: the defender is hit as by a fresh Cloak, the Daggers face the city") {
        Game game = makeGame(fieldMap());
        clearUnits(game);
        int city = game.cityAt(game.index(6, 6));
        game.cities[city].level = 2;
        int defender = addUnit(game, 1, SWORDSMAN, 6, 6);
        int c = addUnit(game, 0, CLOAK, 5, 5);
        game.units[c].hp = 10;  // its own health doesn't matter
        attack(game, c, 6, 6);
        // 2 / (2 + 3) * 2 * 4.5 = 3.6, so 4
        CHECK(game.units[defender].hp == 110);
        for (const Unit &u : game.units) {
            if (u.alive && u.type == DAGGER) {
                CHECK(u.tile != game.index(6, 6));
                CHECK(u.facing == Directions::nearest(6 - game.tileX(u.tile), 6 - game.tileY(u.tile)));
            }
        }
    }
}

TEST_CASE("a boat whose passenger has 3 kills can be promoted (game)") {
    Game game = makeGame(fieldMap({"3 3 W"}));
    clearUnits(game);
    int boat = addUnit(game, 0, WARRIOR, 3, 3);
    game.units[boat].type = RAFT;
    game.units[boat].kills = 3;
    CHECK(hasCommand(game, {CommandTypes::PROMOTE, game.index(3, 3)}));
    game.settings.rulesVersion = RuleVersions::BOATS_PROMOTE_PASSENGER - 1;
    CHECK_FALSE(hasCommand(game, {CommandTypes::PROMOTE, game.index(3, 3)}));
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
