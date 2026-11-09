#include "tribeInference.h"
#include "game.h"
#include "mapgen.h"
#include "view.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>

using namespace TechTypes;
using namespace UnitTypes;

// ---- starting score and climate ----

namespace {
// the tiles in the square of `radius` around a tile
int square(int radius) { return (2 * radius + 1) * (2 * radius + 1); }

// the capital's first borders
int startingTerritory() { return square(Rules::CITY_BORDER_RADIUS); }

int fixedStartingScore(const TribeData &tribe) {
    // everything in the starting score but exploration (GAME.md section 3)
    return Rules::techScore(techData[tribe.startTech].tier) +
           Rules::unitScore(unitInfo(tribe.startUnit).cost) +
           Rules::cityScore(tribe.capitalLevel, 0, 0) +
           Rules::TERRITORY_SCORE * startingTerritory();
}
} // namespace

int startingExplored(TribeTypes::Tribe tribe, int score) {
    // The square around the capital starts explored. Near an edge or with a
    // lighthouse left out it's less, but its borders always are.
    int explored = score - fixedStartingScore(tribeData[tribe]);
    if (explored % Rules::EXPLORED_SCORE != 0) {
        return -1;
    }
    explored /= Rules::EXPLORED_SCORE;
    bool fits = explored >= startingTerritory() &&
                explored <= square(Rules::START_EXPLORED_RADIUS);
    return fits ? explored : -1;
}

TribeSet tribesWithStartingScore(int score) {
    TribeSet possible;
    for (const TribeData &tribe : tribeData) {
        if (startingExplored(tribe.tribe, score) >= 0) {
            possible.set(tribe.tribe);
        }
    }
    return possible;
}

TribeSet climatesSeen(const PlayerView &view) {
    TribeSet seen;
    for (const PlayerView::TileView &tile : view.tiles) {
        if (tile.explored && tile.climate) {
            seen.set(*tile.climate);
        }
    }
    return seen;
}

// ---- the score-change search (docs/tribeInference.md sections 3 to 6) ----

namespace {

struct CityState {
    int level = 1, pop = 0, parks = 0;
    bool workshop = false;
    auto operator<=>(const CityState &) const = default;
};

// The opponent's state as far as score and stars go. Stars, territory,
// explored tiles and units are merged between otherwise equal states,
// keeping the most stars, territory and units of each type and the fewest
// explored tiles: each of those only ever allows more, so merging keeps the
// search sound.
//
// A unit's score depends only on its cost, not its type, so its score is kept
// on its own, exactly, and the counts only bound what the units could do
// (explore, capture, examine ruins, disband). Within a turn the score is
// what the change is matched against; across turns it cancels out, since a
// unit's score stays until it's disbanded.
struct OpState {
    int stars = 0, territory = 0, explored = 0;
    // how far from their capital (Chebyshev) any of their units could be
    int reach = 0;
    // whether they've had the stars for meeting the player
    bool met = false;
    // whether they could have a port, where land units take to the water
    bool port = false;
    TechSet techs;
    int unitScore = 0;
    std::array<int, UNIT_TYPE_SIZE> units{};
    // a ruin's boat with a soldier in it: scores as the soldier, moves as
    // the boat (Rules::RUIN_SEA_BOAT)
    int ruinRammers = 0;
    std::vector<CityState> cities;
    int lighthouses = 0, ruins = 0, connections = 0, embassies = 0;
    // the monuments they own, for the score (a captured city can bring a
    // second of a kind), and a bit per task whose monument they built
    int monuments = 0, monumentsBuilt = 0;
    // the turn each temple was built
    std::vector<int> temples;

    // what has to match exactly for two states to merge; within a turn that
    // includes the unit score
    std::vector<long> key(bool withUnitScore) const {
        std::vector<long> k{static_cast<long>(techs.to_ulong()),
                            withUnitScore ? unitScore : 0, lighthouses, ruins,
                            connections, embassies, monuments, monumentsBuilt,
                            port};
        for (const CityState &c : cities) {
            k.insert(k.end(), {c.level, c.pop, c.parks, c.workshop});
        }
        k.push_back(-1);
        k.insert(k.end(), temples.begin(), temples.end());
        return k;
    }
};

// what happened this turn that the score match needs to know
struct TurnInfo {
    std::array<int, UNIT_TYPE_SIZE> unitsBefore{};
    int rammersBefore = 0;
    // whether a port stood at the start of the turn: moving onto one ends a
    // land unit's move, so boats from it sail from the next turn
    bool portBefore = false;
    int captures = 0, borderGrowths = 0, explorers = 0;
    // units placed next to a tile that might push another unit out: a pushed
    // unit goes as far as it takes to find room
    int pushes = 0;
    // whether they took the meeting stars this turn, which the end of the
    // turn has to bear out
    bool metNow = false;
    // population still to place: in the capital, and in any city
    int capitalPop = 0, anyPop = 0;
    // actions that pay stars back (clearing forest), each using up a tile
    int refunds = 0;
};

using Next = std::function<void(OpState &, TurnInfo &)>;

// Where the capital could be, as its starting explored square shows it: its
// distance to the nearest edge across the rows and across the columns, up
// to the square's radius (which means that far or more), and whether a map
// corner, where a lighthouse stands, is inside the square
struct Shape {
    int nearRow, nearCol;
    bool cornerInside;
};

struct Context {
    int size, mapTiles, ruinsTotal;
    // the other players: the viewer, in the two-player games searched
    int others = 1;
    // the shapes the opponent's starting 5x5 could have, from their starting
    // score and this tribe
    std::vector<Shape> shapes;
    // whether there could be roads the opponent didn't build: the player's,
    // outside their borders
    bool foreignRoads = false;
    // whether the player was attacked this turn, which a kill's move-in and
    // Escape need, and whether ever so far
    bool attacked = false, attackedSoFar = false;
    // The meeting stars, and the nearest the opponent's capital could be to
    // anything of the player's by this turn: they meet the player by seeing
    // it on a tile they've explored.
    int meetingStars = 0, meetDistance = 0;
    // the turn the opponent is playing (for temple levels and what's allowed)
    int turn;
    // the known score before the turn, and the published change to match
    int before = 0, change = 0;
};

bool owns(const OpState &s, TechType t) { return s.techs.test(t); }

bool researchable(const OpState &s, TechType t) {
    return researchableWith(s.techs, t);
}

int techCost(const OpState &s, TechType t) {
    // Literacy comes from Philosophy
    return Rules::techCost(techData[t].tier, static_cast<int>(s.cities.size()),
                           owns(s, PHILOSOPHY));
}

int income(const OpState &s) {
    int stars = 0;
    for (std::size_t c = 0; c < s.cities.size(); c++) {
        const CityState &city = s.cities[c];
        // the first city is the capital
        stars += Rules::cityIncome(city.level, c == 0, city.workshop, city.parks);
    }
    // Their embassies in the player's capital. A peace treaty would pay more,
    // but needs the player's agreement, and the player hasn't met them.
    return stars + Rules::EMBASSY_INCOME * s.embassies;
}

// the score of everything but territory and exploration (GAME.md section 3)
int knownScore(const OpState &s, int turn) {
    int score = 0;
    for (const TechData &tech : techData) {
        if (owns(s, tech.type)) {
            score += Rules::techScore(tech.tier);
        }
    }
    score += s.unitScore;
    for (const CityState &c : s.cities) {
        score += Rules::cityScore(c.level, c.pop, c.parks);
    }
    for (int built : s.temples) {
        score += Rules::templeScore(Rules::templeLevel(built, turn));
    }
    return score + Rules::MONUMENT_SCORE * s.monuments;
}

bool roadsFor(const OpState &s, const Context &ctx) {
    return owns(s, ROADS) || ctx.foreignRoads;
}

// the furthest a unit of this type could move in one go, in steps
int moveLength(UnitType type, const OpState &s, const Context &ctx) {
    const UnitData &data = unitInfo(type);
    bool naval = data.abilities.test(Abilities::WATER);
    int cost = !naval && roadsFor(s, ctx) ? Rules::ROAD_STEP_COST
                                          : Rules::STEP_COST;
    return data.movement * Rules::STEP_COST / cost;
}

// whether their techs let land units onto mountains, for the wider sight
bool climbs(const OpState &s) {
    for (const TechData &tech : techData) {
        if (owns(s, tech.type) &&
            tech.movementUnlocks.test(TerrainTypes::MOUNTAIN)) {
            return true;
        }
    }
    return false;
}

// new tiles one step reveals for sight `r`: a diagonal step shows the most
int stepReveal(int r) { return square(r) - 4 * r * r; }

// The most tiles the opponent's units could reveal this turn. Units can't
// move into fog, so every path starts and runs through explored tiles, and
// each step reveals at most `stepReveal`. Ending on a mountain widens the
// sight: the old square is inside the new one, so at most the difference.
// Units trained this turn can't move, so only those from before count.
int unitExploration(const TurnInfo &info, const OpState &s,
                    const Context &ctx) {
    auto perUnit = [&](UnitType type) {
        const UnitData &data = unitInfo(type);
        bool naval = data.abilities.test(Abilities::WATER);
        int sight = data.abilities.test(Abilities::SCOUT) ? Rules::FAR_SIGHT
                                                          : Rules::UNIT_SIGHT;
        int step = stepReveal(sight);
        int last = !naval && climbs(s)
                       ? std::max(step, square(Rules::FAR_SIGHT) - square(sight))
                       : step;
        int length = moveLength(type, s, ctx);
        if (!ctx.attacked) {
            return (length - 1) * step + last;
        }
        // an attack: Escape moves again, and a melee kill moves one more step
        int moves = 1 + data.abilities.test(Abilities::ESCAPE);
        return moves * ((length - 1) * step + last) + step;
    };
    int total = 0;
    for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
        if (info.unitsBefore[u] > 0) {
            total += info.unitsBefore[u] * perUnit(static_cast<UnitType>(u));
        }
    }
    return total + info.rammersBefore * perUnit(Rules::RUIN_SEA_BOAT);
}

// The fastest boat a land unit of theirs could be: the one it becomes at a
// port, or a Raft upgrade their techs allow
int fastestBoat(const TurnInfo &info, const OpState &s) {
    int most = 0;
    for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
        auto type = static_cast<UnitType>(u);
        if (info.unitsBefore[u] > 0 &&
            !unitInfo(type).abilities.test(Abilities::WATER)) {
            most = std::max(most, unitInfo(boatFor(type)).movement);
        }
    }
    if (most > 0) {
        for (const UnitData &boat : unitData) {
            if (isRaftFamily(boat.type) && owns(s, boat.tech)) {
                most = std::max(most, boat.movement);
            }
        }
    }
    return most;
}

// How much further from the capital a unit could get this turn
int reachStep(const TurnInfo &info, const OpState &s, const Context &ctx) {
    int most = 0;
    auto consider = [&](UnitType type) {
        int length = moveLength(type, s, ctx);
        if (ctx.attacked) {
            bool escape = unitInfo(type).abilities.test(Abilities::ESCAPE);
            length += 1 + (escape ? length : 0);
        }
        most = std::max(most, length);
    };
    for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
        if (info.unitsBefore[u] > 0) {
            consider(static_cast<UnitType>(u));
        }
    }
    if (info.rammersBefore > 0) {
        consider(Rules::RUIN_SEA_BOAT);
    }
    // A land unit can take to the water at a port and move as a boat, and a
    // boat can step off onto land: allow the fastest boat, and the step off.
    if (info.portBefore) {
        most = std::max(most, fastestBoat(info, s) + 1 + (ctx.attacked ? 1 : 0));
    }
    return most;
}

// Whether they could have a unit with the Scout skill: one they have, one
// they could train, or a boat one of their units could become at a port
bool couldScout(const OpState &s) {
    auto scout = [](UnitType type) {
        return unitInfo(type).abilities.test(Abilities::SCOUT);
    };
    for (const UnitData &data : unitData) {
        if (data.trainable && owns(s, data.tech) && scout(data.type)) {
            return true;
        }
        if (s.port && isRaftFamily(data.type) && owns(s, data.tech) &&
            scout(data.type)) {
            return true;
        }
    }
    for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
        auto type = static_cast<UnitType>(u);
        if (s.units[u] > 0 &&
            (scout(type) || (s.port && scout(boatFor(type))))) {
            return true;
        }
    }
    return false;
}

// How far past their units the opponent could see: the wider sight from a
// mountain or with the Scout skill, or a captured city's borders, grown or
// not
int sightRadius(const OpState &s) {
    int sight = Rules::UNIT_SIGHT;
    bool scouts = couldScout(s);
    if (climbs(s) || scouts) {
        sight = std::max(sight, Rules::FAR_SIGHT);
    }
    if (s.cities.size() > 1) {
        sight = std::max(sight, Rules::GROWN_BORDER_RADIUS);
    }
    return sight;
}

// One side of the square of `radius` around the capital, clipped by the map,
// for a capital `near` its nearest edge on that side (up to the starting
// radius, meaning that far or more)
int clippedWidth(int near, int radius, int size) {
    if (near >= Rules::START_EXPLORED_RADIUS) {
        return std::min(2 * radius + 1, size);
    }
    return std::min(radius, near) + std::min(radius, size - 1 - near) + 1;
}

// the most tiles the opponent can have seen with everything within `radius`
// of their capital
int visibleTiles(const Context &ctx, int radius) {
    int most = 0;
    for (const Shape &shape : ctx.shapes) {
        most = std::max(most, clippedWidth(shape.nearRow, radius, ctx.size) *
                                  clippedWidth(shape.nearCol, radius, ctx.size));
    }
    return most;
}

// How many map corners (lighthouses) could be within `radius` of the
// capital. On a side where the capital is at least the starting radius from
// the edge, both edges are within reach only past the middle of the map.
int cornersWithin(const Context &ctx, int radius) {
    const int start = Rules::START_EXPLORED_RADIUS;
    auto sides = [&](int near) {
        if (near < start) {
            return (near <= radius) + (ctx.size - 1 - near <= radius);
        }
        return radius < start ? 0 : radius < ctx.size / 2 ? 1 : 2;
    };
    int most = 0;
    for (const Shape &shape : ctx.shapes) {
        // a corner this close would have been inside the starting square
        int corners = radius <= start && !shape.cornerInside
                          ? 0
                          : sides(shape.nearRow) * sides(shape.nearCol);
        most = std::max(most, corners);
    }
    return most;
}

// The shapes a starting square with `explored` tiles could have. A
// lighthouse starts unexplored, and a map corner (where lighthouses are) can
// be inside the square, so the shape's area is `explored` or one more.
std::vector<Shape> startShapes(int explored) {
    const int start = Rules::START_EXPLORED_RADIUS;
    std::vector<Shape> shapes;
    for (int row = 0; row <= start; row++) {
        for (int col = 0; col <= start; col++) {
            int area = (start + 1 + row) * (start + 1 + col);
            // near two edges at once, a corner is always inside
            bool cornerForced = row < start && col < start;
            if (area == explored && !cornerForced) {
                shapes.push_back({row, col, false});
            }
            if (area == explored + 1) {
                shapes.push_back({row, col, true});
            }
        }
    }
    if (shapes.empty()) {
        shapes.push_back({start, start, true});
    }
    return shapes;
}

// The most score one disband can take back: the dearest unit's
int maxDisband() {
    static const int most = [] {
        int cost = 0;
        for (const UnitData &data : unitData) {
            cost = std::max(cost, data.cost);
        }
        return Rules::unitScore(cost);
    }();
    return most;
}

// The least score one more population adds, in the cities as they are: a
// population point's worth, or at a level-up, the level's worth less the
// population that goes into it. 0 if a level-up could add nothing.
int minPopulationGain(const OpState &s, int pending) {
    int least = Rules::POPULATION_SCORE;
    for (const CityState &c : s.cities) {
        for (int level = c.level; level <= c.level + pending; level++) {
            int before = Rules::populationToLevelUp(level) - 1;
            least = std::min(least, Rules::LEVEL_SCORE -
                                        Rules::POPULATION_SCORE * before);
        }
    }
    return std::max(0, least);
}

// Within a turn the known score only goes up, except by one disband, and
// each population still to place adds at least `minPopulationGain`. So a
// branch whose known score has already passed the change by more than a
// disband can take back can't match, nor can anything after it. A state
// without the disband tech needs no allowance when researching it adds more
// score than a disband takes back.
bool tooHigh(const OpState &s, const TurnInfo &info, const Context &ctx) {
    bool disbands =
        owns(s, Rules::DISBAND_TECH) ||
        Rules::techScore(techData[Rules::DISBAND_TECH].tier) < maxDisband();
    int slack = disbands ? maxDisband() : 0;
    int pop = info.capitalPop + info.anyPop;
    int pending = pop * minPopulationGain(s, pop);
    return knownScore(s, ctx.turn) + pending - ctx.before - slack > ctx.change;
}

// Whether the opponent could have explored a tile with something of the
// player's on it by the end of this turn: they start with the square around
// their capital explored, and see at most `sightRadius` past where their
// units could be. Techs they could research this turn count already: as
// many rounds as the stars could pay at the cheapest a tech could be.
bool meetPossible(const OpState &s, const TurnInfo &info, const Context &ctx) {
    OpState hopeful = s;
    int cheapest = std::max(
        1, Rules::techCost(1, static_cast<int>(s.cities.size()), true));
    for (int budget = s.stars + ctx.meetingStars; budget >= cheapest;
         budget -= cheapest) {
        TechSet more = hopeful.techs;
        for (int t = 1; t < TECH_TYPE_SIZE; t++) {
            if (researchable(hopeful, static_cast<TechType>(t))) {
                more.set(t);
            }
        }
        hopeful.techs = more;
    }
    int reach = s.reach >= ctx.size
                    ? ctx.size
                    : s.reach + reachStep(info, hopeful, ctx);
    return ctx.meetDistance <=
           std::max(Rules::START_EXPLORED_RADIUS, reach + sightRadius(hopeful));
}

// ---- the stages of a turn, each calling the next ----

void research(OpState s, TurnInfo info, const Context &ctx, int from,
              const Next &next) {
    if (tooHigh(s, info, ctx)) {
        return;
    }
    next(s, info);
    // techs in increasing order, so parents (always earlier) come first
    for (int t = from; t < TECH_TYPE_SIZE; t++) {
        auto tech = static_cast<TechType>(t);
        int cost = techCost(s, tech);
        if (researchable(s, tech) && cost <= s.stars) {
            OpState bought = s;
            bought.stars -= cost;
            bought.techs.set(tech);
            research(bought, info, ctx, t + 1, next);
        }
    }
}

void train(OpState s, TurnInfo info, const Context &ctx, int from,
           const Next &next) {
    if (tooHigh(s, info, ctx)) {
        return;
    }
    next(s, info);
    for (int u = from; u < UNIT_TYPE_SIZE; u++) {
        const UnitData &data = unitInfo(static_cast<UnitType>(u));
        if (!data.trainable || !owns(s, data.tech) || data.cost > s.stars) {
            continue;
        }
        // The unit cap is left out: it only ever restricts, so leaving it out
        // keeps the search sound.
        OpState trained = s;
        trained.stars -= data.cost;
        trained.units[u]++;
        trained.unitScore += Rules::unitScore(data.cost);
        train(trained, info, ctx, u, next);
    }
}

struct Improvement {
    int cost, pop, stars;
    bool temple, embassy, port = false;
};

void improve(OpState s, TurnInfo info, const Context &ctx,
             const std::vector<Improvement> &options, std::size_t from,
             const Next &next) {
    if (tooHigh(s, info, ctx)) {
        return;
    }
    next(s, info);
    // Stars bound how much they can build; a tile can take several things in
    // turn (hunted, burned to crop, farmed), so territory doesn't.
    for (std::size_t i = from; i < options.size(); i++) {
        const Improvement &o = options[i];
        // An embassy in each other player's capital, at most. An action that
        // pays stars back uses up its tile (a forest), and bringing one back
        // costs more than it returns, so at most one per tile they own.
        if (o.cost > s.stars || (o.embassy && s.embassies >= ctx.others) ||
            (o.stars > 0 && info.refunds >= s.territory)) {
            continue;
        }
        OpState built = s;
        built.stars += o.stars - o.cost;
        built.embassies += o.embassy;
        built.port |= o.port;
        TurnInfo after = info;
        after.anyPop += o.pop;
        after.refunds += o.stars > 0;
        if (o.temple) {
            built.temples.push_back(ctx.turn);
        }
        improve(built, after, ctx, options, i, next);
    }
}

// A level-up reward's effect on the search's state (GAME.md section 7).
// Rewards that score nothing and give nothing countable (a wall) leave it
// as it is. Returns population still to add.
int applyReward(CityRewards::CityReward reward, OpState &s, TurnInfo &info,
                std::size_t city) {
    using namespace CityRewards;
    switch (reward) {
    case WORKSHOP:
        s.cities[city].workshop = true;
        return 0;
    case EXPLORER:
        info.explorers++;
        return 0;
    case CITYWALL:
        return 0;
    case RESOURCES:
        s.stars += Rules::RESOURCES_STARS;
        return 0;
    case POPULATIONGROWTH:
        return Rules::POPULATION_GROWTH;
    case BORDERGROWTH:
        info.borderGrowths++;
        return 0;
    case PARK:
        s.cities[city].parks++;
        return 0;
    case SUPERUNIT:
        // it appears in the city, pushing out any unit there
        s.units[Rules::SUPER_UNIT]++;
        s.unitScore += Rules::unitScore(unitInfo(Rules::SUPER_UNIT).cost);
        info.pushes++;
        return 0;
    case CITY_REWARD_SIZE:
        break;
    }
    return 0;
}

// one population at a time, choosing each level-up's reward as it comes
void addPop(OpState s, TurnInfo info, std::size_t city, int amount,
            const Next &next) {
    if (amount == 0) {
        next(s, info);
        return;
    }
    CityState &c = s.cities[city];
    c.pop++;
    int needed = Rules::populationToLevelUp(c.level);
    if (c.pop < needed) {
        addPop(s, info, city, amount - 1, next);
        return;
    }
    c.pop -= needed;
    c.level++;
    for (CityRewards::CityReward reward : CityRewards::rewardsForLevel(c.level)) {
        OpState chosen = s;
        TurnInfo chosenInfo = info;
        int more = applyReward(reward, chosen, chosenInfo, city);
        addPop(chosen, chosenInfo, city, amount - 1 + more, next);
    }
}

// population that can go to any city, in every split between them
void spread(OpState s, TurnInfo info, std::size_t city, int amount,
            const Next &next) {
    if (city + 1 >= s.cities.size()) {
        addPop(s, info, city, amount, next);
        return;
    }
    for (int here = 0; here <= amount; here++) {
        addPop(s, info, city, here, [&, city, amount, here](OpState &t,
                                                            TurnInfo &ti) {
            spread(t, ti, city + 1, amount - here, next);
        });
    }
}

void placePopulation(OpState s, TurnInfo info, const Next &next) {
    int capital = info.capitalPop, any = info.anyPop;
    info.capitalPop = info.anyPop = 0;
    addPop(s, info, 0, capital, [&, any](OpState &t, TurnInfo &ti) {
        spread(t, ti, 0, any, next);
    });
}

// Whether a task could be done, as far as the search can tell (the
// engine's own check is Game::taskMet)
bool taskPossible(TaskTypes::Task task, const OpState &s, const Context &ctx) {
    using namespace TaskTypes;
    switch (task) {
    case PACIFIST:
        // peaceful turns can't outnumber the turns played
        return owns(s, Rules::PACIFIST_TECH) && ctx.turn >= Rules::PACIFIST_TURNS;
    case WEALTH:
        return owns(s, Rules::WEALTH_TECH) && s.stars >= Rules::WEALTH_STARS;
    case EXPLORER:
        return s.lighthouses >= Rules::LIGHTHOUSE_COUNT;
    case KILLER:
        // their kills can only be the player's units, which the player
        // knows were attacked; the count isn't known, so any attack will do
        return ctx.attackedSoFar;
    case NETWORK:
        return s.connections >= Rules::NETWORK_CITIES;
    case METROPOLIS:
        return std::any_of(s.cities.begin(), s.cities.end(),
                           [](const CityState &c) {
                               return c.level >= Rules::METROPOLIS_LEVEL;
                           });
    case GENIUS:
        return s.techs.all();
    case TASK_SIZE:
        break;
    }
    return false;
}

// Monuments, once their task is done: score, and population from the
// improvement table
void monuments(OpState s, TurnInfo info, const Context &ctx, int from,
               const Next &next) {
    next(s, info);
    for (int t = from; t < TaskTypes::TASK_SIZE; t++) {
        auto task = static_cast<TaskTypes::Task>(t);
        const ImprovementData &monument =
            improvementData[TaskTypes::monumentFor(task)];
        if ((s.monumentsBuilt >> t & 1) || !taskPossible(task, s, ctx) ||
            monument.cost > s.stars) {
            continue;
        }
        OpState built = s;
        built.monuments++;
        built.monumentsBuilt |= 1 << t;
        built.stars -= monument.cost;
        TurnInfo after = info;
        after.anyPop += monument.population;
        placePopulation(built, after, [&, t](OpState &u, TurnInfo &ui) {
            monuments(u, ui, ctx, t + 1, next);
        });
    }
}

void events(OpState s, TurnInfo info, const Context &ctx, const Next &next) {
    int unitCount = info.rammersBefore;
    for (int n : info.unitsBefore) {
        unitCount += n;
    }
    // Capturing and examining a ruin need a unit that started its turn
    // there, so neither happens on turn 0, and each needs its own unit.
    int maxCaptures = ctx.turn >= 1 ? unitCount : 0;
    int maxRuins =
        ctx.turn >= 1 ? std::min(unitCount, ctx.ruinsTotal - s.ruins) : 0;

    for (int captures = 0; captures <= maxCaptures; captures++) {
        OpState c = s;
        TurnInfo ci = info;
        for (int i = 0; i < captures; i++) {
            c.cities.push_back({});
        }
        ci.captures = captures;
        if (tooHigh(c, ci, ctx)) {
            break;
        }

        // Lighthouses they could still find: only corners within sight of
        // where their units could be
        auto lighthousesLeft = [&](const OpState &r, const TurnInfo &ri) {
            int reach = r.reach >= ctx.size || ri.explorers > 0 || ri.pushes > 0
                            ? ctx.size
                            : r.reach + reachStep(ri, r, ctx);
            int radius = std::min(ctx.size, reach + sightRadius(r));
            return std::max(0, cornersWithin(ctx, radius) - r.lighthouses);
        };

        // ruins, each with its reward
        std::function<void(OpState, TurnInfo, int)> ruins =
            [&](OpState r, TurnInfo ri, int left) {
                if (tooHigh(r, ri, ctx)) {
                    return;
                }
                // lighthouses found (population in the capital), and city
                // connections (population in the city and the capital)
                for (int l = 0; l <= lighthousesLeft(r, ri); l++) {
                    int maxConnections = static_cast<int>(r.cities.size()) - 1 -
                                         r.connections;
                    for (int x = 0; x <= maxConnections; x++) {
                        OpState e = r;
                        TurnInfo ei = ri;
                        e.lighthouses += l;
                        e.connections += x;
                        ei.capitalPop += Rules::LIGHTHOUSE_POPULATION * l +
                                         Rules::CONNECTION_POPULATION * x;
                        ei.anyPop += Rules::CONNECTION_POPULATION * x;
                        // more of either only adds score
                        if (tooHigh(e, ei, ctx)) {
                            break;
                        }
                        next(e, ei);
                        // disbanding a unit, for part of its cost back
                        if (owns(e, Rules::DISBAND_TECH)) {
                            for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
                                if (e.units[u] > 0) {
                                    int cost = unitInfo(static_cast<UnitType>(u)).cost;
                                    OpState d = e;
                                    d.units[u]--;
                                    d.unitScore -= Rules::unitScore(cost);
                                    d.stars += Rules::disbandRefund(cost);
                                    next(d, ei);
                                }
                            }
                        }
                    }
                }
                if (left == 0) {
                    return;
                }
                OpState base = r;
                base.ruins++;
                // each reward (GAME.md section 13, Game::giveRuinReward)
                for (int k = 0; k < Game::RUIN_REWARD_SIZE; k++) {
                    switch (static_cast<Game::RuinReward>(k)) {
                    case Game::RUIN_TECH:
                        for (int t = 1; t < TECH_TYPE_SIZE; t++) {
                            if (researchable(base, static_cast<TechType>(t))) {
                                OpState got = base;
                                got.techs.set(t);
                                ruins(got, ri, left - 1);
                            }
                        }
                        break;
                    case Game::RUIN_STARS: {
                        OpState stars = base;
                        stars.stars += Rules::RUIN_STARS;
                        ruins(stars, ri, left - 1);
                        break;
                    }
                    case Game::RUIN_POPULATION: {
                        TurnInfo pop = ri;
                        pop.capitalPop += Rules::RUIN_POPULATION;
                        ruins(base, pop, left - 1);
                        break;
                    }
                    case Game::RUIN_EXPLORER: {
                        TurnInfo explorer = ri;
                        explorer.explorers++;
                        ruins(base, explorer, left - 1);
                        break;
                    }
                    case Game::RUIN_UNIT: {
                        // a soldier on land, or a boat carrying one on water;
                        // either pushes the examining unit off
                        TurnInfo pushed = ri;
                        pushed.pushes++;
                        OpState land = base;
                        land.units[Rules::RUIN_LAND_UNIT]++;
                        land.unitScore += Rules::unitScore(
                            unitInfo(Rules::RUIN_LAND_UNIT).cost);
                        ruins(land, pushed, left - 1);
                        OpState sea = base;
                        sea.ruinRammers++;
                        sea.unitScore += Rules::unitScore(
                            unitInfo(Rules::RUIN_SEA_PASSENGER).cost);
                        ruins(sea, pushed, left - 1);
                        break;
                    }
                    case Game::RUIN_REWARD_SIZE:
                        break;
                    }
                }
            };
        // each level emits its states before taking another ruin, so this
        // covers every count up to the most
        ruins(c, ci, maxRuins);
    }
}

// Everything they could build or do on a tile with their techs, from the
// engine's table. Roads, bridges, burning and growing forest score nothing
// and only cost stars, so they're left out: spending fewer stars only ever
// allows more.
std::vector<Improvement> improvements(const OpState &s) {
    using namespace ImprovementTypes;
    std::vector<Improvement> options;
    for (const ImprovementData &data : improvementData) {
        if (data.type <= LIGHTHOUSE || isMonument(data.type) ||
            !owns(s, data.tech)) {
            continue;
        }
        switch (data.type) {
        case ROAD:
        case BRIDGE:
        case BURNFOREST:
        case GROWFOREST:
            continue;
        case CLEARFOREST:
            options.push_back(
                {data.cost, 0, Rules::CLEAR_FOREST_REFUND, false, false});
            continue;
        case WINDMILL:
        case SAWMILL:
        case FORGE:
            // population per level, a level per neighbour it builds on
            for (int level = 1; level <= MAX_BUILDING_LEVEL; level++) {
                options.push_back({data.cost, data.population * level, 0,
                                   false, false});
            }
            continue;
        default:
            options.push_back({data.cost, data.population, 0,
                               isTemple(data.type), false, data.type == PORT});
        }
    }
    // A Windmill, Sawmill or Forge levels up with each farm, lumber hut or
    // mine built next to it: those can add its population too
    for (ImprovementType producer : {WINDMILL, SAWMILL, FORGE}) {
        const ImprovementData &p = improvementData[producer];
        if (!owns(s, p.tech)) {
            continue;
        }
        for (const ImprovementData &base : improvementData) {
            if (p.adjacency.test(base.type) && owns(s, base.tech)) {
                // one producer per city, so at most one per city around it
                for (int next = 1;
                     next <= static_cast<int>(s.cities.size()); next++) {
                    options.push_back({base.cost,
                                       base.population + next * p.population, 0,
                                       false, false});
                }
            }
        }
    }
    if (owns(s, Rules::EMBASSY_TECH)) {
        options.push_back({Rules::EMBASSY_COST, 0, 0, false, true});
    }
    return options;
}

void merge(OpState &kept, const OpState &s) {
    kept.stars = std::max(kept.stars, s.stars);
    kept.territory = std::max(kept.territory, s.territory);
    kept.explored = std::min(kept.explored, s.explored);
    for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
        kept.units[u] = std::max(kept.units[u], s.units[u]);
    }
    kept.ruinRammers = std::max(kept.ruinRammers, s.ruinRammers);
    kept.reach = std::max(kept.reach, s.reach);
    kept.met = kept.met && s.met;
}

// the states after a turn
struct Merged {
    std::map<std::vector<long>, OpState> states;
    bool overflow = false;
};

void keep(Merged &merged, const OpState &s, int maxStates) {
    auto [it, inserted] = merged.states.try_emplace(s.key(false), s);
    if (inserted) {
        if (static_cast<int>(merged.states.size()) > maxStates) {
            merged.overflow = true;
        }
        return;
    }
    merge(it->second, s);
}

// The states partway through a turn, after one stage. Merging here, and not
// just at the end, keeps each stage from running once for every way the
// stages before it got to the same place.
struct Stage {
    std::map<std::vector<long>, std::pair<OpState, TurnInfo>> states;
    bool overflow = false;
    // the step's work left, shared by its stages
    long &work;

    explicit Stage(long &work) : work(work) {}

    Next collect() {
        return [this](OpState &s, TurnInfo &info) {
            if (overflow || --work < 0) {
                overflow = true;
                return;
            }
            std::vector<long> k = s.key(true);
            k.insert(k.end(), {info.captures, info.borderGrowths,
                               info.explorers, info.pushes, info.metNow,
                               info.capitalPop, info.anyPop});
            auto [it, inserted] = states.try_emplace(std::move(k), s, info);
            if (!inserted) {
                merge(it->second.first, s);
            }
        };
    }
};

// one opponent turn: every way to match `change` from `prev`
void expand(const OpState &prev, const Context &ctx, Merged &out,
            int maxStates, long &work) {
    OpState s = prev;
    if (ctx.turn >= 1) {
        s.stars += income(s);
    }
    TurnInfo info;
    info.unitsBefore = s.units;
    info.rammersBefore = s.ruinRammers;
    info.portBefore = s.port;
    // Meeting the player: the stars come once, when the opponent sees
    // something of theirs. Both with and without, when it could happen this
    // turn; the end of the turn checks it could.
    std::vector<std::pair<OpState, TurnInfo>> starts{{s, info}};
    if (!s.met && meetPossible(s, info, ctx)) {
        OpState meeting = s;
        meeting.stars += ctx.meetingStars;
        meeting.met = true;
        TurnInfo meetingInfo = info;
        meetingInfo.metNow = true;
        starts.push_back({meeting, meetingInfo});
    }

    // the score match, last of all
    Next match = [&](OpState &t, TurnInfo &ti) {
        if (out.overflow || --work < 0) {
            out.overflow = true;
            return;
        }
        // Markets and starfish give stars the search doesn't model: stop
        // reading rather than miss them
        if (owns(t, improvementData[ImprovementTypes::MARKET].tech) ||
            owns(t, Rules::STARFISH_TECH)) {
            out.overflow = true;
            return;
        }
        int rest = ctx.change - (knownScore(t, ctx.turn) - ctx.before);
        if (rest < 0) {
            return;
        }
        // The rest is new territory and exploration. A captured village
        // claims its borders, at least its own tile; border growth claims
        // the ring out to the grown radius. Both reveal what they claim.
        int capturedBorders = square(Rules::CITY_BORDER_RADIUS);
        int grownRing = square(Rules::GROWN_BORDER_RADIUS) -
                        square(Rules::CITY_BORDER_RADIUS);
        int lo = ti.captures,
            hi = capturedBorders * ti.captures + grownRing * ti.borderGrowths;
        long reveal = unitExploration(ti, t, ctx) +
                      static_cast<long>(capturedBorders) * ti.captures +
                      static_cast<long>(grownRing) * ti.borderGrowths +
                      (ti.explorers > 0 ? ctx.mapTiles : 0);
        // Everything they've seen is near where their units could have been.
        // An Explorer or a push can go anywhere, and from then on so can the
        // bound.
        int reach = prev.reach >= ctx.size || ti.explorers > 0 || ti.pushes > 0
                        ? ctx.size
                        : std::min(ctx.size, prev.reach + reachStep(ti, t, ctx));
        int radius = std::min(ctx.size, reach + sightRadius(t));
        // meeting the player needs the techs and the reach it was given for
        if (ti.metNow &&
            ctx.meetDistance > std::max(Rules::START_EXPLORED_RADIUS, radius)) {
            return;
        }
        int seen = visibleTiles(ctx, radius);
        reveal = std::min<long>(reveal, std::max(0, seen - prev.explored));
        // take as much territory as fits: the most territory and the least
        // exploration, which allows the most later
        for (int k = std::min(hi, rest / Rules::TERRITORY_SCORE); k >= lo; k--) {
            int explored = rest - Rules::TERRITORY_SCORE * k;
            if (explored % Rules::EXPLORED_SCORE != 0) {
                continue;
            }
            int tiles = explored / Rules::EXPLORED_SCORE;
            if (tiles <= reveal) {
                OpState accepted = t;
                accepted.territory += k;
                accepted.explored = prev.explored + tiles;
                accepted.reach = reach;
                keep(out, accepted, maxStates);
                return;
            }
        }
    };

    // each stage from the merged states of the one before
    using StageFn = std::function<void(OpState &, TurnInfo &, const Next &)>;
    std::vector<StageFn> stages = {
        [&](OpState &a, TurnInfo &ai, const Next &next) {
            train(a, ai, ctx, 0, next);
        },
        [&](OpState &b, TurnInfo &bi, const Next &next) {
            improve(b, bi, ctx, improvements(b), 0, next);
        },
        [&](OpState &c, TurnInfo &ci, const Next &next) {
            events(c, ci, ctx, next);
        },
        [&](OpState &d, TurnInfo &di, const Next &next) {
            placePopulation(d, di, next);
        },
    };
    auto current = std::make_unique<Stage>(work);
    for (auto &[start, startInfo] : starts) {
        research(start, startInfo, ctx, 1, current->collect());
    }
    for (const StageFn &stage : stages) {
        auto next = std::make_unique<Stage>(work);
        Next collect = next->collect();
        for (auto &[key, partial] : current->states) {
            stage(partial.first, partial.second, collect);
        }
        if (current->overflow || next->overflow) {
            out.overflow = true;
            return;
        }
        current = std::move(next);
    }
    for (auto &[key, partial] : current->states) {
        monuments(partial.first, partial.second, ctx, 0, match);
        if (out.overflow) {
            return;
        }
    }
}

// The opponent's published changes, in order, and whether the player's own
// actions touched each one
struct Change {
    int amount, turn;
    bool touched;
    // whether they attacked the player during it
    bool attacked;
    // the nearest their capital could be to anything of the player's by then
    int meetDistance;
};

// Where the opponent's capital could be: a tile the player hasn't explored
// (a city on an explored tile would have met them), whose distances to the
// map's edges fit a shape their starting 5x5 could have
std::vector<int> capitalCandidates(const PlayerView &view,
                                   const std::vector<Shape> &shapes) {
    auto fits = [&](int along, int shapeNear) {
        int near = std::min(along, view.size - 1 - along);
        return shapeNear >= Rules::START_EXPLORED_RADIUS ? near >= shapeNear
                                                         : near == shapeNear;
    };
    std::vector<int> candidates;
    for (int t = 0; t < view.size * view.size; t++) {
        if (view.tiles[t].explored) {
            continue;
        }
        int x = t % view.size, y = t / view.size;
        for (const Shape &shape : shapes) {
            if (fits(y, shape.nearRow) && fits(x, shape.nearCol)) {
                candidates.push_back(t);
                break;
            }
        }
    }
    return candidates;
}

// the nearest any candidate is to a tile the player's units or cities had
// stood on by `turn`
int meetDistanceBy(const PlayerView &view, const std::vector<int> &candidates,
                   int turn) {
    int nearest = 1 << 20;
    for (int t = 0; t < view.size * view.size; t++) {
        int first = view.firstOccupied[t];
        if (first < 0 || first > turn) {
            continue;
        }
        for (int c : candidates) {
            int d = std::max(std::abs(c % view.size - t % view.size),
                             std::abs(c / view.size - t / view.size));
            nearest = std::min(nearest, d);
        }
    }
    return nearest;
}

std::vector<Change> changesOf(const PlayerView &view, int opponent,
                              int &startingScore) {
    std::vector<Change> changes;
    startingScore = -1;
    int last = 0;
    for (std::size_t r = 0; r < view.scoreHistory.size(); r++) {
        const ScoreRecord &record = view.scoreHistory[r];
        if (record.player != opponent) {
            continue;
        }
        if (startingScore < 0) {
            startingScore = record.score;
        } else {
            const std::vector<int> &attacks = view.attackedOnTurns;
            changes.push_back(
                {record.score - last, record.turn,
                 view.effectsInChanges[r] != 0,
                 std::find(attacks.begin(), attacks.end(), record.turn) !=
                     attacks.end(),
                 0});
        }
        last = record.score;
    }
    return changes;
}

// The search's states after some prefix of the opponent's changes. A prefix
// either still has states, has none (the tribe is ruled out), or stopped
// being read (too many states, or a change the player touched), which keeps
// the tribe possible from then on.
struct Frontier {
    enum Status { OPEN, RULED_OUT, UNREAD } status = OPEN;
    std::vector<OpState> states;
    int turn = 0;
};

// Frontiers by everything they depend on: the tribe, the board, the meeting
// stars, the state limit and the prefix itself. The opponent's history only
// grows, so each new change costs one more step from the cached prefix
// instead of a replay from the start; and positions in the same turn share
// the whole result.
using FrontierKey = std::vector<int>;
struct FrontierHash {
    std::size_t operator()(const FrontierKey &k) const {
        std::size_t h = 0;
        for (int v : k) {
            h = h * 1000003 ^ static_cast<std::size_t>(v);
        }
        return h;
    }
};
using FrontierCache =
    std::unordered_map<FrontierKey, std::shared_ptr<const Frontier>,
                       FrontierHash>;

std::shared_ptr<const Frontier> startFrontier(TribeTypes::Tribe tribe,
                                              int startingScore) {
    auto frontier = std::make_shared<Frontier>();
    const TribeData &data = tribeData[tribe];
    // the tribe's starting state, from the engine's tables
    OpState start;
    start.techs.set(BASIC);
    start.techs.set(data.startTech);
    start.units[data.startUnit] = 1;
    start.unitScore = Rules::unitScore(unitInfo(data.startUnit).cost);
    start.cities = {{.level = data.capitalLevel}};
    start.territory = startingTerritory();
    start.explored = startingExplored(tribe, startingScore);
    if (start.explored < 0) {
        frontier->status = Frontier::RULED_OUT;
        return frontier;
    }
    start.stars = data.startStars;
    frontier->states = {start};
    return frontier;
}

std::shared_ptr<const Frontier> step(const Frontier &prev, const Change &change,
                                     const Context &board,
                                     const TribeInferenceConfig &config) {
    auto frontier = std::make_shared<Frontier>();
    frontier->turn = change.turn;
    if (prev.status != Frontier::OPEN || change.touched) {
        // a change the player's own actions touched isn't read
        frontier->status =
            prev.status == Frontier::OPEN ? Frontier::UNREAD : prev.status;
        return frontier;
    }
    Context ctx = board;
    ctx.meetDistance = change.meetDistance;
    ctx.turn = change.turn;
    ctx.change = change.amount;
    ctx.attacked = change.attacked;
    Merged next;
    long work = config.maxWork;
    for (const OpState &s : prev.states) {
        ctx.before = knownScore(s, prev.turn);
        expand(s, ctx, next, config.maxStates, work);
        if (next.overflow) {
            frontier->status = Frontier::UNREAD;
            return frontier;
        }
    }
    if (next.states.empty()) {
        frontier->status = Frontier::RULED_OUT;
        return frontier;
    }
    frontier->states.reserve(next.states.size());
    for (auto &[key, s] : next.states) {
        frontier->states.push_back(std::move(s));
    }
    return frontier;
}

} // namespace

int searchModelScore(const Game &game, int player) {
    OpState s;
    s.techs = game.players[player].techs;
    for (const Unit &unit : game.units) {
        if (unit.alive && unit.scoreOwner == player) {
            s.unitScore += Rules::unitScore(unitInfo(unit.passenger).cost);
        }
    }
    for (const City &city : game.cities) {
        if (city.owner == player) {
            s.cities.push_back({.level = city.level,
                                .pop = city.population,
                                .parks = city.parks,
                                .workshop = city.workshop});
        }
    }
    for (int t = 0; t < game.size * game.size; t++) {
        s.explored += game.explored(player, t);
        if (game.tileOwner(t) != player) {
            continue;
        }
        s.territory++;
        ImprovementTypes::ImprovementType building = game.tiles[t].building;
        if (isTemple(building)) {
            s.temples.push_back(game.tiles[t].builtTurn);
        } else if (isMonument(building)) {
            s.monuments++;
        }
    }
    return knownScore(s, game.turn) + Rules::TERRITORY_SCORE * s.territory +
           Rules::EXPLORED_SCORE * s.explored;
}

bool scoreChangesFit(const PlayerView &view, int opponent,
                     TribeTypes::Tribe tribe,
                     const TribeInferenceConfig &config) {
    if (view.tribes.size() != 2) {
        // with more players, others' effects are mixed into each change
        return true;
    }
    int startingScore;
    std::vector<Change> changes = changesOf(view, opponent, startingScore);
    if (startingScore < 0) {
        return true;
    }
    // Meeting the player is worth stars once, by the player's score then:
    // the most it has been gives the most it could be.
    int playerScore = view.scores[view.player];
    for (const ScoreRecord &r : view.scoreHistory) {
        if (r.player == view.player) {
            playerScore = std::max(playerScore, r.score);
        }
    }
    int meetingStars = Rules::meetingBonus(playerScore);

    // a few hundred MB at most: a frontier can hold thousands of states
    thread_local FrontierCache cache;
    thread_local std::size_t cachedStates = 0;
    if (cachedStates > 500000) {
        cache.clear();
        cachedStates = 0;
    }
    // The player's roads outside their borders are open to the opponent too.
    // (Generated maps start without roads.)
    bool foreignRoads = view.techs.test(ROADS);
    FrontierKey key{view.size,       tribe,          meetingStars,
                    config.maxStates, config.maxWork, startingScore,
                    foreignRoads};
    std::shared_ptr<const Frontier> frontier;
    if (auto found = cache.find(key); found != cache.end()) {
        frontier = found->second;
    } else {
        frontier = startFrontier(tribe, startingScore);
        cache.emplace(key, frontier);
    }
    Context board{.size = view.size,
                  .mapTiles = view.size * view.size,
                  .ruinsTotal = ruinCountForSize(view.size),
                  .shapes = startShapes(startingExplored(tribe, startingScore)),
                  .foreignRoads = foreignRoads,
                  .meetingStars = meetingStars,
                  .turn = 0};
    std::vector<int> candidates = capitalCandidates(view, board.shapes);
    int limit = std::min<int>(config.turns, changes.size());
    for (int k = 0; k < limit && frontier->status == Frontier::OPEN; k++) {
        changes[k].meetDistance =
            meetDistanceBy(view, candidates, changes[k].turn);
        key.insert(key.end(),
                   {changes[k].amount, changes[k].turn, changes[k].touched,
                    changes[k].attacked, changes[k].meetDistance});
        if (auto found = cache.find(key); found != cache.end()) {
            frontier = found->second;
        } else {
            frontier = step(*frontier, changes[k], board, config);
            cache.emplace(key, frontier);
            cachedStates += frontier->states.size();
        }
    }
    return frontier->status != Frontier::RULED_OUT;
}

TribeSet possibleTribes(const PlayerView &view, int opponent,
                        const TribeInferenceConfig &config) {
    TribeSet possible;
    if (view.tribes[opponent]) {
        possible.set(*view.tribes[opponent]);
        return possible;
    }

    // the starting score: the opponent's first published score
    possible.set();
    for (const ScoreRecord &record : view.scoreHistory) {
        if (record.player == opponent) {
            possible &= tribesWithStartingScore(record.score);
            break;
        }
    }

    // Climates: every foreign climate seen belongs to some opponent. When
    // this is the only opponent not yet met, a foreign climate not explained
    // by the tribes already known must be theirs. Seeing only your own
    // climate proves nothing, since both players can pick the same tribe.
    TribeSet foreign = climatesSeen(view);
    int unmet = 0;
    for (std::size_t q = 0; q < view.tribes.size(); q++) {
        if (view.tribes[q]) {
            foreign.reset(*view.tribes[q]);
        } else {
            unmet++;
        }
    }
    if (unmet == 1 && foreign.any()) {
        possible &= foreign;
    }

    // the opening's score changes
    for (int t = 0; t < TribeTypes::TRIBE_SIZE; t++) {
        if (possible.test(t) &&
            !scoreChangesFit(view, opponent, static_cast<TribeTypes::Tribe>(t),
                             config)) {
            possible.reset(t);
        }
    }
    return possible;
}
