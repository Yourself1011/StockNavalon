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
        // half the map water, then a tiny island village (game: measured,
        // 49.5% water on every 14x14 map)
        return {0.5, 0.5, 0, 0, 0.5, 0, false, false};
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
    // capitals score their resources within 2 tiles and villages within 4;
    // the gap between the best and worst
    int fairnessGap() const;

  private:
    const MapGenSettings &s;
    Rng rng;
    int n;
    std::vector<Tile> tiles;
    std::vector<int> capitals, villages;
    std::vector<char> settlement;
    // the player whose climate region each tile is in
    std::vector<int> region;
    // Continents: which landmass each land tile belongs to
    std::vector<int> landmass;
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
        for (int c : capitals) {
            best = std::min(best, dist(t, c));
        }
        for (int v : villages) {
            best = std::min(best, dist(t, v));
        }
        return best;
    }

    // every village and capital: on land, off the edge, spaced apart. On
    // Continents and Pangea, also never with a coordinate 3 from its nearer
    // edge (game: measured, 2-3% of villages there, 16% on Drylands)
    bool villageAllowed(int t) const {
        if (!land(t) || edgeDistance(t) == 0 ||
            nearestSettlement(t) < s.settlementSpacing) {
            return false;
        }
        if (s.type == MapTypes::CONTINENTS || s.type == MapTypes::PANGEA) {
            for (int c : {x(t), y(t)}) {
                if (std::min(c, n - 1 - c) == 3) {
                    return false;
                }
            }
        }
        return true;
    }
    void addVillage(int t) {
        villages.push_back(t);
        settlement[t] = 1;
    }

    void placeCapitals();
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

    void shapePangea();
    void shapeContinents();
    // Continents: a village on each landmass first, then as many as fit
    void placeContinentVillages();
    void placeTinyIslandVillages();
    // Pangea and Continents: some villages become capitals
    void pickCapitalsFromVillages();
};

void Generator::placeCapitals() {
    int players = static_cast<int>(s.tribes.size());
    // 4 domains for up to 4 players, 9 for up to 9, 16 for up to 16
    int k = players <= 4 ? 2 : players <= 9 ? 3 : 4;
    std::vector<int> domains(k * k);
    for (int i = 0; i < k * k; i++) {
        domains[i] = i;
    }
    rng.shuffle(domains);

    // Each coordinate capitalDomainMargin in from the domain's sides,
    // weighted towards its middle: 1 2 1 over three places, 1 2 2 1 over
    // four (game: 14x14 gives 2, 3 or 4 into a 7-wide domain at 1:2:1). The
    // margin shrinks where the domain is too small for it.
    auto triangle = [](int c, int lo, int hi) { return std::min(c - lo, hi - c) + 1; };
    for (int i = 0; i < players; i++) {
        int dx = domains[i] % k, dy = domains[i] / k;
        int x0 = dx * n / k, x1 = (dx + 1) * n / k;
        int y0 = dy * n / k, y1 = (dy + 1) * n / k;
        for (int margin = s.capitalDomainMargin;; margin--) {
            if (margin < 0) {
                throw std::runtime_error("No room for a capital");
            }
            int xlo = x0 + margin, xhi = x1 - 1 - margin;
            int ylo = y0 + margin, yhi = y1 - 1 - margin;
            std::vector<int> options, weights;
            int total = 0;
            for (int t = 0; t < n * n; t++) {
                if (x(t) >= xlo && x(t) <= xhi && y(t) >= ylo && y(t) <= yhi &&
                    edgeDistance(t) >= s.capitalEdgeDistance &&
                    nearestSettlement(t) >= s.settlementSpacing) {
                    options.push_back(t);
                    weights.push_back(triangle(x(t), xlo, xhi) *
                                      triangle(y(t), ylo, yhi));
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
            capitals.push_back(options[j]);
            settlement[options[j]] = 1;
            break;
        }
    }
}

void Generator::placeSuburbs() {
    // each capital wants minSuburbs to maxSuburbs; round-robin, so no
    // capital takes the others' space
    std::vector<int> wanted;
    for (std::size_t i = 0; i < capitals.size(); i++) {
        wanted.push_back(s.minSuburbs + rng.below(s.maxSuburbs - s.minSuburbs + 1));
    }
    for (int round = 0; round < s.maxSuburbs; round++) {
        for (std::size_t i = 0; i < capitals.size(); i++) {
            if (round >= wanted[i]) {
                continue;
            }
            std::vector<int> options = tilesWhere([&](int t) {
                int d = dist(t, capitals[i]);
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
    int third = n / 3;
    int base = third * third - static_cast<int>(capitals.size()) -
               static_cast<int>(villages.size());
    int count = static_cast<int>(
        std::lround(std::max(0, base) * preTerrainDensity));
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
    // On Continents and Pangea the ring capitalEdgeDistance in fills first,
    // where the capitals will come from (game: measured, 41% of villages
    // there are on that ring, and capitals are nearly always on opposite
    // sides of it)
    bool ringFirst = s.type == MapTypes::CONTINENTS || s.type == MapTypes::PANGEA;
    while (true) {
        std::vector<int> options =
            tilesWhere([&](int t) { return villageAllowed(t); });
        if (options.empty()) {
            return;
        }
        if (ringFirst) {
            std::vector<int> ring;
            for (int t : options) {
                if (edgeDistance(t) == s.capitalEdgeDistance) {
                    ring.push_back(t);
                }
            }
            if (!ring.empty()) {
                options = std::move(ring);
            }
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
    int count = s.ruins >= 0 ? s.ruins : ruinCountForSize(n);
    // at most a third on water, except on Water World
    int maxOnWater = s.type == MapTypes::WATERWORLD ? count : count / 3;
    int onWater = 0;
    std::vector<int> ruins;
    for (int i = 0; i < count; i++) {
        std::vector<int> options = tilesWhere([&](int t) {
            const Tile &tile = tiles[t];
            bool ocean = tile.terrain == OCEAN;
            // never on shallow water, never next to another ruin or a village;
            // ocean ruins at least 2 tiles from any village
            if (tile.terrain == WATER || (ocean && onWater >= maxOnWater) ||
                tile.resource != ResourceTypes::NONE || corner(t) ||
                nearestSettlement(t) < (ocean ? RUIN_OCEAN_SETTLEMENT_DISTANCE
                                              : RUIN_LAND_SETTLEMENT_DISTANCE)) {
                return false;
            }
            return std::none_of(ruins.begin(), ruins.end(),
                                [&](int r) { return dist(r, t) < RUIN_SPACING; });
        });
        if (options.empty()) {
            return;
        }
        int t = rng.pick(options);
        ruins.push_back(t);
        tiles[t].building = ImprovementTypes::RUIN;
        onWater += tiles[t].terrain == OCEAN;
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

void Generator::shapePangea() {
    // one landmass grown from the centre, roughly round
    int target = n * n - waterTarget();
    fillWater();
    double centre = (n - 1) / 2.0;
    auto fromCentre = [&](int t) {
        return std::hypot(x(t) - centre, y(t) - centre);
    };
    std::vector<int> frontier{(n / 2) * n + n / 2};
    int grown = 0;
    while (grown < target && !frontier.empty()) {
        // of a few random coast tiles, the one nearest the centre
        int bestIndex = -1;
        for (int i = 0; i < 4; i++) {
            int pick = rng.below(static_cast<int>(frontier.size()));
            if (bestIndex < 0 ||
                fromCentre(frontier[pick]) < fromCentre(frontier[bestIndex])) {
                bestIndex = pick;
            }
        }
        int t = frontier[bestIndex];
        frontier[bestIndex] = frontier.back();
        frontier.pop_back();
        if (land(t)) {
            continue;
        }
        makeLand(t);
        grown++;
        forEachStraight(t, [&](int nb) {
            if (!land(nb)) {
                frontier.push_back(nb);
            }
        });
    }
}

void Generator::shapeContinents() {
    // Continents grown from the noise (shapeFromNoise's, the outside of the
    // map counting as land, so land runs round the edges: game, measured,
    // about 65% land on the outer four rings, 20% at the centre). Each takes
    // in turn its unclaimed neighbour with the highest noise, never one
    // touching another continent, so they stay apart by at least a tile,
    // until the land target is reached. How many: one per player, then one
    // fewer or one more at continentFewer and continentMore (game:
    // measured, 2 players give 1, 2 or 3 continents 19 / 53 / 28% of the
    // time).
    std::vector<double> noise = landNoise();
    int target = n * n - waterTarget();
    int players = static_cast<int>(s.tribes.size());
    double r = rng.unit();
    int count = std::max(1, players + (r < s.continentFewer   ? -1
                                       : r > 1 - s.continentMore ? 1
                                                                  : 0));
    fillWater();

    // seeds as far from each other as possible, the highest noise among
    // equals
    std::vector<int> seeds;
    for (int i = 0; i < count; i++) {
        int bestDistance = -1;
        std::vector<int> best;
        for (int t = 0; t < n * n; t++) {
            if (edgeDistance(t) < 2) {
                continue;
            }
            int d = n;
            for (int seed : seeds) {
                d = std::min(d, dist(t, seed));
            }
            if (d > bestDistance) {
                bestDistance = d;
                best.clear();
            }
            if (d == bestDistance) {
                best.push_back(t);
            }
        }
        if (best.empty()) {
            break;
        }
        seeds.push_back(*std::max_element(best.begin(), best.end(), [&](int a, int b) {
            return noise[a] < noise[b];
        }));
    }
    count = static_cast<int>(seeds.size());

    std::vector<std::vector<int>> frontier(count);
    auto canJoin = [&](int t, int mass) {
        if (land(t)) {
            return false;
        }
        bool clear = true;
        forEachNeighbour(t, [&](int nb) {
            clear &= landmass[nb] < 0 || landmass[nb] == mass;
        });
        return clear;
    };
    auto grow = [&](int t, int mass) {
        makeLand(t);
        landmass[t] = mass;
        forEachNeighbour(t, [&](int nb) {
            if (!land(nb)) {
                frontier[mass].push_back(nb);
            }
        });
    };
    for (int i = 0; i < count; i++) {
        grow(seeds[i], i);
    }
    int total = count;
    for (bool growing = true; growing && total < target;) {
        growing = false;
        for (int mass = 0; mass < count && total < target; mass++) {
            auto &f = frontier[mass];
            // the neighbour this continent can still take with the best
            // noise, plus a pull towards tiles it already surrounds
            int best = -1;
            double bestScore = 0;
            for (std::size_t i = 0; i < f.size();) {
                if (!canJoin(f[i], mass)) {
                    f[i] = f.back();
                    f.pop_back();
                    continue;
                }
                int around = 0;
                forEachNeighbour(f[i], [&](int nb) { around += landmass[nb] == mass; });
                double score = noise[f[i]] + s.continentCompactness * around;
                if (best < 0 || score > bestScore) {
                    best = static_cast<int>(i);
                    bestScore = score;
                }
                i++;
            }
            if (best >= 0) {
                int t = f[best];
                f[best] = f.back();
                f.pop_back();
                grow(t, mass);
                total++;
                growing = true;
            }
        }
    }
}

void Generator::placeContinentVillages() {
    int masses = 1 + *std::max_element(landmass.begin(), landmass.end());
    for (int mass = 0; mass < masses; mass++) {
        std::vector<int> options = tilesWhere([&](int t) {
            return landmass[t] == mass && villageAllowed(t);
        });
        if (!options.empty()) {
            addVillage(rng.pick(options));
        }
    }
    placePostTerrainVillages();
}

void Generator::placeTinyIslandVillages() {
    int count = tinyIslandCountForSize(n);
    for (int i = 0; i < count; i++) {
        // open water, away from land and villages
        std::vector<int> options = tilesWhere([&](int t) {
            if (land(t) || edgeDistance(t) == 0 || nearestSettlement(t) < 3) {
                return false;
            }
            bool open = true;
            forEachNeighbour(t, [&](int nb) { open &= !land(nb); });
            return open;
        });
        if (options.empty()) {
            return;
        }
        int t = rng.pick(options);
        makeLand(t);
        addVillage(t);
        tinyIslands.push_back(t);
    }
}

void Generator::pickCapitalsFromVillages() {
    int players = static_cast<int>(s.tribes.size());
    for (int p = 0; p < players; p++) {
        // a village exactly capitalEdgeDistance from the edge (game:
        // measured, on both types), as far from the other capitals as
        // possible
        std::vector<int> best;
        int bestFar = -1;
        for (int v : villages) {
            bool tiny = std::find(tinyIslands.begin(), tinyIslands.end(), v) !=
                        tinyIslands.end();
            if (tiny || edgeDistance(v) != s.capitalEdgeDistance) {
                continue;
            }
            int far = n;
            for (int c : capitals) {
                far = std::min(far, dist(c, v));
            }
            if (far > bestFar) {
                bestFar = far;
                best.clear();
            }
            if (far == bestFar) {
                best.push_back(v);
            }
        }
        if (best.empty()) {
            throw FailedCandidate("No village left to make a capital");
        }
        int chosen = rng.pick(best);
        villages.erase(std::find(villages.begin(), villages.end(), chosen));
        capitals.push_back(chosen);
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
        shapePangea();
        classifyWater();
        placePostTerrainVillages();
        pickCapitalsFromVillages();
        placeTinyIslandVillages();
        break;
    case MapTypes::CONTINENTS:
        shapeContinents();
        classifyWater();
        placeContinentVillages();
        placeTinyIslandVillages();
        pickCapitalsFromVillages();
        break;
    default:
        throw std::invalid_argument("Unknown map type");
    }
    classifyWater();
    if (s.type != MapTypes::PANGEA && s.type != MapTypes::CONTINENTS) {
        placePostTerrainVillages();
    }
    if (s.type == MapTypes::WATERWORLD) {
        placeTinyIslandVillages();
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

int Generator::fairnessGap() const {
    int best = -1, worst = -1;
    for (int capital : capitals) {
        int value = 0;
        for (int t = 0; t < n * n; t++) {
            if (dist(t, capital) <= 2 &&
                tiles[t].resource != ResourceTypes::NONE) {
                value++;
            }
        }
        for (int v : villages) {
            value += dist(v, capital) <= 4;
        }
        best = best < 0 ? value : std::max(best, value);
        worst = worst < 0 ? value : std::min(worst, value);
    }
    return best - worst;
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

int tinyIslandCountForSize(int size) {
    switch (size) {
    case 11:
        return 0;
    case 14:
        return 1;
    case 16:
        return 2;
    case 18:
        return 3;
    case 20:
        return 4;
    case 30:
        return 9;
    default:
        return std::max(0, (size - 12) / 2);
    }
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
    int bestGap = -1;
    Rng seeds(settings.seed);
    int wanted = std::max(1, settings.candidates), made = 0;
    for (int attempt = 0; made < wanted; attempt++) {
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
        int gap = generator.fairnessGap();
        if (bestGap < 0 || gap < bestGap) {
            best = std::move(map);
            bestGap = gap;
        }
    }
    return best;
}
