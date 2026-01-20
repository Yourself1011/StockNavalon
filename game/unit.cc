#include "game.h"
#include "map.h"
#include "player.h"
#include "tech.h"
#include "tile.h"
#include "unit.h"
#include <vector>

Unit::Unit(Game *game, Player *player, Tile *tile, std::string type,
           bool hidden, int cost, int maxHealth, int defence, int movement,
           int range, int attack, int kills, std::string id,
           std::vector<Abilities::Ability> abilities)
    : game{game}, player{player}, tile{tile}, type{type}, hidden{hidden},
      cost{cost}, maxHealth{maxHealth}, health{maxHealth}, defence{defence},
      movement{movement}, range{range}, attack{attack}, kills{kills}, id{id},
      abilities{abilities}, canAttack{false}, canMove{false},
      canUseAbility{false} {};

void Unit::move(Tile *to) {
    tile->unit = nullptr;
    tile = to;
    to->unit = this;
    canMove = false;
    canUseAbility = false;

    bool hasDash = false;
    for (Abilities::Ability ability : abilities) {
        if (ability == Abilities::DASH) {
            hasDash = true;
        }
    }
    if (!hasDash) {
        canAttack = false;
    }
}

void dfs(int dist, std::vector<TerrainTypes::TerrainType> unlockedTerrain,
         Map map, Tile tile, std::vector<Tile> *visited, bool first = false) {

    // check that terrain is unlocked
    bool terrainIsUnlocked = false;
    for (TerrainTypes::TerrainType terrainType : unlockedTerrain) {
        if (tile.terrainType == terrainType) {
            terrainIsUnlocked = true;
            break;
        }
    }
    if (!terrainIsUnlocked) {
        return;
    }

    // add to visited vector if we haven't visited before
    bool found = false;
    for (Tile visitedTile : *visited) {
        if (tile.x == visitedTile.x && tile.y == visitedTile.y) {
            found = true;
            break;
        }
    }
    if (!found) {
        visited->push_back(tile);
    }

    // movement points logic
    if (!first) {
        if (tile.terrainType == TerrainTypes::FIELD) {
            dist--;
        } else if (tile.terrainType == TerrainTypes::FOREST) {
            dist = 0;
        }

        if (dist <= 0) {
            return;
        }
    }

    // go next iteration
    if (tile.x != 0 && tile.y != 0) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x - 1, tile.y - 1),
            visited);
    }
    if (tile.x != 0) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x - 1, tile.y), visited);
    }
    if (tile.x != 0 && tile.y != map.size - 1) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x - 1, tile.y + 1),
            visited);
    }
    if (tile.y != 0) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x, tile.y - 1), visited);
    }
    if (tile.y != map.size - 1) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x, tile.y + 1), visited);
    }
    if (tile.x != map.size - 1 && tile.y != 0) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x + 1, tile.y - 1),
            visited);
    }
    if (tile.x != map.size - 1) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x + 1, tile.y), visited);
    }
    if (tile.x != map.size - 1 && tile.y != map.size - 1) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x + 1, tile.y + 1),
            visited);
    }
}

std::vector<Tile> *Unit::validMoves() {
    std::vector<Tile> *moves = new std::vector<Tile>;
    std::vector<TerrainTypes::TerrainType> terrains;

    for (const Tech *tech : player->techs) {
        terrains.insert(terrains.end(), tech->movementUnlocks.begin(),
                        tech->movementUnlocks.end());
    }

    dfs(movement, terrains, game->map, *tile, moves, true);
    moves->erase(moves->begin());
    return moves;
}
