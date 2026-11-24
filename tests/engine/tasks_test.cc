#include "doctest/doctest.h"
#include "helpers.h"

using namespace TaskTypes;
using CommandTypes::BUILD;
using CommandTypes::ENDTURN;

TEST_CASE("Wealth: 100 stars with Trade unlocks the Emperor's Tomb") {
    Game game = makeGame(bigMap());
    Player &player = game.players[0];
    player.stars = 100;
    game.updateTasks();
    CHECK_FALSE(player.tasksDone.test(WEALTH));

    player.techs.set(TechTypes::TRADE);
    game.apply({ENDTURN});
    game.apply({ENDTURN});
    CHECK(player.tasksDone.test(WEALTH));

    int capital = player.capital;
    int score = game.score(0);
    Command tomb{BUILD, game.index(3, 3), -1, ImprovementTypes::EMPERORSTOMB};
    REQUIRE(hasCommand(game, tomb));
    // nothing else unlocked
    CHECK_FALSE(hasCommand(game, {BUILD, game.index(3, 3), -1,
                                  ImprovementTypes::ALTAROFPEACE}));
    game.apply(tomb);
    // +3 population levels the capital up: worth 400, plus the city's
    // growth; the third point waits on the reward (POPULATION_STACK)
    CHECK(game.cities[capital].level == 2);
    CHECK(game.score(0) == score + 400 + 50);
    CHECK(player.monumentsBuilt.test(WEALTH));
    // once only; it can be destroyed (game), but not before the rule
    game.apply({CommandTypes::CITYREWARD, game.cities[capital].tile, -1,
                CityRewards::WORKSHOP});
    CHECK_FALSE(hasCommand(game, {BUILD, game.index(1, 3), -1,
                                  ImprovementTypes::EMPERORSTOMB}));
    player.techs.set(TechTypes::CHIVALRY);
    CHECK(game.canDestroy(0, game.index(3, 3)));
    game.settings.rulesVersion = RuleVersions::DESTROY_MONUMENTS - 1;
    CHECK_FALSE(game.canDestroy(0, game.index(3, 3)));
}

TEST_CASE("the other tasks") {
    Game game = makeGame(bigMap());
    Player &player = game.players[0];

    SUBCASE("Killer: 10 enemy units") {
        player.kills = 9;
        game.updateTasks();
        CHECK_FALSE(player.tasksDone.test(KILLER));
        player.kills = 10;
        game.updateTasks();
        CHECK(player.tasksDone.test(KILLER));
    }
    SUBCASE("Explorer: all 4 lighthouses") {
        player.lighthousesFound = 4;
        game.updateTasks();
        CHECK(player.tasksDone.test(EXPLORER));
    }
    SUBCASE("Metropolis: a city at level 5") {
        game.cities[player.capital].level = 5;
        game.updateTasks();
        CHECK(player.tasksDone.test(METROPOLIS));
    }
    SUBCASE("Genius: every tech") {
        player.techs.set();
        game.updateTasks();
        CHECK(player.tasksDone.test(GENIUS));
    }
    SUBCASE("Network: 5 cities connected to the capital") {
        for (int i = 0; i < 5; i++) {
            int city = game.foundCity(game.index(1 + 2 * i, 7), 0);
            game.cities[city].connectedFor = i < 4 ? 0 : -1;
        }
        game.updateTasks();
        CHECK_FALSE(player.tasksDone.test(NETWORK));
        game.cities.back().connectedFor = 0;
        game.updateTasks();
        CHECK(player.tasksDone.test(NETWORK));
    }
}

TEST_CASE("kills count for Killer, by attack or retaliation") {
    Game game = makeGame(bigMap());
    clearUnits(game);
    int a = addUnit(game, 0, UnitTypes::WARRIOR, 5, 5);
    int d = addUnit(game, 1, UnitTypes::WARRIOR, 6, 5);
    game.units[d].hp = 10;
    game.apply({CommandTypes::ATTACK, game.index(5, 5), game.index(6, 5)});
    CHECK(game.players[0].kills == 1);
    CHECK(game.units[a].kills == 1);
}

TEST_CASE("Pacifist: a conversion doesn't start the count over (game)") {
    Game game = makeGame(bigMap());
    clearUnits(game);
    Player &player = game.players[0];
    player.techs.set(TechTypes::MEDITATION);
    player.peacefulTurns = 3;
    addUnit(game, 0, UnitTypes::MINDBENDER, 5, 5);
    addUnit(game, 1, UnitTypes::WARRIOR, 6, 5);
    game.apply({CommandTypes::ATTACK, game.index(5, 5), game.index(6, 5)});
    CHECK(game.unitAt(game.index(6, 5))->owner == 0);
    CHECK(player.peacefulTurns == 3);
}

TEST_CASE("Pacifist: a stomp on a hidden unit doesn't start the count over (game)") {
    for (bool hidden : {true, false}) {
        CAPTURE(hidden);
        // water down x = 3, a Juggernaut on it, an enemy beside its next tile
        std::vector<std::string> water;
        for (int y = 0; y < 8; y++) {
            water.push_back("3 " + std::to_string(y) + " W");
        }
        Game game = makeGame(bigMap(water));
        clearUnits(game);
        Player &player = game.players[0];
        player.techs.set(TechTypes::MEDITATION);
        player.peacefulTurns = 3;
        int jug = addUnit(game, 0, UnitTypes::GIANT, 3, 3);
        game.units[jug].type = UnitTypes::JUGGERNAUT;
        int enemy = addUnit(game, 1, hidden ? UnitTypes::CLOAK : UnitTypes::WARRIOR, 2, 5);
        game.units[enemy].hidden = hidden;
        int hp = game.units[enemy].hp;
        game.apply({CommandTypes::MOVE, game.index(3, 3), game.index(3, 4)});
        CHECK(game.units[enemy].hp < hp);
        CHECK(player.peacefulTurns == (hidden ? 3 : 0));
    }
}

TEST_CASE("Pacifist: 5 turns without attacking after Meditation") {
    Game game = makeGame(bigMap());
    clearUnits(game);
    Player &player = game.players[0];
    player.techs.set(TechTypes::MEDITATION);
    int warrior = addUnit(game, 0, UnitTypes::WARRIOR, 5, 5);
    int target = addUnit(game, 1, UnitTypes::DEFENDER, 6, 5);
    for (int turn = 0; turn < 4; turn++) {
        game.apply({ENDTURN});
        game.apply({ENDTURN});
    }
    CHECK(player.peacefulTurns == 4);

    // an attack starts the count over there and then; the turn's end still
    // counts (game)
    game.apply({CommandTypes::ATTACK, game.index(5, 5), game.index(6, 5)});
    CHECK(player.peacefulTurns == 0);
    game.apply({ENDTURN});
    game.apply({ENDTURN});
    CHECK(player.peacefulTurns == 1);
    for (int turn = 0; turn < 3; turn++) {
        game.apply({ENDTURN});
        game.apply({ENDTURN});
    }
    CHECK_FALSE(player.tasksDone.test(PACIFIST));
    for (int turn = 0; turn < 1; turn++) {
        game.apply({ENDTURN});
        game.apply({ENDTURN});
    }
    CHECK(player.tasksDone.test(PACIFIST));
    (void)warrior;
    (void)target;
}
