#pragma once

#include "data/techData.h"
#include <array>
#include <bitset>
#include <string_view>

namespace Abilities {
// GAME.md section 9
enum Ability {
    DASH,
    ESCAPE,
    PERSIST,
    FORTIFY,
    HEAL,
    CONVERT,
    SPLASH,
    STIFF,
    SURPRISE,
    STATIC,
    INDEPENDENT,
    HIDE,
    CREEP,
    SCOUT,
    INFILTRATE,
    CARRY,
    WATER,
    STOMP,
    ABILITY_SIZE
};
} // namespace Abilities

namespace UnitTypes {
enum UnitType {
    WARRIOR,
    RIDER,
    ARCHER,
    DEFENDER,
    SWORDSMAN,
    MINDBENDER,
    CATAPULT,
    KNIGHT,
    CLOAK,
    DAGGER,
    GIANT,
    // boats; SCOUT is the boat, not the Scout skill
    RAFT,
    SCOUT,
    RAMMER,
    BOMBER,
    JUGGERNAUT,
    DINGHY,
    PIRATE,
    UNIT_TYPE_SIZE
};
} // namespace UnitTypes

using AbilitySet = std::bitset<Abilities::ABILITY_SIZE>;

// GAME.md section 8. HP, attack and defence are stored x10 so every value is
// an exact integer.
struct UnitData {
    UnitTypes::UnitType type;
    std::string_view name;
    // two characters, for the map display
    std::string_view id;
    // for boats made by upgrading a Raft, the upgrade price
    int cost;
    // 0 for boats, which take their passenger's HP
    int maxHp;
    int attack, defence;
    int movement, range;
    AbilitySet abilities;
    // the tech needed to train, or to upgrade a Raft into, this unit
    TechTypes::TechType tech;
    bool trainable;
};

extern const std::array<UnitData, UnitTypes::UNIT_TYPE_SIZE> unitData;

inline const UnitData &unitInfo(UnitTypes::UnitType type) {
    return unitData[type];
}

// the boat a land unit becomes when it moves onto a Port
UnitTypes::UnitType boatFor(UnitTypes::UnitType passenger);
// boats a Raft can be upgraded into, and change between
bool isRaftFamily(UnitTypes::UnitType type);
