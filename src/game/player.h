#pragma once

#include "data/techData.h"
#include "data/tribeData.h"
#include <cstdint>

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

    bool hasMet(int player) const { return met >> player & 1; }

    bool hasTech(TechTypes::TechType tech) const { return techs.test(tech); }
};
