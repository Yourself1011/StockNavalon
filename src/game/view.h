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
    // every player's score, met or not: scores are public
    std::vector<int> scores;

    // the player's own tasks (GAME.md section 14): done ones unlock their
    // monument, which can be built once
    TaskSet tasksDone, monumentsBuilt;
    // progress: enemy units killed (Killer), turns in a row without an
    // attack since Meditation (Pacifist), lighthouses found (Explorer)
    int kills, peacefulTurns, lighthousesFound;

    // the player's diplomacy, one entry per player (false for themselves)
    struct Relation {
        bool peace = false;
        // a peace request from them, waiting for this player's answer
        bool theyAsked = false;
        // a peace request from this player, waiting for theirs
        bool weAsked = false;
        // this player's embassy in their capital, and theirs in this
        // player's capital
        bool ourEmbassy = false, theirEmbassy = false;
    };
    std::vector<Relation> relations;
    // index y * size + x
    std::vector<TileView> tiles;
    std::vector<UnitView> units;
    std::vector<CityView> cities;
};

// everything = true ignores fog, for debugging and spectating
PlayerView makeView(const Game &game, int player, bool everything = false);
