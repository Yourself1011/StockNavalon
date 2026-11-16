#pragma once

#include "data/terrain.h"
#include "rules.h"
#include <algorithm>
#include <vector>

// The movement rules (GAME.md section 10) as one search over the map, for
// everything that works out where a unit can go: the engine's legal moves
// (Game::reachable) and the bot's encoding of where enemy units could go
// (from what its player knows). The callers differ only in what they know
// about each tile, which they give as StepFacts; the costs and when movement
// ends are decided here.

// Whether a unit can enter terrain: a naval unit water, and ocean with
// Sailing; a land unit fields and forests, mountains with Climbing, and
// water only over a bridge
inline bool terrainEnterable(bool naval, TerrainTypes::TerrainType terrain, bool bridge,
                             bool sailing, bool climbing) {
    if (naval) {
        return terrain == TerrainTypes::WATER || (terrain == TerrainTypes::OCEAN && sailing);
    }
    switch (terrain) {
    case TerrainTypes::FIELD:
    case TerrainTypes::FOREST:
        return true;
    case TerrainTypes::MOUNTAIN:
        return climbing;
    case TerrainTypes::WATER:
        return bridge;
    default:
        return false;
    }
}

// Whether a road or city tile is a road for a unit: unless it's in the
// borders of a player the unit's owner is at war with
inline bool roadFor(bool roadOrCity, int tileOwner, bool ownerHostile) {
    return roadOrCity && (tileOwner < 0 || !ownerHostile);
}

// The most steps a unit can take in one move: all road steps with roads
// (the last step may cost more than what's left, but uses it all), else one
// per point of movement
inline int mostSteps(int movement, bool roads) {
    return roads ? movement * Rules::STEP_COST / Rules::ROAD_STEP_COST : movement;
}

// What a step onto a tile means for the moving unit
struct StepFacts {
    // the unit can step onto it (terrain, and nobody it can't pass)
    bool enter = false;
    // a naval unit stepping onto land: it lands there, and goes no further
    bool land = false;
    // nobody (that the searcher knows of) is on it, so the unit could stop
    // there; for the callers, the search doesn't use it
    bool free = false;
    // a road step: the tile stepped from and this one are both road-like
    // for the unit
    bool road = false;
    // what ends the unit's movement on arriving; nextToEnemy only matters
    // to units that don't ignore zone of control, so callers can leave it
    bool forest = false, mountain = false, embark = false, nextToEnemy = false;
};

struct Mover {
    int movement;
    // ignores the forest's stop (not a mountain's)
    bool creep = false;
    // ignores zone of control
    bool ignoreZoc = false;
};

// Searches from `start` for `mover`, in half steps (a road step costs half).
// `facts(from, to)` gives a StepFacts for a step between neighbours;
// `arrive(from, to, remaining, facts)` is called for every step the unit can
// take, with the movement it had before it, so callers can collect
// destinations and routes. Any movement left allows a step, even one costing
// more than what's left, which then uses it all.
template <typename Facts, typename Arrive>
void searchMoves(int size, int start, const Mover &mover, Facts facts, Arrive arrive) {
    // reused between calls: this runs for every unit on every legal-move query
    thread_local std::vector<int> best, stack;
    best.assign(static_cast<std::size_t>(size) * size, -1);
    best[start] = mover.movement * Rules::STEP_COST;
    stack.assign(1, start);
    while (!stack.empty()) {
        int from = stack.back();
        stack.pop_back();
        int remaining = best[from];
        if (remaining <= 0) {
            continue;
        }
        int cx = from % size, cy = from / size;
        for (int y = std::max(cy - 1, 0); y <= std::min(cy + 1, size - 1); y++) {
            for (int x = std::max(cx - 1, 0); x <= std::min(cx + 1, size - 1); x++) {
                int to = y * size + x;
                if (to == from) {
                    continue;
                }
                StepFacts f = facts(from, to);
                if (!f.enter && !f.land) {
                    continue;
                }
                arrive(from, to, remaining, f);
                if (f.land) {
                    continue;
                }
                int cost = f.road ? Rules::ROAD_STEP_COST : Rules::STEP_COST;
                bool stop = f.embark || f.mountain || (f.forest && !f.road && !mover.creep) ||
                            (!mover.ignoreZoc && f.nextToEnemy);
                int left = std::max(0, remaining - cost);
                if (!stop && left > best[to]) {
                    best[to] = left;
                    stack.push_back(to);
                }
            }
        }
    }
}
