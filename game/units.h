#ifndef UNIT_FACTORY_H
#define UNIT_FACTORY_H

#include "unit.h"

class Player;
class Tile;

namespace Units {
Unit *warrior(Game *game, Player *player, Tile *tile);
Unit *rider(Game *game, Player *player, Tile *tile);
Unit *knight(Game *game, Player *player, Tile *tile);
Unit *defender(Game *game, Player *player, Tile *tile);
Unit *cloak(Game *game, Player *player, Tile *tile);
Unit *mindBender(Game *game, Player *player, Tile *tile);
Unit *swordsman(Game *game, Player *player, Tile *tile);
Unit *raft(Game *game, Player *player, Tile *tile);
Unit *rammer(Game *game, Player *player, Tile *tile);
Unit *scout(Game *game, Player *player, Tile *tile);
Unit *bomber(Game *game, Player *player, Tile *tile);
Unit *catapult(Game *game, Player *player, Tile *tile);
Unit *archer(Game *game, Player *player, Tile *tile);
Unit *giant(Game *game, Player *player, Tile *tile);
Unit *dagger(Game *game, Player *player, Tile *tile);
}; // namespace Units

#endif // !UNIT_FACTORY_H
