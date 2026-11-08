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
int fixedStartingScore(const TribeData &tribe) {
    // everything in the starting score but exploration, from the engine's
    // own tables and score rules (GAME.md section 3)
    return 100 * techData[tribe.startTech].tier +
           5 * unitInfo(tribe.startUnit).cost + 100 +
           50 * (tribe.capitalLevel - 1) + 20 * 9;
}
} // namespace

TribeSet tribesWithStartingScore(int score) {
    TribeSet possible;
    for (const TribeData &tribe : tribeData) {
        // The 5x5 around the capital starts explored. Near an edge or with a
        // lighthouse left out it's less, but the 3x3 of its borders always is.
        int explored = score - fixedStartingScore(tribe);
        if (explored % 5 == 0 && explored / 5 >= 9 && explored / 5 <= 25) {
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

// The opponent's state as far as score and stars go. Stars, territory and
// explored tiles are merged between otherwise equal states, keeping the most
// stars and territory and the fewest explored tiles: each of those only ever
// allows more, so merging keeps the search sound.
struct OpState {
    int stars = 0, territory = 0, explored = 0;
    TechSet techs;
    std::array<int, UNIT_TYPE_SIZE> units{};
    // a ruin's Rammer carrying a Warrior: worth 10, moves like a Rammer
    int ruinRammers = 0;
    std::vector<CityState> cities;
    int improvements = 0, lighthouses = 0, ruins = 0, connections = 0,
        embassies = 0, monuments = 0;
    // the turn each temple was built
    std::vector<int> temples;

    std::vector<long> key() const {
        std::vector<long> k{static_cast<long>(techs.to_ulong()), ruinRammers,
                            improvements, lighthouses, ruins, connections,
                            embassies, monuments};
        k.insert(k.end(), units.begin(), units.end());
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
    int captures = 0, borderGrowths = 0, explorers = 0;
    // population still to place: in the capital, and in any city
    int capitalPop = 0, anyPop = 0;
};

using Next = std::function<void(OpState &, TurnInfo &)>;

struct Context {
    int mapTiles, ruinsTotal;
    // the turn the opponent is playing (for temple levels and what's allowed)
    int turn;
    // the known score before the turn, and the published change to match
    int before = 0, change = 0;
};

bool owns(const OpState &s, TechType t) { return s.techs.test(t); }

bool researchable(const OpState &s, TechType t) {
    if (t == BASIC || owns(s, t)) {
        return false;
    }
    if (owns(s, techData[t].parent)) {
        return true;
    }
    // backwards research: a child owned
    for (const TechData &child : techData) {
        if (child.parent == t && child.type != t && owns(s, child.type)) {
            return true;
        }
    }
    return false;
}

int techCost(const OpState &s, TechType t) {
    int cost = techData[t].tier * static_cast<int>(s.cities.size()) + 4;
    // Literacy, from Philosophy
    return owns(s, PHILOSOPHY) ? (cost * 2 + 2) / 3 : cost;
}

int income(const OpState &s) {
    int stars = 1; // the capital
    for (const CityState &c : s.cities) {
        stars += c.level + c.workshop + c.parks;
    }
    // an embassy: +2 each way, +4 at peace, so allow the most
    return stars + 4 * s.embassies;
}

// the score of everything but territory and exploration (GAME.md section 3)
int knownScore(const OpState &s, int turn) {
    int score = 0;
    for (const TechData &tech : techData) {
        if (owns(s, tech.type)) {
            score += 100 * tech.tier;
        }
    }
    for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
        score += 5 * unitInfo(static_cast<UnitType>(u)).cost * s.units[u];
    }
    score += 10 * s.ruinRammers;
    for (const CityState &c : s.cities) {
        score += 100 + 50 * (c.level - 1) + 5 * c.pop + 250 * c.parks;
    }
    for (int built : s.temples) {
        score += 100 * std::min(MAX_TEMPLE_LEVEL, 1 + (turn - built) / 2);
    }
    return score + 400 * s.monuments;
}

// The most tiles the opponent's units could reveal this turn. Each step
// along a path reveals at most 4r + 1 new tiles for sight r (a diagonal
// step); ending on a mountain, at most 16. Units can't move into fog, so
// every path starts and runs through explored tiles.
int unitExploration(const TurnInfo &info, const OpState &s) {
    auto perUnit = [&](UnitType type) {
        const UnitData &data = unitInfo(type);
        bool naval = data.abilities.test(Abilities::WATER);
        int sight = data.abilities.test(Abilities::SCOUT) ? 2 : 1;
        int step = 4 * sight + 1;
        int last = !naval && owns(s, CLIMBING) ? std::max(step, 16) : step;
        int length = data.movement * (!naval && owns(s, ROADS) ? 2 : 1);
        int moves = 1 + data.abilities.test(Abilities::ESCAPE);
        // and a melee kill moves it one more tile
        return moves * ((length - 1) * step + last) + step;
    };
    int total = 0;
    for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
        if (info.unitsBefore[u] > 0) {
            total += info.unitsBefore[u] * perUnit(static_cast<UnitType>(u));
        }
    }
    return total + info.rammersBefore * perUnit(RAMMER);
}

// The most score one disband can take back: 5 x the dearest unit's cost
int maxDisband() {
    static const int most = [] {
        int cost = 0;
        for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
            cost = std::max(cost, unitInfo(static_cast<UnitType>(u)).cost);
        }
        return 5 * cost;
    }();
    return most;
}

// Within a turn the known score only goes up, except by one disband. Each
// population still to place adds at least 5 more: one that levels a city up
// adds 50 - 5 x level, and cities stay below level 10 this early. So a branch
// whose known score has already passed the change by more than a disband
// can take back can't match, nor can anything after it. Researching Free
// Spirit itself adds more score than a disband can take back, so a state
// without it needs no allowance.
bool tooHigh(const OpState &s, const TurnInfo &info, const Context &ctx) {
    int slack = owns(s, FREESPIRIT) ? maxDisband() : 0;
    int pending = 5 * (info.capitalPop + info.anyPop);
    return knownScore(s, ctx.turn) + pending - ctx.before - slack > ctx.change;
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
        train(trained, info, ctx, u, next);
    }
}

struct Improvement {
    int cost, pop, stars;
    bool temple, embassy;
};

void improve(OpState s, TurnInfo info, const Context &ctx,
             const std::vector<Improvement> &options, std::size_t from,
             const Next &next) {
    if (tooHigh(s, info, ctx)) {
        return;
    }
    next(s, info);
    // Tiles can take more than one improvement (hunting, then a lumber hut),
    // so allow two per border tile.
    if (s.improvements >= 2 * s.territory) {
        return;
    }
    for (std::size_t i = from; i < options.size(); i++) {
        const Improvement &o = options[i];
        if (o.cost > s.stars || (o.embassy && s.embassies >= 1)) {
            continue;
        }
        OpState built = s;
        built.stars += o.stars - o.cost;
        built.improvements += !o.embassy;
        built.embassies += o.embassy;
        TurnInfo after = info;
        after.anyPop += o.pop;
        if (o.temple) {
            built.temples.push_back(ctx.turn);
        }
        improve(built, after, ctx, options, i, next);
    }
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
    if (c.pop < c.level + 1) {
        addPop(s, info, city, amount - 1, next);
        return;
    }
    c.pop -= c.level + 1;
    c.level++;
    // the two rewards (GAME.md section 7)
    OpState a = s, b = s;
    TurnInfo ia = info, ib = info;
    int moreA = 0;
    switch (c.level) {
    case 2:
        a.cities[city].workshop = true;
        ib.explorers++;
        break;
    case 3:
        b.stars += 5;
        break;
    case 4:
        moreA = 3; // Population Growth
        ib.borderGrowths++;
        break;
    default:
        a.cities[city].parks++;
        b.units[GIANT]++;
        break;
    }
    addPop(a, ia, city, amount - 1 + moreA, next);
    addPop(b, ib, city, amount - 1, next);
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

// Monuments, once their task is done: +400 and +3 population. The tasks that
// can come up early are Explorer (all 4 lighthouses), Metropolis (a city at
// level 5) and Pacifist (5 turns after Meditation).
void monuments(OpState s, TurnInfo info, const Context &ctx, int done,
               const Next &next) {
    next(s, info);
    bool explorer = s.lighthouses >= 4;
    bool metropolis = std::any_of(s.cities.begin(), s.cities.end(),
                                  [](const CityState &c) { return c.level >= 5; });
    bool pacifist = owns(s, MEDITATION) && ctx.turn >= 5;
    int available = explorer + metropolis + pacifist;
    if (done >= available || s.monuments >= 3) {
        return;
    }
    OpState built = s;
    built.monuments++;
    TurnInfo after = info;
    after.anyPop += 3;
    placePopulation(built, after, [&, done](OpState &t, TurnInfo &ti) {
        monuments(t, ti, ctx, done + 1, next);
    });
}

void events(OpState s, TurnInfo info, const Context &ctx, const Next &next) {
    int unitCount = info.rammersBefore;
    for (int n : info.unitsBefore) {
        unitCount += n;
    }
    // Capturing and examining a ruin need a unit that started its turn
    // there, so neither happens on turn 0.
    int maxCaptures = ctx.turn >= 1 ? std::min(unitCount, 2) : 0;
    int maxRuins =
        ctx.turn >= 1 ? std::min({unitCount, ctx.ruinsTotal - s.ruins, 2}) : 0;

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

        // ruins, each with its reward
        std::function<void(OpState, TurnInfo, int)> ruins =
            [&](OpState r, TurnInfo ri, int left) {
                if (tooHigh(r, ri, ctx)) {
                    return;
                }
                // lighthouses found (+1 population in the capital), and city
                // connections (+1 in each of the two cities)
                for (int l = 0; l <= 4 - r.lighthouses; l++) {
                    int maxConnections = static_cast<int>(r.cities.size()) - 1 -
                                         r.connections;
                    for (int x = 0; x <= maxConnections; x++) {
                        OpState e = r;
                        TurnInfo ei = ri;
                        e.lighthouses += l;
                        e.connections += x;
                        ei.capitalPop += l + x;
                        ei.anyPop += x;
                        // more of either only adds score
                        if (tooHigh(e, ei, ctx)) {
                            break;
                        }
                        next(e, ei);
                        // disbanding (Free Spirit): one unit a turn, refunding
                        // half its cost
                        if (owns(e, FREESPIRIT)) {
                            for (int u = 0; u < UNIT_TYPE_SIZE; u++) {
                                if (e.units[u] > 0) {
                                    OpState d = e;
                                    d.units[u]--;
                                    d.stars += unitInfo(static_cast<UnitType>(u)).cost / 2;
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
                // a free tech
                for (int t = 1; t < TECH_TYPE_SIZE; t++) {
                    if (researchable(base, static_cast<TechType>(t))) {
                        OpState got = base;
                        got.techs.set(t);
                        ruins(got, ri, left - 1);
                    }
                }
                // 10 stars
                OpState stars = base;
                stars.stars += 10;
                ruins(stars, ri, left - 1);
                // +3 population in the capital
                TurnInfo pop = ri;
                pop.capitalPop += 3;
                ruins(base, pop, left - 1);
                // an Explorer
                TurnInfo explorer = ri;
                explorer.explorers++;
                ruins(base, explorer, left - 1);
                // a veteran Swordsman, or a Rammer carrying a Warrior
                OpState swordsman = base;
                swordsman.units[SWORDSMAN]++;
                ruins(swordsman, ri, left - 1);
                OpState rammer = base;
                rammer.ruinRammers++;
                ruins(rammer, ri, left - 1);
            };
        // each level emits its states before taking another ruin, so this
        // covers every count up to the most
        ruins(c, ci, maxRuins);
    }
}

std::vector<Improvement> improvements(const OpState &s) {
    std::vector<Improvement> options;
    if (owns(s, ORGANIZATION) || owns(s, HUNTING) || owns(s, FISHING)) {
        options.push_back({2, 1, 0, false, false}); // harvest, hunt, fish
    }
    if (owns(s, FORESTRY)) {
        options.push_back({3, 1, 0, false, false}); // lumber hut
        options.push_back({0, 0, 1, false, false}); // clear forest
    }
    if (owns(s, FARMING) || owns(s, MINING)) {
        options.push_back({5, 2, 0, false, false}); // farm, mine
    }
    if (owns(s, FISHING)) {
        options.push_back({7, 1, 0, false, false}); // port
    }
    int temple = 1000;
    for (auto [tech, cost] : {std::pair{FREESPIRIT, 20}, {SPIRITUALISM, 15},
                              {MEDITATION, 20}, {AQUATISM, 20}}) {
        if (owns(s, tech)) {
            temple = std::min(temple, cost);
        }
    }
    if (temple < 1000) {
        options.push_back({temple, 1, 0, true, false});
    }
    if (owns(s, DIPLOMACY)) {
        options.push_back({5, 0, 0, false, true}); // embassy
    }
    return options;
}

struct Merged {
    std::map<std::vector<long>, OpState> states;
    bool overflow = false;
};

void keep(Merged &merged, const OpState &s, int maxStates) {
    auto [it, inserted] = merged.states.try_emplace(s.key(), s);
    if (inserted) {
        if (static_cast<int>(merged.states.size()) > maxStates) {
            merged.overflow = true;
        }
        return;
    }
    OpState &kept = it->second;
    kept.stars = std::max(kept.stars, s.stars);
    kept.territory = std::max(kept.territory, s.territory);
    kept.explored = std::min(kept.explored, s.explored);
}

// one opponent turn: every way to match `change` from `prev`
void expand(const OpState &prev, const Context &ctx, Merged &out,
            int maxStates) {
    OpState s = prev;
    if (ctx.turn >= 1) {
        s.stars += income(s);
    }
    TurnInfo info;
    info.unitsBefore = s.units;
    info.rammersBefore = s.ruinRammers;

    // the score match, last of all
    Next match = [&](OpState &t, TurnInfo &ti) {
        if (out.overflow) {
            return;
        }
        int rest = ctx.change - (knownScore(t, ctx.turn) - ctx.before);
        if (rest % 5 != 0 || rest < 0) {
            return;
        }
        // the rest is new territory (from captures and border growth) and
        // exploration; take as much territory as fits
        int lo = ti.captures, hi = 9 * ti.captures + 16 * ti.borderGrowths;
        long reveal = unitExploration(ti, t) + 9L * ti.captures +
                      16L * ti.borderGrowths +
                      (ti.explorers > 0 ? ctx.mapTiles : 0);
        reveal = std::min<long>(reveal, ctx.mapTiles - prev.explored);
        for (int k = std::min(hi, rest / 20); k >= lo; k--) {
            int tiles = (rest - 20 * k) / 5;
            if (tiles <= reveal) {
                OpState accepted = t;
                accepted.territory += k;
                accepted.explored = prev.explored + tiles;
                keep(out, accepted, maxStates);
                return;
            }
        }
    };

    research(s, info, ctx, 1, [&](OpState &a, TurnInfo &ai) {
        train(a, ai, ctx, 0, [&](OpState &b, TurnInfo &bi) {
            improve(b, bi, ctx, improvements(b), 0, [&](OpState &c, TurnInfo &ci) {
                events(c, ci, ctx, [&](OpState &d, TurnInfo &di) {
                    placePopulation(d, di, [&](OpState &e, TurnInfo &ei) {
                        monuments(e, ei, ctx, 0, match);
                    });
                });
            });
        });
    });
}

// The opponent's published changes, in order, and whether the player's own
// actions touched each one
struct Change {
    int amount, turn;
    bool touched;
};

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
            changes.push_back({record.score - last, record.turn,
                               view.effectsInChanges[r] != 0});
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
                                              int startingScore,
                                              int meetingStars) {
    auto frontier = std::make_shared<Frontier>();
    const TribeData &data = tribeData[tribe];
    // the tribe's starting state, from the engine's tables
    OpState start;
    start.techs.set(BASIC);
    start.techs.set(data.startTech);
    start.units[data.startUnit] = 1;
    start.cities = {{.level = data.capitalLevel}};
    start.territory = 9;
    int explored = startingScore - knownScore(start, 0) - 20 * 9;
    if (explored % 5 != 0 || explored / 5 < 9 || explored / 5 > 25) {
        frontier->status = Frontier::RULED_OUT;
        return frontier;
    }
    start.explored = explored / 5;
    start.stars = data.startStars + meetingStars;
    frontier->states = {start};
    return frontier;
}

std::shared_ptr<const Frontier> step(const Frontier &prev, const Change &change,
                                     const Context &board, int maxStates) {
    auto frontier = std::make_shared<Frontier>();
    frontier->turn = change.turn;
    if (prev.status != Frontier::OPEN || change.touched) {
        // a change the player's own actions touched isn't read
        frontier->status =
            prev.status == Frontier::OPEN ? Frontier::UNREAD : prev.status;
        return frontier;
    }
    Context ctx = board;
    ctx.turn = change.turn;
    ctx.change = change.amount;
    Merged next;
    for (const OpState &s : prev.states) {
        ctx.before = knownScore(s, prev.turn);
        expand(s, ctx, next, maxStates);
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
    // Meeting the player is worth stars once, by the player's score then.
    // Give them up front, since more stars only ever allows more: the most
    // the player's score has been gives the most it could be.
    int playerScore = view.scores[view.player];
    for (const ScoreRecord &r : view.scoreHistory) {
        if (r.player == view.player) {
            playerScore = std::max(playerScore, r.score);
        }
    }
    int meetingStars = Game::meetingBonus(playerScore);

    // a few hundred MB at most: a frontier can hold thousands of states
    thread_local FrontierCache cache;
    thread_local std::size_t cachedStates = 0;
    if (cachedStates > 500000) {
        cache.clear();
        cachedStates = 0;
    }
    FrontierKey key{view.size, tribe, meetingStars, config.maxStates,
                    startingScore};
    std::shared_ptr<const Frontier> frontier;
    if (auto found = cache.find(key); found != cache.end()) {
        frontier = found->second;
    } else {
        frontier = startFrontier(tribe, startingScore, meetingStars);
        cache.emplace(key, frontier);
    }
    Context board{view.size * view.size, ruinCountForSize(view.size), 0};
    int limit = std::min<int>(config.turns, changes.size());
    for (int k = 0; k < limit && frontier->status == Frontier::OPEN; k++) {
        key.insert(key.end(),
                   {changes[k].amount, changes[k].turn, changes[k].touched});
        if (auto found = cache.find(key); found != cache.end()) {
            frontier = found->second;
        } else {
            frontier = step(*frontier, changes[k], board, config.maxStates);
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
