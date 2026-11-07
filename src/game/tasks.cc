#include "game.h"

using namespace TaskTypes;

bool Game::taskMet(int player, Task task) const {
    const Player &p = players[player];
    switch (task) {
    case PACIFIST:
        // no attacks for 5 turns, counted from researching Meditation
        return p.hasTech(TechTypes::MEDITATION) && p.peacefulTurns >= 5;
    case WEALTH:
        return p.hasTech(TechTypes::TRADE) && p.stars >= 100;
    case EXPLORER:
        return p.lighthousesFound >= 4;
    case KILLER:
        return p.kills >= 10;
    case NETWORK:
        return std::count_if(cities.begin(), cities.end(),
                             [&](const City &c) {
                                 return c.owner == player &&
                                        c.connectedFor == player;
                             }) >= 5;
    case METROPOLIS:
        return std::any_of(cities.begin(), cities.end(), [&](const City &c) {
            return c.owner == player && c.level >= 5;
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
    if (units[unit].alive) {
        units[unit].kills++;
    }
    players[units[unit].owner].kills++;
}

void Game::onAttack(int attacker, int defender) {
    players[attacker].attackedThisTurn = true;
    // attacking a tribe destroys the embassies between the two
    players[attacker].embassies &= ~(std::uint32_t{1} << defender);
    players[defender].embassies &= ~(std::uint32_t{1} << attacker);
}
