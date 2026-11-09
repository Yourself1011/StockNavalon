#pragma once

#include "data/improvementData.h"
#include "data/techData.h"
#include "data/tribeData.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>
#include <vector>

namespace TaskTypes {
// GAME.md section 14. Each unlocks the monument of the same position.
enum Task {
    PACIFIST,
    WEALTH,
    EXPLORER,
    KILLER,
    NETWORK,
    METROPOLIS,
    GENIUS,
    TASK_SIZE
};

const std::array<std::string_view, TASK_SIZE> taskNames = {
    "pacifist", "wealth",     "explorer", "killer",
    "network",  "metropolis", "genius"};

inline ImprovementTypes::ImprovementType monumentFor(Task task) {
    return static_cast<ImprovementTypes::ImprovementType>(
        static_cast<int>(ImprovementTypes::ALTAROFPEACE) + task);
}
// the task whose monument this is
inline Task taskFor(ImprovementTypes::ImprovementType monument) {
    return static_cast<Task>(monument - ImprovementTypes::ALTAROFPEACE);
}
} // namespace TaskTypes

using TaskSet = std::bitset<TaskTypes::TASK_SIZE>;

struct Player {
    TribeTypes::Tribe tribe;
    int stars = 0;
    TechSet techs;
    // index into Game::cities of this player's original capital
    int capital = -1;
    bool alive = true;
    int lighthousesFound = 0;
    // bit q is set once this player has met player q
    std::uint32_t met = 0;

    // tasks: completed ones unlock their monument, which can be built once
    TaskSet tasksDone, monumentsBuilt;
    // enemy units killed, for Killer
    int kills = 0;
    // own turns in a row without an attack since Meditation, for Pacifist
    int peacefulTurns = 0;
    bool attackedThisTurn = false;
    // the turns this player's units or cities were attacked, oldest first
    std::vector<int> attackedOnTurns;

    // diplomacy, one bit per other player: a peace treaty with them, a
    // peace request from them waiting for an answer, an embassy in their
    // capital
    std::uint32_t peace = 0, peaceRequests = 0, embassies = 0;

    bool hasMet(int player) const { return met >> player & 1; }
    bool hasTech(TechTypes::TechType tech) const { return techs.test(tech); }
    bool hasEmbassyWith(int player) const { return embassies >> player & 1; }
};
