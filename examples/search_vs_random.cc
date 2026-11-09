// stockNavalon's search (stage 1, with the score evaluator standing in for a
// network) against the random bot: games to turn 20, alternating seats.
//
//   cmake --build --preset release && ./build/release/search_vs_random [horizon] [simulations] [games]

#include "bots/randomBot.h"
#include "game.h"
#include "mapgen.h"
#include "search.h"
#include "view.h"
#include <chrono>
#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
    using namespace TribeTypes;
    int horizon = argc > 1 ? std::atoi(argv[1]) : 1;
    int simulations = argc > 2 ? std::atoi(argv[2]) : 200;
    int games = argc > 3 ? std::atoi(argv[3]) : 6;
    int wins = 0, ahead = 0, decisions = 0;
    double seconds = 0;
    for (int g = 0; g < games; g++) {
        std::vector<Tribe> lineup = {static_cast<Tribe>(g % TRIBE_SIZE),
                                     static_cast<Tribe>((g * 5 + 3) % TRIBE_SIZE)};
        std::uint64_t seed = g;
        Game game(generateMap({.size = 11,
                               .type = static_cast<MapTypes::MapType>(g % 6),
                               .tribes = lineup,
                               .seed = seed}),
                  lineup, {.seed = seed});
        RandomBot bot(seed);
        ScoreEvaluator evaluator;
        SearchConfig config{.simulations = simulations, .samples = 2,
                            .horizon = horizon};
        std::mt19937_64 rng(seed);
        int me = g % 2;
        while (!game.gameOver() && game.turn < 20) {
            if (game.currentPlayer != me) {
                bot.playTurn(game);
                continue;
            }
            auto start = std::chrono::steady_clock::now();
            SearchResult result = search(game, priorBeliefs(makeView(game, me)),
                                         evaluator, config, rng());
            seconds += std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - start)
                           .count();
            decisions++;
            game.apply(result.choose(0, rng));
        }
        wins += game.winner == me;
        ahead += game.score(me) > game.score(1 - me);
        std::cout << "game " << g << ": "
                  << (game.winner == me ? "won" : game.winner >= 0 ? "lost" : "no winner")
                  << ", score " << game.score(me) << " to " << game.score(1 - me)
                  << " (turn " << game.turn << ")\n";
    }
    std::cout << "horizon " << horizon << ", " << simulations
              << " simulations x 2 samples: won " << wins << "/" << games
              << ", ahead " << ahead << "/" << games << "; "
              << 1000 * seconds / decisions << " ms a decision\n";
}
