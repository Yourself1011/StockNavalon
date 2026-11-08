#include "doctest/doctest.h"
#include "helpers.h"

using namespace TechTypes;
using CommandTypes::BUILD;
using CommandTypes::CITYREWARD;
using CommandTypes::ENDTURN;

namespace {
// picks the first reward wherever one is waiting
void takeRewards(Game &game) {
    while (true) {
        std::vector<Command> legal = game.legalCommands();
        if (legal.empty() || legal[0].type != CITYREWARD) {
            return;
        }
        game.apply(legal[0]);
    }
}

void build(Game &game, int x, int y, ImprovementTypes::ImprovementType type) {
    game.apply({BUILD, game.index(x, y), -1, type});
    takeRewards(game);
}
} // namespace

TEST_CASE("data tables are indexed by their enums") {
    for (std::size_t i = 0; i < techData.size(); i++) {
        CHECK(techData[i].type == static_cast<int>(i));
    }
    for (std::size_t i = 0; i < unitData.size(); i++) {
        CHECK(unitData[i].type == static_cast<int>(i));
    }
    for (std::size_t i = 0; i < improvementData.size(); i++) {
        CHECK(improvementData[i].type == static_cast<int>(i));
    }
    for (std::size_t i = 0; i < tribeData.size(); i++) {
        CHECK(tribeData[i].tribe == static_cast<int>(i));
    }
}

TEST_CASE("tribe starts") {
    Game game = makeGame(fieldMap(), {TribeTypes::LUXIDOOR, TribeTypes::VENGIR});
    CHECK(game.players[0].stars == 2);
    CHECK(game.cities[game.players[0].capital].level == 3);
    CHECK(game.cities[game.players[0].capital].pendingRewards == 0);
    CHECK(game.players[1].stars == 5);
    CHECK(game.players[1].hasTech(SMITHERY));
    CHECK(game.unitAt(game.index(6, 6))->type == UnitTypes::SWORDSMAN);
}

TEST_CASE("tech cost is tier x cities + 4") {
    Game game = makeGame(fieldMap());
    CHECK(game.techCost(0, RIDING) == 5);
    CHECK(game.techCost(0, ROADS) == 6);
    CHECK(game.techCost(0, TRADE) == 7);

    game.foundCity(game.index(4, 1), 0);
    CHECK(game.techCost(0, RIDING) == 6);
    CHECK(game.techCost(0, ROADS) == 8);
    CHECK(game.techCost(0, TRADE) == 10);

    // Literacy: a third cheaper, rounded up
    game.players[0].techs.set(PHILOSOPHY);
    CHECK(game.techCost(0, RIDING) == 4);
    CHECK(game.techCost(0, TRADE) == 7);
}

TEST_CASE("research needs the parent, or a child") {
    Game game = makeGame(fieldMap(), {TribeTypes::VENGIR, TribeTypes::BARDUR});
    CHECK(game.canResearch(0, CLIMBING));
    CHECK(game.canResearch(0, MINING));
    CHECK_FALSE(game.canResearch(0, MEDITATION));
    CHECK_FALSE(game.canResearch(0, SMITHERY));
    CHECK_FALSE(game.canResearch(0, ROADS));

    game.apply({CommandTypes::RESEARCH, -1, -1, RIDING});
    CHECK(game.players[0].stars == 0);
    CHECK(game.canResearch(0, ROADS));
    CHECK_FALSE(game.isLegal({CommandTypes::RESEARCH, -1, -1, ROADS}));
}

TEST_CASE("income: no stars on turn 0, then city level + 1 for the capital") {
    Game game = makeGame(fieldMap());
    CHECK(game.players[0].stars == 5);
    CHECK(game.income(0) == 2);
    game.apply({ENDTURN});
    CHECK(game.players[1].stars == 5);
    game.apply({ENDTURN});
    CHECK(game.turn == 1);
    CHECK(game.players[0].stars == 7);
}

TEST_CASE("a city under siege makes nothing") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    addUnit(game, 1, UnitTypes::WARRIOR, 1, 1);
    CHECK(game.income(0) == 0);
    std::vector<Command> legal = game.legalCommands();
    CHECK(std::none_of(legal.begin(), legal.end(), [](const Command &c) {
        return c.type == CommandTypes::TRAIN;
    }));
}

TEST_CASE("population levels a city up, and a reward must be chosen") {
    Game game = makeGame(fieldMap({"0 0 Ff", "2 0 Ff"}));
    game.apply({BUILD, game.index(0, 0), -1, ImprovementTypes::HARVESTFRUIT});
    City &capital = game.cities[game.players[0].capital];
    CHECK(capital.population == 1);
    CHECK(game.tiles[game.index(0, 0)].resource == ResourceTypes::NONE);
    CHECK(game.players[0].stars == 3);

    game.apply({BUILD, game.index(2, 0), -1, ImprovementTypes::HARVESTFRUIT});
    CHECK(capital.level == 2);
    CHECK(capital.population == 0);

    std::vector<Command> legal = game.legalCommands();
    REQUIRE(legal.size() == 2);
    CHECK(legal[0] == Command{CITYREWARD, capital.tile, -1,
                              CityRewards::WORKSHOP});
    CHECK(legal[1] == Command{CITYREWARD, capital.tile, -1,
                              CityRewards::EXPLORER});
    CHECK_FALSE(game.isLegal({ENDTURN}));

    game.apply(legal[0]);
    CHECK(capital.workshop);
    // level 2, +1 capital, +1 workshop
    CHECK(game.income(0) == 4);
}

TEST_CASE("population growth can level a city up again") {
    Game game = makeGame(fieldMap());
    City &capital = game.cities[game.players[0].capital];
    capital.level = 3;
    game.addPopulation(game.players[0].capital, 4);
    CHECK(capital.level == 4);
    REQUIRE(game.legalCommands().size() == 2);
    game.apply({CITYREWARD, capital.tile, -1, CityRewards::POPULATIONGROWTH});
    CHECK(capital.population == 3);

    game.addPopulation(game.players[0].capital, 2);
    CHECK(capital.level == 5);
    CHECK(capital.population == 0);
}

TEST_CASE("negative population costs income but never levels") {
    Game game = makeGame(fieldMap());
    int capital = game.players[0].capital;
    game.addPopulation(capital, -1);
    CHECK(game.cities[capital].level == 1);
    CHECK(game.income(0) == 1);
    game.addPopulation(capital, -5);
    CHECK(game.income(0) == 0);
}

TEST_CASE("windmills level with their farms; markets earn from them") {
    Game game = makeGame(fieldMap({"0 0 Fc", "1 0 Fc", "2 1 Fc"}));
    Player &player = game.players[0];
    player.techs |= TechSet().set(FARMING).set(CONSTRUCTION).set(TRADE);
    player.stars = 100;
    int capital = player.capital;

    build(game, 0, 0, ImprovementTypes::FARM);
    build(game, 1, 0, ImprovementTypes::FARM);
    CHECK_FALSE(game.canBuild(0, game.index(2, 2), ImprovementTypes::WINDMILL));
    build(game, 0, 1, ImprovementTypes::WINDMILL);
    CHECK(game.tiles[game.index(0, 1)].level == 2);
    // windmills are one per city
    CHECK_FALSE(game.canBuild(0, game.index(2, 0), ImprovementTypes::WINDMILL));

    int levelBefore = game.cities[capital].level;
    int popBefore = game.cities[capital].population;
    build(game, 0, 2, ImprovementTypes::MARKET);
    CHECK(game.marketIncome(game.index(0, 2)) == 2);
    CHECK(game.cities[capital].level == levelBefore);
    CHECK(game.cities[capital].population == popBefore);

    // destroying a farm takes a level away again
    game.players[0].techs.set(CHIVALRY);
    game.apply({CommandTypes::DESTROY, game.index(1, 0)});
    CHECK(game.tiles[game.index(0, 1)].level == 1);
    CHECK(game.marketIncome(game.index(0, 2)) == 1);
}

TEST_CASE("clear, burn and grow forest") {
    Game game = makeGame(fieldMap({"0 0 Tg", "1 0 T", "2 0 F"}));
    Player &player = game.players[0];
    player.techs |= TechSet().set(FORESTRY).set(CONSTRUCTION).set(SPIRITUALISM);
    player.stars = 20;

    build(game, 0, 0, ImprovementTypes::CLEARFOREST);
    CHECK(game.tiles[game.index(0, 0)].terrain == TerrainTypes::FIELD);
    CHECK(game.tiles[game.index(0, 0)].resource == ResourceTypes::NONE);
    CHECK(player.stars == 21);

    build(game, 1, 0, ImprovementTypes::BURNFOREST);
    CHECK(game.tiles[game.index(1, 0)].terrain == TerrainTypes::FIELD);
    CHECK(game.tiles[game.index(1, 0)].resource == ResourceTypes::CROP);
    CHECK(player.stars == 18);

    build(game, 2, 0, ImprovementTypes::GROWFOREST);
    CHECK(game.tiles[game.index(2, 0)].terrain == TerrainTypes::FOREST);
    CHECK(player.stars == 13);
}

TEST_CASE("roads can be built outside your borders, not in enemy ones") {
    Game game = makeGame(fieldMap());
    game.players[0].techs.set(ROADS);
    CHECK(game.canBuild(0, game.index(2, 2), ImprovementTypes::ROAD));
    CHECK(game.canBuild(0, game.index(4, 4), ImprovementTypes::ROAD));
    CHECK_FALSE(game.canBuild(0, game.index(5, 5), ImprovementTypes::ROAD));
    CHECK_FALSE(game.canBuild(0, game.index(1, 1), ImprovementTypes::ROAD));
    CHECK_FALSE(game.canBuild(0, game.index(4, 4), ImprovementTypes::FARM));
}

TEST_CASE("starfish harvest") {
    Game game = makeGame(fieldMap({"4 4 Os"}));
    clearUnits(game);
    int boat = addUnit(game, 0, UnitTypes::WARRIOR, 4, 4);
    game.units[boat].type = UnitTypes::RAFT;
    Command harvest{CommandTypes::HARVEST, game.index(4, 4)};
    CHECK_FALSE(hasCommand(game, harvest));
    game.players[0].techs.set(NAVIGATION);
    game.apply(harvest);
    CHECK(game.players[0].stars == 13);
    CHECK(game.tiles[game.index(4, 4)].resource == ResourceTypes::NONE);
}
