#include "doctest/doctest.h"
#include "helpers.h"
#include "view.h"

using namespace TribeTypes;
using CommandTypes::EXAMINERUINS;
using CommandTypes::MOVE;

TEST_CASE("starting scores match GAME.md section 3") {
    struct Case {
        Tribe tribe;
        int score;
    };
    for (Case c : {Case{XINXI, 515}, Case{IMPERIUS, 515}, Case{BARDUR, 515},
                   Case{KICKOO, 515}, Case{LUXIDOOR, 515}, Case{OUMAJI, 520},
                   Case{ZEBASI, 615}, Case{YADAKK, 615}, Case{QUETZALI, 620},
                   Case{HOODRICK, 620}, Case{VENGIR, 730}, Case{AIMO, 730}}) {
        CAPTURE(tribeData[c.tribe].name);
        Game game = makeFogGame(bigMap(), {c.tribe, BARDUR});
        CHECK(game.score(0) == c.score);
    }
}

TEST_CASE("score follows cities, temples, techs and units") {
    Game game = makeFogGame(bigMap());
    int start = game.score(0);
    int capital = game.players[0].capital;

    SUBCASE("each level above 1 is 50, each population point 5") {
        game.addPopulation(capital, 1);
        CHECK(game.score(0) == start + 5);
        game.addPopulation(capital, 1);
        REQUIRE(game.cities[capital].level == 2);
        CHECK(game.score(0) == start + 50);
    }
    SUBCASE("parks are 250") {
        game.cities[capital].parks = 1;
        CHECK(game.score(0) == start + 250);
    }
    SUBCASE("100 a tier of each tech") {
        game.players[0].techs.set(TechTypes::TRADE);
        CHECK(game.score(0) == start + 300);
    }
    SUBCASE("temples gain 100 a level every 2 turns, up to 5") {
        game.tiles[game.index(3, 3)].building = ImprovementTypes::TEMPLE;
        game.tiles[game.index(3, 3)].builtTurn = game.turn;
        CHECK(game.score(0) == start + 100);
        game.turn += 2;
        CHECK(game.score(0) == start + 200);
        game.turn += 20;
        CHECK(game.score(0) == start + 500);
    }
    SUBCASE("units count for whoever first had them") {
        clearUnits(game);
        CHECK(game.score(0) == start - 10);
        int giant = addUnit(game, 1, UnitTypes::GIANT, 5, 5);
        int before = game.score(1);
        game.units[giant].owner = 0; // converted
        CHECK(game.score(1) == before);
        game.units[giant].alive = false;
        game.tiles[game.index(5, 5)].unit = -1;
        CHECK(game.score(1) == before - 50);
    }
}

TEST_CASE("the star bonus for meeting a tribe") {
    CHECK(Rules::meetingBonus(515) == 3);
    CHECK(Rules::meetingBonus(1000) == 3);
    CHECK(Rules::meetingBonus(1001) == 5);
    CHECK(Rules::meetingBonus(4500) == 11);
    CHECK(Rules::meetingBonus(9000) == 11);

    Game game = makeFogGame(bigMap());
    clearUnits(game);
    int stars = game.players[0].stars;
    // hidden, so player 1 can't see it back
    int cloak = addUnit(game, 0, UnitTypes::CLOAK, 6, 8);
    game.units[cloak].hidden = true;

    // from (6, 8) it sees (8, 8) inside player 1's borders, but a border tile
    // isn't enough
    game.checkMeetings();
    CHECK_FALSE(game.players[0].hasMet(1));

    // from (7, 9) it sees their city at (9, 9)
    game.tiles[game.index(6, 8)].unit = -1;
    game.tiles[game.index(7, 9)].unit = cloak;
    game.units[cloak].tile = game.index(7, 9);
    game.revealForUnit(cloak);
    int theirScore = game.score(1);
    game.checkMeetings();
    CHECK(game.players[0].hasMet(1));
    CHECK(game.players[0].stars == stars + Rules::meetingBonus(theirScore));
    // only the player who saw the other meets it
    CHECK_FALSE(game.players[1].hasMet(0));

    game.checkMeetings();
    CHECK(game.players[0].stars == stars + Rules::meetingBonus(theirScore));
}

TEST_CASE("hidden units don't count for meeting") {
    Game game = makeFogGame(bigMap());
    clearUnits(game);
    int cloak = addUnit(game, 1, UnitTypes::CLOAK, 4, 3);
    game.units[cloak].hidden = true;
    game.checkMeetings();
    CHECK_FALSE(game.players[0].hasMet(1));
    game.units[cloak].hidden = false;
    game.checkMeetings();
    CHECK(game.players[0].hasMet(1));
}

TEST_CASE("examining a ruin") {
    Game game = makeGame(bigMap({"4 4 FR"}));
    clearUnits(game);
    int unit = addUnit(game, 0, UnitTypes::DEFENDER, 3, 4);
    game.apply({MOVE, game.index(3, 4), game.index(4, 4)});
    Command examine{EXAMINERUINS, game.index(4, 4)};
    // not on the turn it got there
    CHECK_FALSE(hasCommand(game, examine));
    game.apply({CommandTypes::ENDTURN});
    game.apply({CommandTypes::ENDTURN});
    REQUIRE(hasCommand(game, examine));
    game.apply(examine);
    CHECK(game.tiles[game.index(4, 4)].building == ImprovementTypes::NONE);
    CHECK_FALSE(game.units[unit].canMove);
    CHECK_FALSE(game.units[unit].canAttack);
    CHECK_FALSE(hasCommand(game, examine));
}

TEST_CASE("ruin rewards and their conditions") {
    Game game = makeGame(bigMap({"5 5 FR", "7 2 OR", "6 1 W", "7 1 W", "8 1 W",
                                 "6 2 W", "8 2 W", "6 3 W", "7 3 W", "8 3 W"}));
    clearUnits(game);
    int unit = addUnit(game, 0, UnitTypes::WARRIOR, 5, 5);
    using G = Game;

    // everything is explored, so there's no Explorer
    CHECK(game.ruinRewards(unit) ==
          std::vector<G::RuinReward>{G::RUIN_TECH, G::RUIN_STARS,
                                     G::RUIN_POPULATION, G::RUIN_UNIT});

    SUBCASE("stars") {
        int stars = game.players[0].stars;
        game.giveRuinReward(unit, G::RUIN_STARS);
        CHECK(game.players[0].stars == stars + 10);
    }
    SUBCASE("a free researchable tech") {
        TechSet before = game.players[0].techs;
        game.giveRuinReward(unit, G::RUIN_TECH);
        CHECK(game.players[0].techs.count() == before.count() + 1);
        CHECK(game.players[0].stars == 5);
    }
    SUBCASE("population in the capital") {
        game.giveRuinReward(unit, G::RUIN_POPULATION);
        CHECK(game.cities[game.players[0].capital].level == 2);
        CHECK(game.cities[game.players[0].capital].population == 1);
    }
    SUBCASE("a veteran Swordsman on the ruin, not counting toward the cap") {
        int capital = game.players[0].capital;
        int count = game.unitCount(capital);
        game.giveRuinReward(unit, G::RUIN_UNIT);
        const Unit *swordsman = game.unitAt(game.index(5, 5));
        REQUIRE(swordsman);
        CHECK(swordsman->type == UnitTypes::SWORDSMAN);
        CHECK(swordsman->veteran);
        CHECK(swordsman->hp == 200);
        CHECK(swordsman->home == capital);
        CHECK(game.unitCount(capital) == count);
        // the examiner never moved, so it's pushed toward the centre
        CHECK(game.units[unit].tile == game.index(6, 6));
    }
    SUBCASE("the examiner is pushed onward the way it was moving") {
        game.units[unit].lastDx = -1;
        game.units[unit].lastDy = 0;
        game.giveRuinReward(unit, G::RUIN_UNIT);
        CHECK(game.units[unit].tile == game.index(4, 5));
    }
    SUBCASE("on water, a veteran Rammer carrying a Warrior") {
        int boat = addUnit(game, 0, UnitTypes::WARRIOR, 7, 2);
        game.units[boat].type = UnitTypes::RAFT;
        game.giveRuinReward(boat, G::RUIN_UNIT);
        const Unit *rammer = game.unitAt(game.index(7, 2));
        REQUIRE(rammer);
        CHECK(rammer->type == UnitTypes::RAMMER);
        CHECK(rammer->passenger == UnitTypes::WARRIOR);
        CHECK(rammer->hp == 150);
        // the raft is pushed onto water, toward the centre
        CHECK(game.units[boat].tile == game.index(6, 3));
    }
}

TEST_CASE("a pushed examiner goes further out when nearby tiles are blocked") {
    // the ruin is a forest tile ringed by mountains, without Climbing
    std::vector<std::string> ring;
    for (int y = 4; y <= 6; y++) {
        for (int x = 4; x <= 6; x++) {
            if (x != 5 || y != 5) {
                ring.push_back(std::to_string(x) + " " + std::to_string(y) +
                               " M");
            }
        }
    }
    ring.push_back("5 5 TR");
    Game game = makeGame(bigMap(ring));
    clearUnits(game);
    int unit = addUnit(game, 0, UnitTypes::WARRIOR, 5, 5);
    game.giveRuinReward(unit, Game::RUIN_UNIT);
    REQUIRE(game.unitAt(game.index(5, 5)));
    CHECK(game.unitAt(game.index(5, 5))->type == UnitTypes::SWORDSMAN);
    // toward the centre, two tiles out
    CHECK(game.units[unit].alive);
    CHECK(game.units[unit].tile == game.index(7, 7));
}

TEST_CASE("an Explorer from a ruin needs fog nearby") {
    Game game = makeFogGame(bigMap({"5 2 FR"}));
    clearUnits(game);
    int unit = addUnit(game, 0, UnitTypes::WARRIOR, 5, 2);
    std::vector<Game::RuinReward> rewards = game.ruinRewards(unit);
    CHECK(std::find(rewards.begin(), rewards.end(), Game::RUIN_EXPLORER) !=
          rewards.end());
}

TEST_CASE("others see a score only when its player ends their turn") {
    Game game = makeFogGame(bigMap());
    int start = game.score(0);
    // everyone starts out seeing the starting scores
    CHECK(makeView(game, 1).scores[0] == start);
    REQUIRE(game.scoreHistory.size() == 2);

    // research changes player 0's live score, which only they see
    game.apply({CommandTypes::RESEARCH, -1, -1, TechTypes::RIDING});
    CHECK(game.score(0) == start + 100);
    CHECK(makeView(game, 0).scores[0] == start + 100);
    CHECK(makeView(game, 1).scores[0] == start);
    // with fog off, everything is live
    CHECK(makeView(game, 1, true).scores[0] == start + 100);

    game.apply({CommandTypes::ENDTURN});
    CHECK(makeView(game, 1).scores[0] == start + 100);
    REQUIRE(game.scoreHistory.size() == 3);
    CHECK(game.scoreHistory.back() == ScoreRecord{0, 0, start + 100});

    // player 1's own changes are live for them, hidden from player 0
    int theirs = game.score(1);
    game.players[1].techs.set(TechTypes::TRADE);
    CHECK(makeView(game, 1).scores[1] == theirs + 300);
    CHECK(makeView(game, 0).scores[1] == theirs);
    game.apply({CommandTypes::ENDTURN});
    CHECK(makeView(game, 0).scores[1] == theirs + 300);
    CHECK(makeView(game, 0).scoreHistory == game.scoreHistory);
}

TEST_CASE("a temple's level and next level-up are visible to everyone") {
    Game game = makeGame(bigMap());
    int tile = game.index(3, 3);
    game.tiles[tile].building = ImprovementTypes::TEMPLE;
    game.tiles[tile].builtTurn = game.turn;
    for (int player : {0, 1}) {
        PlayerView::TileView t = makeView(game, player).tiles[tile];
        CHECK(t.level == 1);
        CHECK(t.turnsToLevelUp == 2);
    }
    game.turn += 3;
    CHECK(makeView(game, 1).tiles[tile].level == 2);
    CHECK(makeView(game, 1).tiles[tile].turnsToLevelUp == 1);
    game.turn += 20;
    CHECK(makeView(game, 1).tiles[tile].level == 5);
    CHECK(makeView(game, 1).tiles[tile].turnsToLevelUp == 0);
}

TEST_CASE("a player knows where their units have stood, and when they were attacked") {
    Game game = makeGame(fieldMap());
    int capital = game.index(1, 1);
    PlayerView view = makeView(game, 0);
    CHECK(view.firstOccupied[capital] == 0);
    CHECK(view.firstOccupied[game.index(6, 6)] == -1);
    CHECK(view.attackedOnTurns.empty());

    clearUnits(game);
    int warrior = addUnit(game, 0, UnitTypes::WARRIOR, 3, 3);
    game.apply({MOVE, game.index(3, 3), game.index(4, 4)});
    CHECK(makeView(game, 0).firstOccupied[game.index(4, 4)] == 0);
    CHECK(makeView(game, 1).firstOccupied[game.index(4, 4)] == -1);

    // player 1 attacks it on their turn 0
    game.apply({CommandTypes::ENDTURN});
    int theirs = addUnit(game, 1, UnitTypes::WARRIOR, 5, 5);
    game.apply({CommandTypes::ATTACK, game.units[theirs].tile,
                game.units[warrior].tile});
    CHECK(makeView(game, 0).attackedOnTurns == std::vector<int>{0});
    CHECK(makeView(game, 1).attackedOnTurns.empty());
}
