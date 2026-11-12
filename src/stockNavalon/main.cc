// Play against stockNavalon in the terminal: the engine's CLI (README "Run"),
// with stockNavalon in the seats given with --bot. It searches on
// determinized games, so it only knows what its own player could.
//
//   ./build/release/play_stocknavalon lakes Imperius Bardur --size 11 --bot 1
//
// Its own options: --simulations N (per sample, default 200), --samples N
// (determinized games per command, default 4), --horizon N (turn changes a
// line may cross, default 1).

#include "cli.h"
#include "game.h"
#include "search.h"
#include <memory>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    SearchConfig config{.simulations = 200, .samples = 4, .horizon = 1};
    std::vector<char *> rest{argv[0]};
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        bool hasValue = i + 1 < argc;
        if (arg == "--simulations" && hasValue) {
            config.simulations = std::stoi(argv[++i]);
        } else if (arg == "--samples" && hasValue) {
            config.samples = std::stoi(argv[++i]);
        } else if (arg == "--horizon" && hasValue) {
            config.horizon = std::stoi(argv[++i]);
        } else {
            rest.push_back(argv[i]);
        }
    }

    return runCli(static_cast<int>(rest.size()), rest.data(),
                  [config](std::uint64_t seed) -> BotTurn {
        auto rng = std::make_shared<std::mt19937_64>(seed);
        auto evaluator = std::make_shared<ScoreEvaluator>();
        return [config, rng, evaluator](Game &game) {
            return playTurn(game, *evaluator, config, *rng);
        };
    });
}
