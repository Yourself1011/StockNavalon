// A minimal search bot: tries every legal command on a copy of the game and
// keeps the one that leaves it furthest ahead on score. It shows the pattern
// a real search uses: copy the Game and its LegalMoves, apply, evaluate.
//
// The evaluation reads the full Game (scores include hidden information),
// which a real bot shouldn't; see docs/cpp.md on fog.
//
//   cmake --build --preset release && ./build/release/one_ply_search

#include "bots/randomBot.h"
#include "game.h"
#include "legalMoves.h"
#include "mapgen.h"
#include <iostream>

namespace {

int evaluate(const Game &game, int me) {
    int best = 0;
    for (std::size_t p = 0; p < game.players.size(); p++) {
        if (static_cast<int>(p) != me) {
            best = std::max(best, game.score(static_cast<int>(p)));
        }
    }
    return game.score(me) - best;
}

// plays one turn, choosing each command by a one-ply lookahead
void playTurn(Game &game) {
    int me = game.currentPlayer;
    LegalMoves legal(game);
    while (!game.gameOver() && game.currentPlayer == me) {
        Command best{CommandTypes::ENDTURN};
        int bestValue = evaluate(game, me);
        for (const Command &command : legal.commands()) {
            if (command.type == CommandTypes::ENDTURN) {
                continue;
            }
            // branch: copies are independent and cheap
            Game child = game;
            LegalMoves childLegal = legal;
            childLegal.apply(child, command);
            int value = evaluate(child, me);
            if (value > bestValue) {
                best = command;
                bestValue = value;
            }
        }
        // a level-up reward must be chosen before End Turn is legal
        if (best.type == CommandTypes::ENDTURN && !game.isLegal(best)) {
            best = legal.commands().front();
        }
        legal.apply(game, best);
    }
}

} // namespace

int main() {
    std::vector<TribeTypes::Tribe> tribes = {TribeTypes::IMPERIUS,
                                             TribeTypes::BARDUR};
    Game game(generateMap({.tribes = tribes, .seed = 3}), tribes, {.seed = 3});
    RandomBot opponent(3);

    while (!game.gameOver() && game.turn < 30) {
        if (game.currentPlayer == 0) {
            playTurn(game);
        } else {
            opponent.playTurn(game);
        }
    }
    std::cout << "after turn " << game.turn << ": search bot " << game.score(0)
              << ", random bot " << game.score(1) << '\n';
}
