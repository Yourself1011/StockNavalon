#pragma once

// Forced openings (notes/design.md, "Forced openings: a plan library"): for
// its first turns a side may only choose among the commands a plan allows,
// so self-play sees strategies it doesn't find by itself. The search still
// picks among them; legality and costs stay the engine's.

#include "command.h"
#include <string>
#include <vector>

class Game;

enum class Plan {
    NONE,
    // A way to the enemy over bridges, then free play: research toward the
    // bridge's tech, saving stars for it, then the fewest bridges that join
    // the home landmass to the enemy's, saving stars for each while one
    // could be built but for stars (a purchase it can afford is made before
    // the turn ends); once joined, no constraint
    BRIDGE_RUSH,
    // On to the enemy by sea: research toward the port's tech (and the
    // ocean's, if shallow water doesn't reach the enemy's shore), then a port
    // (the one nearest the enemy capital), saving and buying as above; then
    // the march by land and sea: units outside their own cities move only
    // toward the enemy capital (walking, embarking at a port, sailing,
    // disembarking), and the turn doesn't end while one can get closer
    NAVAL,
    // Winning when ahead (with SelfPlayConfig::planWhenAhead): the way over,
    // bridges where the fewest can join the landmasses, else by sea as
    // NAVAL, then the march by land and sea
    CONVERT,
};

std::string planName(Plan plan);
// "none", "bridge", "naval", "convert"; throws on anything else
Plan planNamed(const std::string &name);

// The commands among `legal` that `plan` allows the player to move in
// `game`, in their order: all of them once the plan puts no constraint left.
// Two-player games.
std::vector<Command> planAllowed(Plan plan, const Game &game, const std::vector<Command> &legal);

// Whether the plan has nothing left to do for the player to move: from then
// on it allows everything (a plan with a march is done only when its turns
// are over)
bool planDone(Plan plan, const Game &game);
