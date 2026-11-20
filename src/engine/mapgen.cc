#include "mapgen.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <tuple>

namespace {

using namespace TerrainTypes;
using ResourceTypes::ResourceType;

// splitmix64, so a seed gives the same map with every compiler
class Rng {
    std::uint64_t state;

  public:
    explicit Rng(std::uint64_t seed) : state{seed} {}

    std::uint64_t next() {
        std::uint64_t z = (state += 0x9e3779b97f4a7c15);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
        z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
        return z ^ (z >> 31);
    }
    // uniform in [0, n)
    int below(int n) {
        return static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }
    // uniform in [0, 1)
    double unit() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

    template <typename T> void shuffle(std::vector<T> &v) {
        for (int i = static_cast<int>(v.size()) - 1; i > 0; i--) {
            std::swap(v[i], v[below(i + 1)]);
        }
    }
    template <typename T> const T &pick(const std::vector<T> &v) {
        return v[below(static_cast<int>(v.size()))];
    }
};

// a candidate map that can't be finished; generateMap tries another seed
struct FailedCandidate : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// a map type's defaults for the settings left negative. Water shares on the
// noise path are the real game's 10th to 90th percentiles on 14x14 (game:
// measured); smoothing passes, wetness and the edge's noise its generator's
// settings (smoothIterations, wetness, surroundingSpaceValue).
struct TypeDefaults {
    double minWater, maxWater, preTerrainDensity;
    int smoothing;
    double wetness, edgeNoise;
    // the noise path's lifted tiles (game: measured, the real generator's
    // own rule isn't read): all 8 of a capital's neighbours or only its
    // straight ones, and the straight neighbours of the other early
    // villages or none
    bool liftCapitalDiagonals, liftVillages;
};

TypeDefaults typeDefaults(MapTypes::MapType type) {
    switch (type) {
    case MapTypes::DRYLANDS:
        return {0, 0, 0, 0, 0, 0.5, false, false};
    case MapTypes::LAKES:
        return {0.255, 0.281, 0.3, 2, 0.3, 1, true, true};
    case MapTypes::PANGEA:
        // one smoothing pass, the outside of the map counting as water (the
        // generator's settings)
        return {0.5, 0.5, 0, 1, 0.5, 0, false, false};
    case MapTypes::CONTINENTS:
        // one smoothing pass, the outside of the map counting as land (the
        // generator's settings)
        return {0.362, 0.413, 0, 1, 0.5, 1, false, false};
    case MapTypes::ARCHIPELAGO:
        return {0.612, 0.643, 0.3, 0, 0.65, 0.5, false, false};
    default:
        return {0.847, 0.857, 0.1, 2, 0.85, 0.5, true, true};
    }
}

class Generator {
  public:
    Generator(const MapGenSettings &settings, std::uint64_t seed)
        : s{settings}, rng{seed}, n{settings.size}, tiles(n * n),
          settlement(n * n, 0), region(n * n, -1), landmass(n * n, -1) {
        TypeDefaults defaults = typeDefaults(s.type);
        minWater = s.minWater >= 0 ? s.minWater : defaults.minWater;
        maxWater = s.maxWater >= 0 ? s.maxWater : defaults.maxWater;
        preTerrainDensity = s.preTerrainDensity >= 0
                                ? s.preTerrainDensity
                                : defaults.preTerrainDensity;
        smoothing = s.smoothing >= 0 ? s.smoothing : defaults.smoothing;
        wetness = s.wetness >= 0 ? s.wetness : defaults.wetness;
        edgeNoise = s.edgeNoise >= 0 ? s.edgeNoise : defaults.edgeNoise;
        liftCapitalDiagonals = defaults.liftCapitalDiagonals;
        liftVillages = defaults.liftVillages;
        edgeLand = s.type == MapTypes::LAKES || s.type == MapTypes::DRYLANDS;
    }

    MapData run();

  private:
    const MapGenSettings &s;
    Rng rng;
    int n;
    std::vector<Tile> tiles;
    std::vector<int> capitals, villages;
    std::vector<char> settlement;
    // the player whose climate region each tile is in
    std::vector<int> region;
    // Continents and Pangea: which continent each land tile belongs to, and
    // each continent's tiles in the order it grew
    std::vector<int> landmass;
    std::vector<std::vector<int>> continentTiles;
    // villages on 1-tile islands, which never become capitals
    std::vector<int> tinyIslands;
    double minWater, maxWater, preTerrainDensity;
    int smoothing;
    double wetness, edgeNoise;
    bool liftCapitalDiagonals, liftVillages;
    // the outer ring is always land (game: Lakes, and Drylands has no water)
    bool edgeLand;

    // a water target picked in the type's range
    int waterTarget() {
        return static_cast<int>(std::lround(
            n * n * (minWater + rng.unit() * (maxWater - minWater))));
    }
    void fillWater() {
        for (Tile &tile : tiles) {
            tile.terrain = WATER;
        }
    }
    void makeLand(int t) { tiles[t].terrain = FIELD; }
    int landCount() const {
        int count = 0;
        for (int t = 0; t < n * n; t++) {
            count += land(t);
        }
        return count;
    }
    // straight neighbours, for rounder shapes
    template <typename F> void forEachStraight(int t, F f) const {
        if (x(t) > 0) f(t - 1);
        if (x(t) < n - 1) f(t + 1);
        if (y(t) > 0) f(t - n);
        if (y(t) < n - 1) f(t + n);
    }

    int x(int t) const { return t % n; }
    int y(int t) const { return t / n; }
    int dist(int a, int b) const {
        return std::max(std::abs(x(a) - x(b)), std::abs(y(a) - y(b)));
    }
    // 0 on the edge
    int edgeDistance(int t) const {
        return std::min({x(t), y(t), n - 1 - x(t), n - 1 - y(t)});
    }
    bool land(int t) const { return !isWater(tiles[t].terrain); }
    bool corner(int t) const { return isCornerTile(x(t), y(t), n); }
    template <typename F> void forEachNeighbour(int t, F f) const {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x(t) + dx, ny = y(t) + dy;
                if ((dx || dy) && nx >= 0 && ny >= 0 && nx < n && ny < n) {
                    f(ny * n + nx);
                }
            }
        }
    }
    template <typename P> std::vector<int> tilesWhere(P pred) const {
        std::vector<int> out;
        for (int t = 0; t < n * n; t++) {
            if (pred(t)) {
                out.push_back(t);
            }
        }
        return out;
    }
    int nearestSettlement(int t) const {
        int best = n;
        // (-1: a capital not placed yet)
        for (int c : capitals) {
            if (c >= 0) {
                best = std::min(best, dist(t, c));
            }
        }
        for (int v : villages) {
            best = std::min(best, dist(t, v));
        }
        return best;
    }

    // every village and capital: on land, off the edge, spaced apart
    bool villageAllowed(int t) const {
        return land(t) && edgeDistance(t) > 0 && nearestSettlement(t) >= s.settlementSpacing;
    }
    void addVillage(int t) {
        villages.push_back(t);
        settlement[t] = 1;
    }

    void placeCapitals();
    // count points, one in each of count domains of a grid centred on the
    // map (game: the generator's capital positions, also its continent seeds)
    std::vector<int> domainPositions(int count);
    int earlyVillageRoom() const;
    void placeSuburbs();
    void placePreTerrainVillages();
    // the noise path's water: Lakes, Archipelago, Water World. False when a
    // Lakes capital can't reach 2 villages over land
    bool shapeFromNoise();
    std::vector<double> landNoise();
    void classifyWater();
    void placePostTerrainVillages();
    void growClimateRegions();
    void placeTerrain();
    void placeResources();
    void placeStartingResources();
    void placeRuins();
    void placeStarfish();

    // Continents and Pangea: land, villages and capitals together. False
    // when there's no room for the capitals
    bool generateIslands();
    void growContinent(int c, const std::vector<double> &noise, std::vector<char> &broken);
    bool islandVillageSpot(int t) const;
    bool addContinentVillage(int c);
    void fillContinentVillages();
    bool capitalSpot(int t, int radius) const;
    bool addIslandCapital(int player, int c);
    bool placeIslandCapitals(int populated);
    bool placeCapitalsInOrder();
    void placeTinyIslandVillages();
};

void Generator::placeCapitals() {
    for (int t : domainPositions(static_cast<int>(s.tribes.size()))) {
        capitals.push_back(t);
        settlement[t] = 1;
    }
}

std::vector<int> Generator::domainPositions(int count) {
    // 4 domains for up to 4, 9 for up to 9, 16 for up to 16
    int k = count <= 4 ? 2 : count <= 9 ? 3 : 4;
    std::vector<int> domains(k * k);
    for (int i = 0; i < k * k; i++) {
        domains[i] = i;
    }
    rng.shuffle(domains);

    // domains of n / k tiles, the grid centred on the map with what's left
    // over at the edges, the odd tile first (game: measured, 11x11 capitals
    // are always on 3 or 8, in domains [1, 6) and [6, 11))
    int size = n / k, shift = (n - k * size + 1) / 2;
    // Each coordinate capitalDomainMargin in from the domain's sides,
    // weighted by (half the domain - its distance from offset half the
    // domain), squared, at least 1 (game: measured, 7-wide domains give 2, 3
    // or 4 at 1 : 2.3 : 1, 6-wide 2 or 3 at 31 : 69). The margin shrinks
    // where the domain is too small for it.
    auto weight = [&](int c, int from) {
        int half = size / 2, w = std::max(1, half - std::abs(c - (from + half)));
        return w * w;
    };
    std::vector<int> positions;
    for (int i = 0; i < count; i++) {
        int dx = domains[i] % k, dy = domains[i] / k;
        int x0 = shift + dx * size, x1 = x0 + size;
        int y0 = shift + dy * size, y1 = y0 + size;
        for (int margin = s.capitalDomainMargin;; margin--) {
            if (margin < 0) {
                throw std::runtime_error("No room for a capital");
            }
            int xlo = x0 + margin, xhi = x1 - 1 - margin;
            int ylo = y0 + margin, yhi = y1 - 1 - margin;
            std::vector<int> options, weights;
            int total = 0;
            for (int t = 0; t < n * n; t++) {
                bool spaced = std::all_of(positions.begin(), positions.end(), [&](int p) {
                    return dist(p, t) >= s.settlementSpacing;
                });
                if (x(t) >= xlo && x(t) <= xhi && y(t) >= ylo && y(t) <= yhi &&
                    edgeDistance(t) >= s.capitalEdgeDistance && spaced &&
                    nearestSettlement(t) >= s.settlementSpacing) {
                    options.push_back(t);
                    weights.push_back(weight(x(t), x0) * weight(y(t), y0));
                    total += weights.back();
                }
            }
            if (options.empty()) {
                continue;
            }
            int r = rng.below(total), j = 0;
            while ((r -= weights[j]) >= 0) {
                j++;
            }
            positions.push_back(options[j]);
            break;
        }
    }
    return positions;
}

// t = int(0.33 x the map's width); t^2 - capitals is how many early villages
// the map could hold (game: the generator's own count, for 11 to 30 wide t is
// 3, 4, 5, 5, 6, 9)
int Generator::earlyVillageRoom() const {
    int t = static_cast<int>(static_cast<float>(n) * 0.33f);
    return t * t - static_cast<int>(capitals.size());
}

void Generator::placeSuburbs() {
    // (game: the generator's own count) each capital gets the room for
    // early villages shared between the players, between minSuburbs and
    // maxSuburbs; round-robin, so no capital takes the others' space
    int players = static_cast<int>(capitals.size());
    int wanted = std::clamp(earlyVillageRoom() / std::max(1, players), s.minSuburbs,
                            s.maxSuburbs);
    for (int round = 0; round < wanted; round++) {
        for (int capital : capitals) {
            std::vector<int> options = tilesWhere([&](int t) {
                int d = dist(t, capital);
                return d >= s.minSuburbDistance && d <= s.maxSuburbDistance &&
                       villageAllowed(t);
            });
            if (!options.empty()) {
                addVillage(rng.pick(options));
            }
        }
    }
}

void Generator::placePreTerrainVillages() {
    // the density times the room left after the suburbs, truncated (game:
    // the generator's own count; so 11x11 Water World gets none)
    int room = earlyVillageRoom() - static_cast<int>(villages.size());
    int count = static_cast<int>(static_cast<float>(preTerrainDensity) *
                                 static_cast<float>(std::max(0, room)));
    for (int i = 0; i < count; i++) {
        std::vector<int> options =
            tilesWhere([&](int t) { return villageAllowed(t); });
        if (options.empty()) {
            return;
        }
        addVillage(rng.pick(options));
    }
}

std::vector<double> Generator::landNoise() {
    // (game: the generator's noise) a random value per tile, high for land;
    // around the capitals and early villages (TypeDefaults) it's lifted to
    // at least the wetness, so those tiles lean to land. Each smoothing pass
    // averages every 3x3, the outside of the map counting as edgeNoise.
    std::vector<double> noise(n * n);
    for (double &v : noise) {
        v = rng.unit();
    }
    std::vector<char> lifted(n * n, 0);
    auto lift = [&](int nb) { lifted[nb] = 1; };
    for (int c : capitals) {
        if (liftCapitalDiagonals) {
            forEachNeighbour(c, lift);
        } else {
            forEachStraight(c, lift);
        }
    }
    if (liftVillages) {
        for (int v : villages) {
            forEachStraight(v, lift);
        }
    }
    for (int t = 0; t < n * n; t++) {
        if (lifted[t]) {
            noise[t] = wetness + (1 - wetness) * noise[t];
        }
    }
    for (int pass = 0; pass < smoothing; pass++) {
        std::vector<double> smoothed(n * n);
        for (int t = 0; t < n * n; t++) {
            double sum = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x(t) + dx, ny = y(t) + dy;
                    bool inside = nx >= 0 && ny >= 0 && nx < n && ny < n;
                    sum += inside ? noise[ny * n + nx] : edgeNoise;
                }
            }
            smoothed[t] = sum / 9;
        }
        noise = std::move(smoothed);
    }
    return noise;
}

bool Generator::shapeFromNoise() {
    // the lowest noise becomes water, up to the type's share
    std::vector<double> noise = landNoise();
    // settlements stay land, and on Lakes the outer ring
    std::vector<int> order = tilesWhere([&](int t) {
        return !settlement[t] && !(edgeLand && edgeDistance(t) == 0);
    });
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return noise[a] < noise[b]; });
    for (Tile &tile : tiles) {
        tile.terrain = FIELD;
    }
    int target = std::min(waterTarget(), static_cast<int>(order.size()));
    for (int i = 0; i < target; i++) {
        tiles[order[i]].terrain = WATER;
    }
    if (s.type != MapTypes::LAKES) {
        return true;
    }

    // every player needs a land route to at least 2 villages
    int need = std::min<int>(2, static_cast<int>(villages.size()));
    for (int capital : capitals) {
        std::vector<char> seen(n * n, 0);
        std::vector<int> stack{capital};
        seen[capital] = 1;
        int reached = 0;
        while (!stack.empty()) {
            int t = stack.back();
            stack.pop_back();
            reached += settlement[t] &&
                       std::find(capitals.begin(), capitals.end(), t) ==
                           capitals.end();
            forEachNeighbour(t, [&](int nb) {
                if (!seen[nb] && land(nb)) {
                    seen[nb] = 1;
                    stack.push_back(nb);
                }
            });
        }
        if (reached < need) {
            return false;
        }
    }
    return true;
}

void Generator::classifyWater() {
    // water with land among its straight neighbours is shallow, the rest
    // ocean (game)
    for (int t = 0; t < n * n; t++) {
        if (land(t)) {
            continue;
        }
        bool coast = false;
        forEachStraight(t, [&](int nb) { coast |= land(nb); });
        tiles[t].terrain = coast ? WATER : OCEAN;
    }
}

void Generator::placePostTerrainVillages() {
    while (true) {
        std::vector<int> options =
            tilesWhere([&](int t) { return villageAllowed(t); });
        if (options.empty()) {
            return;
        }
        addVillage(rng.pick(options));
    }
}

void Generator::growClimateRegions() {
    // The regions take turns claiming one tile each, a random one from the
    // unclaimed tiles beside it, so they come out the same size with
    // irregular borders (game: measured, the larger of two regions holds
    // 50.7% of a Drylands map's land, and 7% of tiles lie nearer the other
    // capital)
    int players = static_cast<int>(capitals.size());
    std::vector<std::vector<int>> frontier(players);
    auto claim = [&](int t, int p) {
        region[t] = p;
        forEachNeighbour(t, [&](int nb) {
            if (region[nb] < 0) {
                frontier[p].push_back(nb);
            }
        });
    };
    for (int p = 0; p < players; p++) {
        claim(capitals[p], p);
    }
    for (bool any = true; any;) {
        any = false;
        for (int p = 0; p < players; p++) {
            auto &f = frontier[p];
            while (!f.empty()) {
                int i = rng.below(static_cast<int>(f.size()));
                int t = f[i];
                f[i] = f.back();
                f.pop_back();
                if (region[t] < 0) {
                    claim(t, p);
                    any = true;
                    break;
                }
            }
        }
    }
}

void Generator::placeTerrain() {
    // field 48, forest 38 and mountain 14, the last two times the tribe's
    // modifiers, as shares of their sum (game: measured, every tribe within
    // about a point), hit as exactly as possible in each tribe's region
    for (std::size_t p = 0; p < capitals.size(); p++) {
        const TribeData &tribe = tribeData[s.tribes[p]];
        std::vector<int> open = tilesWhere([&](int t) {
            return region[t] == static_cast<int>(p) && land(t) &&
                   !settlement[t] && !corner(t);
        });
        double fieldWeight = 0.48, forestWeight = 0.38 * tribe.forestModifier,
               mountainWeight = 0.14 * tribe.mountainModifier;
        double sum = fieldWeight + forestWeight + mountainWeight;
        double mountain = mountainWeight / sum, forest = forestWeight / sum;
        int total = static_cast<int>(open.size());
        int mountains = static_cast<int>(std::lround(total * mountain));
        int forests = std::min(total - mountains,
                               static_cast<int>(std::lround(total * forest)));
        rng.shuffle(open);
        for (int i = 0; i < total; i++) {
            tiles[open[i]].terrain = i < mountains             ? MOUNTAIN
                                     : i < mountains + forests ? FOREST
                                                               : FIELD;
        }
    }
}

void Generator::placeResources() {
    // within 2 of a settlement (resourceRates): next to one, each resource
    // tries at its chance times the tribe's modifier, and one of those that
    // succeed is picked; two away, the same draw outerShare of the time
    for (int t = 0; t < n * n; t++) {
        int d = nearestSettlement(t);
        if (settlement[t] || corner(t) || d > 2 ||
            (d == 2 && rng.unit() >= s.outerShare)) {
            continue;
        }
        const TribeData &tribe = tribeData[s.tribes[region[t]]];
        std::vector<ResourceType> won;
        for (const ResourceRate &r : resourceRates(tiles[t].terrain, s)) {
            if (rng.unit() < r.chance * tribe.resourceModifiers[r.resource]) {
                won.push_back(r.resource);
            }
        }
        if (!won.empty()) {
            tiles[t].resource = rng.pick(won);
        }
    }
}

void Generator::placeStartingResources() {
    for (std::size_t p = 0; p < capitals.size(); p++) {
        int capital = capitals[p];
        std::vector<int> placed;
        const std::vector<ResourceType> &starting =
            tribeData[s.tribes[p]].startingResources;
        for (ResourceType resource : starting) {
            // the capital's neighbours get at least as many of each as the
            // tribe's list has, counting what's there already (game:
            // measured, e.g. Hoodrick's 2 game come to 2.5 on average)
            int wanted = static_cast<int>(
                std::count(starting.begin(), starting.end(), resource));
            int have = 0;
            forEachNeighbour(capital, [&](int t) { have += tiles[t].resource == resource; });
            if (have >= wanted) {
                continue;
            }
            // a random tile next to the capital, changed to fit if needed,
            // whatever was on it (game: measured, Kickoo's fish take land
            // resources with them as often as not), except a tile holding
            // a resource the list still needs. Water next to another
            // landmass never becomes land, so Continents stay apart.
            bool wantsWater = resource == ResourceTypes::FISH;
            auto bridges = [&](int t) {
                bool other = false;
                forEachNeighbour(t, [&](int nb) {
                    other |= landmass[nb] >= 0 &&
                             landmass[nb] != landmass[capital];
                });
                return !wantsWater && !land(t) && other;
            };
            auto needed = [&](int t) {
                ResourceType held = tiles[t].resource;
                int listed = static_cast<int>(std::count(starting.begin(), starting.end(), held));
                int around = 0;
                forEachNeighbour(capital, [&](int u) { around += tiles[u].resource == held; });
                return held != ResourceTypes::NONE && around <= listed;
            };
            std::vector<int> options;
            forEachNeighbour(capital, [&](int t) {
                bool used = std::find(placed.begin(), placed.end(), t) != placed.end();
                if (!settlement[t] && !used && edgeDistance(t) > 0 && !bridges(t) &&
                    !needed(t)) {
                    options.push_back(t);
                }
            });
            if (options.empty()) {
                continue;
            }
            int chosen = rng.pick(options);
            placed.push_back(chosen);
            Tile &tile = tiles[chosen];
            switch (resource) {
            case ResourceTypes::GAME:
                tile.terrain = FOREST;
                break;
            case ResourceTypes::METAL:
                tile.terrain = MOUNTAIN;
                break;
            case ResourceTypes::FISH:
                tile.terrain = WATER;
                break;
            default:
                tile.terrain = FIELD;
                break;
            }
            tile.resource = resource;
        }
    }
}

void Generator::placeRuins() {
    // (game: the generator's own rules) one pass over the tiles in random
    // order, a ruin wherever the rules in mapgen.h allow, as many on ocean as
    // fit
    int count = s.ruins >= 0 ? s.ruins : ruinCountForSize(n);
    std::vector<int> order(n * n), ruins;
    for (int t = 0; t < n * n; t++) {
        order[t] = t;
    }
    rng.shuffle(order);
    auto near = [&](const std::vector<int> &from, int t, int distance) {
        return std::any_of(from.begin(), from.end(), [&](int f) { return f >= 0 && dist(f, t) < distance; });
    };
    for (int t : order) {
        if (static_cast<int>(ruins.size()) == count) {
            return;
        }
        bool ocean = tiles[t].terrain == OCEAN;
        if (tiles[t].terrain == WATER || corner(t) || tiles[t].building != ImprovementTypes::NONE ||
            near(villages, t, RUIN_LAND_SETTLEMENT_DISTANCE) ||
            near(capitals, t, RUIN_CAPITAL_DISTANCE) || near(ruins, t, RUIN_SPACING) ||
            (ocean && (edgeDistance(t) < 1 || nearestSettlement(t) < RUIN_OCEAN_SETTLEMENT_DISTANCE))) {
            continue;
        }
        ruins.push_back(t);
        tiles[t].building = ImprovementTypes::RUIN;
    }
}

void Generator::placeStarfish() {
    // the type's wetness times the tiles, over 25, rounded up (game: the
    // generator's own count, e.g. 3 on 14x14 Lakes, 7 on Water World). Each
    // try is a random water tile, kept if it fits, with as many tries as
    // there are water tiles. A corner can take one, and loses it to its
    // lighthouse, placed after in the real game (game: measured, starfish
    // are next to corners as often as anywhere, and Pangea, all of whose
    // corners are water, has one fewer on 11% of maps)
    int count = static_cast<int>(std::ceil(wetness * n * n / 25.0 - 1e-9));
    std::vector<int> water = tilesWhere([&](int t) { return !land(t); });
    std::vector<int> starfish;
    auto fits = [&](int t) {
        // empty, not next to a village or another starfish
        if ((tiles[t].resource != ResourceTypes::NONE ||
             tiles[t].building != ImprovementTypes::NONE) && !corner(t)) {
            return false;
        }
        return nearestSettlement(t) >= 2 &&
               std::none_of(starfish.begin(), starfish.end(),
                            [&](int f) { return dist(f, t) < 2; });
    };
    for (std::size_t tries = 0;
         tries < water.size() && static_cast<int>(starfish.size()) < count; tries++) {
        int t = rng.pick(water);
        if (fits(t)) {
            starfish.push_back(t);
            if (!corner(t)) {
                tiles[t].resource = ResourceTypes::STARFISH;
            }
        }
    }
}

// ---- Continents and Pangea (game: the generator's islands path, read from
// its code: TryGenerateMapFromIslands, AddContinents and what they call) ----

namespace {
// ceil(sqrt(players)): the side of the domain grid
int gridSide(int players) {
    int k = 1;
    while (k * k < players) {
        k++;
    }
    return k;
}
} // namespace

bool Generator::generateIslands() {
    const int tiles2 = n * n;
    const int players = static_cast<int>(s.tribes.size());
    std::vector<double> noise = landNoise();
    fillWater();

    // seeds: on Continents a count picked from 2 to 7, those that give each
    // a land share of 30 to 200 tiles, at domain positions; on Pangea one in
    // the centre
    std::vector<int> seeds;
    if (s.type == MapTypes::CONTINENTS) {
        std::vector<int> counts;
        for (int c = 2; c <= 7; c++) {
            double share = (1 - wetness) * static_cast<double>(tiles2 / c);
            if (share >= 30 && share <= 200) {
                counts.push_back(c);
            }
        }
        seeds = domainPositions(counts.empty() ? 2 : rng.pick(counts));
    } else {
        seeds.push_back((n / 2) * n + n / 2);
    }
    const int count = static_cast<int>(seeds.size());

    // how many continents get capitals: all of them when there are at least
    // as many players, otherwise fewer until each has at least 2
    int populated = count;
    while (populated > 1) {
        double perContinent = std::max(static_cast<double>(players) / populated, 1.0);
        if (perContinent >= 2 || perContinent == 1) {
            break;
        }
        populated--;
    }

    // each continent's size: half the land shared out evenly, half by the
    // capitals it will hold, round-robin over the populated ones; with
    // several continents the land is half a continent bigger
    double land = (1 - wetness) * tiles2 * (count >= 2 ? 1 + 0.5 / count : 1);
    std::vector<int> capitalsOn(count, 0);
    for (int p = 0; p < players; p++) {
        capitalsOn[p % populated]++;
    }
    continentTiles.assign(count, {});
    for (int i = 0; i < count; i++) {
        makeLand(seeds[i]);
        landmass[seeds[i]] = i;
        continentTiles[i].push_back(seeds[i]);
    }
    std::vector<char> broken(tiles2, 0);
    for (int i = 0; i < count; i++) {
        double target = land / 2 / count + land / 2 / players * capitalsOn[i];
        for (int step = 0; step < 1000 && target > continentTiles[i].size(); step++) {
            growContinent(i, noise, broken);
        }
    }
    classifyWater();

    fillContinentVillages();
    if (!placeIslandCapitals(populated)) {
        // start over with the villages, the capitals one by one along the map
        for (int v : villages) {
            settlement[v] = 0;
        }
        villages.clear();
        if (!placeCapitalsInOrder()) {
            return false;
        }
        fillContinentVillages();
    }
    placeTinyIslandVillages();
    return true;
}

void Generator::growContinent(int c, const std::vector<double> &noise, std::vector<char> &broken) {
    // From a random tile of the continent, the straight neighbour scoring
    // best above 0: its noise + 1, less a fifth of its distance from the edge
    // on Continents (land runs round the edges) or 1 over it on Pangea (never
    // the edge), and for each tile of another continent around it a random
    // 0.5 to 3 less, 10 more when straight beside it. A tile with nothing to
    // take is broken for good.
    std::vector<int> order = continentTiles[c];
    rng.shuffle(order);
    for (int p : order) {
        if (broken[p]) {
            continue;
        }
        std::vector<int> around;
        forEachStraight(p, [&](int nb) { around.push_back(nb); });
        rng.shuffle(around);
        int best = -1;
        double bestScore = 0;
        for (int t : around) {
            if (landmass[t] >= 0) {
                continue;
            }
            double score = noise[t] + 1;
            int d = edgeDistance(t);
            if (s.type == MapTypes::PANGEA) {
                score = d == 0 ? -1e9 : score - 1.0 / d;
            } else {
                score -= d / 5.0;
            }
            forEachNeighbour(t, [&](int q) {
                if (landmass[q] >= 0 && landmass[q] != c) {
                    score -= 0.5 + 2.5 * rng.unit();
                    if (x(q) == x(t) || y(q) == y(t)) {
                        score -= 10;
                    }
                }
            });
            if (score > bestScore) {
                best = t;
                bestScore = score;
            }
        }
        if (best >= 0) {
            makeLand(best);
            landmass[best] = c;
            continentTiles[c].push_back(best);
            return;
        }
        broken[p] = 1;
    }
}

bool Generator::islandVillageSpot(int t) const {
    // land, off the edge, no village within 2, at least 2 from the edge,
    // and neither coordinate 3 from its nearer edge
    auto axis = [&](int c) { return std::min(c, n - 1 - c); };
    return land(t) && edgeDistance(t) >= 2 && nearestSettlement(t) >= s.settlementSpacing &&
           axis(x(t)) != 3 && axis(y(t)) != 3;
}

bool Generator::addContinentVillage(int c) {
    // a random tile of the continent fit for a village; failing that, any
    // off the edge and away from other villages that touches its own
    // continent and no other, placed but counted as a failure
    if (continentTiles[c].size() < 2) {
        return false;
    }
    std::vector<int> order = continentTiles[c];
    rng.shuffle(order);
    for (int t : order) {
        if (islandVillageSpot(t)) {
            addVillage(t);
            return true;
        }
    }
    for (int t : order) {
        bool own = false, other = false;
        forEachNeighbour(t, [&](int q) {
            own |= landmass[q] == c;
            other |= landmass[q] >= 0 && landmass[q] != c;
        });
        if (edgeDistance(t) >= 1 && own && !other &&
            nearestSettlement(t) >= s.settlementSpacing) {
            addVillage(t);
            return false;
        }
    }
    return false;
}

void Generator::fillContinentVillages() {
    // round-robin over the continents until every one in a row has failed
    const int count = static_cast<int>(continentTiles.size());
    for (int c = 0, failures = 0; failures < count; c = (c + 1) % count) {
        failures = addContinentVillage(c) ? 0 : failures + 1;
    }
}

bool Generator::capitalSpot(int t, int radius) const {
    // a village spot with no capital within the radius
    return islandVillageSpot(t) &&
           std::none_of(capitals.begin(), capitals.end(), [&](int c) {
               return c >= 0 && dist(c, t) <= radius;
           });
}

bool Generator::addIslandCapital(int player, int c) {
    // The continent's village, in the order the continent grew, furthest
    // from the capitals so far (counted up to 20, at least 4 apart where the
    // domains are wider than 4, else 3), at least 2 from the edge, one more
    // for 2 to 6 water tiles around it; the first of equals. With none, a
    // random tile fit for a capital anywhere.
    const int players = static_cast<int>(s.tribes.size());
    const bool wide = (n - 1) / gridSide(players) > 4;
    const int apart = wide ? 4 : 3;
    int best = -1, bestScore = 0;
    for (int t : continentTiles[c]) {
        if (!settlement[t] || std::find(capitals.begin(), capitals.end(), t) != capitals.end()) {
            continue;
        }
        int far = 20;
        for (int other : capitals) {
            if (other >= 0) {
                far = std::min(far, dist(other, t));
            }
        }
        if (far < apart || edgeDistance(t) < 2) {
            continue;
        }
        int water = 0;
        forEachNeighbour(t, [&](int q) { water += !land(q); });
        int score = far + (water >= 2 && water <= 6 ? 1 : 0);
        if (score > bestScore) {
            best = t;
            bestScore = score;
        }
    }
    if (best >= 0) {
        villages.erase(std::find(villages.begin(), villages.end(), best));
    } else {
        std::vector<int> options = tilesWhere([&](int t) { return capitalSpot(t, wide ? 3 : 2); });
        if (options.empty()) {
            return false;
        }
        best = rng.pick(options);
        settlement[best] = 1;
    }
    capitals[player] = best;
    return true;
}

bool Generator::placeIslandCapitals(int populated) {
    // each player in turn on the next populated continent, then each again
    // with the others all placed
    const int players = static_cast<int>(s.tribes.size());
    capitals.assign(players, -1);
    for (int p = 0; p < players; p++) {
        if (!addIslandCapital(p, p % populated)) {
            capitals.clear();
            return false;
        }
    }
    for (int p = 0; p < players; p++) {
        int t = capitals[p];
        capitals[p] = -1;
        villages.push_back(t);
        int c = landmass[t] >= 0 ? landmass[t] : p % populated;
        if (!addIslandCapital(p, c)) {
            capitals.clear();
            return false;
        }
    }
    return true;
}

bool Generator::placeCapitalsInOrder() {
    // the fallback: each player the next tile along the map fit for a
    // capital, whatever its continent
    const int players = static_cast<int>(s.tribes.size());
    const int radius = (n - 1) / gridSide(players) > 4 ? 3 : 2;
    capitals.assign(players, -1);
    int t = 0;
    for (int p = 0; p < players; p++) {
        while (t < n * n && !capitalSpot(t, radius)) {
            t++;
        }
        if (t == n * n) {
            capitals.clear();
            return false;
        }
        capitals[p] = t;
        settlement[t] = 1;
    }
    return true;
}

void Generator::placeTinyIslandVillages() {
    // the wetness times the tiles, over 50 (1, 1, 2, 3, 4, 9 for 11 to 30
    // wide), each on open water with no land around and no village within 2
    int count = static_cast<int>(wetness * n * n / 50);
    std::vector<int> order(n * n);
    for (int t = 0; t < n * n; t++) {
        order[t] = t;
    }
    rng.shuffle(order);
    for (int t : order) {
        if (count == 0) {
            return;
        }
        bool open = !land(t) && edgeDistance(t) >= 1 && nearestSettlement(t) >= s.settlementSpacing;
        forEachNeighbour(t, [&](int q) { open &= !land(q); });
        if (open) {
            makeLand(t);
            addVillage(t);
            tinyIslands.push_back(t);
            count--;
        }
    }
}

MapData Generator::run() {
    switch (s.type) {
    case MapTypes::DRYLANDS:
        placeCapitals();
        break;
    case MapTypes::LAKES:
    case MapTypes::ARCHIPELAGO:
    case MapTypes::WATERWORLD: {
        placeCapitals();
        if (s.type != MapTypes::WATERWORLD) {
            placeSuburbs();
        }
        placePreTerrainVillages();
        bool ok = false;
        for (int attempt = 0; attempt < 30 && !ok; attempt++) {
            ok = shapeFromNoise();
        }
        break;
    }
    case MapTypes::PANGEA:
    case MapTypes::CONTINENTS:
        if (!generateIslands()) {
            throw FailedCandidate("No room for the capitals");
        }
        break;
    default:
        throw std::invalid_argument("Unknown map type");
    }
    classifyWater();
    if (s.type != MapTypes::PANGEA && s.type != MapTypes::CONTINENTS) {
        placePostTerrainVillages();
    }
    growClimateRegions();
    placeTerrain();
    // a lighthouse in each corner, on whatever is there: water stays water,
    // land becomes field (game)
    for (int t = 0; t < n * n; t++) {
        if (corner(t)) {
            if (land(t)) {
                tiles[t].terrain = FIELD;
            }
            tiles[t].building = ImprovementTypes::LIGHTHOUSE;
        }
    }
    placeResources();
    placeStartingResources();
    // starting fish can turn land into water and the other way round
    classifyWater();
    for (int v : villages) {
        tiles[v].terrain = FIELD;
        tiles[v].resource = ResourceTypes::NONE;
        tiles[v].building = ImprovementTypes::VILLAGE;
    }
    for (int c : capitals) {
        tiles[c].terrain = FIELD;
        tiles[c].resource = ResourceTypes::NONE;
    }
    placeRuins();
    placeStarfish();

    MapData map;
    map.size = n;
    map.type = s.type;
    map.tiles = tiles;
    map.capitals = capitals;
    map.regions = region;
    return map;
}

} // namespace

std::vector<ResourceRate> resourceRates(TerrainType terrain,
                                        const MapGenSettings &s) {
    (void)s;
    switch (terrain) {
    case FIELD:
        return {{ResourceTypes::FRUIT, 0.5}, {ResourceTypes::CROP, 0.5}};
    case FOREST:
        return {{ResourceTypes::GAME, 0.5}};
    case MOUNTAIN:
        return {{ResourceTypes::METAL, 0.8}};
    case WATER:
    case OCEAN:
        return {{ResourceTypes::FISH, 0.52}, {ResourceTypes::AQUACROP, 0.3}};
    default:
        return {};
    }
}

bool resourceFitsTerrain(ResourceType resource, TerrainType terrain) {
    if (resource == ResourceTypes::NONE) {
        return true;
    }
    // starfish go on any water (placeStarfish)
    if (resource == ResourceTypes::STARFISH) {
        return isWater(terrain);
    }
    std::vector<ResourceRate> rates = resourceRates(terrain);
    return std::any_of(rates.begin(), rates.end(),
                       [&](const ResourceRate &r) { return r.resource == resource; });
}

bool isCornerTile(int x, int y, int size) {
    return (x == 0 || x == size - 1) && (y == 0 || y == size - 1);
}

int ruinCountForSize(int size) {
    switch (size) {
    case 11:
        return 4;
    case 14:
        return 5;
    case 16:
        return 7;
    case 18:
        return 9;
    case 20:
        return 11;
    case 30:
        return 23;
    default:
        // close to the table for other sizes
        return std::max(1, static_cast<int>(std::lround(size * size / 40.0)));
    }
}

double startInequality(const MapData &map) {
    const int n = map.size;
    const std::vector<Tile> &tiles = map.tiles;
    const std::vector<int> &capitals = map.capitals;
    auto land = [&](int t) { return !isWater(tiles[t].terrain); };
    auto settlement = [&](int t) {
        return tiles[t].building == ImprovementTypes::VILLAGE ||
               std::find(capitals.begin(), capitals.end(), t) != capitals.end();
    };
    auto forEachNeighbour = [&](int t, auto f) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = t % n + dx, ny = t / n + dy;
                if ((dx || dy) && nx >= 0 && ny >= 0 && nx < n && ny < n) {
                    f(ny * n + nx);
                }
            }
        }
    };
    // (game: the generator's fairness score, checked against the game's own)
    // Territories: one search from every capital, lowest cost first (the
    // earliest found among equals), a cheaper way to a tile found later
    // taking it over, and an equally cheap one too if its player is first in
    // seat order. A step costs 1 onto land, 4 onto a mountain or from water to
    // water, and into water from land 8 from a capital, 16 from a village,
    // 1000 from anywhere else.
    struct Entry {
        int tile, cost, player;
    };
    std::vector<Entry> next;
    for (std::size_t p = 0; p < capitals.size(); p++) {
        next.push_back({capitals[p], 0, static_cast<int>(p)});
    }
    auto isCapital = [&](int t) {
        return std::find(capitals.begin(), capitals.end(), t) != capitals.end();
    };
    std::vector<char> done(n * n, 0);
    std::vector<float> score(capitals.size(), 0);
    while (!next.empty()) {
        std::size_t lowest = 0;
        for (std::size_t i = 1; i < next.size(); i++) {
            if (next[i].cost < next[lowest].cost) {
                lowest = i;
            }
        }
        Entry e = next[lowest];
        next.erase(next.begin() + static_cast<std::ptrdiff_t>(lowest));
        done[e.tile] = 1;

        // the tile's score: d is its distance, 0 within 3 steps, 1 at
        // 0.8 of the map's width
        float d = std::clamp((e.cost - 3.0f) / (n * 0.8f - 3.0f), 0.0f, 1.0f);
        float value = 0.1f;
        if (settlement(e.tile)) {
            value += 100 - 90 * d + (e.cost == 3 ? 40 : e.cost == 4 ? 20 : 0);
        } else if (tiles[e.tile].building == ImprovementTypes::RUIN) {
            value += 5 - 3 * d + (e.cost <= 4 ? 5 : 0);
        }
        if (land(e.tile)) {
            value += 1.5f - d;
        }
        score[e.player] += value;

        forEachNeighbour(e.tile, [&](int nb) {
            if (done[nb]) {
                return;
            }
            int step = 1;
            if (tiles[nb].terrain == MOUNTAIN) {
                step = 4;
            } else if (!land(nb)) {
                step = !land(e.tile)       ? 4
                       : isCapital(e.tile) ? 8
                       : settlement(e.tile) ? 16
                                            : 1000;
            }
            int cost = e.cost + step;
            for (Entry &other : next) {
                if (other.tile == nb) {
                    // a tie goes to the player first in seat order
                    if (cost < other.cost || (cost == other.cost && e.player < other.player)) {
                        other.cost = cost;
                        other.player = e.player;
                    }
                    return;
                }
            }
            next.push_back({nb, cost, e.player});
        });
    }
    float best = *std::max_element(score.begin(), score.end());
    float worst = *std::min_element(score.begin(), score.end());
    return best / std::max(worst, 0.001f);
}

MapData generateMap(const MapGenSettings &settings) {
    if (settings.type < 0 || settings.type >= MapTypes::MAP_TYPE_SIZE) {
        throw std::invalid_argument("Unknown map type");
    }
    if (settings.tribes.empty() || settings.tribes.size() > 16) {
        throw std::invalid_argument("Maps need 1 to 16 players");
    }
    if (settings.size < 8) {
        throw std::invalid_argument("Maps must be at least 8 tiles wide");
    }

    MapData best;
    double bestInequality = -1;
    Rng seeds(settings.seed);
    int wanted = std::max(1, settings.candidates), made = 0;
    for (int attempt = 0; made < wanted && !(bestInequality >= 0 &&
                                             bestInequality <= settings.equalityLimit);
         attempt++) {
        Generator generator(settings, seeds.next());
        MapData map;
        try {
            map = generator.run();
        } catch (const FailedCandidate &) {
            if (attempt >= 50 * wanted) {
                throw;
            }
            continue;
        }
        made++;
        double inequality = startInequality(map);
        if (bestInequality < 0 || inequality < bestInequality) {
            best = std::move(map);
            bestInequality = inequality;
        }
    }
    return best;
}
