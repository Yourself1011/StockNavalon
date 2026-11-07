#include "bots/randomBot.h"
#include <array>

Command RandomBot::choose(const Game &game) {
    std::array<std::vector<Command>, CommandTypes::COMMAND_TYPE_SIZE> byType;
    for (const Command &command : game.legalCommands()) {
        byType[command.type].push_back(command);
    }
    std::vector<int> types;
    for (int type = 0; type < CommandTypes::COMMAND_TYPE_SIZE; type++) {
        if (!byType[type].empty()) {
            types.push_back(type);
        }
    }
    const std::vector<Command> &options = byType[types[rng() % types.size()]];
    return options[rng() % options.size()];
}

std::vector<Command> RandomBot::playTurn(Game &game, int maxCommands) {
    std::vector<Command> played;
    int player = game.currentPlayer;
    while (!game.gameOver() && game.currentPlayer == player) {
        Command command = static_cast<int>(played.size()) < maxCommands
                              ? choose(game)
                              : Command{CommandTypes::ENDTURN};
        // a level-up reward has to be taken before the turn can end
        if (command.type == CommandTypes::ENDTURN &&
            !game.isLegal(command)) {
            command = choose(game);
        }
        game.applyUnchecked(command);
        played.push_back(command);
    }
    return played;
}
