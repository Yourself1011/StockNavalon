#include "data/unitData.h"
#include <bitset>
#include <string>
#include <vector>
#ifndef UNIT_H
#define UNIT_H

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
    int cost, maxHealth, defence, movement, range, kills, promotionLevel;
    double attack, health;
    std::string id;
    std::bitset<Abilities::ABILITY_SIZE> abilities;
    bool canAttack, canMove, canUseAbility;

    Unit *leader, *follower; // only for centipedes

    Unit(Game *game, Player *player, Tile *tile, const UnitData &data);

    void move(Tile *to);
    std::vector<Tile> *validMoves();
    void attackUnit(Unit *defender);
    std::vector<Tile> *validAttacks();
    double getDefenceBonus();
    void die();
};

#endif
