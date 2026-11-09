#include "game.h"
#include <cmath>

double Game::defenceBonus(const Unit &unit) const {
    const Tile &tile = tiles[unit.tile];
    int city = cityAt(unit.tile);
    if (city >= 0 && cities[city].owner == unit.owner &&
        unit.has(Abilities::FORTIFY)) {
        return cities[city].wall ? Rules::WALL_BONUS : Rules::FORTIFY_BONUS;
    }
    for (const TechData &tech : techData) {
        if (players[unit.owner].hasTech(tech.type) &&
            tech.defenceBonuses.test(tile.terrain)) {
            return Rules::TERRAIN_BONUS;
        }
    }
    return 1.0;
}

Game::AttackResult Game::previewAttack(const Unit &attacker,
                                       const Unit &defender) const {
    Rules::Damage d = Rules::damage(
        attacker.data().attack, attacker.hp, attacker.maxHp,
        defender.data().defence, defender.hp, defender.maxHp,
        defenceBonus(defender));
    return {d.dealt, d.returned};
}

std::vector<int> Game::attackTargets(int unitIdx) const {
    const Unit &unit = units[unitIdx];
    std::vector<int> targets;

    // Infiltrate units only ever target adjacent enemy cities
    if (unit.has(Abilities::INFILTRATE)) {
        forEachNeighbour(unit.tile, [&](int n) {
            int city = cityAt(n);
            if (city >= 0 && hostile(unit.owner, cities[city].owner) &&
                explored(unit.owner, n)) {
                targets.push_back(n);
            }
        });
        return targets;
    }
    if (unit.data().attack == 0 && !unit.has(Abilities::CONVERT)) {
        return targets;
    }
    forEachInRadius(unit.tile, unit.data().range, [&](int n) {
        const Unit *other = unitAt(n);
        if (other && hostile(unit.owner, other->owner) &&
            canSee(unit.owner, *other)) {
            targets.push_back(n);
        }
    });
    std::sort(targets.begin(), targets.end());
    return targets;
}

void Game::splashDamage(int attackerIdx, int target, int amount) {
    Unit &unit = units[target];
    unit.hp -= amount;
    if (unit.hp <= 0) {
        killUnit(target);
        creditKill(attackerIdx);
    }
}

void Game::stomp(int unitIdx) {
    const Unit &unit = units[unitIdx];
    std::vector<int> victims;
    forEachNeighbour(unit.tile, [&](int n) {
        const Unit *other = unitAt(n);
        if (other && hostile(unit.owner, other->owner)) {
            victims.push_back(tiles[n].unit);
        }
    });
    // half of what an attack on each would deal, not rounded
    for (int victim : victims) {
        splashDamage(unitIdx, victim,
                     previewAttack(unit, units[victim]).damage /
                         Rules::SPLASH_DIVISOR);
    }
}

void Game::attack(int unitIdx, int target) {
    Unit &unit = units[unitIdx];
    if (unit.has(Abilities::INFILTRATE)) {
        infiltrate(unitIdx, target);
        return;
    }
    int defenderIdx = tiles[target].unit;
    if (unit.has(Abilities::CONVERT)) {
        convert(unitIdx, defenderIdx);
        return;
    }
    Unit &defender = units[defenderIdx];
    onAttack(unit.owner, defender.owner);

    AttackResult result = previewAttack(unit, defender);
    unit.canAttack = false;
    unit.canAct = false;
    unit.canMove = unit.has(Abilities::ESCAPE);

    defender.hp -= result.damage;
    bool retaliates = defender.hp > 0 &&
                      distance(unit.tile, target) <= defender.data().range &&
                      !defender.has(Abilities::STIFF) &&
                      // an attacker the defender can't see isn't hit back
                      canSee(defender.owner, unit) &&
                      !unit.has(Abilities::SURPRISE) && result.retaliation > 0;

    if (unit.has(Abilities::SPLASH)) {
        std::vector<int> splashed;
        forEachNeighbour(target, [&](int n) {
            const Unit *other = unitAt(n);
            if (other && hostile(unit.owner, other->owner)) {
                splashed.push_back(tiles[n].unit);
            }
        });
        for (int victim : splashed) {
            splashDamage(unitIdx, victim,
                         result.damage / Rules::SPLASH_DIVISOR);
        }
    }

    if (defender.hp <= 0) {
        killUnit(defenderIdx);
        creditKill(unitIdx);
        // melee attackers move into the tile, if they could stand there
        if (unit.data().range == 1 && canEnterTerrain(unit, target)) {
            placeUnit(unitIdx, target);
        }
        if (unit.has(Abilities::PERSIST)) {
            unit.canAttack = true;
        }
    } else if (retaliates) {
        unit.hp -= result.retaliation;
        if (unit.hp <= 0) {
            killUnit(unitIdx);
            creditKill(defenderIdx);
        }
    }
}

void Game::convert(int unitIdx, int targetIdx) {
    Unit &unit = units[unitIdx];
    Unit &target = units[targetIdx];
    onAttack(unit.owner, target.owner);
    target.owner = unit.owner;
    target.home = -1;
    markOccupied(unit.owner, target.tile);
    // counts toward the capital, or a random city without one, even past its
    // cap
    if (!target.has(Abilities::INDEPENDENT)) {
        int capital = players[unit.owner].capital;
        if (capital >= 0 && cities[capital].owner == unit.owner) {
            target.home = capital;
        } else {
            std::vector<int> owned;
            for (std::size_t i = 0; i < cities.size(); i++) {
                if (cities[i].owner == unit.owner) {
                    owned.push_back(static_cast<int>(i));
                }
            }
            if (!owned.empty()) {
                target.home = owned[randomInt(static_cast<int>(owned.size()))];
            }
        }
    }
    target.canMove = target.canAttack = target.canAct = false;
    target.hidden = false;
    revealForUnit(targetIdx);
    unit.canAttack = false;
    unit.canAct = false;
    unit.canMove = unit.has(Abilities::ESCAPE);
}

void Game::infiltrate(int unitIdx, int target) {
    int owner = units[unitIdx].owner;
    int city = cityAt(target);
    onAttack(owner, cities[city].owner);
    killUnit(unitIdx);
    int count = std::min(cities[city].level, Rules::INFILTRATION_MAX_DAGGERS);

    // the city tile first, then outward
    std::vector<int> spots;
    for (int radius = 0; radius <= 2; radius++) {
        auto consider = [&](int t) {
            if (distance(t, target) == radius && tiles[t].territory == city &&
                tiles[t].unit < 0) {
                spots.push_back(t);
            }
        };
        if (radius == 0) {
            consider(target);
        } else {
            forEachInRadius(target, radius, consider);
        }
    }

    for (int i = 0; i < count && i < static_cast<int>(spots.size()); i++) {
        int tile = spots[i];
        bool water = TerrainTypes::isWater(tiles[tile].terrain) &&
                     !tiles[tile].road;
        int dagger = spawnUnit(owner, water ? UnitTypes::PIRATE
                                            : UnitTypes::DAGGER,
                               tile, -1);
        units[dagger].passenger = UnitTypes::DAGGER;
    }
}
