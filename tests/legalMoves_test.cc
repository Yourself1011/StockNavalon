#include "bots/randomBot.h"
#include "doctest/doctest.h"
#include "helpers.h"
#include "legalMoves.h"
#include "mapgen.h"
#include <algorithm>
#include <tuple>

namespace {
std::vector<Command> sorted(std::vector<Command> commands) {
    std::sort(commands.begin(), commands.end(),
              [](const Command &a, const Command &b) {
                  return std::tie(a.type, a.from, a.to, a.arg) <
                         std::tie(b.type, b.from, b.to, b.arg);
              });
    return commands;
}
} // namespace

TEST_CASE("incremental legal moves always match a full regeneration") {
    for (std::uint64_t seed = 0; seed < 36; seed++) {
        CAPTURE(seed);
        std::vector<TribeTypes::Tribe> tribes = {
            static_cast<TribeTypes::Tribe>(seed % TribeTypes::TRIBE_SIZE),
            static_cast<TribeTypes::Tribe>((seed * 5 + 3) %
                                           TribeTypes::TRIBE_SIZE)};
        auto type =
            static_cast<MapTypes::MapType>(seed % MapTypes::MAP_TYPE_SIZE);
        Game game(generateMap({.type = type,
                               .tribes = tribes,
                               .seed = seed,
                               .candidates = 1}),
                  tribes, {.seed = seed});
        LegalMoves legal(game);
        RandomBot bot(seed);
        // rich games reach more of the rules
        bool rich = seed % 2 == 1;

        for (int step = 0; step < 3000 && !game.gameOver(); step++) {
            CAPTURE(step);
            REQUIRE(sorted(legal.commands()) == sorted(game.legalCommands()));
            Command command = bot.choose(game);
            int turn = game.turn;
            legal.apply(game, command);
            if (rich && game.turn != turn) {
                Game before = game;
                for (Player &player : game.players) {
                    player.stars += 10;
                }
                legal.update(before, game);
            }
        }
        CHECK(sorted(legal.commands()) == sorted(game.legalCommands()));
    }
}

TEST_CASE("a node's legal moves copy with it, for search") {
    std::vector<TribeTypes::Tribe> tribes = {TribeTypes::IMPERIUS,
                                             TribeTypes::BARDUR};
    Game root(generateMap({.tribes = tribes, .seed = 3}), tribes);
    LegalMoves rootMoves(root);
    // two children from the same parent stay independent
    std::vector<Command> options = rootMoves.commands();
    REQUIRE(options.size() >= 2);
    for (int i = 0; i < 2; i++) {
        Game child = root;
        LegalMoves childMoves = rootMoves;
        childMoves.apply(child, options[i]);
        CHECK(sorted(childMoves.commands()) == sorted(child.legalCommands()));
    }
    CHECK(sorted(rootMoves.commands()) == sorted(root.legalCommands()));
}
