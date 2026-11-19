#include "plans.h"
#include "game.h"
#include "moveSearch.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>

using namespace CommandTypes;

std::string planName(Plan plan) {
    switch (plan) {
    case Plan::BRIDGE_RUSH:
        return "bridge";
    case Plan::NAVAL:
        return "naval";
    default:
        return "none";
    }
}

Plan planNamed(const std::string &name) {
    if (name == "none") {
        return Plan::NONE;
    }
    if (name == "bridge") {
        return Plan::BRIDGE_RUSH;
    }
    if (name == "naval") {
        return Plan::NAVAL;
    }
    throw std::invalid_argument("Unknown plan: " + name);
}

namespace {
constexpr int FAR = std::numeric_limits<int>::max();

// The stars a command costs the player to move (negative: it pays), by
// playing it on a copy, so the costs stay the engine's. Moves, attacks and
// End Turn are taken to cost nothing.
int starsSpent(const Game &game, const Command &command) {
    if (command.type == MOVE || command.type == ATTACK || command.type == ENDTURN) {
        return 0;
    }
    Game copy = game;
    int player = game.currentPlayer;
    copy.apply(command);
    return game.players[player].stars - copy.players[player].stars;
}

// The tech to research next on the way to `target`: the first one along its
// line whose parent the player owns
TechTypes::TechType nextTech(const Player &player, TechTypes::TechType target) {
    TechTypes::TechType tech = target;
    while (!player.hasTech(techData[tech].parent)) {
        tech = techData[tech].parent;
    }
    return tech;
}

// Land tiles and bridges joined by steps between neighbours: each tile's
// landmass, -1 for open water. Land is what a land unit could walk on with
// every tech (a bridge is a road on water).
std::vector<int> landmasses(const Game &game) {
    const int count = static_cast<int>(game.tiles.size());
    auto passable = [&](int t) {
        const Tile &tile = game.tiles[t];
        return terrainEnterable(false, tile.terrain, tile.road, true, true);
    };
    std::vector<int> mass(count, -1);
    int next = 0;
    for (int start = 0; start < count; start++) {
        if (mass[start] >= 0 || !passable(start)) {
            continue;
        }
        std::vector<int> stack{start};
        mass[start] = next;
        while (!stack.empty()) {
            int t = stack.back();
            stack.pop_back();
            game.forEachNeighbour(t, [&](int n) {
                if (mass[n] < 0 && passable(n)) {
                    mass[n] = next;
                    stack.push_back(n);
                }
            });
        }
        next++;
    }
    return mass;
}

// Saving for a purchase: the purchases themselves, anything that costs
// nothing, and whatever leaves enough stars for the purchase; and once one
// is affordable, the turn doesn't end without it
std::vector<Command> saving(const Game &game, const std::vector<Command> &legal,
                            const std::vector<Command> &purchases, int reserve) {
    int stars = game.players[game.currentPlayer].stars;
    std::vector<Command> out;
    for (const Command &c : legal) {
        bool purchase = std::find(purchases.begin(), purchases.end(), c) != purchases.end();
        int spent = starsSpent(game, c);
        if (purchase || spent <= 0 || stars - spent >= reserve) {
            out.push_back(c);
        }
    }
    bool affordable = std::any_of(legal.begin(), legal.end(), [&](const Command &c) {
        return std::find(purchases.begin(), purchases.end(), c) != purchases.end();
    });
    if (affordable) {
        std::erase_if(out, [](const Command &c) { return c.type == ENDTURN; });
    }
    return out;
}

// The march: a unit outside its own cities moves (or disembarks) only
// toward the enemy capital, by `stepsFor(unit)` (steps from each tile; no
// further away), and the turn can't end while one of them can still get
// closer
template <typename StepsFor>
std::vector<Command> march(const Game &game, const std::vector<Command> &legal,
                           StepsFor stepsFor) {
    const int player = game.currentPlayer;
    auto marching = [&](int tile) {
        const Tile &t = game.tiles[tile];
        int unit = t.unit;
        return unit >= 0 && game.units[unit].owner == player &&
               !(t.building == ImprovementTypes::CITY && game.tileOwner(tile) == player) &&
               stepsFor(unit)[tile] != FAR;
    };
    std::vector<Command> out;
    bool closer = false;
    for (const Command &c : legal) {
        if ((c.type == MOVE || c.type == DISEMBARK) && marching(c.from)) {
            const std::vector<int> &s = stepsFor(game.tiles[c.from].unit);
            if (s[c.to] > s[c.from]) {
                continue;
            }
            closer |= s[c.to] < s[c.from];
        }
        out.push_back(c);
    }
    if (closer) {
        std::erase_if(out, [](const Command &c) { return c.type == ENDTURN; });
    }
    return out;
}

// The bridge sites to build next on a way from the home landmass to the
// target's with the fewest bridges: shallow water a bridge would span
// (Game::bridgeSpans), each joining the landmasses around it. Empty when
// they're joined, or no bridges could join them.
std::vector<int> nextCrossing(const Game &game, const std::vector<int> &mass, int home,
                              int target) {
    if (mass[home] == mass[target]) {
        return {};
    }
    const ImprovementData &bridge = improvementInfo(ImprovementTypes::BRIDGE);
    const int masses = 1 + *std::max_element(mass.begin(), mass.end());
    // each site and the landmasses it joins
    std::vector<std::pair<int, std::vector<int>>> sites;
    for (int t = 0; t < static_cast<int>(game.tiles.size()); t++) {
        const Tile &tile = game.tiles[t];
        if (tile.road || tile.building != ImprovementTypes::NONE ||
            !bridge.terrain.test(tile.terrain) || !game.bridgeSpans(t)) {
            continue;
        }
        std::vector<int> joined;
        game.forEachNeighbour(t, [&](int n) {
            if (mass[n] >= 0 &&
                std::find(joined.begin(), joined.end(), mass[n]) == joined.end()) {
                joined.push_back(mass[n]);
            }
        });
        if (joined.size() >= 2) {
            sites.push_back({t, std::move(joined)});
        }
    }
    // bridges needed from each landmass to the target's
    std::vector<int> needed(masses, FAR);
    std::queue<int> queue;
    needed[mass[target]] = 0;
    queue.push(mass[target]);
    while (!queue.empty()) {
        int m = queue.front();
        queue.pop();
        for (const auto &[t, joined] : sites) {
            if (std::find(joined.begin(), joined.end(), m) == joined.end()) {
                continue;
            }
            for (int other : joined) {
                if (needed[other] == FAR) {
                    needed[other] = needed[m] + 1;
                    queue.push(other);
                }
            }
        }
    }
    // the sites from home onto a landmass one bridge closer
    std::vector<int> out;
    if (needed[mass[home]] == FAR) {
        return out;
    }
    for (const auto &[t, joined] : sites) {
        bool fromHome = false, closer = false;
        for (int m : joined) {
            fromHome |= m == mass[home];
            closer |= needed[m] == needed[mass[home]] - 1;
        }
        if (fromHome && closer) {
            out.push_back(t);
        }
    }
    return out;
}

// The capitals' tiles of the player to move and the enemy, or -1 if either
// is missing
std::pair<int, int> capitals(const Game &game) {
    const int player = game.currentPlayer, enemy = 1 - player;
    int mine = game.players[player].capital, theirs = game.players[enemy].capital;
    if (mine < 0 || theirs < 0) {
        return {-1, -1};
    }
    return {game.cities[mine].tile, game.cities[theirs].tile};
}

// the bridge rush has its tech and the two landmasses joined (or no bridges
// could join them)
bool bridgeDone(const Game &game) {
    auto [home, target] = capitals(game);
    const Player &me = game.players[game.currentPlayer];
    return home < 0 || (me.hasTech(improvementInfo(ImprovementTypes::BRIDGE).tech) &&
                        nextCrossing(game, landmasses(game), home, target).empty());
}

std::vector<Command> bridgeRush(const Game &game, const std::vector<Command> &legal) {
    const int player = game.currentPlayer;
    const Player &me = game.players[player];
    auto [home, target] = capitals(game);
    if (home < 0) {
        return legal;
    }
    const ImprovementData &bridge = improvementInfo(ImprovementTypes::BRIDGE);
    // the bridge's tech first
    if (!me.hasTech(bridge.tech)) {
        TechTypes::TechType next = nextTech(me, bridge.tech);
        Command research{RESEARCH, -1, -1, next};
        return saving(game, legal, {research}, game.techCost(player, next));
    }
    // then the next bridge on the way with the fewest, until the two
    // landmasses are joined; after that, nothing
    std::vector<int> sites = nextCrossing(game, landmasses(game), home, target);
    if (sites.empty()) {
        return legal;
    }
    std::vector<Command> purchases;
    for (const Command &c : legal) {
        if (c.type == BUILD && c.arg == ImprovementTypes::BRIDGE &&
            std::find(sites.begin(), sites.end(), c.from) != sites.end()) {
            purchases.push_back(c);
        }
    }
    // nothing to save for while no site can be built yet (unexplored, in
    // someone's borders): stars only wait for a site lacking only them
    if (purchases.empty() &&
        std::none_of(sites.begin(), sites.end(), [&](int t) {
            return game.canBuild(player, t, ImprovementTypes::BRIDGE, false);
        })) {
        return legal;
    }
    int reserve = purchases.empty() ? bridge.cost : starsSpent(game, purchases.front());
    return saving(game, legal, purchases, reserve);
}

// The tech that lets boats onto the ocean
TechTypes::TechType oceanTech() {
    for (const TechData &tech : techData) {
        if (tech.movementUnlocks.test(TerrainTypes::OCEAN)) {
            return tech.type;
        }
    }
    return TechTypes::BASIC;
}

// Whether boats without the ocean's tech can get from the home landmass's
// shore to the target's, over water they can enter
bool shallowRoute(const Game &game, const std::vector<int> &mass, int home, int target) {
    std::vector<char> seen(game.tiles.size(), 0);
    std::vector<int> stack;
    auto shallow = [&](int t) {
        return terrainEnterable(true, game.tiles[t].terrain, false, false, false);
    };
    for (int t = 0; t < static_cast<int>(game.tiles.size()); t++) {
        if (mass[t] == mass[home]) {
            game.forEachNeighbour(t, [&](int n) {
                if (!seen[n] && shallow(n)) {
                    seen[n] = 1;
                    stack.push_back(n);
                }
            });
        }
    }
    bool reached = false;
    while (!stack.empty() && !reached) {
        int t = stack.back();
        stack.pop_back();
        game.forEachNeighbour(t, [&](int n) {
            reached |= mass[n] == mass[target];
            if (!seen[n] && shallow(n)) {
                seen[n] = 1;
                stack.push_back(n);
            }
        });
    }
    return reached;
}

// Steps from each tile to `target` for the player's units by land and sea:
// walking, embarking onto a port, sailing, disembarking. A tile a land unit
// can enter is walked on; other water is sailed.
std::vector<int> routeSteps(const Game &game, int player, int target) {
    const int count = static_cast<int>(game.tiles.size());
    std::vector<char> land(count);
    for (int t = 0; t < count; t++) {
        land[t] = game.canEnterTerrain(player, false, t);
    }
    std::vector<int> steps(count, FAR);
    std::queue<int> queue;
    steps[target] = 0;
    queue.push(target);
    // backwards from the target: which steps n -> t are possible
    while (!queue.empty()) {
        int t = queue.front();
        queue.pop();
        game.forEachNeighbour(t, [&](int n) {
            if (steps[n] != FAR) {
                return;
            }
            bool step = land[t] ? land[n] || TerrainTypes::isWater(game.tiles[n].terrain)
                                : (!land[n] && game.canEnterTerrain(player, true, t)) ||
                                      (land[n] && game.embarksAt(player, t));
            // a disembark lands from water; a walk needs land to land
            if (land[t] && !land[n] && !game.canEnterTerrain(player, true, n)) {
                step = false;
            }
            if (step) {
                steps[n] = steps[t] + 1;
                queue.push(n);
            }
        });
    }
    return steps;
}

std::vector<Command> naval(const Game &game, const std::vector<Command> &legal) {
    const int player = game.currentPlayer, enemy = 1 - player;
    const Player &me = game.players[player];
    if (me.capital < 0 || game.players[enemy].capital < 0) {
        return legal;
    }
    const int home = game.cities[me.capital].tile;
    const int target = game.cities[game.players[enemy].capital].tile;
    const ImprovementData &port = improvementInfo(ImprovementTypes::PORT);
    // the port's tech, and the ocean's if shallow water doesn't reach the
    // enemy's shore
    std::vector<TechTypes::TechType> techs{port.tech};
    if (!shallowRoute(game, landmasses(game), home, target)) {
        techs.push_back(oceanTech());
    }
    for (TechTypes::TechType tech : techs) {
        if (!me.hasTech(tech)) {
            TechTypes::TechType next = nextTech(me, tech);
            Command research{RESEARCH, -1, -1, next};
            return saving(game, legal, {research}, game.techCost(player, next));
        }
    }
    // then a port, the one nearest the enemy capital
    bool hasPort = false;
    for (int t = 0; t < static_cast<int>(game.tiles.size()); t++) {
        hasPort |= game.tiles[t].building == ImprovementTypes::PORT && game.tileOwner(t) == player;
    }
    if (!hasPort) {
        // nothing to save for while no port can be built yet
        bool buildable = false;
        for (int t = 0; t < static_cast<int>(game.tiles.size()) && !buildable; t++) {
            buildable = game.canBuild(player, t, ImprovementTypes::PORT, false);
        }
        if (!buildable) {
            return legal;
        }
        std::vector<Command> nearest;
        int best = FAR;
        for (const Command &c : legal) {
            if (c.type != BUILD || c.arg != ImprovementTypes::PORT) {
                continue;
            }
            int d = game.distance(c.from, target);
            if (d < best) {
                best = d;
                nearest.clear();
            }
            if (d == best) {
                nearest.push_back(c);
            }
        }
        int reserve = nearest.empty() ? port.cost : starsSpent(game, nearest.front());
        return saving(game, legal, nearest, reserve);
    }
    // then over the sea to the enemy
    std::vector<int> steps = routeSteps(game, player, target);
    return march(game, legal, [&](int) -> const std::vector<int> & { return steps; });
}
} // namespace

bool planDone(Plan plan, const Game &game) {
    switch (plan) {
    case Plan::BRIDGE_RUSH:
        return bridgeDone(game);
    case Plan::NAVAL:
        return false;
    default:
        return true;
    }
}

std::vector<Command> planAllowed(Plan plan, const Game &game, const std::vector<Command> &legal) {
    switch (plan) {
    case Plan::BRIDGE_RUSH:
        return bridgeRush(game, legal);
    case Plan::NAVAL:
        return naval(game, legal);
    default:
        return legal;
    }
}
