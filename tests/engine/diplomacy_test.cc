#include "doctest/doctest.h"
#include "helpers.h"
#include "view.h"

using namespace CommandTypes;
using namespace UnitTypes;

namespace {
void makePeace(Game &game) {
    game.players[0].techs.set(TechTypes::STRATEGY);
    game.apply({PEACETREATY, -1, -1, 1});
    game.apply({ENDTURN});
    game.apply({PEACEREQUESTRESPONSE, -1, -1, 0 * 2 + 1});
    game.apply({ENDTURN});
}

bool canReach(const Game &game, int unit, int x, int y) {
    std::vector<int> moves = game.moveTargets(unit);
    return std::find(moves.begin(), moves.end(), game.index(x, y)) !=
           moves.end();
}
} // namespace

TEST_CASE("proposing and answering peace") {
    Game game = makeGame(bigMap());
    // proposing needs Strategy
    CHECK_FALSE(hasCommand(game, {PEACETREATY, -1, -1, 1}));
    game.players[0].techs.set(TechTypes::STRATEGY);
    REQUIRE(hasCommand(game, {PEACETREATY, -1, -1, 1}));
    game.apply({PEACETREATY, -1, -1, 1});
    // asked once
    CHECK_FALSE(hasCommand(game, {PEACETREATY, -1, -1, 1}));
    game.apply({ENDTURN});

    SUBCASE("accepted") {
        REQUIRE(hasCommand(game, {PEACEREQUESTRESPONSE, -1, -1, 1}));
        game.apply({PEACEREQUESTRESPONSE, -1, -1, 1});
        CHECK(game.atPeace(0, 1));
        CHECK(game.atPeace(1, 0));
        CHECK(hasCommand(game, {BREAKPEACE, -1, -1, 0}));
    }
    SUBCASE("declined") {
        game.apply({PEACEREQUESTRESPONSE, -1, -1, 0});
        CHECK_FALSE(game.atPeace(0, 1));
        CHECK_FALSE(hasCommand(game, {PEACEREQUESTRESPONSE, -1, -1, 1}));
    }
}

TEST_CASE("allies can't attack, siege or capture each other") {
    Game game = makeGame(bigMap());
    clearUnits(game);
    makePeace(game);
    int w = addUnit(game, 0, WARRIOR, 8, 8);
    addUnit(game, 1, WARRIOR, 7, 8);
    CHECK(game.attackTargets(w).empty());

    game.tiles[game.index(8, 8)].unit = -1;
    game.units[w].tile = game.index(9, 9);
    game.tiles[game.index(9, 9)].unit = w;
    CHECK_FALSE(game.besieged(game.cityAt(game.index(9, 9))));
    CHECK_FALSE(hasCommand(game, {CAPTURE, game.index(9, 9)}));
}

TEST_CASE("allied units neither block nor exert zone of control") {
    // (5, 5) is the only way from (4, 5) to (6, 5)
    Game game = makeGame(bigMap({"5 4 M", "5 6 M"}));
    clearUnits(game);
    int rider = addUnit(game, 0, RIDER, 4, 5);
    addUnit(game, 1, WARRIOR, 5, 5);
    CHECK_FALSE(canReach(game, rider, 6, 5));
    makePeace(game);
    CHECK(canReach(game, rider, 6, 5));
}

TEST_CASE("allies use each other's roads") {
    // roads from (5, 9) to player 1's capital; (8, 9) is in their borders
    Game game = makeGame(bigMap({"5 9 F=", "6 9 F=", "7 9 F=", "8 9 F="}));
    clearUnits(game);
    int rider = addUnit(game, 0, RIDER, 5, 9);
    CHECK_FALSE(canReach(game, rider, 9, 9));
    makePeace(game);
    CHECK(canReach(game, rider, 9, 9));
}

TEST_CASE("breaking peace freezes the breaker and disbands units in the "
          "other's borders") {
    Game game = makeGame(bigMap());
    clearUnits(game);
    makePeace(game);
    int inside = addUnit(game, 0, WARRIOR, 8, 8);
    int outside = addUnit(game, 0, WARRIOR, 5, 5);
    int stars = game.players[0].stars;
    game.apply({BREAKPEACE, -1, -1, 1});
    CHECK_FALSE(game.atPeace(0, 1));
    CHECK_FALSE(game.units[inside].alive);
    CHECK(game.units[outside].alive);
    CHECK_FALSE(game.units[outside].canMove);
    CHECK_FALSE(game.units[outside].canAttack);
    CHECK(game.players[0].stars == stars);
}

TEST_CASE("embassies") {
    Game game = makeFogGame(bigMap());
    Player &player = game.players[0];
    player.techs.set(TechTypes::DIPLOMACY);
    player.stars = 10;
    // only with a tribe you've met
    CHECK_FALSE(hasCommand(game, {ESTABLISHEMBASSY, -1, -1, 1}));
    player.met = ~std::uint32_t{0};
    REQUIRE(hasCommand(game, {ESTABLISHEMBASSY, -1, -1, 1}));

    int before = game.income(0), hostBefore = game.income(1);
    game.apply({ESTABLISHEMBASSY, -1, -1, 1});
    CHECK(player.stars == 5);
    CHECK(player.hasEmbassyWith(1));
    CHECK_FALSE(hasCommand(game, {ESTABLISHEMBASSY, -1, -1, 1}));
    // +2 a turn for both
    CHECK(game.income(0) == before + 2);
    CHECK(game.income(1) == hostBefore + 2);
    // the builder sees the 3x3 around the host's capital
    CHECK(game.explored(0, game.index(10, 10)));

    SUBCASE("+4 each with a peace treaty") {
        game.players[0].peace = game.players[1].peace = ~std::uint32_t{0};
        CHECK(game.income(0) == before + 4);
        CHECK(game.income(1) == hostBefore + 4);
    }
    SUBCASE("attacking the tribe destroys it") {
        clearUnits(game);
        addUnit(game, 0, WARRIOR, 5, 5);
        addUnit(game, 1, WARRIOR, 6, 5);
        game.reveal(0, game.index(6, 5));
        game.apply({ATTACK, game.index(5, 5), game.index(6, 5)});
        CHECK_FALSE(player.hasEmbassyWith(1));
    }
    SUBCASE("the host can destroy it") {
        game.apply({ENDTURN});
        REQUIRE(hasCommand(game, {DESTROYEMBASSY, -1, -1, 0}));
        game.apply({DESTROYEMBASSY, -1, -1, 0});
        CHECK_FALSE(player.hasEmbassyWith(1));
    }
    SUBCASE("it goes when the capital is captured") {
        clearUnits(game);
        addUnit(game, 0, WARRIOR, 9, 9);
        int other = game.foundCity(game.index(9, 5), 1);
        (void)other;
        game.apply({CAPTURE, game.index(9, 9)});
        CHECK_FALSE(player.hasEmbassyWith(1));
    }
}

TEST_CASE("Capital Vision shows the capitals of tribes you've met") {
    Game game = makeFogGame(bigMap());
    Player &player = game.players[0];
    player.met = ~std::uint32_t{0};
    game.apply({ENDTURN});
    game.apply({ENDTURN});
    CHECK_FALSE(game.explored(0, game.index(9, 9)));
    player.techs.set(TechTypes::DIPLOMACY);
    game.apply({ENDTURN});
    CHECK(game.explored(0, game.index(9, 9)));
}

TEST_CASE("a shared neutral road only gives new connections to the first "
          "player in seat order") {
    // player 1's city at (9, 5) reaches its capital over roads at (9, 6),
    // (9, 7) and (9, 8); (9, 7) is neutral
    Game game = makeGame(bigMap({"9 6 F=", "9 7 F=", "9 8 F="}));
    int city = game.foundCity(game.index(9, 5), 1);

    SUBCASE("unshared, it connects") {
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == 1);
    }
    SUBCASE("touching player 0's borders too, it doesn't") {
        // its borders take (8, 7), next to the neutral road
        game.foundCity(game.index(7, 7), 0);
        REQUIRE(game.tileOwner(game.index(8, 7)) == 0);
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == -1);
    }
    SUBCASE("a connection that already exists stays") {
        game.updateAllConnections();
        REQUIRE(game.cities[city].connectedFor == 1);
        game.foundCity(game.index(7, 7), 0);
        game.updateAllConnections();
        CHECK(game.cities[city].connectedFor == 1);
    }
}

TEST_CASE("the player view has the player's tasks and diplomacy") {
    Game game = makeGame(bigMap());
    Player &me = game.players[0];
    me.techs.set(TechTypes::STRATEGY);
    me.techs.set(TechTypes::DIPLOMACY);
    me.kills = 3;
    me.tasksDone.set(TaskTypes::KILLER);
    game.apply({PEACETREATY, -1, -1, 1});
    game.apply({ESTABLISHEMBASSY, -1, -1, 1});

    PlayerView mine = makeView(game, 0);
    // their own score live, the other's as published at their last turn end
    CHECK(mine.scores ==
          std::vector<int>{game.score(0), game.publishedScore(1)});
    CHECK(mine.kills == 3);
    CHECK(mine.tasksDone.test(TaskTypes::KILLER));
    CHECK_FALSE(mine.monumentsBuilt.test(TaskTypes::KILLER));
    CHECK(mine.relations[1].weAsked);
    CHECK_FALSE(mine.relations[1].theyAsked);
    CHECK(mine.relations[1].ourEmbassy);
    CHECK_FALSE(mine.relations[1].peace);

    PlayerView theirs = makeView(game, 1);
    CHECK(theirs.relations[0].theyAsked);
    CHECK(theirs.relations[0].theirEmbassy);
    // their own tasks, not player 0's
    CHECK(theirs.kills == 0);
    CHECK_FALSE(theirs.tasksDone.test(TaskTypes::KILLER));

    game.apply({ENDTURN});
    game.apply({PEACEREQUESTRESPONSE, -1, -1, 1});
    CHECK(makeView(game, 0).relations[1].peace);
    CHECK(makeView(game, 1).relations[0].peace);
}

TEST_CASE("without peace treaties, none can be proposed, answered or broken") {
    Game game = makeGame(bigMap());
    game.settings.peaceTreaties = false;
    game.players[0].techs.set(TechTypes::STRATEGY);
    game.players[0].techs.set(TechTypes::DIPLOMACY);
    game.players[0].stars = 100;
    auto treaties = [&] {
        int n = 0;
        for (const Command &c : game.legalCommands()) {
            n += c.type == PEACETREATY || c.type == PEACEREQUESTRESPONSE ||
                 c.type == BREAKPEACE;
        }
        return n;
    };
    CHECK(treaties() == 0);
    // a request from before is left unanswerable, and embassies still work
    game.players[0].peaceRequests |= 1u << 1;
    CHECK(treaties() == 0);
    CHECK(hasCommand(game, {ESTABLISHEMBASSY, -1, -1, 1}));
}

TEST_CASE("an embassy or a peace offer reveals the sender to the other tribe") {
    for (CommandType type : {ESTABLISHEMBASSY, PEACETREATY}) {
        CAPTURE(type);
        Game game = makeFogGame(bigMap());
        Player &sender = game.players[0];
        sender.techs.set(TechTypes::DIPLOMACY);
        sender.techs.set(TechTypes::STRATEGY);
        sender.stars = 10;
        // the sender has met them, but not the other way round
        sender.met |= std::uint32_t{1} << 1;
        REQUIRE_FALSE(game.players[1].hasMet(0));
        int stars = game.players[1].stars;
        game.apply({type, -1, -1, 1});
        CHECK(game.players[1].hasMet(0));
        // with the meeting stars
        CHECK(game.players[1].stars == stars + Rules::meetingBonus(game.score(0)));
        // and once only
        game.players[1].stars = stars;
        game.meet(1, 0);
        CHECK(game.players[1].stars == stars);
    }
    // replaying a game recorded before the rule: no meeting
    Game game = makeFogGame(bigMap());
    game.settings.diplomacyMeets = false;
    game.players[0].techs.set(TechTypes::DIPLOMACY);
    game.players[0].stars = 10;
    game.players[0].met |= std::uint32_t{1} << 1;
    game.apply({ESTABLISHEMBASSY, -1, -1, 1});
    CHECK_FALSE(game.players[1].hasMet(0));
}
