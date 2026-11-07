#pragma once

#include "game.h"
#include <vector>

// What one player knows about the game: explored tiles with their live
// contents, units they can see, their own stars and techs, and nothing else.
// Bots should read this, not the Game, so they can't cheat.
struct PlayerView {
    struct TileView {
        bool explored = false;
        // the rest is only filled in for explored tiles
        TerrainTypes::TerrainType terrain = TerrainTypes::FIELD;
        // NONE until the player can see that kind of resource
        ResourceTypes::ResourceType resource = ResourceTypes::NONE;
        ImprovementTypes::ImprovementType building = ImprovementTypes::NONE;
        int level = 0;
        bool road = false;
        // the player whose borders contain it, -1 if none
        int owner = -1;
        // index into units, -1 if no visible unit
        int unit = -1;
        // index into cities if a city stands here, -1 if none
        int city = -1;
    };

    struct UnitView {
        UnitTypes::UnitType type, passenger;
        int owner, tile;
        // x10
        int hp, maxHp;
        int kills;
        bool veteran, hidden;
        // only for the player's own units; false for enemies
        bool canMove = false, canAttack = false, canAct = false;
        // own units only: an enemy hidden unit is next to it
        bool hiddenEnemyNear = false;
    };

    struct CityView {
        int tile, owner, level, population;
        // the player whose original capital it is, -1 if none
        int capitalOf;
        bool wall;
    };

    int player;
    int size;
    int turn, currentPlayer, winner;
    // the player's own
    int stars;
    int income;
    TechSet techs;
    std::vector<TribeTypes::Tribe> tribes;
    std::vector<bool> alive;
    // tribes this player has met
    std::vector<bool> met;
    // the player's own score and those of tribes they've met; -1 otherwise
    std::vector<int> scores;
    // index y * size + x
    std::vector<TileView> tiles;
    std::vector<UnitView> units;
    std::vector<CityView> cities;
};

// everything = true ignores fog, for debugging and spectating
PlayerView makeView(const Game &game, int player, bool everything = false);
