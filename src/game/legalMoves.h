#pragma once

#include "command.h"
#include <vector>

class Game;

// The current player's legal commands, kept up to date one command at a time
// instead of being regenerated from scratch. For search: a node keeps its
// LegalMoves next to its Game, and a child's list comes from the parent's
// plus the one command between them.
//
// commands() always matches Game::legalCommands() as a set (the fuzz test
// checks this after every command), though not necessarily in the same order.
//
// How it stays cheap: the state before and after a command is compared to
// find the tiles, units and cities that changed. Only unit and tile commands
// near those are regenerated; research, training, diplomacy and End Turn
// are cheap and always redone. Anything that only depends on stars (can
// this build or upgrade be afforded?) is filtered on output, since stars
// change with almost every command. A change of turn, techs, tasks or
// diplomacy regenerates everything.
class LegalMoves {
  public:
    explicit LegalMoves(const Game &game);

    const std::vector<Command> &commands() const { return list; }

    // applies the command (which must be in commands()) and updates the list
    void apply(Game &game, const Command &command);
    // updates the list for a game that went from before to after by any
    // means, e.g. several commands or edits in a test
    void update(const Game &before, const Game &after);

  private:
    // unit and tile commands as if the player had unlimited stars
    std::vector<Command> structural;
    std::vector<Command> list;

    void rebuild(const Game &game);
    // structural commands from the given sources only
    void generate(const Game &game, const std::vector<char> &unitTiles,
                  const std::vector<char> &tiles);
    // the structural commands that can be afforded, plus the global ones
    void assemble(const Game &game);
};
