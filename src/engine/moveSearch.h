#pragma once

#include "data/terrain.h"
#include "rules.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <tuple>
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
    // takes road steps: a Creep unit doesn't (game), every step costs it the
    // same off mountains
    bool roads = true;
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
                bool road = f.road && mover.roads;
                int cost = road ? Rules::ROAD_STEP_COST : Rules::STEP_COST;
                bool stop = f.embark || f.mountain || (f.forest && !road && !mover.creep) ||
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

// The game's path finder (PathFinder.GetPath): an A* toward the
// straight-line distance to the target, in the game's units (an ordinary
// step 10). `cost(from, to)` is a step's cost, or 0 or less if it can't be
// taken; a tile is searched on from only while less than `limit` has been
// spent there. Neighbours are taken column by column from the west, and of
// equally promising tiles the first found goes first. Fills `parents` with
// each reached tile's previous tile (-1 for none); returns whether `target`
// was reached.
template <typename Cost>
bool gamePath(int size, int start, int target, int limit, Cost cost, std::vector<int> &parents) {
    constexpr int MOST_POPS = 1000;
    const int tiles = size * size;
    std::vector<int> spent(tiles, 0);
    parents.assign(tiles, -1);
    // (priority, order pushed, tile): the least first, then the earliest
    using Entry = std::tuple<float, int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    int pushed = 0;
    auto priority = [&](int tile, int g) {
        int dx = target % size - tile % size, dy = target / size - tile / size;
        return static_cast<float>(g) + static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
    };
    open.push({priority(start, 0), pushed++, start});
    for (int pops = 0; !open.empty() && pops < MOST_POPS; pops++) {
        int from = std::get<2>(open.top());
        open.pop();
        if (from == target) {
            return true;
        }
        if (spent[from] >= limit) {
            continue;
        }
        int cx = from % size, cy = from / size;
        for (int x = std::max(cx - 1, 0); x <= std::min(cx + 1, size - 1); x++) {
            for (int y = std::max(cy - 1, 0); y <= std::min(cy + 1, size - 1); y++) {
                int to = y * size + x;
                if (to == from) {
                    continue;
                }
                int c = cost(from, to);
                if (c <= 0) {
                    continue;
                }
                int g = spent[from] + c;
                // the game takes an unreached tile's spent 0 as unreached, the
                // start's too
                if (spent[to] < 1 || g < spent[to]) {
                    spent[to] = g;
                    parents[to] = from;
                    open.push({priority(to, g), pushed++, to});
                }
            }
        }
    }
    return false;
}

// The route a unit takes to `target`, as the game's own path finder picks it
// among routes of equal cost: gamePath over the same steps as searchMoves,
// with the game's costs: 10 a step (5 on a road), 1000 for a step that ends
// the unit's movement, searched on only while less than 10 per point of
// movement has been spent.
template <typename Facts>
void searchPath(int size, int start, int target, const Mover &mover, Facts facts,
                std::vector<int> &parents) {
    constexpr int STEP = 10, ROAD_STEP = 5, STOP_STEP = 1000;
    gamePath(size, start, target, STEP * mover.movement, [&](int from, int to) {
        StepFacts f = facts(from, to);
        if (!f.enter && !f.land) {
            return 0;
        }
        bool road = f.road && mover.roads;
        bool stop = f.land || f.embark || f.mountain || (f.forest && !road && !mover.creep) ||
                    (!mover.ignoreZoc && f.nextToEnemy);
        return stop ? STOP_STEP : road ? ROAD_STEP : STEP;
    }, parents);
}
