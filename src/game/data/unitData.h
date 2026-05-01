
#ifndef UNIT_DATA_H
#define UNIT_DATA_H

#include <bitset>
#include <string>

namespace Abilities {

enum Ability {
    DASH,
    ESCAPE,
    SCOUT,
    SNEAK,
    HIDE,
    BUILD,
    PERSIST,
    CONVERT,
    HEAL,
    SWIM,
    CARRY,
    GROW,
    FLY,
    SPLASH,
    DECAY,
    NAVIGATE,
    CRUSH,
    FREEZE,
    FREEZEAREA,
    AUTOFREEZE,
    SKATE,
    FORTIFY,
    CREEP,
    BOOST,
    INDEPENDENT,
    POISON,
    EAT,
    UNIQUE,
    EXPLODE,
    SURPRISE,
    AGENT,
    INFILTRATE,
    DETECT,
    INTERCEPT,
    STIFF,
    PROTECT,
    STOMP,
    AUTOHEAL,
    STATIC,
    DIVE,
    TENTACLES,
    AUTOFLOOD,
    DRENCH,
    AMPHIBIOUS,
    WATER,
    LAND,
    ABILITY_SIZE
};
}

namespace UnitTypes {
enum UnitType {
    WARRIOR,
    RIDER,
    KNIGHT,
    DEFENDER,
    CLOAK,
    MINDBENDER,
    SWORDSMAN,
    RAFT,
    RAMMER,
    SCOUT,
    BOMBER,
    CATAPULT,
    ARCHER,
    GIANT,
    DAGGER,
    UNIT_TYPE_SIZE
};
}

struct UnitData {
    UnitTypes::UnitType type;
    bool trainable;
    int cost, maxHealth, defence, movement, range;
    double attack;
    std::string id;
    std::bitset<Abilities::ABILITY_SIZE> abilities;
};

namespace Units {
extern const UnitData warrior;
extern const UnitData rider;
extern const UnitData knight;
extern const UnitData defender;
extern const UnitData cloak;
extern const UnitData mindBender;
extern const UnitData swordsman;
extern const UnitData raft;
extern const UnitData rammer;
extern const UnitData scout;
extern const UnitData bomber;
extern const UnitData catapult;
extern const UnitData archer;
extern const UnitData giant;
extern const UnitData dagger;
} // namespace Units
#endif // !UNIT_DATA_H
