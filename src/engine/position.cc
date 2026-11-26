#include "game.h"

#include <algorithm>
#include <numeric>

namespace {
// A running 64-bit hash: each value mixed in with splitmix64's finalizer
struct Hasher {
    std::uint64_t h = 0x9e3779b97f4a7c15ULL;
    void add(std::int64_t value) {
        std::uint64_t z = h ^ (static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        h = z ^ (z >> 31);
    }
    template <typename T> void addAll(const std::vector<T> &values) {
        add(static_cast<std::int64_t>(values.size()));
        for (const T &v : values) {
            add(static_cast<std::int64_t>(v));
        }
    }
};
} // namespace

void Game::canonicalize() {
    std::vector<Unit> alive;
    for (const Unit &u : units) {
        if (u.alive) {
            alive.push_back(u);
        }
    }
    std::sort(alive.begin(), alive.end(),
              [](const Unit &a, const Unit &b) { return a.tile < b.tile; });

    std::vector<int> order(cities.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return cities[a].tile < cities[b].tile; });
    std::vector<int> newIndex(cities.size());
    std::vector<City> sorted;
    for (int old : order) {
        newIndex[old] = static_cast<int>(sorted.size());
        sorted.push_back(cities[old]);
    }
    auto renumber = [&](int city) { return city < 0 ? -1 : newIndex[city]; };
    for (Tile &tile : tiles) {
        tile.territory = renumber(tile.territory);
        tile.unit = -1;
    }
    for (Unit &u : alive) {
        u.home = renumber(u.home);
        u.passengerHome = renumber(u.passengerHome);
    }
    for (Player &p : players) {
        p.capital = renumber(p.capital);
    }
    for (auto &[city, step] : populationQueue) {
        city = renumber(city);
    }
    units = std::move(alive);
    cities = std::move(sorted);
    for (std::size_t i = 0; i < units.size(); i++) {
        tiles[units[i].tile].unit = static_cast<int>(i);
    }
}

std::uint64_t Game::positionHash() const {
    Hasher h;
    // a city by its tile, never its index: indices follow the order things
    // were made in
    auto cityTile = [&](int city) { return city < 0 ? -1 : cities[city].tile; };

    h.add(turn);
    h.add(currentPlayer);
    h.add(winner);
    h.add(static_cast<std::int64_t>(rng));
    h.add(connectionsDue);

    int alive = 0;
    for (std::size_t t = 0; t < tiles.size(); t++) {
        const Tile &tile = tiles[t];
        h.add(tile.terrain);
        h.add(tile.resource);
        h.add(tile.building);
        h.add(tile.level);
        h.add(tile.builtTurn);
        h.add(tile.road);
        h.add(tile.roadBuilder);
        h.add(cityTile(tile.territory));
        h.add(tile.climate);
        h.add(tile.explored);
        if (tile.unit < 0) {
            h.add(-1);
            continue;
        }
        // the unit standing here, its home city by tile
        const Unit &u = units[tile.unit];
        alive++;
        h.add(u.type);
        h.add(u.passenger);
        h.add(u.owner);
        h.add(u.hp);
        h.add(u.maxHp);
        h.add(cityTile(u.home));
        h.add(cityTile(u.passengerHome));
        h.add(u.freeOfCap);
        h.add(u.scoreOwner);
        h.add(u.kills);
        h.add(u.veteran);
        h.add(u.hidden);
        h.add(u.canMove);
        h.add(u.canAttack);
        h.add(u.canAct);
        h.add(u.attacked);
        h.add(u.lastDx);
        h.add(u.lastDy);
        h.add(u.facing);
    }
    // every living unit stands on a tile
    h.add(alive);

    std::vector<int> byTile(cities.size());
    std::iota(byTile.begin(), byTile.end(), 0);
    std::sort(byTile.begin(), byTile.end(),
              [&](int a, int b) { return cities[a].tile < cities[b].tile; });
    h.add(static_cast<std::int64_t>(cities.size()));
    for (int i : byTile) {
        const City &c = cities[i];
        h.add(c.tile);
        h.add(c.owner);
        h.add(c.level);
        h.add(c.population);
        h.add(c.capitalOf);
        h.add(c.connectedFor);
        h.add(c.pendingRewards);
        h.add(c.parks);
        h.add(c.workshop);
        h.add(c.wall);
        h.add(c.borderGrown);
        h.add(c.robbed);
    }

    for (const Player &p : players) {
        h.add(p.tribe);
        h.add(p.stars);
        h.add(static_cast<std::int64_t>(p.techs.to_ullong()));
        h.add(cityTile(p.capital));
        h.add(p.alive);
        h.add(p.lighthousesFound);
        h.add(p.met);
        h.add(static_cast<std::int64_t>(p.tasksDone.to_ullong()));
        h.add(static_cast<std::int64_t>(p.monumentsBuilt.to_ullong()));
        h.add(p.kills);
        h.add(p.peacefulTurns);
        h.add(p.attackedThisTurn);
        h.addAll(p.attackedOnTurns);
        h.addAll(p.lastAttackTurn);
        h.add(static_cast<std::int64_t>(p.convertedAway.size()));
        for (auto [type, passenger] : p.convertedAway) {
            h.add(type);
            h.add(passenger);
        }
        h.add(p.peace);
        h.add(p.peaceRequests);
        h.add(p.embassies);
        h.add(p.incomeDue);
        h.add(p.coolDown);
    }

    // what the players have seen of each other's scores, and where they've
    // been: read by their views
    h.add(static_cast<std::int64_t>(scoreHistory.size()));
    for (const ScoreRecord &s : scoreHistory) {
        h.add(s.player);
        h.add(s.turn);
        h.add(s.score);
    }
    h.addAll(scoreEffects);
    h.addAll(scoreEffectHistory);
    h.addAll(firstOccupied);
    h.addAll(waterRoute);

    // population a level-up holds back, in order, its city by tile
    h.add(static_cast<std::int64_t>(populationQueue.size()));
    for (auto [city, step] : populationQueue) {
        h.add(cityTile(city));
        h.add(step);
    }

    // outcomes fixed from outside (mirrored games)
    h.add(forced.ruinReward);
    h.add(forced.ruinTech);
    h.add(forced.explored.has_value());
    if (forced.explored) {
        h.addAll(*forced.explored);
    }
    h.add(forced.convertedHome);
    h.add(forced.daggers.has_value());
    if (forced.daggers) {
        h.add(static_cast<std::int64_t>(forced.daggers->size()));
        for (auto [tile, facing] : *forced.daggers) {
            h.add(tile);
            h.add(facing);
        }
    }
    // Left out: the map's size and type, the settings and whether it
    // started with roads, which never change during a game.
    return h.h;
}
