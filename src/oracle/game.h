#pragma once

// Games in the real rules (docs/oracle.md): the rules data loaded into the
// game's own data manager, and games built as the app builds them.

#include "il2cpp.h"

#include <string>
#include <vector>

namespace oracle {

// Starts the runtime from a pinned build directory (oracle/setup.sh) and
// loads its rules data. Returns the game version.
int start(const std::string &buildDir);

// The value of one of the game's own enums by name, ignoring case: enumType
// is "MapPreset", "GameMode", "BotDifficulty" or "TribeType". Throws on an
// unknown name.
int enumValue(const std::string &enumType, const std::string &name);

// A single-player game, built by the game's GameStateUtils.CreateLocalGame
// as the app's New Game menu does: the own player with tribes[0], the game's
// AI with the others. The map is generated on the way. Values are the
// game's enums (enumValue).
struct LocalGame {
    int map = 0;
    int size = 0;
    int mode = 0;
    int difficulty = 0;
    std::vector<int> tribes;
};
il2cpp::Object *createLocalGame(const LocalGame &game);

// Makes every player a person (no AI plays any seat by itself) and starts
// the match as the app does, with a StartMatchCommand. Every seat gets the
// human handicap, unless keepHandicaps: then the AI seats keep their
// difficulty's (for playing against the AI with aiMove). Returns the game's
// ActionManager, which executes its commands from then on.
il2cpp::Object *startMatch(il2cpp::Object *state, bool keepHandicaps = false);

// The command the game's AI would play next for the current player, as the
// game plays a bot's turn: a pending trigger's answer
// (CommandTriggerUtils.TryGetTriggerCommand), else AI.GetMove at the game's
// difficulty. Not executed.
il2cpp::Object *aiMove(il2cpp::Object *state);

// The current player's legal commands, as the client would offer them: the
// game's own generators (CommandUtils per explored tile, each unit's movement and
// attack options, research, diplomacy, a pending city reward or peace
// offer's answer, end turn),
// each kept only if its IsValid accepts it.
std::vector<il2cpp::Object *> legalCommands(il2cpp::Object *state);

// Executes a command; returns "" or the game's error.
std::string execute(il2cpp::Object *actionManager, il2cpp::Object *command);

// For setting up a scenario: one of the game's own actions (a class such as
// "BuildAction" or "IncreaseCurrencyAction"), made by its constructor with
// that many parameters from the arguments as text (an enum value by name,
// coordinates as "x,y", numbers, booleans as 1 or 0), queued as a command's
// actions are and run to the end.
std::string runAction(il2cpp::Object *state, il2cpp::Object *actionManager,
                      const std::string &name, const std::vector<std::string> &args);
// Sets an integer, byte, short, bool or enum field of an object directly.
void setInt(il2cpp::Object *target, const std::string &field, int value);

} // namespace oracle
