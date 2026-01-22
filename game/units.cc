#include "units.h"

Unit *Units::warrior(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "warrior", false, 2, 10, 2, 1, 1, 2, 0,
                    "wa",
                    {Abilities::DASH, Abilities::FORTIFY, Abilities::LAND});
}
Unit *Units::rider(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "rider", false, 3, 10, 1, 2, 1, 2, 0,
                    "ri",
                    {Abilities::DASH, Abilities::ESCAPE, Abilities::FORTIFY,
                     Abilities::LAND});
}
