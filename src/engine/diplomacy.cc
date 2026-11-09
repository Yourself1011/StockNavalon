#include "game.h"

using namespace CommandTypes;

namespace {
std::uint32_t bit(int player) { return std::uint32_t{1} << player; }
} // namespace

int Game::embassyIncome(int player) const {
    int stars = 0;
    for (std::size_t i = 0; i < players.size(); i++) {
        int other = static_cast<int>(i);
        if (other == player || !players[other].alive) {
            continue;
        }
        int each = atPeace(player, other) ? Rules::EMBASSY_INCOME_AT_PEACE
                                          : Rules::EMBASSY_INCOME;
        stars += each * players[player].hasEmbassyWith(other);
        stars += each * players[other].hasEmbassyWith(player);
    }
    return stars;
}

void Game::addDiplomacyCommands(std::vector<Command> &out) const {
    const Player &me = players[currentPlayer];
    for (std::size_t i = 0; i < players.size(); i++) {
        int other = static_cast<int>(i);
        const Player &them = players[other];
        if (other == currentPlayer || !them.alive) {
            continue;
        }
        // an embassy of theirs in our capital, met or not
        if (them.hasEmbassyWith(currentPlayer)) {
            out.push_back({DESTROYEMBASSY, -1, -1, other});
        }
        if (!me.hasMet(other)) {
            continue;
        }

        if (atPeace(currentPlayer, other)) {
            out.push_back({BREAKPEACE, -1, -1, other});
        } else if (me.peaceRequests & bit(other)) {
            // arg: the proposer times 2, plus 1 to accept
            out.push_back({PEACEREQUESTRESPONSE, -1, -1, other * 2 + 1});
            out.push_back({PEACEREQUESTRESPONSE, -1, -1, other * 2});
        } else if (me.hasTech(TechTypes::STRATEGY) &&
                   !(them.peaceRequests & bit(currentPlayer))) {
            out.push_back({PEACETREATY, -1, -1, other});
        }

        // in their capital, while they still hold it
        int capital = them.capital;
        if (me.hasTech(Rules::EMBASSY_TECH) && !me.hasEmbassyWith(other) &&
            me.stars >= Rules::EMBASSY_COST && capital >= 0 && cities[capital].owner == other) {
            out.push_back({ESTABLISHEMBASSY, -1, -1, other});
        }
    }
}

void Game::applyDiplomacy(const Command &command) {
    Player &me = players[currentPlayer];
    switch (command.type) {
    case PEACETREATY:
        players[command.arg].peaceRequests |= bit(currentPlayer);
        break;
    case PEACEREQUESTRESPONSE: {
        int proposer = command.arg / 2;
        me.peaceRequests &= ~bit(proposer);
        if (command.arg % 2 == 1) {
            me.peace |= bit(proposer);
            players[proposer].peace |= bit(currentPlayer);
            // their roads are ours to use now
            updateAllConnections();
        }
        break;
    }
    case BREAKPEACE: {
        int other = command.arg;
        me.peace &= ~bit(other);
        players[other].peace &= ~bit(currentPlayer);
        // the breaker's units freeze; those inside the other tribe's borders
        // are disbanded with no refund
        for (std::size_t i = 0; i < units.size(); i++) {
            Unit &unit = units[i];
            if (!unit.alive || unit.owner != currentPlayer) {
                continue;
            }
            unit.canMove = unit.canAttack = unit.canAct = false;
            if (tileOwner(unit.tile) == other) {
                killUnit(static_cast<int>(i));
            }
        }
        // connections over the former ally's roads break
        updateAllConnections();
        break;
    }
    case ESTABLISHEMBASSY: {
        int host = command.arg;
        me.stars -= Rules::EMBASSY_COST;
        me.embassies |= bit(host);
        // the builder sees the 3x3 around the host's capital
        revealAround(currentPlayer, cities[players[host].capital].tile, 1);
        break;
    }
    case DESTROYEMBASSY:
        players[command.arg].embassies &= ~bit(currentPlayer);
        break;
    default:
        break;
    }
}

void Game::clearDiplomacy(int player) {
    for (Player &other : players) {
        other.peace &= ~bit(player);
        other.peaceRequests &= ~bit(player);
        other.embassies &= ~bit(player);
    }
    Player &p = players[player];
    p.peace = p.peaceRequests = p.embassies = 0;
}
