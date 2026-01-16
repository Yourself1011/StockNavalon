#include "allUnits.h"
#include "player.h"
#include "tile.h"

Warrior::Warrior(Player *player, Tile *tile, int idx)
    : Unit(player, tile, "warrior", idx++, false, 2, 10, 2, 1, 1, 2, 0, "wa",
           {DASH, FORTIFY}) {}
