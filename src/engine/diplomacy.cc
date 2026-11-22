#include "game.h"

#include <bit>

using namespace CommandTypes;

namespace {
std::uint32_t bit(int player) { return std::uint32_t{1} << player; }
} // namespace

bool Game::atWar(int a, int b) const {
    if (!settings.follows(RuleVersions::EMBASSIES_AT_WAR) || atPeace(a, b)) {
        return false;
    }
    // from either's attack on the other until the attacker's next turn ends
    // (game: HasWarWith)
    auto attacked = [&](int attacker, int victim) {
        const std::vector<int> &last = players[attacker].lastAttackTurn;
        if (static_cast<int>(last.size()) <= victim) {
            return false;
        }
        int since = turn - last[victim];
        return since < 1 || (since == 1 && attacker >= currentPlayer);
    };
    return attacked(a, b) || attacked(b, a);
}

int Game::embassyIncome(int player) const {
    int stars = 0;
    for (std::size_t i = 0; i < players.size(); i++) {
        int other = static_cast<int>(i);
        if (other == player || !players[other].alive) {
            continue;
        }
        if (atWar(player, other)) {
            continue;
        }
        int each = atPeace(player, other) ? Rules::EMBASSY_INCOME_AT_PEACE
                                          : Rules::EMBASSY_INCOME;
        stars += each * players[player].hasEmbassyWith(other);
        stars += each * players[other].hasEmbassyWith(player);
    }
    return stars;
}

int Game::embassyPays(int builder, int host) const {
    if (builder == host || !players[host].alive || !players[builder].hasEmbassyWith(host) ||
        atWar(builder, host)) {
        return 0;
    }
    return atPeace(builder, host) ? Rules::EMBASSY_INCOME_AT_PEACE : Rules::EMBASSY_INCOME;
}

int Game::hostedEmbassyIncome(int player) const {
    int stars = 0;
    for (std::size_t i = 0; i < players.size(); i++) {
        stars += embassyPays(static_cast<int>(i), player);
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
        if (them.hasEmbassyWith(currentPlayer) &&
            !settings.follows(RuleVersions::NO_DESTROY_EMBASSY)) {
            out.push_back({DESTROYEMBASSY, -1, -1, other});
        }
        if (!me.hasMet(other)) {
            continue;
        }

        if (!settings.peaceTreaties) {
            // no treaties to make or break
        } else if (atPeace(currentPlayer, other)) {
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
            me.stars >= embassyCost(currentPlayer) && capital >= 0 &&
            cities[capital].owner == other) {
            out.push_back({ESTABLISHEMBASSY, -1, -1, other});
        }
    }
}

void Game::applyDiplomacy(const Command &command) {
    Player &me = players[currentPlayer];
    switch (command.type) {
    case PEACETREATY:
        players[command.arg].peaceRequests |= bit(currentPlayer);
        // an offer reveals the sender (from TURN_PLAYER_MEETS, at the start
        // of the receiver's turn: checkMeetings)
        if (settings.follows(RuleVersions::DIPLOMACY_MEETS) &&
            !settings.follows(RuleVersions::TURN_PLAYER_MEETS)) {
            meet(command.arg, currentPlayer);
        }
        break;
    case PEACEREQUESTRESPONSE: {
        int proposer = command.arg / 2;
        me.peaceRequests &= ~bit(proposer);
        // the turn's start the offer held up goes on first, so its income
        // is from before the answer (game: a war still counts)
        if (me.incomeDue && pendingRewardCity() < 0 && pendingPeaceOffer() < 0) {
            me.incomeDue = false;
            me.stars += income(currentPlayer);
        }
        if (command.arg % 2 == 1) {
            me.peace |= bit(proposer);
            players[proposer].peace |= bit(currentPlayer);
            // peace replaces a cooldown between them (game: one relation state)
            if (settings.follows(RuleVersions::GAME_COOLDOWN)) {
                me.coolDown &= ~bit(proposer);
                players[proposer].coolDown &= ~bit(currentPlayer);
            }
            // their roads are ours to use now, and ours theirs
            connectionsChanged(bit(currentPlayer) | bit(proposer));
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
        // the other side cools down: it can't attack the breaker in its
        // next turn (the breaker's own cooldown lasts the rest of its turn,
        // which the freeze covers)
        if (settings.follows(RuleVersions::PEACE_COOLDOWN)) {
            players[other].coolDown |= bit(currentPlayer);
        }
        // the breaker too, for the rest of its turn
        if (settings.follows(RuleVersions::GAME_COOLDOWN)) {
            me.coolDown |= bit(other);
        }
        // connections over the former ally's roads break (the game: not
        // until something else makes it recompute them)
        if (!settings.follows(RuleVersions::GAME_CONNECTIONS)) {
            updateAllConnections();
        }
        break;
    }
    case ESTABLISHEMBASSY: {
        int host = command.arg;
        me.stars -= embassyCost(currentPlayer);
        me.embassies |= bit(host);
        // the builder sees the 3x3 around the host's capital, and the host
        // meets the builder
        revealAround(currentPlayer, cities[players[host].capital].tile, 1);
        if (settings.follows(RuleVersions::DIPLOMACY_MEETS)) {
            meet(host, currentPlayer);
        }
        break;
    }
    case DESTROYEMBASSY:
        players[command.arg].embassies &= ~bit(currentPlayer);
        break;
    default:
        break;
    }
}

int Game::embassyCost(int player) const {
    if (!settings.follows(RuleVersions::GROWING_EMBASSY_COST)) {
        return Rules::OLD_EMBASSY_COST;
    }
    return Rules::embassyCost(std::popcount(players[player].embassies));
}

void Game::clearDiplomacy(int player) {
    for (Player &other : players) {
        other.peace &= ~bit(player);
        other.peaceRequests &= ~bit(player);
        other.embassies &= ~bit(player);
        other.coolDown &= ~bit(player);
    }
    Player &p = players[player];
    p.peace = p.peaceRequests = p.embassies = p.coolDown = 0;
}
