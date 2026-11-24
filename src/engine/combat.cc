#include "game.h"
#include <cmath>

double Game::defenceBonus(const Unit &unit) const {
    const Tile &tile = tiles[unit.tile];
    int city = cityAt(unit.tile);
    if (city >= 0 && cities[city].owner == unit.owner &&
        unit.has(Abilities::FORTIFY)) {
        return cities[city].wall ? Rules::WALL_BONUS : Rules::FORTIFY_BONUS;
    }
    return terrainDefended(unit.owner, tile.terrain) ? Rules::TERRAIN_BONUS : 1.0;
}

bool Game::terrainDefended(int player, TerrainTypes::TerrainType terrain) const {
    for (const TechData &tech : techData) {
        if (players[player].hasTech(tech.type) && tech.defenceBonuses.test(terrain)) {
            return true;
        }
    }
    return false;
}

Game::AttackResult Game::previewAttack(const Unit &attacker,
                                       const Unit &defender) const {
    Rules::Damage d = Rules::damage(
        attacker.data().attack, attacker.hp, attacker.maxHp,
        defender.data().defence, defender.hp, defender.maxHp,
        defenceBonus(defender), settings.follows(RuleVersions::INTEGER_DAMAGE));
    return {d.dealt, d.returned};
}

std::vector<int> Game::attackTargets(int unitIdx, int from) const {
    const Unit &unit = units[unitIdx];
    const int at = from >= 0 ? from : unit.tile;
    std::vector<int> targets;

    // a player that broke peace can't attack the former ally for a while
    auto attackable = [&](int owner) {
        return hostile(unit.owner, owner) && !(players[unit.owner].coolDown >> owner & 1);
    };
    // Infiltrate units only ever target adjacent enemy cities
    if (unit.has(Abilities::INFILTRATE)) {
        forEachNeighbour(at, [&](int n) {
            int city = cityAt(n);
            const Unit *on = unitAt(n);
            bool friendlyOn = on && !hostile(unit.owner, on->owner) &&
                              settings.follows(RuleVersions::NO_INFILTRATE_OWN_OCCUPIED);
            if (city >= 0 && attackable(cities[city].owner) && !friendlyOn &&
                !cities[city].robbed && explored(unit.owner, n)) {
                targets.push_back(n);
            }
        });
        return targets;
    }
    if (unit.data().attack == 0 && !unit.has(Abilities::CONVERT)) {
        return targets;
    }
    forEachInRadius(at, unit.data().range, [&](int n) {
        const Unit *other = unitAt(n);
        if (other && attackable(other->owner) && canSee(unit.owner, *other)) {
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

int Game::areaDamage(const Unit &attacker, const Unit &victim) const {
    int half = previewAttack(attacker, victim).damage / Rules::SPLASH_DIVISOR;
    // floored to whole HP (game: GetFlooredHalfAttackDamage)
    return settings.follows(RuleVersions::AREA_DAMAGE_FLOORED) ? half / 10 * 10 : half;
}

std::vector<int> Game::areaVictims(const Unit &attacker, int tile) const {
    bool floored = settings.follows(RuleVersions::AREA_DAMAGE_FLOORED);
    std::vector<int> victims;
    // the game's order, column by column from the west, taken back to front
    // (its attacks are stacked)
    int x = tileX(tile), y = tileY(tile);
    for (int dx = 1; dx >= -1; dx--) {
        for (int dy = 1; dy >= -1; dy--) {
            if ((dx || dy) && inBounds(x + dx, y + dy)) {
                const Unit *other = unitAt(index(x + dx, y + dy));
                // the game spares own units, peace partners and a cooldown
                if (other && hostile(attacker.owner, other->owner) &&
                    !(floored && (players[attacker.owner].coolDown >> other->owner & 1))) {
                    victims.push_back(tiles[index(x + dx, y + dy)].unit);
                }
            }
        }
    }
    return victims;
}

void Game::stomp(int unitIdx) {
    const Unit &unit = units[unitIdx];
    // each victim turns to face the stomper, and it them in turn (game)
    for (int victim : areaVictims(unit, unit.tile)) {
        int dx = tileX(units[victim].tile) - tileX(unit.tile);
        int dy = tileY(units[victim].tile) - tileY(unit.tile);
        units[unitIdx].facing = Directions::nearest(dx, dy);
        units[victim].facing = Directions::nearest(-dx, -dy);
        int amount = areaDamage(unit, units[victim]);
        // a stomp that hurts starts Pacifist over, and is an attack on the
        // victim's tribe, unless the victim was hidden
        bool counts = amount > 0 && !(units[victim].hidden &&
                                      settings.follows(RuleVersions::HIDDEN_STOMPS_UNCOUNTED));
        if (counts && settings.follows(RuleVersions::STOMP_AND_SIEGE_ATTACK)) {
            onAttack(unit.owner, units[victim].owner, true);
        } else if (counts && settings.follows(RuleVersions::PACIFIST_NEEDS_DAMAGE)) {
            players[unit.owner].peacefulTurns = 0;
        }
        splashDamage(unitIdx, victim, amount);
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
    AttackResult result = previewAttack(unit, defender);
    unit.attacked = true;
    // (an attack that rounds to no damage doesn't count for Pacifist)
    onAttack(unit.owner, defender.owner, result.damage > 0);
    // they turn to face each other
    int dx = tileX(target) - tileX(unit.tile), dy = tileY(target) - tileY(unit.tile);
    unit.facing = Directions::nearest(dx, dy);
    defender.facing = Directions::nearest(-dx, -dy);

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

    bool floored = settings.follows(RuleVersions::AREA_DAMAGE_FLOORED);
    if (unit.has(Abilities::SPLASH) && !floored) {
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
    // the game's splash, once the target is dealt with: each victim as its
    // own attack, the attacker turning to each (only from range 2 up)
    auto splash = [&] {
        if (!floored || !unit.has(Abilities::SPLASH) || unit.data().range < 2) {
            return;
        }
        for (int victim : areaVictims(unit, target)) {
            int dx = tileX(units[victim].tile) - tileX(unit.tile);
            int dy = tileY(units[victim].tile) - tileY(unit.tile);
            int amount = areaDamage(unit, units[victim]);
            unit.facing = Directions::nearest(dx, dy);
            units[victim].facing = Directions::nearest(-dx, -dy);
            splashDamage(unitIdx, victim, amount);
        }
    };

    if (defender.hp <= 0) {
        killUnit(defenderIdx);
        creditKill(unitIdx);
        splash();
        // melee attackers move into the tile, if they could stand there; a
        // boat onto land lets its passenger off there
        bool lands = settings.follows(RuleVersions::KILL_LANDS) && unit.isNaval() &&
                     !TerrainTypes::isWater(tiles[target].terrain) &&
                     canEnterTerrain(unit.owner, false, target);
        bool embarks = settings.follows(RuleVersions::KILL_EMBARKS) && !unit.isNaval() &&
                       embarksAt(unit.owner, target);
        bool movesIn = unit.data().range == 1 &&
                       (lands || embarks || canEnterTerrain(unit, target));
        // a unit that embarks or lands there is done for the turn
        bool turnEnds = settings.follows(RuleVersions::PERSIST_BEFORE_MOVE_IN) &&
                        movesIn && (lands || embarks);
        if (unit.data().range == 1 && lands) {
            disembark(unitIdx, target);
        } else if (unit.data().range == 1 && embarks) {
            placeUnit(unitIdx, target);
            embark(unitIdx);
        } else if (unit.data().range == 1 && canEnterTerrain(unit, target)) {
            placeUnit(unitIdx, target);
        }
        // targets as seen from where it ends up (game: tiles the move in
        // reveals count)
        if (unit.has(Abilities::PERSIST) && !turnEnds &&
            (!settings.follows(RuleVersions::PERSIST_NEEDS_TARGET) ||
             !attackTargets(unitIdx).empty())) {
            unit.canAttack = true;
        }
    } else if (retaliates) {
        splash();
        // the hit back turns them to each other again
        unit.facing = Directions::nearest(dx, dy);
        defender.facing = Directions::nearest(-dx, -dy);
        unit.hp -= result.retaliation;
        if (unit.hp <= 0) {
            killUnit(unitIdx);
            creditKill(defenderIdx);
        }
    } else {
        splash();
    }
}

void Game::convert(int unitIdx, int targetIdx) {
    Unit &unit = units[unitIdx];
    Unit &target = units[targetIdx];
    unit.attacked = true;
    onAttack(unit.owner, target.owner, false);
    players[target.owner].convertedAway.push_back({target.type, target.passenger});
    if (settings.follows(RuleVersions::CONVERSION_TAKES_SCORE)) {
        // the unit's score goes with it, and they turn to face each other
        int dx = tileX(target.tile) - tileX(unit.tile), dy = tileY(target.tile) - tileY(unit.tile);
        unit.facing = Directions::nearest(dx, dy);
        target.facing = Directions::nearest(-dx, -dy);
        // seen by the converter, off the former owner's score
        if (target.scoreOwner != unit.owner && !scoreEffects.empty()) {
            scoreEffects[unit.owner * players.size() + target.scoreOwner] -= unitScore(target);
        }
        target.scoreOwner = unit.owner;
    }
    target.owner = unit.owner;
    target.home = -1;
    markOccupied(unit.owner, target.tile);
    // counts toward the capital, or a random city without one, even past its
    // cap
    if (!target.has(Abilities::INDEPENDENT) ||
        (target.type != target.passenger && settings.follows(RuleVersions::GAME_DAGGER_HOMES))) {
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
            if (forced.convertedHome >= 0) {
                target.home = cityAt(forced.convertedHome);
                forced.convertedHome = -1;
            } else if (!owned.empty()) {
                target.home = owned[randomInt(static_cast<int>(owned.size()))];
            }
        }
    }
    target.passengerHome = target.home;
    target.canMove = target.canAttack = target.canAct = false;
    // spent, as the game's new units are (an upgrade gives it no attack)
    target.attacked = settings.follows(RuleVersions::ACTIONS_SET_ATTACKED);
    target.hidden = false;
    revealForUnit(targetIdx);
    unit.canAttack = false;
    unit.canAct = false;
    unit.canMove = unit.has(Abilities::ESCAPE);
}

void Game::infiltrate(int unitIdx, int target) {
    if (!settings.follows(RuleVersions::GAME_INFILTRATION)) {
        infiltrateOld(unitIdx, target);
        return;
    }
    int owner = units[unitIdx].owner;
    int city = cityAt(target);
    City &c = cities[city];
    onAttack(owner, c.owner, false);
    int level = c.level;
    killUnit(unitIdx);

    // the defender, hit as by a fresh Cloak of the infiltrator's, which takes
    // nothing back; it turns toward (-1, -1), the attack's missing origin
    bool defended = tiles[target].unit >= 0;
    if (defended) {
        int d = tiles[target].unit;
        Unit cloak;
        cloak.type = UnitTypes::CLOAK;
        cloak.owner = owner;
        cloak.tile = target;
        cloak.hp = cloak.maxHp = unitInfo(UnitTypes::CLOAK).maxHp;
        Unit &defender = units[d];
        int damage = std::min(previewAttack(cloak, defender).damage, defender.hp);
        defender.facing = Directions::nearest(-1 - tileX(target), -1 - tileY(target));
        defender.hp -= damage;
        if (defender.hp <= 0) {
            killUnit(d);
            players[owner].kills++;
        }
    }

    // the Daggers' tiles: explored ones of the city's border area it can
    // stand on (Pirates on water, with the water's tech), best first
    bool pirates = players[owner].hasTech(TechTypes::FISHING);
    auto standable = [&](int t) {
        bool land = canEnterTerrain(owner, false, t) || embarksAt(owner, t);
        return land || (pirates && canEnterTerrain(owner, true, t));
    };
    std::vector<std::pair<float, int>> ranked;
    int radius = c.borderGrown ? Rules::GROWN_BORDER_RADIUS : Rules::CITY_BORDER_RADIUS;
    auto score = [&](int t, float jitter) {
        float s = jitter;
        s += tiles[t].building == ImprovementTypes::CITY ? 2.0f : 0.0f;
        s -= TerrainTypes::isWater(tiles[t].terrain) ? 0.1f : 0.0f;
        s += tiles[t].resource != ResourceTypes::NONE && resourceVisible(owner, tiles[t].resource)
                 ? 0.5f : 0.0f;
        s += terrainDefended(owner, tiles[t].terrain) ? 1.0f : 0.0f;
        return s;
    };
    std::vector<float> jitter(tiles.size(), 0.0f);
    auto consider = [&](int t) {
        if (tiles[t].territory == city && tiles[t].unit < 0 && explored(owner, t) && standable(t)) {
            // (the game's jitter is a hash of the tile and turn, below 0.1)
            jitter[t] = static_cast<float>(randomInt(1000)) / 10000.0f;
            ranked.push_back({score(t, jitter[t]), t});
        }
    };
    consider(target);
    forEachInRadius(target, radius, consider);
    // a city nobody was on is on the list twice: the second Dagger there
    // pushes the first off (game); one whose defender fell, once
    if (!defended && standable(target)) {
        ranked.push_back({score(target, jitter[target]), target});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });

    auto spawnDagger = [&](int tile, int facing, bool look) {
        bool boat = TerrainTypes::isWater(tiles[tile].terrain) && !tiles[tile].road &&
                    !embarksAt(owner, tile);
        // on the city tile it belonged to that city before GAME_DAGGER_HOMES
        int home = tile == target && settings.follows(RuleVersions::GAME_UNIT_HOMES) &&
                           !settings.follows(RuleVersions::GAME_DAGGER_HOMES)
                       ? city : -1;
        int dagger = spawnUnit(owner, boat ? UnitTypes::PIRATE : UnitTypes::DAGGER, tile, home);
        Unit &u = units[dagger];
        u.passenger = UnitTypes::DAGGER;
        // a Pirate is a boat: the health is its passenger's, the Dagger's
        u.hp = u.maxHp = unitInfo(UnitTypes::DAGGER).maxHp;
        u.canMove = u.canAttack = u.canAct = false;
        if (facing >= 0) {
            u.facing = facing;
        } else if (look) {
            // it faces the city; on the city itself no way, which pushes as SW
            int dx = tileX(target) - tileX(tile), dy = tileY(target) - tileY(tile);
            u.facing = tile == target ? 0 : Directions::nearest(dx, dy);
        }
        return dagger;
    };
    if (forced.daggers) {
        std::vector<std::pair<int, int>> placed = std::move(*forced.daggers);
        forced.daggers.reset();
        for (auto [tile, facing] : placed) {
            spawnDagger(tile, facing, true);
        }
    } else {
        int count = std::min({level, Rules::INFILTRATION_MAX_DAGGERS, static_cast<int>(ranked.size())});
        for (int i = 0; i < count; i++) {
            int tile = ranked[i].second;
            if (tiles[tile].unit >= 0) {
                // the city a second time: the Dagger there is pushed off, and
                // the new one faces as a new unit does
                pushUnit(tiles[tile].unit, owner);
                spawnDagger(tile, -1, false);
            } else {
                spawnDagger(tile, -1, true);
            }
        }
    }

    // the rebellion: the city's base production for the infiltrator (its
    // level, workshop and parks, 1 more for its owner's original capital),
    // and it's robbed
    players[owner].stars += level + c.workshop + c.parks + (c.capitalOf == c.owner ? 1 : 0);
    c.robbed = true;
}

void Game::infiltrateOld(int unitIdx, int target) {
    int owner = units[unitIdx].owner;
    int city = cityAt(target);
    onAttack(owner, cities[city].owner, false);
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
        if (settings.follows(RuleVersions::DAGGERS_FACE_CITY)) {
            // the game's NONE on the city tile itself: a push treats it as
            // SW, both for its owner's pushes and turned round for others'
            int dx = tileX(target) - tileX(tile), dy = tileY(target) - tileY(tile);
            units[dagger].facing = tile == target ? 0 : Directions::nearest(dx, dy);
        }
    }
}
