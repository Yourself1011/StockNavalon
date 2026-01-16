
#include "unit.h"

#ifndef ALLUNIT_H
#define ALLUNIT_H

class Player;
class Tile;

class Warrior : public Unit {
  public:
    Warrior(Player *player, Tile *tile, int idx);
};

#endif
