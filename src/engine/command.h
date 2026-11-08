#pragma once

#include <array>
#include <string>
#include <string_view>

class Game;

namespace CommandTypes {
// GAME.md section 2, Actions. One per command type of the real game.
enum CommandType {
    // units
    MOVE,
    ATTACK,
    RECOVER,
    HEALOTHERS,
    PROMOTE,
    DISBAND,
    STAY,
    DISEMBARK,
    UPGRADE,
    // tiles and cities
    BUILD,
    DESTROY,
    HARVEST,
    CAPTURE,
    EXAMINERUINS,
    // economy
    TRAIN,
    RESEARCH,
    CITYREWARD,
    // diplomacy
    PEACETREATY,
    PEACEREQUESTRESPONSE,
    BREAKPEACE,
    ESTABLISHEMBASSY,
    DESTROYEMBASSY,
    // turn and game
    ENDTURN,
    RESIGN,
    COMMAND_TYPE_SIZE
};

const std::array<std::string_view, COMMAND_TYPE_SIZE> commandNames = {
    "move",       "attack",
    "recover",    "heal others",
    "promote",    "disband",
    "stay",       "disembark",
    "upgrade",
    "build",      "destroy",
    "harvest",    "capture",
    "examine ruins", "train",
    "research",   "city reward",
    "peace treaty", "peace request response",
    "break peace", "establish embassy",
    "destroy embassy", "end turn",
    "resign"};
} // namespace CommandTypes

// Tiles are indices into Game::tiles, -1 when unused. arg is a unit type,
// improvement type, tech, city reward or player, depending on the command.
struct Command {
    CommandTypes::CommandType type;
    int from = -1, to = -1;
    int arg = -1;

    bool operator==(const Command &) const = default;
};

std::string toString(const Game &game, const Command &command);
