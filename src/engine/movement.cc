#include "game.h"
#include "moveSearch.h"

namespace {
int sign(int v) { return (v > 0) - (v < 0); }

// what the mover's owner knows of each step, for searchMoves and searchPath
// (the rules are theirs)
auto stepFacts(const Game &game, const Unit &unit) {
    bool naval = unit.isNaval();
    bool hide = unit.has(Abilities::HIDE);
    bool ignoreZoc = unit.has(Abilities::CREEP) || hide;
    Unit passenger = unit;
    passenger.type = unit.passenger;
    return [&game, &unit, naval, hide, ignoreZoc, passenger](int from, int to) {
        StepFacts f;
        const Tile &t = game.tiles[to];
        // moves never touch unexplored tiles
        if (!game.explored(unit.owner, to)) {
            return f;
        }
        // what the mover's owner sees on the tile: hidden enemies look
        // like empty ground
        const Unit *other = t.unit >= 0 ? &game.units[t.unit] : nullptr;
        // allies are passed through like friends
        bool seenFriend = other && !game.hostile(unit.owner, other->owner);
        bool seenEnemy = other && !seenFriend && game.canSee(unit.owner, *other);
        bool looksFree = !seenFriend && !seenEnemy;
        f.free = looksFree;
        if (naval && !TerrainTypes::isWater(t.terrain)) {
            f.land = looksFree && game.canEnterTerrain(passenger, to);
            return f;
        }
        f.embark = !naval && game.embarksAt(unit.owner, to);
        // friendly units can be passed through; enemies only by Hide units;
        // a bridge only along it or diagonally
        f.enter = (f.embark || game.canEnterTerrain(unit, to)) && !(seenEnemy && !hide) &&
                  !(!naval && game.bridgeSideStep(unit.owner, from, to));
        if (!f.enter) {
            return f;
        }
        f.road = !naval && game.isRoadFor(from, unit.owner) && game.isRoadFor(to, unit.owner);
        f.mountain = t.terrain == TerrainTypes::MOUNTAIN;
        f.forest = t.terrain == TerrainTypes::FOREST;
        f.nextToEnemy = !ignoreZoc && game.nextToEnemy(to, unit.owner);
        return f;
    };
}

Mover moverOf(const Game &game, const Unit &unit) {
    bool creep = unit.has(Abilities::CREEP);
    return {.movement = unit.data().movement, .creep = creep,
            .ignoreZoc = creep || unit.has(Abilities::HIDE),
            .roads = !(creep && game.settings.follows(RuleVersions::CREEP_NO_ROADS))};
}
} // namespace

bool Game::canEnterTerrain(const Unit &unit, int tile) const {
    return canEnterTerrain(unit.owner, unit.isNaval(), tile);
}

bool Game::canEnterTerrain(int player, bool naval, int tile) const {
    const Tile &t = tiles[tile];
    const Player &p = players[player];
    // naval units may use shallow water without Fishing: owning a boat
    // already means they got onto the water
    return terrainEnterable(naval, t.terrain, t.road, p.hasTech(TechTypes::SAILING),
                            p.hasTech(TechTypes::CLIMBING));
}

bool Game::embarksAt(int player, int tile) const {
    int owner = tileOwner(tile);
    if (settings.follows(RuleVersions::EMBARK_NEEDS_PORT_TECH) &&
        !players[player].hasTech(improvementInfo(ImprovementTypes::PORT).tech)) {
        return false;
    }
    return tiles[tile].building == ImprovementTypes::PORT && owner >= 0 &&
           !hostile(owner, player);
}

bool Game::isRoadFor(int tile, int player) const {
    const Tile &t = tiles[tile];
    // a peace treaty opens the ally's roads
    int owner = tileOwner(tile);
    bool village = t.building == ImprovementTypes::VILLAGE &&
                   settings.follows(RuleVersions::VILLAGE_ROADS);
    return roadFor(t.road || t.building == ImprovementTypes::CITY || village, owner,
                   owner >= 0 && hostile(owner, player));
}

bool Game::nextToEnemy(int tile, int player) const {
    // units in fog exert zone of control too, hidden ones (Hide) don't
    // (game)
    bool found = false;
    forEachNeighbour(tile, [&](int n) {
        const Unit *other = unitAt(n);
        found |= other && hostile(player, other->owner) && !other->hidden &&
                 (settings.follows(RuleVersions::UNSEEN_ZONE_OF_CONTROL) ||
                  canSee(player, *other));
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
              .scoreOwner = player,
              .facing = Directions::nearest(size / 2 - tileX(tile), size / 2 - tileY(tile))};
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
    markOccupied(player, tile);
    revealForUnit(idx);
    return idx;
}

void Game::reachable(int unitIdx, std::vector<int> *moves,
                     std::vector<int> *disembarks,
                     std::vector<int> *parents) const {
    const Unit &unit = units[unitIdx];
    bool hide = unit.has(Abilities::HIDE);

    // the buffers are reused between calls: this runs for every unit on
    // every legal-move query
    thread_local std::vector<int> arrival;
    thread_local std::vector<char> destination, landing;
    if (parents) {
        // the most movement left on arriving at each tile, to keep the best
        // route to it
        arrival.assign(tiles.size(), -1);
        parents->assign(tiles.size(), -1);
    }
    destination.assign(tiles.size(), 0);
    landing.assign(tiles.size(), 0);

    auto facts = stepFacts(*this, unit);
    auto arrive = [&](int from, int to, int remaining, const StepFacts &f) {
        int city = cityAt(to);
        // no stopping on the city of a player at peace with them, or one
        // they broke peace with and are cooling down from (game)
        bool peacefulCity =
            city >= 0 && settings.follows(RuleVersions::NO_STOP_ON_PEACEFUL_CITIES) &&
            (atPeace(unit.owner, cities[city].owner) ||
             (players[unit.owner].coolDown >> cities[city].owner & 1));
        if (f.land) {
            // a Cloak lands as itself: not on an enemy city, which it
            // infiltrates instead
            bool hiddenOnCity = city >= 0 && hostile(unit.owner, cities[city].owner) &&
                                unitInfo(unit.passenger).abilities.test(Abilities::HIDE) &&
                                settings.follows(RuleVersions::NO_HIDDEN_LANDING_ON_CITIES);
            if (!(peacefulCity && settings.follows(RuleVersions::NO_LANDING_ON_PEACEFUL_CITIES)) &&
                !hiddenOnCity) {
                landing[to] = 1;
            }
        } else {
            // Hide units can pass through an enemy city but not stop on it;
            // they infiltrate it instead
            bool enemyCity = city >= 0 && hostile(unit.owner, cities[city].owner);
            if (f.free && !(hide && enemyCity) && !peacefulCity) {
                destination[to] = 1;
            }
        }
        if (parents && remaining > arrival[to]) {
            arrival[to] = remaining;
            (*parents)[to] = from;
        }
    };
    searchMoves(size, unit.tile, moverOf(*this, unit), facts, arrive);

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

void Game::markOccupied(int player, int tile) {
    int &first = firstOccupied[player * size * size + tile];
    if (first < 0) {
        first = turn;
    }
}

void Game::placeUnit(int unitIdx, int to, int from) {
    Unit &unit = units[unitIdx];
    int dx = sign(tileX(to) - tileX(unit.tile)),
        dy = sign(tileY(to) - tileY(unit.tile));
    if (dx != 0 || dy != 0) {
        unit.lastDx = dx;
        unit.lastDy = dy;
        // it turns the way its last step went (game), before a stomp turns
        // it to its victims
        int last = from >= 0 ? from : unit.tile;
        unit.facing = Directions::nearest(tileX(to) - tileX(last), tileY(to) - tileY(last));
    }
    tiles[unit.tile].unit = -1;
    tiles[to].unit = unitIdx;
    unit.tile = to;
    markOccupied(unit.owner, to);
    revealForUnit(unitIdx);
    if (unit.has(Abilities::STOMP)) {
        stomp(unitIdx);
    }
}

namespace {
// A move onto a tile with a hidden enemy reveals it. The mover stays where it
// was and keeps its move. Returns true if that happened.
bool bumpsHidden(Game &game, int unitIdx, int to) {
    Unit *other = game.unitAt(to);
    if (!other) {
        return false;
    }
    other->hidden = false;
    // the mover stays, turned toward it
    if (game.settings.follows(RuleVersions::BUMP_TURNS)) {
        Unit &unit = game.units[unitIdx];
        unit.facing = Directions::nearest(game.tileX(to) - game.tileX(unit.tile),
                                          game.tileY(to) - game.tileY(unit.tile));
    }
    return true;
}
} // namespace

int Game::revealAlongPath(int unitIdx, int to) {
    const Unit &unit = units[unitIdx];
    std::vector<int> parents;
    if (settings.follows(RuleVersions::GAME_PATHS)) {
        searchPath(size, unit.tile, to, moverOf(*this, unit), stepFacts(*this, unit), parents);
    } else {
        // before: the route the legal-move search found first leaving the
        // most movement
        reachable(unitIdx, nullptr, nullptr, &parents);
    }
    // every tile passed on the way, not the start or the end
    for (int t = parents[to]; t >= 0 && t != unit.tile; t = parents[t]) {
        int sight = unit.has(Abilities::SCOUT) ||
                            tiles[t].terrain == TerrainTypes::MOUNTAIN
                        ? Rules::FAR_SIGHT
                        : Rules::UNIT_SIGHT;
        revealAround(unit.owner, t, sight);
    }
    return parents[to] >= 0 ? parents[to] : unit.tile;
}

void Game::moveUnit(int unitIdx, int to) {
    Unit &unit = units[unitIdx];
    if (bumpsHidden(*this, unitIdx, to)) {
        return;
    }
    // tiles are revealed along the whole path, not just at the end
    int last = revealAlongPath(unitIdx, to);
    placeUnit(unitIdx, to, last);
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
        embark(unitIdx);
    }
}

void Game::embark(int unitIdx) {
    Unit &unit = units[unitIdx];
    unit.type = boatFor(unit.passenger);
    // its turn is over, attacking included, so an upgrade gives it no attack
    // back (game: embarking sets moved and attacked)
    unit.canMove = unit.canAttack = false;
    unit.attacked = true;
    // a Giant embarking becomes a Juggernaut, which stomps as it arrives
    // (game)
    if (settings.follows(RuleVersions::EMBARK_STOMP) && unit.has(Abilities::STOMP)) {
        stomp(unitIdx);
    }
}

void Game::disembark(int unitIdx, int to) {
    Unit &unit = units[unitIdx];
    if (bumpsHidden(*this, unitIdx, to)) {
        return;
    }
    int last = revealAlongPath(unitIdx, to);
    placeUnit(unitIdx, to, last);
    unit.type = unit.passenger;
    unit.canMove = unit.canAttack = unit.canAct = false;
    if (unit.has(Abilities::HIDE)) {
        unit.hidden = true;
    }
    revealForUnit(unitIdx);
}

void Game::killUnit(int unitIdx) {
    Unit &unit = units[unitIdx];
    // a unit the current player saw die off someone else's score
    if (unit.scoreOwner != currentPlayer && canSee(currentPlayer, unit) &&
        !scoreEffects.empty()) {
        scoreEffects[currentPlayer * players.size() + unit.scoreOwner] -=
            unitScore(unit);
    }
    tiles[unit.tile].unit = -1;
    unit.alive = false;
    unit.hidden = false;
}
