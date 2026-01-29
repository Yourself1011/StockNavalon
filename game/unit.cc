#include "game.h"
#include "map.h"
#include "player.h"
#include "tech.h"
#include "tile.h"
#include "unit.h"
#include "utils.h"
#include <bitset>
#include <cmath>
#include <iostream>
#include <vector>

Unit::Unit(Game *game, Player *player, Tile *tile, std::string type,
           bool hidden, int cost, int maxHealth, double attack, int defence,
           int movement, int range, int kills, std::string id,
           std::vector<Abilities::Ability> abilitiesVec)
    : game{game}, player{player}, tile{tile}, type{type}, hidden{hidden},
      cost{cost}, maxHealth{maxHealth}, health{(double)maxHealth},
      attack{attack}, defence{defence}, movement{movement}, range{range},
      kills{kills}, id{id}, abilities{}, canAttack{false}, canMove{false},
      canUseAbility{false} {
    vectorToBitset(&abilities, abilitiesVec);
};

void Unit::move(Tile *to) {
    tile->unit = nullptr;
    tile = to;
    to->unit = this;
    canMove = false;
    canUseAbility = false;

    if (!abilities.test(Abilities::DASH)) {
        canAttack = false;
    }
}

void dfs(int dist,
         std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> &unlockedTerrain,
         Map &map, Tile &tile, Unit &unit, std::vector<Tile> *visited,
         bool first = false) {

    // if the unit can move on water, allow it even if water movement is not
    // unlocked yet
    if (!(tile.terrainType == TerrainTypes::WATER &&
          unit.abilities.test(Abilities::WATER))) {
        // check that terrain is unlocked
        if ((!unlockedTerrain.test(tile.terrainType)) ||
            // only allow water units to go on water
            ((tile.terrainType == TerrainTypes::WATER ||
              tile.terrainType == TerrainTypes::OCEAN) &&
             !unit.abilities.test(Abilities::WATER)) ||
            // only allow land units to go on land
            ((tile.terrainType == TerrainTypes::FIELD ||
              tile.terrainType == TerrainTypes::FOREST ||
              tile.terrainType == TerrainTypes::MOUNTAIN) &&
             !unit.abilities.test(Abilities::LAND))) {
            return;
        }
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
        if (tile.terrainType == TerrainTypes::FOREST ||
            tile.terrainType == TerrainTypes::MOUNTAIN) {
            dist = 0;
        } else {
            dist--;
        }

        if (dist <= 0) {
            return;
        }
    }

    // go next iteration
    if (tile.x != 0 && tile.y != 0) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x - 1, tile.y - 1), unit,
            visited);
    }
    if (tile.x != 0) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x - 1, tile.y), unit,
            visited);
    }
    if (tile.x != 0 && tile.y != map.size - 1) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x - 1, tile.y + 1), unit,
            visited);
    }
    if (tile.y != 0) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x, tile.y - 1), unit,
            visited);
    }
    if (tile.y != map.size - 1) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x, tile.y + 1), unit,
            visited);
    }
    if (tile.x != map.size - 1 && tile.y != 0) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x + 1, tile.y - 1), unit,
            visited);
    }
    if (tile.x != map.size - 1) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x + 1, tile.y), unit,
            visited);
    }
    if (tile.x != map.size - 1 && tile.y != map.size - 1) {
        dfs(dist, unlockedTerrain, map, *map.at(tile.x + 1, tile.y + 1), unit,
            visited);
    }
}

std::vector<Tile> *Unit::validMoves() {
    std::vector<Tile> *moves = new std::vector<Tile>;
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> terrains;

    for (const Tech *tech : player->techs) {
        terrains |= tech->movementUnlocks;
    }

    dfs(movement, terrains, game->map, *tile, *this, moves, true);

    if (!moves->empty()) {
        moves->erase(moves->begin());
    }
    return moves;
}

double Unit::getDefenceBonus() {
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> defenceTerrains;

    for (const Tech *tech : player->techs) {
        defenceTerrains |= tech->defenceBonuses;
    }

    if (defenceTerrains.test(this->tile->terrainType)) {
        return 1.5;
    };

    return 1;
}

void Unit::attackUnit(Unit *defender) {
    double attackForce = this->attack * (this->health / this->maxHealth),
           defenseForce = defender->defence *
                          (defender->health / defender->maxHealth) *
                          defender->getDefenceBonus(),
           totalDamage = attackForce + defenseForce;
    defender->health -= round((attackForce / totalDamage) * this->attack * 4.5);

    if (defender->health > 0 && !defender->abilities.test(Abilities::STIFF) &&
        abs(this->tile->x - defender->tile->x) <= defender->range &&
        abs(this->tile->y - defender->tile->y) <= defender->range) {
        this->health -=
            round((defenseForce / totalDamage) * defender->defence * 4.5);
    }

    canUseAbility = false;
    canAttack = false;
    canMove = false;
    if (defender->health <= 0) {
        Tile *tile = defender->tile;
        defender->die();
        this->tile->unit = nullptr;
        this->tile = tile;
        this->tile->unit = this;
        this->kills++;
        if (abilities.test(Abilities::PERSIST)) {
            canAttack = true;
        }
    } else if (this->health <= 0) {
        this->die();
    }

    if (abilities.test(Abilities::ESCAPE)) {
        canMove = true;
    }
}

std::vector<Tile> *Unit::validAttacks() {
    std::vector<Tile> *attacks = new std::vector<Tile>;

    for (int x = std::max(tile->x - range, 0);
         x <= std::min(tile->x + range, game->map.size - 1); x++) {
        for (int y = std::max(tile->y - range, 0);
             y <= std::min(tile->y + range, game->map.size - 1); y++) {

            Tile *tile = game->map.at(x, y);
            if (tile->unit && tile->unit->player != this->player) {
                attacks->push_back(*tile);
            }
        }
    }

    return attacks;
}

void Unit::die() {
    tile->unit = nullptr;
    delete this;
}
