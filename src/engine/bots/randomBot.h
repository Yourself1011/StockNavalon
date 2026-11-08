#pragma once

#include "game.h"
#include <cstdint>
#include <random>
#include <vector>

// Plays legal commands at random. It picks a command type first, then a
// command of that type, so the many build commands don't crowd out moving,
// attacking and ending the turn.
class RandomBot {
  public:
    explicit RandomBot(std::uint64_t seed = 0) : rng{seed} {}

    // one legal command for the current player; the game must not be over
    Command choose(const Game &game);

    // plays the current player's whole turn, ending it after maxCommands at
    // the latest; returns what it played
    std::vector<Command> playTurn(Game &game, int maxCommands = 200);

  private:
    std::mt19937_64 rng;
};
