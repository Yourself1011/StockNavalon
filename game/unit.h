#include <string>
#include <vector>
#ifndef UNIT_H
#define UNIT_H

class Tile;
class Player;

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
    NAVAL, // NOTE: changed from "WATER" to prevent duplicate enum
    LAND
};

class Unit {
  public:
    Player *player;
    Tile *tile;
    std::string type;
    int idx;
    bool hidden; // is hidden from troop training menu in city (stuff that
                 // spawns outside a city)
    int cost, maxHealth, health, defence, movement, range, attack, kills;
    std::string id;
    std::vector<Ability> abilities;
    bool canAttack, canMove, canUseAbility;

    Unit(Player *player, Tile *tile, std::string type, int idx, bool hidden,
         int cost, int maxHealth, int defence, int movement, int range,
         int attack, int kills, std::string id, std::vector<Ability> abilities);

    void move(Tile *to);
};

#endif
