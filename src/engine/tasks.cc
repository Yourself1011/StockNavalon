#include "game.h"

using namespace TaskTypes;

bool Game::taskMet(int player, Task task) const {
    const Player &p = players[player];
    switch (task) {
    case PACIFIST:
        // no attacks for a while, counted from researching Meditation
        return p.hasTech(Rules::PACIFIST_TECH) &&
               p.peacefulTurns >= Rules::PACIFIST_TURNS;
    case WEALTH:
        return p.hasTech(Rules::WEALTH_TECH) && p.stars >= Rules::WEALTH_STARS;
    case EXPLORER:
        return p.lighthousesFound >= Rules::LIGHTHOUSE_COUNT;
    case KILLER:
        return p.kills >= Rules::KILLER_KILLS;
    case NETWORK:
        return std::count_if(cities.begin(), cities.end(),
                             [&](const City &c) {
                                 return c.owner == player &&
                                        c.connectedFor == player;
                             }) >= Rules::NETWORK_CITIES;
    case METROPOLIS:
        return std::any_of(cities.begin(), cities.end(), [&](const City &c) {
            return c.owner == player && c.level >= Rules::METROPOLIS_LEVEL;
        });
    case GENIUS:
        return p.techs.all();
    default:
        return false;
    }
}

void Game::updateTasks() {
    for (std::size_t i = 0; i < players.size(); i++) {
        Player &player = players[i];
        if (!player.alive) {
            continue;
        }
        for (int task = 0; task < TASK_SIZE; task++) {
            if (taskMet(static_cast<int>(i), static_cast<Task>(task))) {
                player.tasksDone.set(task);
            }
        }
    }
}

void Game::creditKill(int unit) {
    // a boat's kills don't count toward promotion, nor a Static unit's
    bool counts = !settings.follows(RuleVersions::BOATS_GET_NO_KILLS) ||
                  !(units[unit].has(Abilities::CARRY) || units[unit].has(Abilities::STATIC));
    if (units[unit].alive && counts) {
        units[unit].kills++;
    }
    players[units[unit].owner].kills++;
}

void Game::onAttack(int attacker, int defender, bool damaging) {
    players[attacker].attackedThisTurn = true;
    if (settings.follows(RuleVersions::PACIFIST_RESET_AT_ATTACK) &&
        (damaging || !settings.follows(RuleVersions::PACIFIST_NEEDS_DAMAGE))) {
        players[attacker].peacefulTurns = 0;
    }
    std::vector<int> &attacked = players[defender].attackedOnTurns;
    if (attacked.empty() || attacked.back() != turn) {
        attacked.push_back(turn);
    }
    if (static_cast<int>(players[attacker].lastAttackTurn.size()) > defender) {
        players[attacker].lastAttackTurn[defender] = turn;
    }
    // attacking a tribe destroys the embassies between the two, and puts
    // them at war for a while (EMBASSIES_AT_WAR), in which new ones pay
    // nothing
    players[attacker].embassies &= ~(std::uint32_t{1} << defender);
    players[defender].embassies &= ~(std::uint32_t{1} << attacker);
}
