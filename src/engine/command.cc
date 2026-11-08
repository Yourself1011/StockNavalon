#include "command.h"
#include "game.h"

namespace {
std::string tileString(const Game &game, int tile) {
    return "(" + std::to_string(game.tileX(tile)) + "," +
           std::to_string(game.tileY(tile)) + ")";
}
} // namespace

std::string toString(const Game &game, const Command &command) {
    using namespace CommandTypes;
    if (command.type < 0 || command.type >= COMMAND_TYPE_SIZE) {
        return "unknown command";
    }
    std::string out{commandNames[command.type]};
    bool validFrom = command.from >= 0 && command.from < game.size * game.size;
    bool validTo = command.to >= 0 && command.to < game.size * game.size;

    switch (command.type) {
    case UPGRADE:
    case TRAIN:
        if (command.arg >= 0 && command.arg < UnitTypes::UNIT_TYPE_SIZE) {
            out += " " + std::string{unitData[command.arg].name};
        }
        break;
    case BUILD:
        if (command.arg >= 0 &&
            command.arg < ImprovementTypes::IMPROVEMENT_TYPE_SIZE) {
            out += " " + std::string{improvementData[command.arg].name};
        }
        break;
    case RESEARCH:
        if (command.arg >= 0 && command.arg < TechTypes::TECH_TYPE_SIZE) {
            out += " " + std::string{techData[command.arg].name};
        }
        break;
    case PEACETREATY:
    case BREAKPEACE:
    case ESTABLISHEMBASSY:
    case DESTROYEMBASSY:
        out += " player " + std::to_string(command.arg);
        break;
    case PEACEREQUESTRESPONSE:
        out += (command.arg % 2 ? ": accept player " : ": decline player ") +
               std::to_string(command.arg / 2);
        break;
    case CITYREWARD:
        if (command.arg >= 0 && command.arg < CityRewards::CITY_REWARD_SIZE) {
            out += " " + std::string{CityRewards::rewardNames[command.arg]};
        }
        break;
    default:
        break;
    }
    if (validFrom) {
        out += " " + tileString(game, command.from);
    }
    if (validTo) {
        out += " -> " + tileString(game, command.to);
    }
    return out;
}
