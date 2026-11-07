#include "game.h"

namespace {
int sign(int v) { return (v > 0) - (v < 0); }
} // namespace

bool Game::canEnterTerrain(const Unit &unit, int tile) const {
    const Tile &t = tiles[tile];
    const Player &player = players[unit.owner];
    // naval units may use shallow water without Fishing: owning a boat
    // already means they got onto the water
    if (unit.isNaval()) {
        return t.terrain == TerrainTypes::WATER ||
               (t.terrain == TerrainTypes::OCEAN &&
                player.hasTech(TechTypes::SAILING));
    }
    switch (t.terrain) {
    case TerrainTypes::FIELD:
    case TerrainTypes::FOREST:
        return true;
    case TerrainTypes::MOUNTAIN:
        return player.hasTech(TechTypes::CLIMBING);
    case TerrainTypes::WATER:
        // a bridge
        return t.road;
    default:
        return false;
    }
}

bool Game::isRoadFor(int tile, int player) const {
    const Tile &t = tiles[tile];
    if (!t.road && t.building != ImprovementTypes::CITY) {
        return false;
    }
    // a peace treaty opens the ally's roads
    int owner = tileOwner(tile);
    return owner < 0 || !hostile(owner, player);
}

bool Game::nextToEnemy(int tile, int player) const {
    bool found = false;
    forEachNeighbour(tile, [&](int n) {
        const Unit *other = unitAt(n);
        found |=
            other && hostile(player, other->owner) && canSee(player, *other);
    });
    return found;
}

int Game::spawnUnit(int player, UnitTypes::UnitType type, int tile, int home) {
    Unit unit{.type = type,
              .passenger = type,
              .owner = player,
              .tile = tile,
              .hp = unitInfo(type).maxHp,
              .maxHp = unitInfo(type).maxHp,
              .home = home,
              .scoreOwner = player};
    auto slot = std::find_if(units.begin(), units.end(),
                             [](const Unit &u) { return !u.alive; });
    int idx;
    if (slot == units.end()) {
        idx = static_cast<int>(units.size());
        units.push_back(unit);
    } else {
        idx = static_cast<int>(slot - units.begin());
        *slot = unit;
    }
    tiles[tile].unit = idx;
    revealForUnit(idx);
    return idx;
}

void Game::reachable(int unitIdx, std::vector<int> *moves,
                     std::vector<int> *disembarks) const {
    const Unit &unit = units[unitIdx];
    bool naval = unit.isNaval();
    bool creep = unit.has(Abilities::CREEP);
    bool hide = unit.has(Abilities::HIDE);
    bool ignoreZoc = creep || hide;
    Unit passenger = unit;
    passenger.type = unit.passenger;

    // movement is counted in half steps, since a road step costs half.
    // The buffers are reused between calls: this runs for every unit on
    // every legal-move query.
    thread_local std::vector<int> best, stack;
    thread_local std::vector<char> destination, landing;
    best.assign(tiles.size(), -1);
    destination.assign(tiles.size(), 0);
    landing.assign(tiles.size(), 0);
    best[unit.tile] = unit.data().movement * 2;
    stack.assign(1, unit.tile);

    while (!stack.empty()) {
        int from = stack.back();
        stack.pop_back();
        int remaining = best[from];

        forEachNeighbour(from, [&](int to) {
            const Tile &t = tiles[to];
            // moves never touch unexplored tiles
            if (!explored(unit.owner, to)) {
                return;
            }
            // what the mover's owner sees on the tile: hidden enemies look
            // like empty ground
            const Unit *other = t.unit >= 0 ? &units[t.unit] : nullptr;
            // allies are passed through like friends
            bool seenFriend = other && !hostile(unit.owner, other->owner);
            bool seenEnemy = other && !seenFriend && canSee(unit.owner, *other);
            bool looksFree = !seenFriend && !seenEnemy;

            if (naval && !TerrainTypes::isWater(t.terrain)) {
                if (remaining >= 2 && looksFree &&
                    canEnterTerrain(passenger, to)) {
                    landing[to] = 1;
                }
                return;
            }
            bool embark = !naval && t.building == ImprovementTypes::PORT &&
                          tileOwner(to) >= 0 &&
                          !hostile(tileOwner(to), unit.owner);
            if (!embark && !canEnterTerrain(unit, to)) {
                return;
            }
            // friendly units can be passed through; enemies only by Hide
            // units
            if (seenEnemy && !hide) {
                return;
            }

            bool roadStep = !naval && isRoadFor(from, unit.owner) &&
                            isRoadFor(to, unit.owner);
            int cost = roadStep ? 1 : 2;
            if (remaining < cost) {
                return;
            }
            // Hide units can pass through an enemy city but not stop on it;
            // they infiltrate it instead
            int city = cityAt(to);
            bool enemyCity =
                city >= 0 && hostile(unit.owner, cities[city].owner);
            if (looksFree && !(hide && enemyCity)) {
                destination[to] = 1;
            }

            bool stop =
                embark || t.terrain == TerrainTypes::MOUNTAIN ||
                (t.terrain == TerrainTypes::FOREST && !roadStep && !creep) ||
                (!ignoreZoc && nextToEnemy(to, unit.owner));
            int left = remaining - cost;
            if (!stop && left > best[to]) {
                best[to] = left;
                stack.push_back(to);
            }
        });
    }

    for (int tile = 0; tile < size * size; tile++) {
        if (moves && destination[tile] && tile != unit.tile) {
            moves->push_back(tile);
        }
        if (disembarks && landing[tile]) {
            disembarks->push_back(tile);
        }
    }
}

std::vector<int> Game::moveTargets(int unit) const {
    std::vector<int> moves;
    reachable(unit, &moves, nullptr);
    return moves;
}

std::vector<int> Game::disembarkTargets(int unit) const {
    std::vector<int> disembarks;
    if (units[unit].isNaval()) {
        reachable(unit, nullptr, &disembarks);
    }
    return disembarks;
}

void Game::placeUnit(int unitIdx, int to) {
    Unit &unit = units[unitIdx];
    int dx = sign(tileX(to) - tileX(unit.tile)),
        dy = sign(tileY(to) - tileY(unit.tile));
    if (dx != 0 || dy != 0) {
        unit.lastDx = dx;
        unit.lastDy = dy;
    }
    tiles[unit.tile].unit = -1;
    tiles[to].unit = unitIdx;
    unit.tile = to;
    revealForUnit(unitIdx);
    if (unit.has(Abilities::STOMP)) {
        stomp(unitIdx);
    }
}

namespace {
// A move onto a tile with a hidden enemy reveals it. The mover stays where it
// was and keeps its move. Returns true if that happened.
bool bumpsHidden(Game &game, int to) {
    Unit *other = game.unitAt(to);
    if (!other) {
        return false;
    }
    other->hidden = false;
    return true;
}
} // namespace

void Game::moveUnit(int unitIdx, int to) {
    Unit &unit = units[unitIdx];
    if (bumpsHidden(*this, to)) {
        return;
    }
    placeUnit(unitIdx, to);
    unit.canMove = false;
    unit.canAct = false;
    unit.canAttack = unit.canAttack && unit.has(Abilities::DASH);
    if (unit.has(Abilities::HIDE)) {
        // going from visible to hidden takes the rest of the turn: no Dash
        if (!unit.hidden) {
            unit.canAttack = false;
        }
        unit.hidden = true;
    }

    if (!unit.isNaval() && tiles[to].building == ImprovementTypes::PORT) {
        unit.type = boatFor(unit.passenger);
        unit.canAttack = false;
    }
}

void Game::disembark(int unitIdx, int to) {
    Unit &unit = units[unitIdx];
    if (bumpsHidden(*this, to)) {
        return;
    }
    placeUnit(unitIdx, to);
    unit.type = unit.passenger;
    unit.canMove = unit.canAttack = unit.canAct = false;
    if (unit.has(Abilities::HIDE)) {
        unit.hidden = true;
    }
    revealForUnit(unitIdx);
}

void Game::killUnit(int unitIdx) {
    Unit &unit = units[unitIdx];
    tiles[unit.tile].unit = -1;
    unit.alive = false;
    unit.hidden = false;
}
