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

// a map type's defaults for the settings left negative
struct TypeDefaults {
    double minWater, maxWater, preTerrainDensity;
};

TypeDefaults typeDefaults(MapTypes::MapType type) {
    switch (type) {
    case MapTypes::DRYLANDS:
        return {0, 0, 0};
    case MapTypes::LAKES:
        return {0.25, 0.30, 0.3};
    case MapTypes::PANGEA:
    case MapTypes::CONTINENTS:
        return {0.45, 0.55, 0};
    case MapTypes::ARCHIPELAGO:
        return {0.60, 0.80, 0.3};
    default:
        return {0.90, 1.0, 0.1};
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
    bool corner(int t) const {
        return (x(t) == 0 || x(t) == n - 1) && (y(t) == 0 || y(t) == n - 1);
    }
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

    // every village and capital: on land, off the edge, at least 3 apart.
    // Post-terrain villages also avoid the 2nd and 4th tiles from the edge.
    bool villageAllowed(int t, bool postTerrain) const {
        int edge = edgeDistance(t);
        if (!land(t) || edge == 0 || nearestSettlement(t) < 3) {
            return false;
        }
        return !postTerrain || (edge != 1 && edge != 3);
    }
    void addVillage(int t) {
        villages.push_back(t);
        settlement[t] = 1;
    }

    void placeCapitals();
    void placeSuburbs();
    void placePreTerrainVillages();
    bool placeWater();
    void classifyWater();
    void placePostTerrainVillages();
    void growClimateRegions();
    void placeTerrain();
    void placeResources();
    void placeStartingResources();
    void placeRuins();
    void placeStarfish();

    void shapeArchipelago();
    void shapeWaterWorld();
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

    for (int i = 0; i < players; i++) {
        int dx = domains[i] % k, dy = domains[i] / k;
        int x0 = dx * n / k, x1 = (dx + 1) * n / k;
        int y0 = dy * n / k, y1 = (dy + 1) * n / k;
        // keep a tile of margin inside the domain where possible
        for (int margin = 1; margin >= 0; margin--) {
            std::vector<int> options = tilesWhere([&](int t) {
                return x(t) >= x0 + margin && x(t) < x1 - margin &&
                       y(t) >= y0 + margin && y(t) < y1 - margin &&
                       edgeDistance(t) >= s.capitalEdgeDistance &&
                       nearestSettlement(t) >= 3;
            });
            if (!options.empty()) {
                int t = rng.pick(options);
                capitals.push_back(t);
                settlement[t] = 1;
                break;
            }
            if (margin == 0) {
                throw std::runtime_error("No room for a capital");
            }
        }
    }
}

void Generator::placeSuburbs() {
    // round-robin, so no capital takes the others' space
    for (int round = 0; round < s.suburbsPerCapital; round++) {
        for (int capital : capitals) {
            std::vector<int> options = tilesWhere([&](int t) {
                int d = dist(t, capital);
                return d >= s.minSuburbDistance && d <= s.maxSuburbDistance &&
                       villageAllowed(t, false);
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
            tilesWhere([&](int t) { return villageAllowed(t, false); });
        if (options.empty()) {
            return;
        }
        addVillage(rng.pick(options));
    }
}

bool Generator::placeWater() {
    for (Tile &tile : tiles) {
        tile.terrain = FIELD;
    }
    int target = waterTarget();
    // the outer ring stays land, and so do capitals and villages
    auto locked = [&](int t) { return edgeDistance(t) == 0 || settlement[t]; };

    int water = 0;
    while (water < target) {
        // start a new lake away from the others where possible
        std::vector<int> seeds = tilesWhere([&](int t) {
            if (!land(t) || locked(t)) {
                return false;
            }
            bool nearWater = false;
            forEachNeighbour(t, [&](int nb) { nearWater |= !land(nb); });
            return !nearWater;
        });
        if (seeds.empty()) {
            seeds = tilesWhere([&](int t) { return land(t) && !locked(t); });
        }
        if (seeds.empty()) {
            break;
        }
        int size = std::min(target - water,
                            s.minLake + rng.below(s.maxLake - s.minLake + 1));
        std::vector<int> frontier{rng.pick(seeds)};
        int grown = 0;
        while (grown < size && !frontier.empty()) {
            int i = rng.below(static_cast<int>(frontier.size()));
            int t = frontier[i];
            frontier[i] = frontier.back();
            frontier.pop_back();
            if (!land(t) || locked(t)) {
                continue;
            }
            tiles[t].terrain = WATER;
            water++;
            grown++;
            // straight neighbours, for rounder lakes
            for (int d : {-1, 1, -n, n}) {
                int nb = t + d;
                if (nb >= 0 && nb < n * n && dist(nb, t) == 1) {
                    frontier.push_back(nb);
                }
            }
        }
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
    // water next to land is shallow, the rest ocean
    for (int t = 0; t < n * n; t++) {
        if (land(t)) {
            continue;
        }
        bool coast = false;
        forEachNeighbour(t, [&](int nb) { coast |= land(nb); });
        tiles[t].terrain = coast ? WATER : OCEAN;
    }
}

void Generator::placePostTerrainVillages() {
    while (true) {
        std::vector<int> options =
            tilesWhere([&](int t) { return villageAllowed(t, true); });
        if (options.empty()) {
            return;
        }
        addVillage(rng.pick(options));
    }
}

void Generator::growClimateRegions() {
    // every capital claims one ring of tiles a round, ties broken at random
    std::vector<std::vector<int>> frontier(capitals.size());
    for (std::size_t p = 0; p < capitals.size(); p++) {
        region[capitals[p]] = static_cast<int>(p);
        frontier[p] = {capitals[p]};
    }
    while (true) {
        std::vector<std::vector<int>> claimants(n * n);
        bool any = false;
        for (std::size_t p = 0; p < frontier.size(); p++) {
            for (int t : frontier[p]) {
                forEachNeighbour(t, [&](int nb) {
                    auto &c = claimants[nb];
                    int player = static_cast<int>(p);
                    if (region[nb] < 0 &&
                        std::find(c.begin(), c.end(), player) == c.end()) {
                        c.push_back(player);
                        any = true;
                    }
                });
            }
        }
        if (!any) {
            return;
        }
        for (auto &f : frontier) {
            f.clear();
        }
        for (int t = 0; t < n * n; t++) {
            if (!claimants[t].empty()) {
                int p = rng.pick(claimants[t]);
                region[t] = p;
                frontier[p].push_back(t);
            }
        }
    }
}

void Generator::placeTerrain() {
    // field 48%, forest 38%, mountain 14%, hit as exactly as possible in each
    // tribe's region. The mountain modifier goes first, with forest and field
    // scaling to fit; then the forest modifier, taking only from fields.
    for (std::size_t p = 0; p < capitals.size(); p++) {
        const TribeData &tribe = tribeData[s.tribes[p]];
        std::vector<int> open = tilesWhere([&](int t) {
            return region[t] == static_cast<int>(p) && land(t) &&
                   !settlement[t] && !corner(t);
        });
        double mountain = std::min(1.0, 0.14 * tribe.mountainModifier);
        double forest = std::min(1.0 - mountain, 0.38 * (1 - mountain) / 0.86 *
                                                     tribe.forestModifier);
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
    // shares of all tiles of each terrain: inner (next to a village) and
    // outer (two away)
    struct Rate {
        ResourceType resource;
        double inner, outer;
    };
    auto rates = [&](TerrainType terrain) -> std::vector<Rate> {
        switch (terrain) {
        case FIELD:
            return {{ResourceTypes::FRUIT, 0.18 / 0.48, 0.06 / 0.48},
                    {ResourceTypes::CROP, 0.18 / 0.48, 0.06 / 0.48}};
        case FOREST:
            return {{ResourceTypes::GAME, 0.19 / 0.38, 0.06 / 0.38}};
        case MOUNTAIN:
            return {{ResourceTypes::METAL, 0.11 / 0.14, 0.03 / 0.14}};
        case WATER:
            return {{ResourceTypes::FISH, 0.5, 0.5}};
        case OCEAN:
            return {{ResourceTypes::FISH, s.oceanFishInner, s.oceanFishOuter}};
        default:
            return {};
        }
    };

    for (int t = 0; t < n * n; t++) {
        int d = nearestSettlement(t);
        if (settlement[t] || corner(t) || d > 2) {
            continue;
        }
        const TribeData &tribe = tribeData[s.tribes[region[t]]];
        std::vector<Rate> options = rates(tiles[t].terrain);
        // each chance times the tribe modifier, scaled down to fit if they
        // add up to more than 1
        std::vector<double> chances;
        double sum = 0;
        for (const Rate &r : options) {
            double chance = (d == 1 ? r.inner : r.outer) *
                            tribe.resourceModifiers[r.resource];
            chances.push_back(chance);
            sum += chance;
        }
        double roll = rng.unit() * std::max(1.0, sum);
        for (std::size_t i = 0; i < options.size(); i++) {
            if (roll < chances[i]) {
                tiles[t].resource = options[i].resource;
                break;
            }
            roll -= chances[i];
        }
    }
}

void Generator::placeStartingResources() {
    for (std::size_t p = 0; p < capitals.size(); p++) {
        int capital = capitals[p];
        std::vector<int> placed;
        for (ResourceType resource :
             tribeData[s.tribes[p]].startingResources) {
            // random tiles next to the capital, changed to fit if needed.
            // Tiles whose terrain already fits come first (empty ones, then
            // ones whose resource gets replaced), so the map's shape barely
            // changes; only then does a tile's terrain change. Water next to
            // another landmass never becomes land, so Continents stay apart.
            bool wantsWater = resource == ResourceTypes::FISH;
            auto bridges = [&](int t) {
                bool other = false;
                forEachNeighbour(t, [&](int nb) {
                    other |= landmass[nb] >= 0 &&
                             landmass[nb] != landmass[capital];
                });
                return !wantsWater && !land(t) && other;
            };
            std::vector<int> options;
            for (int pass = 0; pass < 4 && options.empty(); pass++) {
                forEachNeighbour(capital, [&](int t) {
                    bool used = std::find(placed.begin(), placed.end(), t) !=
                                placed.end();
                    bool empty = tiles[t].resource == ResourceTypes::NONE;
                    bool fits = land(t) != wantsWater;
                    bool allowed = pass == 0   ? fits && empty
                                   : pass == 1 ? fits
                                   : pass == 2 ? empty
                                               : true;
                    if (!settlement[t] && !used && edgeDistance(t) > 0 &&
                        !bridges(t) && allowed) {
                        options.push_back(t);
                    }
                });
            }
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
                nearestSettlement(t) < (ocean ? 3 : 2)) {
                return false;
            }
            return std::none_of(ruins.begin(), ruins.end(),
                                [&](int r) { return dist(r, t) < 2; });
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
    int water = static_cast<int>(tilesWhere([&](int t) { return !land(t); }).size());
    int count = static_cast<int>(std::lround(water / 25.0));
    std::vector<int> starfish;
    for (int i = 0; i < count; i++) {
        // never next to another starfish, a lighthouse or a village
        std::vector<int> options = tilesWhere([&](int t) {
            if (land(t) || tiles[t].resource != ResourceTypes::NONE ||
                tiles[t].building != ImprovementTypes::NONE ||
                nearestSettlement(t) < 2) {
                return false;
            }
            bool nearLighthouse = false;
            forEachNeighbour(t, [&](int nb) { nearLighthouse |= corner(nb); });
            return !nearLighthouse &&
                   std::none_of(starfish.begin(), starfish.end(),
                                [&](int f) { return dist(f, t) < 2; });
        });
        if (options.empty()) {
            return;
        }
        int t = rng.pick(options);
        starfish.push_back(t);
        tiles[t].resource = ResourceTypes::STARFISH;
    }
}

void Generator::shapeArchipelago() {
    // land grown as peninsulas around the capitals and villages
    int target = n * n - waterTarget();
    fillWater();
    std::vector<int> frontier;
    auto grow = [&](int t) {
        makeLand(t);
        forEachStraight(t, [&](int nb) {
            if (!land(nb)) {
                frontier.push_back(nb);
            }
        });
    };
    for (int t = 0; t < n * n; t++) {
        if (settlement[t]) {
            grow(t);
            forEachStraight(t, [&](int nb) {
                if (!land(nb)) {
                    grow(nb);
                }
            });
        }
    }
    while (landCount() < target && !frontier.empty()) {
        // of a few random coast tiles, the one with the least land around
        // it, so land reaches out in fingers
        int best = -1, bestLand = 9;
        for (int i = 0; i < 3; i++) {
            int pick = rng.below(static_cast<int>(frontier.size()));
            int t = frontier[pick];
            if (land(t)) {
                frontier[pick] = frontier.back();
                frontier.pop_back();
                if (frontier.empty()) {
                    break;
                }
                continue;
            }
            int around = 0;
            forEachStraight(t, [&](int nb) { around += land(nb); });
            if (around < bestLand) {
                best = t;
                bestLand = around;
            }
        }
        if (best >= 0) {
            grow(best);
        }
    }
}

void Generator::shapeWaterWorld() {
    // all water, with land forced under every city: a plus shape under each
    // capital and a tile or three under each village
    fillWater();
    for (int c : capitals) {
        makeLand(c);
        forEachStraight(c, [&](int nb) { makeLand(nb); });
    }
    for (int v : villages) {
        makeLand(v);
        int extra = rng.below(3);
        for (int i = 0; i < extra; i++) {
            std::vector<int> around;
            forEachNeighbour(v, [&](int nb) { around.push_back(nb); });
            makeLand(rng.pick(around));
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
    // separate landmasses, at least a tile apart, of 30 to 200 tiles
    int target = n * n - waterTarget();
    int players = static_cast<int>(s.tribes.size());
    // about 60 tiles each, no fewer than 30, but at least one per player
    int count = std::min(static_cast<int>(std::lround(target / 60.0)),
                         target / 30);
    count = std::max({1, players, count});
    fillWater();

    // seeds spread out: each as far as possible from the others
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
        seeds.push_back(rng.pick(best));
    }

    std::vector<int> sizes(count, 0);
    std::vector<std::vector<int>> frontier(count);
    // a tile can join a landmass only if no other landmass touches it
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
        sizes[mass]++;
        forEachStraight(t, [&](int nb) { frontier[mass].push_back(nb); });
    };
    for (int i = 0; i < count; i++) {
        grow(seeds[i], i);
    }
    int total = count;
    bool growing = true;
    while (total < target && growing) {
        growing = false;
        for (int mass = 0; mass < count && total < target; mass++) {
            auto &f = frontier[mass];
            while (sizes[mass] < 200 && !f.empty()) {
                int pick = rng.below(static_cast<int>(f.size()));
                int t = f[pick];
                f[pick] = f.back();
                f.pop_back();
                if (canJoin(t, mass)) {
                    grow(t, mass);
                    total++;
                    growing = true;
                    break;
                }
            }
        }
    }
}

void Generator::placeContinentVillages() {
    int masses = 1 + *std::max_element(landmass.begin(), landmass.end());
    for (int mass = 0; mass < masses; mass++) {
        std::vector<int> options = tilesWhere([&](int t) {
            return landmass[t] == mass && villageAllowed(t, true);
        });
        if (options.empty()) {
            options = tilesWhere([&](int t) {
                return landmass[t] == mass && villageAllowed(t, false);
            });
        }
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
    auto coastal = [&](int t) {
        bool water = false;
        forEachNeighbour(t, [&](int nb) { water |= !land(nb); });
        return water;
    };
    for (int p = 0; p < players; p++) {
        // as far from the other capitals as possible, on a new landmass
        // where possible, then next to water
        std::vector<int> pool;
        for (int v : villages) {
            bool tiny = std::find(tinyIslands.begin(), tinyIslands.end(), v) !=
                        tinyIslands.end();
            if (!tiny && edgeDistance(v) >= s.capitalEdgeDistance) {
                pool.push_back(v);
            }
        }
        if (pool.empty()) {
            for (int v : villages) {
                if (std::find(tinyIslands.begin(), tinyIslands.end(), v) ==
                    tinyIslands.end()) {
                    pool.push_back(v);
                }
            }
        }
        if (pool.empty()) {
            throw std::runtime_error("No village left to make a capital");
        }
        std::vector<int> best;
        std::tuple<int, int, int> bestKey{-1, -1, -1};
        for (int v : pool) {
            bool newMass = landmass[v] < 0 ||
                           std::none_of(capitals.begin(), capitals.end(),
                                        [&](int c) {
                                            return landmass[c] == landmass[v];
                                        });
            int far = n;
            for (int c : capitals) {
                far = std::min(far, dist(c, v));
            }
            std::tuple<int, int, int> key{newMass, far, coastal(v)};
            if (key > bestKey) {
                bestKey = key;
                best.clear();
            }
            if (key == bestKey) {
                best.push_back(v);
            }
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
    case MapTypes::LAKES: {
        placeCapitals();
        placeSuburbs();
        placePreTerrainVillages();
        bool ok = false;
        for (int attempt = 0; attempt < 30 && !ok; attempt++) {
            ok = placeWater();
        }
        break;
    }
    case MapTypes::ARCHIPELAGO:
        placeCapitals();
        placeSuburbs();
        placePreTerrainVillages();
        shapeArchipelago();
        break;
    case MapTypes::WATERWORLD:
        placeCapitals();
        placePreTerrainVillages();
        shapeWaterWorld();
        break;
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
    for (int t = 0; t < n * n; t++) {
        if (corner(t)) {
            tiles[t].terrain = FIELD;
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
    for (int i = 0; i < std::max(1, settings.candidates); i++) {
        Generator generator(settings, seeds.next());
        MapData map = generator.run();
        int gap = generator.fairnessGap();
        if (bestGap < 0 || gap < bestGap) {
            best = std::move(map);
            bestGap = gap;
        }
    }
    return best;
}
