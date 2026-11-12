#include "determinize.h"
#include "game.h"
#include "mapgen.h"
#include "tribeInference.h"
#include "view.h"
#include <algorithm>
#include <random>
#include <stdexcept>

using namespace TechTypes;
using namespace UnitTypes;

Beliefs priorBeliefs(const PlayerView &view) {
    const int tiles = view.size * view.size;
    Beliefs beliefs;
    beliefs.players.resize(view.tribes.size());
    for (OpponentBeliefs &b : beliefs.players) {
        b.tribe.fill(1);
        b.tech.fill(0.1f);
        b.stars.assign(21, 1);
        b.hiddenUnitCount.assign(4, 1);
        b.unitHere.assign(tiles, 1);
        std::array<float, UNIT_TYPE_SIZE> any;
        any.fill(1);
        b.unitType.assign(tiles, any);
    }
    return beliefs;
}

namespace {

// a weighted pick; -1 if every weight is 0
template <typename Weights>
int pick(const Weights &weights, std::mt19937_64 &rng) {
    double total = 0;
    for (float w : weights) {
        total += std::max(0.0f, w);
    }
    if (total <= 0) {
        return -1;
    }
    double r = std::uniform_real_distribution<double>(0, total)(rng);
    int last = -1;
    for (std::size_t i = 0; i < weights.size(); i++) {
        float w = std::max(0.0f, weights[i]);
        if (w > 0) {
            last = static_cast<int>(i);
            r -= w;
            if (r < 0) {
                return last;
            }
        }
    }
    return last;
}

// Unexplored tiles from the network's beliefs (Beliefs::terrain and the
// rest), within the map generator's rules (mapgen.h): first each tile's
// feature, then its terrain (a village's is a field, a ruin's never shallow
// water), then a resource that fits it. Villages and ruins too close to a
// settlement or each other are dropped, as the generator would never place
// them; corners stay the generated map's lighthouses.
template <typename Known>
void sampleUnexplored(Game &g, const Beliefs &beliefs, const Known &known,
                      std::mt19937_64 &rng) {
    using namespace TerrainTypes;
    const int tiles = g.size * g.size;
    auto dist = [&](int a, int b) {
        return std::max(std::abs(g.tileX(a) - g.tileX(b)),
                        std::abs(g.tileY(a) - g.tileY(b)));
    };
    std::vector<int> villages, ruins;
    for (int t = 0; t < tiles; t++) {
        if (known(t) || isCornerTile(g.tileX(t), g.tileY(t), g.size)) {
            continue;
        }
        Tile &tile = g.tiles[t];
        auto feature = static_cast<TileFeature>(std::max(0, pick(beliefs.feature[t], rng)));
        std::array<float, TERRAIN_TYPE_SIZE> terrain = beliefs.terrain[t];
        if (feature == TileFeature::VILLAGE) {
            terrain.fill(0);
            terrain[FIELD] = 1;
        } else if (feature == TileFeature::RUIN) {
            terrain[WATER] = 0;
        }
        int sampled = pick(terrain, rng);
        tile.terrain = sampled >= 0 ? static_cast<TerrainType>(sampled) : FIELD;
        std::array<float, ResourceTypes::RESOURCE_TYPE_SIZE> resource = beliefs.resource[t];
        for (int r = 0; r < ResourceTypes::RESOURCE_TYPE_SIZE; r++) {
            // a village or ruin has no resource
            if (!resourceFitsTerrain(static_cast<ResourceTypes::ResourceType>(r), tile.terrain) ||
                (feature != TileFeature::NONE && r != ResourceTypes::NONE)) {
                resource[r] = 0;
            }
        }
        int r = pick(resource, rng);
        tile.resource = r >= 0 ? static_cast<ResourceTypes::ResourceType>(r)
                               : ResourceTypes::NONE;
        tile.building = ImprovementTypes::NONE;
        if (feature == TileFeature::VILLAGE) {
            villages.push_back(t);
        } else if (feature == TileFeature::RUIN) {
            ruins.push_back(t);
        }
    }
    // the settlements already there: known villages and every city
    std::vector<int> settlements;
    for (int t = 0; t < tiles; t++) {
        if (g.tiles[t].building == ImprovementTypes::VILLAGE ||
            g.tiles[t].building == ImprovementTypes::CITY) {
            settlements.push_back(t);
        }
    }
    auto nearest = [&](int t) {
        int best = g.size;
        for (int s : settlements) {
            best = std::min(best, dist(s, t));
        }
        return best;
    };
    const int spacing = MapGenSettings{}.settlementSpacing;
    for (int v : villages) {
        if (nearest(v) >= spacing) {
            g.tiles[v].building = ImprovementTypes::VILLAGE;
            settlements.push_back(v);
        }
    }
    std::vector<int> kept;
    for (int t : ruins) {
        bool ocean = g.tiles[t].terrain == OCEAN;
        if (nearest(t) >= (ocean ? RUIN_OCEAN_SETTLEMENT_DISTANCE
                                 : RUIN_LAND_SETTLEMENT_DISTANCE) &&
            std::none_of(kept.begin(), kept.end(),
                         [&](int k) { return dist(k, t) < RUIN_SPACING; })) {
            g.tiles[t].building = ImprovementTypes::RUIN;
            kept.push_back(t);
        }
    }
}

// Renumbers units and cities by tile, dropping dead units, so a sample
// carries nothing of the order things happened in the real game
void canonicalize(Game &g) {
    std::vector<Unit> units;
    for (const Unit &u : g.units) {
        if (u.alive) {
            units.push_back(u);
        }
    }
    std::sort(units.begin(), units.end(),
              [](const Unit &a, const Unit &b) { return a.tile < b.tile; });

    std::vector<int> order(g.cities.size());
    for (std::size_t i = 0; i < order.size(); i++) {
        order[i] = static_cast<int>(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return g.cities[a].tile < g.cities[b].tile;
    });
    std::vector<int> newIndex(g.cities.size());
    std::vector<City> cities;
    for (int old : order) {
        newIndex[old] = static_cast<int>(cities.size());
        cities.push_back(g.cities[old]);
    }
    auto renumber = [&](int city) { return city < 0 ? -1 : newIndex[city]; };
    for (Tile &tile : g.tiles) {
        tile.territory = renumber(tile.territory);
        tile.unit = -1;
    }
    for (Unit &u : units) {
        u.home = renumber(u.home);
    }
    for (Player &p : g.players) {
        p.capital = renumber(p.capital);
    }
    g.units = std::move(units);
    g.cities = std::move(cities);
    for (std::size_t i = 0; i < g.units.size(); i++) {
        g.tiles[g.units[i].tile].unit = static_cast<int>(i);
    }
}

// a city's border radius
int borderRadius(const City &city) {
    return city.borderGrown ? Rules::GROWN_BORDER_RADIUS
                            : Rules::CITY_BORDER_RADIUS;
}

// One sample for a two-player game, or nullopt if it can't match the score
std::optional<Game> sample(const Game &real, int player,
                           const PlayerView &view, const Beliefs &beliefs,
                           std::uint64_t seed, Detail detail) {
    std::mt19937_64 rng(seed);
    Game g = real;
    const int q = 1 - player;
    const int tiles = g.size * g.size;
    const OpponentBeliefs &b = beliefs.players.at(q);
    Player &them = g.players[q];
    auto known = [&](int t) { return view.tiles[t].explored; };

    // the random state decides ruin rewards and Explorer paths: never kept
    g.settings.seed = rng();
    g.rng = rng();

    // ---- the tribe ----
    if (!view.tribes[q]) {
        TribeSet possible = possibleTribes(view, q);
        std::array<float, TribeTypes::TRIBE_SIZE> weights{};
        for (int t = 0; t < TribeTypes::TRIBE_SIZE; t++) {
            weights[t] = possible.test(t) ? b.tribe[t] : 0;
        }
        int tribe = pick(weights, rng);
        if (tribe < 0) {
            // beliefs that rule out every possible tribe: any possible one
            for (int t = 0; t < TribeTypes::TRIBE_SIZE; t++) {
                weights[t] = possible.test(t);
            }
            tribe = pick(weights, rng);
        }
        them.tribe = static_cast<TribeTypes::Tribe>(tribe);
    }
    const TribeData &tribe = tribeData[them.tribe];

    // ---- their units the player can't see ----
    // in the fog, and hidden ones (the Hide skill) on explored tiles too
    for (Unit &u : g.units) {
        if (u.alive && u.owner == q && view.tiles[u.tile].unit < 0) {
            u.alive = false;
            g.tiles[u.tile].unit = -1;
        }
    }

    // ---- their cities in the fog ----
    // Each fogged city either has borders the player has seen, which says
    // roughly where it is, or no evidence at all. The capital is always
    // there; other cities without evidence are dropped (no belief head for
    // them yet).
    std::vector<char> drop(g.cities.size(), 0);
    std::vector<int> moving;
    for (std::size_t c = 0; c < g.cities.size(); c++) {
        const City &city = g.cities[c];
        if (city.owner != q || known(city.tile)) {
            continue;
        }
        bool evidence = false;
        for (int t = 0; t < tiles; t++) {
            evidence |= known(t) && g.tiles[t].territory == static_cast<int>(c);
        }
        if (evidence || static_cast<int>(c) == them.capital) {
            moving.push_back(static_cast<int>(c));
        } else {
            drop[c] = 1;
        }
    }

    // ---- unexplored tiles, from a generated map ----
    std::vector<TribeTypes::Tribe> lineup;
    for (const Player &p : g.players) {
        lineup.push_back(p.tribe);
    }
    MapData map = generateMap({.size = g.size,
                               .type = g.mapType,
                               .tribes = lineup,
                               .seed = rng(),
                               .candidates = 1});
    for (int t = 0; t < tiles; t++) {
        if (known(t)) {
            continue;
        }
        Tile &tile = g.tiles[t];
        const Tile &from = map.tiles[t];
        tile.terrain = from.terrain;
        tile.resource = from.resource;
        bool feature = from.building == ImprovementTypes::VILLAGE ||
                       from.building == ImprovementTypes::RUIN ||
                       from.building == ImprovementTypes::LIGHTHOUSE;
        tile.building = feature ? from.building : ImprovementTypes::NONE;
        tile.level = 0;
        tile.builtTurn = 0;
        tile.road = false;
        tile.roadBuilder = -1;
        tile.territory = -1;
    }
    if (!beliefs.terrain.empty()) {
        sampleUnexplored(g, beliefs, known, rng);
    }
    // on explored tiles the player knows which roads are theirs, and in 1v1
    // on a map that started without roads, who built the rest
    for (int t = 0; t < tiles; t++) {
        Tile &tile = g.tiles[t];
        if (known(t) && tile.road) {
            tile.roadBuilder = view.tiles[t].ownRoad ? player
                               : g.startedWithRoads ? -1
                                                    : q;
        }
    }

    // drop cities without evidence, renumbering what refers to cities
    {
        std::vector<int> newIndex(g.cities.size(), -1);
        std::vector<City> cities;
        for (std::size_t c = 0; c < g.cities.size(); c++) {
            if (!drop[c]) {
                newIndex[c] = static_cast<int>(cities.size());
                cities.push_back(g.cities[c]);
            }
        }
        auto renumber = [&](int c) { return c < 0 ? -1 : newIndex[c]; };
        for (Tile &tile : g.tiles) {
            tile.territory = renumber(tile.territory);
        }
        for (Unit &u : g.units) {
            u.home = renumber(u.home);
        }
        for (Player &p : g.players) {
            p.capital = renumber(p.capital);
        }
        for (int &c : moving) {
            c = renumber(c);
        }
        g.cities = std::move(cities);
    }

    // place the fogged cities: the capital where their starting score allows,
    // each within reach of the borders the player has seen
    const int spacing = MapGenSettings{}.settlementSpacing;
    std::vector<int> capitalTiles = possibleCapitalTiles(view, q, them.tribe);
    std::vector<char> capitalOk(tiles, 0);
    for (int t : capitalTiles) {
        capitalOk[t] = 1;
    }
    for (int c : moving) {
        City &city = g.cities[c];
        bool isCapital = c == them.capital;
        std::vector<int> seenBorders;
        for (int t = 0; t < tiles; t++) {
            if (known(t) && g.tiles[t].territory == c) {
                seenBorders.push_back(t);
            }
        }
        std::vector<float> weights(tiles, 0);
        for (int t = 0; t < tiles; t++) {
            // the terrain here was just sampled, and a city makes it a field
            if (known(t) ||
                (isCapital && !capitalTiles.empty() && !capitalOk[t])) {
                continue;
            }
            bool fits = true;
            for (int s : seenBorders) {
                fits &= g.distance(t, s) <= Rules::GROWN_BORDER_RADIUS;
            }
            for (std::size_t o = 0; o < g.cities.size(); o++) {
                if (static_cast<int>(o) != c &&
                    g.distance(t, g.cities[o].tile) < spacing) {
                    fits = false;
                }
            }
            weights[t] = fits;
        }
        int at = pick(weights, rng);
        if (at < 0) {
            return std::nullopt;
        }
        bool needsGrowth = false;
        for (int s : seenBorders) {
            needsGrowth |= g.distance(at, s) > Rules::CITY_BORDER_RADIUS;
        }
        city.tile = at;
        Tile &tile = g.tiles[at];
        tile.terrain = TerrainTypes::FIELD;
        tile.resource = ResourceTypes::NONE;
        tile.building = ImprovementTypes::CITY;
        tile.territory = c;
        // what's inside a city in the fog is hidden too
        int startLevel = isCapital ? tribe.capitalLevel : 1;
        std::uniform_int_distribution<int> grown(0, g.turn / 4);
        city.level = startLevel + grown(rng);
        city.population = 0;
        city.parks = 0;
        city.workshop = city.level >= 2 && rng() % 2;
        city.wall = false;
        city.borderGrown = needsGrowth;
        city.pendingRewards = 0;
        city.connectedFor = -1;
    }
    // their borders over unexplored tiles; explored tiles keep theirs
    for (std::size_t c = 0; c < g.cities.size(); c++) {
        const City &city = g.cities[c];
        g.forEachInRadius(city.tile, borderRadius(city), [&](int t) {
            if (!known(t) && g.tiles[t].territory < 0) {
                g.tiles[t].territory = static_cast<int>(c);
            }
        });
    }

    // the climate of unexplored tiles: the nearest capital's
    std::vector<int> capitals;
    for (const Player &p : g.players) {
        capitals.push_back(p.capital < 0 ? -1 : g.cities[p.capital].tile);
    }
    for (int t = 0; t < tiles; t++) {
        if (!known(t)) {
            int seat = g.nearestCapitalSeat(t, capitals);
            g.tiles[t].climate = seat < 0 ? -1 : g.players[seat].tribe;
        }
    }

    // ---- techs: the start, what the player has proof of, and samples ----
    TechSet techs;
    techs.set(BASIC);
    techs.set(tribe.startTech);
    auto withAncestors = [&](TechType t) {
        for (; t != BASIC && !techs.test(t); t = techData[t].parent) {
            techs.set(t);
        }
    };
    // Proofs: only what nothing but their own research explains. With more
    // players, a unit could be converted from a third player, a city with
    // buildings captured from one, a road or a hidden unit be theirs, so then
    // these are left to the beliefs.
    const bool twoPlayers = g.players.size() == 2;
    // Their units prove a tech once there are more of them needing it than
    // other explanations could cover: units converted from the player (who
    // knows which they lost), and ruin soldiers (veterans of the ruin types,
    // no more than the ruins they could have reached).
    if (twoPlayers) {
        int ruins = ruinCountForSize(g.size);
        for (int t = 0; t < tiles; t++) {
            ruins -= known(t) && g.tiles[t].building == ImprovementTypes::RUIN;
        }
        std::array<int, TECH_TYPE_SIZE> need{}, fromRuins{}, converted{};
        auto techsOf = [](UnitType type, UnitType passenger) {
            TechSet set;
            set.set(unitInfo(type).tech);
            set.set(unitInfo(passenger).tech);
            return set;
        };
        for (const Unit &u : g.units) {
            if (!u.alive || u.owner != q) {
                continue;
            }
            bool ruinSoldier =
                u.veteran &&
                ((u.type == Rules::RUIN_LAND_UNIT &&
                  u.passenger == Rules::RUIN_LAND_UNIT) ||
                 (u.type == Rules::RUIN_SEA_BOAT &&
                  u.passenger == Rules::RUIN_SEA_PASSENGER));
            TechSet set = techsOf(u.type, u.passenger);
            for (int k = 0; k < TECH_TYPE_SIZE; k++) {
                need[k] += set.test(k);
                fromRuins[k] += set.test(k) && ruinSoldier;
            }
        }
        for (auto [type, passenger] : view.convertedAway) {
            TechSet set = techsOf(type, passenger);
            for (int k = 0; k < TECH_TYPE_SIZE; k++) {
                converted[k] += set.test(k);
            }
        }
        for (int k = 0; k < TECH_TYPE_SIZE; k++) {
            if (need[k] > std::min(fromRuins[k], std::max(0, ruins)) + converted[k]) {
                withAncestors(static_cast<TechType>(k));
            }
        }
        // a road they built, on a map that started without roads
        for (int t = 0; t < tiles; t++) {
            if (known(t) && g.tiles[t].roadBuilder == q) {
                bool water = TerrainTypes::isWater(g.tiles[t].terrain);
                withAncestors(improvementData[water ? ImprovementTypes::BRIDGE
                                                    : ImprovementTypes::ROAD]
                                  .tech);
            }
        }
    }
    // A building needs its builder's own territory, so one in their city
    // proves its tech, unless the city could have been the player's (they've
    // stood on it)
    auto theirOwn = [&](int city) {
        return city >= 0 && g.cities[city].owner == q &&
               view.firstOccupied[g.cities[city].tile] < 0;
    };
    for (int t = 0; t < tiles; t++) {
        const Tile &tile = g.tiles[t];
        if (twoPlayers && known(t) && theirOwn(tile.territory) &&
            tile.building > ImprovementTypes::LIGHTHOUSE) {
            withAncestors(improvementData[tile.building].tech);
            // a monument proves its task, and some tasks need techs
            if (isMonument(tile.building)) {
                switch (TaskTypes::taskFor(tile.building)) {
                case TaskTypes::GENIUS:
                    for (int k = 0; k < TECH_TYPE_SIZE; k++) {
                        withAncestors(static_cast<TechType>(k));
                    }
                    break;
                case TaskTypes::WEALTH:
                    withAncestors(Rules::WEALTH_TECH);
                    break;
                case TaskTypes::PACIFIST:
                    withAncestors(Rules::PACIFIST_TECH);
                    break;
                default:
                    break;
                }
            }
        }
    }
    // A warning next to one of the player's units proves a hidden unit,
    // which proves the tech for one, if only one tech gives them
    bool warned = twoPlayers &&
                  std::any_of(view.units.begin(), view.units.end(),
                              [&](const PlayerView::UnitView &u) {
                                  return u.owner == player && u.hiddenEnemyNear;
                              });
    if (warned) {
        TechSet hiders;
        for (const UnitData &data : unitData) {
            if (data.trainable && data.abilities.test(Abilities::HIDE)) {
                hiders.set(data.tech);
            }
        }
        if (hiders.count() == 1) {
            for (int t = 0; t < TECH_TYPE_SIZE; t++) {
                if (hiders.test(t)) {
                    withAncestors(static_cast<TechType>(t));
                }
            }
        }
    }
    // parents before children: a tech is only sampled once it's researchable
    for (int t = 1; t < TECH_TYPE_SIZE; t++) {
        auto tech = static_cast<TechType>(t);
        if (!techs.test(t) && researchableWith(techs, tech) &&
            std::bernoulli_distribution(std::clamp(b.tech[t], 0.0f, 1.0f))(rng)) {
            techs.set(t);
        }
    }
    them.techs = techs;

    // ---- hidden units ----
    // The units they could have on a tile: a trainable type their techs
    // allow, on terrain it can enter, or at sea in the boat it would be in.
    // Each comes with a weight from the beliefs, split between the
    // passengers a boat could carry.
    struct Option {
        UnitType type, passenger;
        float weight;
    };
    auto options = [&](int at, bool hideOnly) {
        std::vector<Option> found;
        std::array<int, UNIT_TYPE_SIZE> carriers{};
        auto add = [&](UnitType type, UnitType passenger) {
            const UnitData &data = unitInfo(type);
            Unit probe{.type = type, .passenger = passenger, .owner = q,
                       .tile = at, .hp = 1, .maxHp = 1, .scoreOwner = q};
            if ((!hideOnly || data.abilities.test(Abilities::HIDE)) &&
                techs.test(data.tech) && g.canEnterTerrain(probe, at)) {
                found.push_back({type, passenger, 0});
                carriers[type]++;
            }
        };
        for (const UnitData &land : unitData) {
            if (!land.trainable || !techs.test(land.tech) ||
                land.abilities.test(Abilities::WATER)) {
                continue;
            }
            add(land.type, land.type);
            UnitType boat = boatFor(land.type);
            add(boat, land.type);
            if (boat == RAFT) {
                for (const UnitData &upgrade : unitData) {
                    if (isRaftFamily(upgrade.type) && upgrade.type != RAFT) {
                        add(upgrade.type, land.type);
                    }
                }
            }
        }
        for (Option &o : found) {
            o.weight = b.unitType.at(at)[o.type] / carriers[o.type];
        }
        return found;
    };
    auto place = [&](int at, const Option &o) {
        const UnitData &passenger = unitInfo(o.passenger);
        g.units.push_back({.type = o.type,
                           .passenger = o.passenger,
                           .owner = q,
                           .tile = at,
                           .hp = passenger.maxHp,
                           .maxHp = passenger.maxHp,
                           .home = them.capital,
                           .scoreOwner = q,
                           .hidden = unitInfo(o.type).abilities.test(Abilities::HIDE)});
        g.tiles[at].unit = static_cast<int>(g.units.size()) - 1;
    };
    auto pickOption = [&](const std::vector<Option> &found) {
        std::vector<float> weights;
        for (const Option &o : found) {
            weights.push_back(o.weight);
        }
        int i = pick(weights, rng);
        if (i < 0 && !found.empty()) {
            i = static_cast<int>(rng() % found.size());
        }
        return i;
    };

    // First the hidden units on explored tiles, which the player only knows
    // of from warnings: one next to every warned unit of theirs, and none
    // next to an unwarned one
    std::vector<char> calm(tiles, 0), covered(tiles, 0);
    std::vector<int> warnings;
    for (const PlayerView::UnitView &u : view.units) {
        if (u.owner != player) {
            continue;
        }
        if (u.hiddenEnemyNear) {
            warnings.push_back(u.tile);
        } else {
            g.forEachNeighbour(u.tile, [&](int n) { calm[n] = 1; });
        }
    }
    std::shuffle(warnings.begin(), warnings.end(), rng);
    int placed = 0;
    for (int w : warnings) {
        if (covered[w]) {
            continue;
        }
        std::vector<float> where(tiles, 0);
        std::vector<std::vector<Option>> found(tiles);
        g.forEachNeighbour(w, [&](int n) {
            int city = g.cityAt(n);
            if (!known(n) || g.tiles[n].unit >= 0 || calm[n] ||
                (city >= 0 && g.cities[city].owner == player)) {
                return;
            }
            found[n] = options(n, true);
            if (!found[n].empty()) {
                where[n] = std::max(b.unitHere.at(n), 0.01f);
            }
        });
        int at = pick(where, rng);
        if (at < 0) {
            return std::nullopt;
        }
        place(at, found[at][pickOption(found[at])]);
        placed++;
        g.forEachNeighbour(at, [&](int n) { covered[n] = 1; });
    }

    // then the rest, in the fog
    int count = std::max(0, pick(b.hiddenUnitCount, rng) - placed);
    std::vector<float> where(tiles, 0);
    for (int t = 0; t < tiles; t++) {
        if (!known(t) && g.tiles[t].unit < 0) {
            where[t] = b.unitHere.at(t);
        }
    }
    for (int n = 0; n < count; n++) {
        int at = pick(where, rng);
        if (at < 0) {
            break;
        }
        where[at] = 0;
        std::vector<Option> found = options(at, false);
        int i = pickOption(found);
        if (i >= 0) {
            place(at, found[i]);
        }
    }
    // what the player can't know about the units they do see
    for (Unit &u : g.units) {
        if (u.alive && u.owner == q) {
            u.kills = 0;
            u.lastDx = u.lastDy = 0;
            u.freeOfCap = false;
            u.canMove = u.canAttack = u.canAct = false;
            if (u.home < 0 || g.cities[u.home].owner != q) {
                u.home = them.capital;
            }
        }
    }

    // ---- their private state ----
    them.stars = detail == Detail::EVERYTHING ? std::max(0, pick(b.stars, rng)) : 0;
    them.kills = 0;
    them.peacefulTurns = 0;
    them.attackedThisTurn = false;
    them.tasksDone.reset();
    them.monumentsBuilt.reset();
    // monuments they built: in their own cities, not ones they captured
    for (int t = 0; t < tiles; t++) {
        const Tile &tile = g.tiles[t];
        if (twoPlayers && isMonument(tile.building) && theirOwn(tile.territory)) {
            them.monumentsBuilt.set(TaskTypes::taskFor(tile.building));
            them.tasksDone.set(TaskTypes::taskFor(tile.building));
        }
    }

    // ---- which tiles they've explored ----
    const std::uint32_t bit = std::uint32_t{1} << q;
    for (Tile &tile : g.tiles) {
        tile.explored &= ~bit;
    }
    auto explore = [&](int centre, int radius) {
        g.tiles[centre].explored |= bit;
        g.forEachInRadius(centre, radius,
                          [&](int t) { g.tiles[t].explored |= bit; });
    };
    if (them.capital >= 0) {
        explore(g.cities[them.capital].tile, Rules::START_EXPLORED_RADIUS);
    }
    for (int t = 0; t < tiles; t++) {
        int c = g.tiles[t].territory;
        if (c >= 0 && g.cities[c].owner == q) {
            g.tiles[t].explored |= bit;
        }
    }
    for (const Unit &u : g.units) {
        if (u.alive && u.owner == q) {
            explore(u.tile, g.sightRange(u));
        }
    }
    if (detail == Detail::EVERYTHING) {
        // Their published score, with what the player did to them since, has
        // to come out exactly; exploration is the slack (section 5.5)
        int target = g.publishedScore(q) + view.effectsSincePublished[q];
        int missing = target - g.score(q);
        // More score than exploring every tile left could give: techs are the
        // biggest hidden part of a score, so add researchable ones (weighted
        // by the beliefs) until exploration can cover the rest
        auto unexplored = [&] {
            int n = 0;
            for (int t = 0; t < tiles; t++) {
                n += !(g.tiles[t].explored & bit);
            }
            return n;
        };
        while (missing > Rules::EXPLORED_SCORE * unexplored()) {
            std::vector<float> weights(TECH_TYPE_SIZE, 0);
            for (int t = 1; t < TECH_TYPE_SIZE; t++) {
                auto tech = static_cast<TechType>(t);
                if (researchableWith(them.techs, tech) &&
                    Rules::techScore(techData[t].tier) <= missing) {
                    weights[t] = std::max(b.tech[t], 0.01f);
                }
            }
            int t = pick(weights, rng);
            if (t < 0) {
                return std::nullopt;
            }
            them.techs.set(t);
            missing -= Rules::techScore(techData[t].tier);
        }
        if (missing < 0 || missing % Rules::EXPLORED_SCORE != 0) {
            return std::nullopt;
        }
        int more = missing / Rules::EXPLORED_SCORE;
        for (; more > 0; more--) {
            // a random unexplored tile next to what they've explored
            std::vector<float> edge(tiles, 0);
            for (int t = 0; t < tiles; t++) {
                if (!(g.tiles[t].explored & bit)) {
                    continue;
                }
                g.forEachNeighbour(t, [&](int n) {
                    if (!(g.tiles[n].explored & bit)) {
                        edge[n] = 1;
                    }
                });
            }
            int t = pick(edge, rng);
            if (t < 0) {
                return std::nullopt;
            }
            g.tiles[t].explored |= bit;
        }
    }
    them.lighthousesFound = 0;
    for (int t = 0; t < tiles; t++) {
        them.lighthousesFound += g.tiles[t].building == ImprovementTypes::LIGHTHOUSE &&
                                 (g.tiles[t].explored & bit);
    }

    // whether they've met the player: seen one of their cities or units, or
    // been sent the player's embassy or peace offer, which the player knows
    const std::uint32_t playerBit = std::uint32_t{1} << player;
    bool met = g.players[player].hasEmbassyWith(q) || (them.peaceRequests & playerBit) ||
               (them.peace & playerBit);
    for (int t = 0; t < tiles; t++) {
        if (!(g.tiles[t].explored & bit)) {
            continue;
        }
        const Unit *u = g.unitAt(t);
        met |= u && u->owner == player && !u->hidden;
        int c = g.cities.empty() ? -1 : g.cityAt(t);
        met |= c >= 0 && g.cities[c].owner == player;
    }
    them.met = (them.met & ~playerBit) | (met ? playerBit : 0);

    // where their units and cities first stood is theirs to know
    for (int t = 0; t < tiles; t++) {
        g.firstOccupied[q * tiles + t] = -1;
    }
    for (const City &c : g.cities) {
        if (c.owner == q) {
            g.firstOccupied[q * tiles + c.tile] = 0;
        }
    }
    for (const Unit &u : g.units) {
        if (u.alive && u.owner == q) {
            g.firstOccupied[q * tiles + u.tile] = g.turn;
        }
    }

    canonicalize(g);
    return g;
}

} // namespace

std::optional<Game> determinize(const Game &real, int player,
                                const Beliefs &beliefs, std::uint64_t seed,
                                Detail detail, int maxTries,
                                DeterminizeStats *stats) {
    if (real.players.size() != 2) {
        throw std::invalid_argument("determinize: two-player games only, for now");
    }
    const PlayerView view = makeView(real, player);
    std::mt19937_64 seeds(seed);
    for (int attempt = 0; attempt < maxTries; attempt++) {
        if (stats) {
            stats->tries++;
        }
        std::optional<Game> g =
            sample(real, player, view, beliefs, seeds(), detail);
        if (g) {
            return g;
        }
        if (stats) {
            stats->rejected++;
        }
    }
    return std::nullopt;
}
