#include "data/unitData.h"
#include "utils.h"

namespace {
using namespace UnitTypes;
using namespace Abilities;
using TechTypes::TechType;

UnitData unit(UnitType type, std::string_view name, std::string_view id,
              int cost, int maxHp, int attack, int defence, int movement,
              int range, std::initializer_list<int> abilities, TechType tech,
              bool trainable) {
    return {type,     name,  id,
            cost,     maxHp, attack,
            defence,  movement, range,
            makeBitset<ABILITY_SIZE>(abilities), tech, trainable};
}
} // namespace

const std::array<UnitData, UNIT_TYPE_SIZE> unitData = {
    unit(WARRIOR, "warrior", "wa", 2, 100, 20, 20, 1, 1, {DASH, FORTIFY},
         TechTypes::BASIC, true),
    unit(RIDER, "rider", "ri", 3, 100, 20, 10, 2, 1, {DASH, ESCAPE, FORTIFY},
         TechTypes::RIDING, true),
    unit(ARCHER, "archer", "ar", 3, 100, 20, 10, 1, 2, {DASH, FORTIFY},
         TechTypes::ARCHERY, true),
    unit(DEFENDER, "defender", "de", 3, 150, 10, 30, 1, 1, {FORTIFY},
         TechTypes::STRATEGY, true),
    unit(SWORDSMAN, "swordsman", "sw", 5, 150, 30, 30, 1, 1, {DASH},
         TechTypes::SMITHERY, true),
    unit(MINDBENDER, "mind bender", "mb", 5, 100, 0, 10, 1, 1,
         {HEAL, CONVERT, STIFF}, TechTypes::PHILOSOPHY, true),
    unit(CATAPULT, "catapult", "ca", 8, 100, 40, 0, 1, 3, {STIFF},
         TechTypes::MATHEMATICS, true),
    unit(KNIGHT, "knight", "kn", 8, 100, 35, 10, 3, 1,
         {DASH, PERSIST, FORTIFY}, TechTypes::CHIVALRY, true),
    unit(CLOAK, "cloak", "cl", 8, 50, 20, 5, 2, 1,
         {HIDE, INFILTRATE, DASH, Abilities::SCOUT, CREEP, STIFF, STATIC},
         TechTypes::DIPLOMACY, true),
    unit(DAGGER, "dagger", "da", 2, 100, 20, 20, 1, 1,
         {DASH, SURPRISE, INDEPENDENT, STATIC}, TechTypes::BASIC, false),
    unit(GIANT, "giant", "gi", 10, 400, 50, 40, 1, 1, {STATIC},
         TechTypes::BASIC, false),

    unit(RAFT, "raft", "rf", 0, 0, 0, 10, 2, 2, {WATER, CARRY, STIFF, STATIC},
         TechTypes::FISHING, false),
    unit(UnitTypes::SCOUT, "scout", "sc", 5, 0, 20, 10, 3, 2,
         {WATER, DASH, CARRY, Abilities::SCOUT, STATIC}, TechTypes::SAILING, false),
    unit(RAMMER, "rammer", "rm", 5, 0, 30, 30, 3, 1,
         {WATER, DASH, CARRY, STATIC}, TechTypes::RAMMING, false),
    unit(BOMBER, "bomber", "bo", 15, 0, 30, 20, 2, 3,
         {WATER, CARRY, SPLASH, STIFF, STATIC}, TechTypes::NAVIGATION, false),
    unit(JUGGERNAUT, "juggernaut", "ju", 0, 0, 40, 40, 2, 1,
         {WATER, CARRY, STIFF, STOMP, STATIC}, TechTypes::FISHING, false),
    unit(DINGHY, "dinghy", "dy", 0, 0, 20, 5, 2, 1,
         {WATER, CARRY, HIDE, INFILTRATE, DASH, Abilities::SCOUT, STIFF, STATIC},
         TechTypes::FISHING, false),
    // a Pirate spawned on water has no passenger and uses its own 15 HP
    unit(PIRATE, "pirate", "pi", 0, 150, 20, 20, 2, 1,
         {WATER, CARRY, DASH, SURPRISE, INDEPENDENT, STATIC},
         TechTypes::FISHING, false),
};

UnitTypes::UnitType boatFor(UnitTypes::UnitType passenger) {
    switch (passenger) {
    case GIANT:
        return JUGGERNAUT;
    case CLOAK:
        return DINGHY;
    case DAGGER:
        return PIRATE;
    default:
        return RAFT;
    }
}

bool isRaftFamily(UnitTypes::UnitType type) {
    return type == RAFT || type == UnitTypes::SCOUT || type == RAMMER || type == BOMBER;
}
