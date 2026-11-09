// The engine's CLI, with the random bot (README "Run")

#include "bots/randomBot.h"
#include "cli.h"
#include <memory>

int main(int argc, char **argv) {
    return runCli(argc, argv, [](std::uint64_t seed) -> BotTurn {
        auto bot = std::make_shared<RandomBot>(seed);
        return [bot](Game &game) { return bot->playTurn(game); };
    });
}
