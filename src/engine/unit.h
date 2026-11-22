#pragma once

#include "data/unitData.h"
#include "rules.h"

#include <array>
#include <cmath>
#include <numbers>

// The 8 directions as the game numbers them, as (dx, dy) on the map
namespace Directions {
constexpr std::array<std::array<int, 2>, 8> OFFSETS = {
    {{-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}}};
// the direction nearest the vector by angle, the lowest on a tie, and the
// first for no vector (game: GetDirectionFromDistanceVector)
inline int nearest(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return 0;
    }
    double angle = std::atan2(dy, dx);
    int best = 0;
    double bestGap = 10;
    for (int d = 0; d < 8; d++) {
        double gap = std::abs(std::remainder(
            angle - std::atan2(OFFSETS[d][1], OFFSETS[d][0]), 2 * std::numbers::pi));
        if (gap < bestGap) {
            best = d;
            bestGap = gap;
        }
    }
    return best;
}
} // namespace Directions

// what a unit scores: its own cost and its passenger's, at
// Rules::UNIT_SCORE_PER_STAR a star (game: a boat counts both)
inline int unitScoreOf(UnitTypes::UnitType type, UnitTypes::UnitType passenger) {
    int cost = unitInfo(type).cost + (passenger != type ? unitInfo(passenger).cost : 0);
    return Rules::unitScore(cost);
}

struct Unit {
    // for a boat, the boat type
    UnitTypes::UnitType type;
    // for a boat, the land unit it carries; otherwise the same as type
    UnitTypes::UnitType passenger;
    int owner;
    // index into Game::tiles
    int tile;
    // x10, like UnitData
    int hp, maxHp;
    // index into Game::cities of the city supporting this unit, -1 if none
    int home = -1;
    // a unit from a ruin belongs to a city but doesn't count toward its cap
    bool freeOfCap = false;
    // the player whose score this unit counts for. Converting a unit doesn't
    // change any score, so it stays with whoever first had it.
    int scoreOwner;
    int kills = 0;
    bool veteran = false;
    bool alive = true;
    // invisible to enemies (the Hide skill)
    bool hidden = false;
    // canAct: no move or attack yet this turn, so abilities (Recover,
    // Capture, ...) are still allowed
    bool canMove = false, canAttack = false, canAct = false;
    // the game's "attacked" flag: it attacked or embarked this turn (for an
    // upgrade to a Dash boat, UPGRADE_DASHES)
    bool attacked = false;
    // direction of the last step, for Giant pushes (before GAME_PUSH_ORDER)
    int lastDx = 0, lastDy = 0;
    // the way it faces (Directions), for pushes: toward the map's centre when
    // it appears, then the way its last step went, or at whatever it last
    // attacked or was attacked by (game)
    int facing = 0;

    const UnitData &data() const { return unitInfo(type); }
    bool has(Abilities::Ability ability) const {
        return data().abilities.test(ability);
    }
    bool isNaval() const { return has(Abilities::WATER); }
    bool operator==(const Unit &) const = default;
};
