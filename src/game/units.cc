#include "unit.h"
#include "units.h"

Unit *Units::warrior(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "warrior", false, 2, 10, 2, 2, 1, 1, 0,
                    "wa",
                    {Abilities::DASH, Abilities::FORTIFY, Abilities::LAND});
}

Unit *Units::rider(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "rider", false, 3, 10, 2, 1, 2, 1, 0,
                    "ri",
                    {Abilities::DASH, Abilities::ESCAPE, Abilities::FORTIFY,
                     Abilities::LAND});
}

Unit *Units::knight(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "knight", false, 8, 10, 3.5, 1, 3, 1, 0,
                    "kn",
                    {Abilities::DASH, Abilities::PERSIST, Abilities::FORTIFY,
                     Abilities::LAND});
}

Unit *Units::defender(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "defender", false, 3, 15, 1, 3, 1, 1, 0,
                    "de", {Abilities::FORTIFY, Abilities::LAND});
}

Unit *Units::cloak(Game *game, Player *player, Tile *tile) {
    return new Unit(
        game, player, tile, "cloak", false, 3, 15, 1, 3, 1, 1, 0, "cl",
        {Abilities::HIDE, Abilities::CREEP, Abilities::INFILTRATE,
         Abilities::DASH, Abilities::SCOUT, Abilities::STIFF, Abilities::LAND});
}

Unit *Units::mindBender(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "mindBender", false, 5, 10, 0, 1, 1, 1,
                    0, "mb",
                    {Abilities::HEAL, Abilities::CONVERT, Abilities::STIFF,
                     Abilities::LAND});
}

Unit *Units::swordsman(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "swordsman", false, 5, 15, 3, 3, 1, 1,
                    0, "sw", {Abilities::DASH, Abilities::LAND});
}

Unit *Units::raft(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "raft", true, 0, 10, 0, 1, 2, 2, 0,
                    "rf",
                    {Abilities::CARRY, Abilities::STATIC, Abilities::STIFF,
                     Abilities::WATER});
}

Unit *Units::rammer(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "rammer", true, 5, 10, 3, 3, 3, 1, 0,
                    "rm",
                    {Abilities::DASH, Abilities::CARRY, Abilities::STATIC,
                     Abilities::WATER});
}

Unit *Units::scout(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "scout", true, 5, 10, 3, 3, 3, 1, 0,
                    "sc",
                    {Abilities::DASH, Abilities::CARRY, Abilities::SCOUT,
                     Abilities::STATIC, Abilities::WATER});
}

Unit *Units::bomber(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "bomber", true, 15, 10, 3, 2, 2, 3, 0,
                    "bo",
                    {Abilities::CARRY, Abilities::SPLASH, Abilities::STIFF,
                     Abilities::STATIC, Abilities::WATER});
}

Unit *Units::catapult(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "catapult", false, 8, 10, 4, 0, 1, 3, 0,
                    "ca", {Abilities::STIFF, Abilities::LAND});
}

Unit *Units::archer(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "archer", false, 3, 10, 2, 1, 1, 2, 0,
                    "ar",
                    {Abilities::DASH, Abilities::FORTIFY, Abilities::LAND});
}

Unit *Units::giant(Game *game, Player *player, Tile *tile) {
    return new Unit(game, player, tile, "giant", true, 10, 40, 5, 4, 1, 1, 0,
                    "gi", {Abilities::STATIC, Abilities::LAND});
}

Unit *Units::dagger(Game *game, Player *player, Tile *tile) {
    return new Unit(
        game, player, tile, "dagger", true, 2, 10, 2, 2, 1, 1, 0, "da",
        {Abilities::DASH, Abilities::SURPRISE, Abilities::INDEPENDENT,
         Abilities::STATIC, Abilities::LAND});
}
