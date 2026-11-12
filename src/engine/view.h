#pragma once

#include "game.h"
#include <optional>
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
        // Windmill, Sawmill or Forge level, or a temple's level (visible for
        // every temple the player can see, whoever owns it)
        int level = 0;
        // temples: turns until the next level, 0 at the top level
        int turnsToLevelUp = 0;
        // the tribe whose climate the tile is in
        std::optional<TribeTypes::Tribe> climate;
        bool road = false;
        // a road this player built themselves
        bool ownRoad = false;
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
        int parks;
    };

    int player;
    int size;
    // the kind of map, public from the start
    MapTypes::MapType mapType;
    int turn, currentPlayer, winner;
    // the player's own
    int stars;
    int income;
    TechSet techs;
    // each player's tribe: the player's own, and those of tribes they've
    // met; empty until then. Each player's starting score (the first record
    // in scoreHistory) narrows an unmet tribe down.
    std::vector<std::optional<TribeTypes::Tribe>> tribes;
    std::vector<bool> alive;
    // tribes this player has met
    std::vector<bool> met;
    // every player's score, met or not, as this player sees it: their own
    // live, everyone else's from the end of their last turn
    std::vector<int> scores;
    // every published score so far (public), oldest first
    std::vector<ScoreRecord> scoreHistory;
    // per player: what this player's own actions visibly did to their score
    // (units seen dying, cities captured), since they last published, and
    // within the change they last published. Negative for losses.
    std::vector<int> effectsSincePublished, effectsInLastChange;
    // per scoreHistory record: what this player had visibly done to that
    // record's player's score within that change
    std::vector<int> effectsInChanges;

    // the player's own tasks (GAME.md section 14): done ones unlock their
    // monument, which can be built once
    TaskSet tasksDone, monumentsBuilt;
    // progress: enemy units killed (Killer), turns in a row without an
    // attack since Meditation (Pacifist), lighthouses found (Explorer)
    int kills, peacefulTurns, lighthousesFound;
    // the turns the player's units or cities were attacked: they always know
    std::vector<int> attackedOnTurns;
    // the player's units taken by conversion: type and passenger
    std::vector<std::pair<UnitTypes::UnitType, UnitTypes::UnitType>> convertedAway;
    // the first turn one of the player's units or cities stood on each tile,
    // -1 for never
    std::vector<int> firstOccupied;

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
