// A complete game between two random bots on a generated map.
//
//   cmake --build --preset release && ./build/release/random_game

#include "bots/randomBot.h"
#include "game.h"
#include "mapgen.h"
#include "view.h"
#include <iostream>

int main() {
    std::vector<TribeTypes::Tribe> tribes = {TribeTypes::IMPERIUS,
                                             TribeTypes::BARDUR};
    MapData map = generateMap({.type = MapTypes::LAKES,
                               .tribes = tribes,
                               .seed = 7});
    Game game(map, tribes, {.seed = 7});

    RandomBot bot(7);
    while (!game.gameOver() && game.turn < 100) {
        bot.playTurn(game);
    }

    for (std::size_t p = 0; p < game.players.size(); p++) {
        PlayerView view = makeView(game, static_cast<int>(p));
        std::cout << "player " << p << ": score " << game.score(p)
                  << ", stars " << view.stars << ", cities "
                  << game.cityCount(p) << '\n';
    }
    std::cout << (game.gameOver() ? "winner: player " +
                                        std::to_string(game.winner)
                                  : std::string("no winner by turn 100"))
              << '\n';
}
