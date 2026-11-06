#pragma once

#include "data/unitData.h"
#include <bitset>
#include <string>
#include <vector>

class Tile;
class Player;
class Game;

class Unit {
  public:
    Game *game;
    Player *player;
    Tile *tile, *home;
    UnitTypes::UnitType type;
    bool trainable;
    int cost, maxHealth, defence, movement, range;
    int kills = 0, promotionLevel = 3;
    double attack, health;
    std::string id;
    std::bitset<Abilities::ABILITY_SIZE> abilities;
    bool canAttack = false, canMove = false, canUseAbility = false;

    Unit *leader = nullptr, *follower = nullptr; // only for centipedes

    Unit(Game *game, Player *player, Tile *tile, const UnitData &data);

    void move(Tile *to);
    std::vector<Tile *> validMoves() const;
    // may destroy this unit and/or the defender
    void attackUnit(Unit *defender);
    std::vector<Tile *> validAttacks() const;
    double getDefenceBonus() const;
    // destroys this unit; do not use it afterwards
    void die();
};
