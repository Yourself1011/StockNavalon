#pragma once

#include "data/unitData.h"

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
    // direction of the last step, for Giant pushes
    int lastDx = 0, lastDy = 0;

    const UnitData &data() const { return unitInfo(type); }
    bool has(Abilities::Ability ability) const {
        return data().abilities.test(ability);
    }
    bool isNaval() const { return has(Abilities::WATER); }
};
