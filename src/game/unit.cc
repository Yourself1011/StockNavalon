#include "unit.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "tech.h"
#include "tile.h"
#include "utils.h"
#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

Unit::Unit(Game *game, Player *player, Tile *tile, const UnitData &data)
    : game{game}, player{player}, tile{tile}, home{tile}, type{data.type},
      trainable{data.trainable}, cost{data.cost}, maxHealth{data.maxHealth},
      defence{data.defence}, movement{data.movement}, range{data.range},
      attack{data.attack}, health{static_cast<double>(data.maxHealth)},
      id{data.id}, abilities{data.abilities} {}

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

namespace {

std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE>
unlockedMovement(const Player &player) {
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> terrains;
    for (const Tech *tech : BitsetIndex{player.techs, TechDataAccessor{}}) {
        terrains |= tech->movementUnlocks;
    }
    return terrains;
}

bool isWater(TerrainTypes::TerrainType terrain) {
    return terrain == TerrainTypes::WATER || terrain == TerrainTypes::OCEAN;
}

bool canEnter(const Unit &unit, const Tile &tile,
              const std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> &unlocked) {
    // if the unit can move on water, allow it even if water movement is not
    // unlocked yet
    if (tile.terrainType == TerrainTypes::WATER &&
        unit.abilities.test(Abilities::WATER)) {
        return true;
    }
    if (!unlocked.test(tile.terrainType)) {
        return false;
    }
    // water units stay on water, land units stay on land
    return isWater(tile.terrainType) ? unit.abilities.test(Abilities::WATER)
                                     : unit.abilities.test(Abilities::LAND);
}

bool inEnemyZoneOfControl(Map &map, const Tile &tile, const Player *player) {
    for (int x = tile.x - 1; x <= tile.x + 1; x++) {
        for (int y = tile.y - 1; y <= tile.y + 1; y++) {
            if (!map.inBounds(x, y)) {
                continue;
            }
            const Unit *other = map.at(x, y)->unit;
            if (other && other->player != player) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

std::vector<Tile *> Unit::validMoves() const {
    Map &map = game->map;
    const auto unlocked = unlockedMovement(*player);

    // most movement left when leaving each tile, -1 if never left
    std::vector<int> bestRemaining(map.size * map.size, -1);
    std::vector<bool> reachable(map.size * map.size, false);
    std::vector<std::pair<Tile *, int>> frontier{{tile, movement}};
    bestRemaining[tile->x * map.size + tile->y] = movement;

    while (!frontier.empty()) {
        auto [from, remaining] = frontier.back();
        frontier.pop_back();

        for (int x = from->x - 1; x <= from->x + 1; x++) {
            for (int y = from->y - 1; y <= from->y + 1; y++) {
                if (!map.inBounds(x, y)) {
                    continue;
                }
                Tile *to = map.at(x, y);
                if (!canEnter(*this, *to, unlocked)) {
                    continue;
                }
                int idx = x * map.size + y;
                reachable[idx] = true;

                // forests and mountains end movement
                int left = (to->terrainType == TerrainTypes::FOREST ||
                            to->terrainType == TerrainTypes::MOUNTAIN)
                               ? 0
                               : remaining - 1;
                if (left <= 0 || left <= bestRemaining[idx] ||
                    inEnemyZoneOfControl(map, *to, player)) {
                    continue;
                }
                bestRemaining[idx] = left;
                frontier.emplace_back(to, left);
            }
        }
    }

    std::vector<Tile *> moves;
    for (int x = 0; x < map.size; x++) {
        for (int y = 0; y < map.size; y++) {
            Tile *to = map.at(x, y);
            // units may pass through friendly units but not stop on them
            if (reachable[x * map.size + y] && to->unit == nullptr) {
                moves.push_back(to);
            }
        }
    }
    return moves;
}

double Unit::getDefenceBonus() const {
    std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE> defenceTerrains;

    for (const Tech *tech : BitsetIndex{player->techs, TechDataAccessor{}}) {
        defenceTerrains |= tech->defenceBonuses;
    }

    if (defenceTerrains.test(tile->terrainType)) {
        return 1.5;
    }

    return 1;
}

void Unit::attackUnit(Unit *defender) {
    double attackForce = attack * (health / maxHealth),
           defenseForce = defender->defence *
                          (defender->health / defender->maxHealth) *
                          defender->getDefenceBonus(),
           totalDamage = attackForce + defenseForce;
    defender->health -= std::round((attackForce / totalDamage) * attack * 4.5);

    if (defender->health > 0 && !defender->abilities.test(Abilities::STIFF) &&
        std::abs(tile->x - defender->tile->x) <= defender->range &&
        std::abs(tile->y - defender->tile->y) <= defender->range) {
        health -= std::round((defenseForce / totalDamage) * defender->defence *
                             4.5);
    }

    canUseAbility = false;
    canAttack = false;
    canMove = abilities.test(Abilities::ESCAPE);

    if (defender->health <= 0) {
        Tile *defenderTile = defender->tile;
        defender->die();
        // only melee units move into the tile of a unit they kill
        if (range == 1) {
            move(defenderTile);
            canMove = abilities.test(Abilities::ESCAPE);
        }
        kills++;
        if (abilities.test(Abilities::PERSIST)) {
            canAttack = true;
        }
    } else if (health <= 0) {
        die();
    }
}

std::vector<Tile *> Unit::validAttacks() const {
    std::vector<Tile *> attacks;
    Map &map = game->map;

    for (int x = std::max(tile->x - range, 0);
         x <= std::min(tile->x + range, map.size - 1); x++) {
        for (int y = std::max(tile->y - range, 0);
             y <= std::min(tile->y + range, map.size - 1); y++) {
            Tile *target = map.at(x, y);
            if (target->unit && target->unit->player != player) {
                attacks.push_back(target);
            }
        }
    }

    return attacks;
}

void Unit::die() {
    tile->unit = nullptr;
    player->removeUnit(this);
}
