#ifndef UNIT_FACTORY_H
#define UNIT_FACTORY_H

#include "unit.h"

class Player;
class Tile;

namespace Units {
Unit *warrior(Game *game, Player *player, Tile *tile);
Unit *rider(Game *game, Player *player, Tile *tile);
}; // namespace Units

#endif // !UNIT_FACTORY_H
