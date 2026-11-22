#pragma once

// The command-line game (README "Run"): a map in the terminal, commands
// typed by the players, and bots for the seats given with --bot. The engine's
// own CLI plays the random bot; stockNavalon's plays itself.

#include "command.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Game;

// A bot's turn: its commands, and a note shown with them (empty for none),
// such as how the bot rates its position
struct BotResult {
    std::vector<Command> commands;
    std::string note;
};
// Plays the whole of the current player's turn
using BotTurn = std::function<BotResult(Game &game)>;
// makes the bot for a game, given the game's seed
using BotMaker = std::function<BotTurn(std::uint64_t seed)>;

// Ways in for a game played alongside another, such as the real game's
// (stockNavalon.play --crazy). Each is optional.
struct CliHooks {
    // the starting game, instead of one from the arguments (its tribes come
    // with it)
    std::function<void(Game &game)> start;
    // called with every command a person or a bot plays, just before it's
    // applied: the bots' own (BotMaker's) get it through their BotTurn
    std::function<void(Game &game, const Command &command)> beforeCommand;
    // seats played from outside: their turn plays itself on the game
    std::vector<int> outsideSeats;
    BotTurn outsideTurn;
};

// Runs the CLI with these arguments (see cli.cc); returns the exit code
int runCli(int argc, char **argv, const BotMaker &makeBot, const CliHooks &hooks = {});
