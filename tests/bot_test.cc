#include "bots/randomBot.h"
#include "doctest/doctest.h"
#include "helpers.h"
#include "mapgen.h"

TEST_CASE("the random bot only plays legal commands and always ends its turn") {
    for (std::uint64_t seed = 0; seed < 10; seed++) {
        CAPTURE(seed);
        std::vector<TribeTypes::Tribe> tribes = {TribeTypes::IMPERIUS,
                                                 TribeTypes::BARDUR};
        Game game(generateMap({.tribes = tribes, .seed = seed}), tribes,
                  {.seed = seed});
        RandomBot bot(seed);
        for (int turn = 0; turn < 100 && !game.gameOver(); turn++) {
            int player = game.currentPlayer;
            // check each command before it's played
            while (!game.gameOver() && game.currentPlayer == player) {
                Command command = bot.choose(game);
                REQUIRE(game.isLegal(command));
                game.applyUnchecked(command);
            }
        }
    }
}

TEST_CASE("a bot turn ends, even if it never picks End Turn itself") {
    Game game = makeGame(fieldMap());
    RandomBot bot(1);
    std::vector<Command> played = bot.playTurn(game, 0);
    CHECK(game.currentPlayer == 1);
    REQUIRE_FALSE(played.empty());
    CHECK(played.back().type == CommandTypes::ENDTURN);
}

TEST_CASE("the same seed plays the same game") {
    auto play = [](std::uint64_t seed) {
        std::vector<TribeTypes::Tribe> tribes = {TribeTypes::XINXI,
                                                 TribeTypes::KICKOO};
        Game game(generateMap({.tribes = tribes, .seed = 5}), tribes,
                  {.seed = 5});
        RandomBot bot(seed);
        std::vector<Command> all;
        for (int turn = 0; turn < 20; turn++) {
            for (const Command &c : bot.playTurn(game)) {
                all.push_back(c);
            }
        }
        return all;
    };
    CHECK(play(3) == play(3));
    CHECK(play(3) != play(4));
}
