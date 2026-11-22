#include "game.h"
#include "moveSearch.h"
#include <cmath>
#include <cstdlib>
#include <numbers>

using namespace CityRewards;

std::array<CityReward, 2> CityRewards::rewardsForLevel(int level) {
    switch (level) {
    case 2:
        return {WORKSHOP, EXPLORER};
    case 3:
        return {CITYWALL, RESOURCES};
    case 4:
        return {POPULATIONGROWTH, BORDERGROWTH};
    default:
        return {PARK, SUPERUNIT};
    }
}

void Game::addPopulation(int city, int amount) {
    if (settings.follows(RuleVersions::POPULATION_STACK)) {
        for (int i = 0; i < std::abs(amount); i++) {
            populationQueue.emplace_back(city, amount > 0 ? 1 : -1);
        }
        runPopulation();
        return;
    }
    City &c = cities[city];
    c.population += amount;
    // level n -> n + 1 needs n + 1 population; leftovers carry over
    while (c.population >= Rules::populationToLevelUp(c.level)) {
        c.population -= Rules::populationToLevelUp(c.level);
        c.level++;
        c.pendingRewards++;
    }
}

void Game::runPopulation() {
    std::size_t done = 0;
    while (done < populationQueue.size() && pendingRewardCity() < 0) {
        auto [city, step] = populationQueue[done++];
        City &c = cities[city];
        c.population += step;
        if (c.population >= Rules::populationToLevelUp(c.level)) {
            c.population -= Rules::populationToLevelUp(c.level);
            c.level++;
            c.pendingRewards++;
        }
    }
    populationQueue.erase(populationQueue.begin(), populationQueue.begin() + done);
}

int Game::healAmount(const Unit &unit) const {
    int owner = tileOwner(unit.tile);
    // an ally's borders count as the unit's own (ALLIED_HEALING)
    bool home = owner == unit.owner ||
                (owner >= 0 && atPeace(owner, unit.owner) &&
                 settings.follows(RuleVersions::ALLIED_HEALING));
    return home ? Rules::HEAL_IN_TERRITORY : Rules::HEAL_ELSEWHERE;
}

void Game::claimBorders(int city, int radius) {
    int centre = cities[city].tile;
    tiles[centre].territory = city;
    forEachInRadius(centre, radius, [&](int t) {
        if (tiles[t].territory < 0) {
            tiles[t].territory = city;
        }
    });
    // everything inside your borders is always revealed
    revealTerritory(city);
}

int Game::foundCity(int tile, int owner) {
    int city = static_cast<int>(cities.size());
    cities.push_back({.tile = tile, .owner = owner});
    tiles[tile].building = ImprovementTypes::CITY;
    tiles[tile].resource = ResourceTypes::NONE;
    markOccupied(owner, tile);
    claimBorders(city, Rules::CITY_BORDER_RADIUS);
    return city;
}

// ---- connections ----

std::vector<char> Game::sharedRoadsBlocked(int player) const {
    std::vector<char> blocked(tiles.size(), 0), seen(tiles.size(), 0);
    auto neutralRoad = [&](int t) {
        return tiles[t].road && tiles[t].territory < 0;
    };
    for (int start = 0; start < size * size; start++) {
        if (seen[start] || !neutralRoad(start)) {
            continue;
        }
        // one network of neutral road tiles, and the players it touches
        std::vector<int> component{start};
        seen[start] = 1;
        std::uint32_t touching = 0;
        for (std::size_t i = 0; i < component.size(); i++) {
            forEachNeighbour(component[i], [&](int n) {
                int owner = tileOwner(n);
                if (owner >= 0) {
                    touching |= std::uint32_t{1} << owner;
                }
                if (!seen[n] && neutralRoad(n)) {
                    seen[n] = 1;
                    component.push_back(n);
                }
            });
        }
        bool shared = (touching & (touching - 1)) != 0;
        bool first = (touching & -touching) == (std::uint32_t{1} << player);
        if (shared && !first) {
            for (int t : component) {
                blocked[t] = 1;
            }
        }
    }
    return blocked;
}

std::vector<bool> Game::connectedCities(int player, bool newOnly) const {
    std::vector<bool> connected(cities.size(), false);
    int capital = players[player].capital;
    if (capital < 0 || cities[capital].owner != player) {
        return connected;
    }

    auto ownPort = [&](int t) {
        return tiles[t].building == ImprovementTypes::PORT &&
               tileOwner(t) == player;
    };
    auto ownCity = [&](int t) {
        int city = cityAt(t);
        return city >= 0 && cities[city].owner == player;
    };
    const bool game = settings.follows(RuleVersions::GAME_CONNECTIONS);
    std::vector<char> blocked;
    if (newOnly && !game) {
        blocked = sharedRoadsBlocked(player);
    }
    // the game's: a step between two tiles that are both roads, bridges,
    // cities, villages or ports of anyone's, unless it goes from one
    // player's land into another's that they aren't at peace with
    auto link = [&](int t) {
        ImprovementTypes::ImprovementType b = tiles[t].building;
        return tiles[t].road || b == ImprovementTypes::CITY ||
               b == ImprovementTypes::VILLAGE || b == ImprovementTypes::PORT;
    };
    // or both on a water route
    auto step = [&](int from, int to) {
        int a = tileOwner(from), b = tileOwner(to);
        return (b < 0 || a < 0 || a == b || atPeace(a, b)) &&
               ((link(from) && link(to)) ||
                (!waterRoute.empty() && waterRoute[from] && waterRoute[to]));
    };
    // part of the network: own cities and ports, and roads and bridges
    // outside enemy borders
    auto onNetwork = [&](int t) {
        if (ownCity(t) || ownPort(t)) {
            return true;
        }
        int owner = tileOwner(t);
        return tiles[t].road && explored(player, t) &&
               (owner < 0 || !hostile(owner, player)) &&
               !(newOnly && blocked[t]);
    };
    // no connection forms through tiles the player hasn't explored
    auto waterRoute = [&](int t) {
        int owner = tileOwner(t);
        if (!explored(player, t) || (owner >= 0 && hostile(owner, player))) {
            return false;
        }
        return tiles[t].terrain == TerrainTypes::WATER ||
               (tiles[t].terrain == TerrainTypes::OCEAN &&
                players[player].hasTech(TechTypes::NAVIGATION));
    };

    std::vector<char> seen(tiles.size(), 0);
    std::vector<int> stack{cities[capital].tile};
    seen[cities[capital].tile] = 1;
    while (!stack.empty()) {
        int from = stack.back();
        stack.pop_back();
        int city = cityAt(from);
        if (city >= 0) {
            connected[city] = true;
        }

        forEachNeighbour(from, [&](int n) {
            if (!seen[n] && (game ? step(from, n) : onNetwork(n))) {
                seen[n] = 1;
                stack.push_back(n);
            }
        });

        // the game's water links are the routes above
        if (game || !ownPort(from)) {
            continue;
        }
        // a port reaches ports and cities a few tiles away over water
        std::vector<int> depth(tiles.size(), -1), frontier{from};
        depth[from] = 0;
        for (std::size_t i = 0; i < frontier.size(); i++) {
            int at = frontier[i];
            if (depth[at] == Rules::PORT_CONNECTION_RANGE) {
                continue;
            }
            forEachNeighbour(at, [&](int n) {
                if (depth[n] >= 0) {
                    return;
                }
                depth[n] = depth[at] + 1;
                if (ownPort(n) || ownCity(n)) {
                    if (!seen[n]) {
                        seen[n] = 1;
                        stack.push_back(n);
                    }
                } else if (waterRoute(n)) {
                    frontier.push_back(n);
                }
            });
        }
    }
    return connected;
}

void Game::updateConnections(int player) {
    // connections that already exist may keep using shared neutral roads;
    // new ones may not
    std::vector<bool> kept = connectedCities(player);
    std::vector<bool> fresh = connectedCities(player, true);
    int capital = players[player].capital;
    // the game goes over the cities by tile, and its stack runs the last
    // first; each connection gives the city its point before the capital
    std::vector<int> order(cities.size());
    for (std::size_t i = 0; i < cities.size(); i++) {
        order[i] = static_cast<int>(i);
    }
    if (settings.follows(RuleVersions::POPULATION_STACK)) {
        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return cities[a].tile > cities[b].tile; });
    }
    for (int idx : order) {
        City &city = cities[idx];
        std::size_t i = static_cast<std::size_t>(idx);
        bool current = city.connectedFor == player;
        bool reached = current ? kept[i] : fresh[i];
        bool target = city.owner == player && idx != capital && reached;
        if (target == current) {
            continue;
        }
        // the city and the capital each get a population, or lose it again.
        // A captured capital still loses it.
        int change = target ? Rules::CONNECTION_POPULATION
                            : -Rules::CONNECTION_POPULATION;
        city.connectedFor = target ? player : -1;
        addPopulation(idx, change);
        addPopulation(capital, change);
    }
}

void Game::updateWaterRoutes() {
    waterRoute.assign(tiles.size(), 0);
    // each player's routers (ports, and bridges after BRIDGE_ROUTES), in
    // tile order, every pair within the first one's range, joined by the
    // game's path over the water both open and the player's techs allow,
    // through explored tiles that are unclaimed, the player's or a peace
    // partner's (city tiles too). Each path is marked, and makes later ones
    // cheaper: 5 a step onto a road, city or route, else 10 (game).
    constexpr int STEP = 10, ROAD_STEP = 5;
    bool bridges = settings.follows(RuleVersions::BRIDGE_ROUTES);
    for (std::size_t p = 0; p < players.size(); p++) {
        int player = static_cast<int>(p);
        const Player &me = players[p];
        bool ocean = me.hasTech(TechTypes::SAILING);
        bool water = ocean || me.hasTech(TechTypes::FISHING);
        if (!water) {
            continue;
        }
        std::vector<int> routers;
        for (int t = 0; t < size * size; t++) {
            if (tileOwner(t) == player &&
                (tiles[t].building == ImprovementTypes::PORT || (bridges && isBridge(t)))) {
                routers.push_back(t);
            }
        }
        bool oceanStep = ocean;
        bool landings = settings.follows(RuleVersions::ROUTE_LANDINGS);
        auto cost = [&](int from, int to) {
            const Tile &t = tiles[to];
            int owner = tileOwner(to);
            bool city = t.building == ImprovementTypes::CITY || t.building == ImprovementTypes::VILLAGE;
            bool terrain = (t.terrain == TerrainTypes::WATER && water) ||
                           (t.terrain == TerrainTypes::OCEAN && oceanStep);
            // off land (a city) only where a unit would embark, or a bridge
            // (game: router settings embark)
            if (landings && terrain && !TerrainTypes::isWater(tiles[from].terrain) &&
                t.building != ImprovementTypes::PORT && !isBridge(to)) {
                return 0;
            }
            if (!explored(player, to) || !(owner < 0 || owner == player || atPeace(player, owner)) ||
                !(terrain || city)) {
                return 0;
            }
            return t.road || city || waterRoute[to] ? ROAD_STEP : STEP;
        };
        std::vector<int> parents;
        for (std::size_t i = 0; i < routers.size(); i++) {
            bool port = tiles[routers[i]].building == ImprovementTypes::PORT;
            int range = port ? Rules::PORT_CONNECTION_RANGE : Rules::BRIDGE_CONNECTION_RANGE;
            for (std::size_t j = i + 1; j < routers.size(); j++) {
                // only ports both open the ocean
                oceanStep = ocean && port && tiles[routers[j]].building == ImprovementTypes::PORT;
                if (distance(routers[i], routers[j]) > range ||
                    !gamePath(size, routers[i], routers[j], STEP * range, cost, parents)) {
                    continue;
                }
                for (int t = routers[j]; t >= 0; t = t == routers[i] ? -1 : parents[t]) {
                    waterRoute[t] = 1;
                }
            }
        }
    }
}

void Game::updateAllConnections(std::uint32_t which) {
    if (!settings.follows(RuleVersions::ACTING_CONNECTIONS)) {
        which = ALL_PLAYERS;
    }
    if (settings.follows(RuleVersions::GAME_CONNECTIONS)) {
        updateWaterRoutes();
    }
    for (std::size_t i = 0; i < players.size(); i++) {
        if (which >> i & 1) {
            updateConnections(static_cast<int>(i));
        }
    }
}

void Game::connectNow(std::uint32_t which) {
    if (settings.follows(RuleVersions::POPULATION_STACK)) {
        which |= connectionsDue;
        connectionsDue = 0;
        updateAllConnections(which);
    } else {
        connectionsChanged(which);
    }
}

void Game::connectionsChanged(std::uint32_t which) {
    if (settings.follows(RuleVersions::GAME_CONNECTIONS)) {
        connectionsDue |= which;
    } else {
        updateAllConnections();
    }
}

// ---- capture and training ----

void Game::capture(int unitIdx) {
    Unit &unit = units[unitIdx];
    int tile = unit.tile;
    int owner = unit.owner;
    unit.canMove = unit.canAttack = unit.canAct = false;
    // capturing reveals a hidden unit until it moves again
    unit.hidden = false;

    int city;
    int previous = -1;
    if (tiles[tile].building == ImprovementTypes::VILLAGE) {
        city = foundCity(tile, owner);
    } else {
        city = tiles[tile].territory;
        // the previous owner's score loses the city, its borders, and the
        // temples and monuments in them: all visible once it's captured
        previous = cities[city].owner;
        const City &c = cities[city];
        int loss = cityScore(c);
        forEachInRadius(tile, Rules::MAX_BORDER_RADIUS, [&](int t) {
            if (tiles[t].territory == city) {
                loss += tileScore(t);
            }
        });
        scoreEffects[owner * players.size() + previous] -= loss;

        cities[city].owner = owner;
        // a reward the old owner hadn't picked goes; one the capture itself
        // brings (its producers, below) is the new owner's
        cities[city].pendingRewards = 0;
        markOccupied(owner, cities[city].tile);
        if (settings.follows(RuleVersions::CAPTURE_REVEALS_AREA)) {
            int radius = cities[city].borderGrown ? Rules::GROWN_BORDER_RADIUS
                                                  : Rules::CITY_BORDER_RADIUS;
            forEachInRadius(tile, radius, [&](int t) { reveal(owner, t); });
        }
        revealTerritory(city);
        // producers count only neighbours of their own owner: those on the
        // city's land and next to it, the old owner's included
        if (settings.follows(RuleVersions::CAPTURE_REFRESHES_PRODUCERS)) {
            forEachInRadius(tile, Rules::MAX_BORDER_RADIUS, [&](int t) {
                if (tiles[t].territory == city) {
                    if (isProducer(tiles[t].building)) {
                        refreshProducer(t);
                    }
                    refreshProducersAround(t);
                }
            });
        }
        // embassies only stand in a capital its owner still holds
        int capitalOf = cities[city].capitalOf;
        if (capitalOf >= 0) {
            for (Player &p : players) {
                p.embassies &= ~(std::uint32_t{1} << capitalOf);
            }
        }
        // its units lose their home, and don't get it back if it's retaken
        for (Unit &u : units) {
            if (u.alive && u.home == city) {
                u.home = -1;
            }
        }
    }
    if (!unit.has(Abilities::INDEPENDENT)) {
        unit.home = city;
    }
    if (!settings.follows(RuleVersions::GAME_CONNECTIONS) ||
        settings.follows(RuleVersions::CAPTURE_CONNECTS)) {
        if (settings.follows(RuleVersions::CAPTURE_CONNECTS_BOTH)) {
            // the old owner's first (the game stacks its updates last), then
            // the capturer's at the command's end
            if (previous >= 0) {
                connectNow(std::uint32_t{1} << previous);
            }
            connectionsChanged();
        } else {
            connectionsChanged(ALL_PLAYERS);
        }
    }
    checkGameEnd();
}

void Game::train(int city, UnitTypes::UnitType type) {
    players[currentPlayer].stars -= unitInfo(type).cost;
    spawnUnit(currentPlayer, type, cities[city].tile, city);
}

// ---- level-up rewards ----

void Game::chooseReward(int city, CityReward reward) {
    City &c = cities[city];
    c.pendingRewards--;
    // the reward's own population comes before what the level-up held back
    std::vector<std::pair<int, int>> held;
    held.swap(populationQueue);
    rewardEffect(city, reward);
    populationQueue.insert(populationQueue.end(), held.begin(), held.end());
    runPopulation();
}

void Game::rewardEffect(int city, CityReward reward) {
    City &c = cities[city];
    switch (reward) {
    case WORKSHOP:
        c.workshop = true;
        break;
    case EXPLORER:
        runExplorer(c.owner, c.tile);
        break;
    case CITYWALL:
        c.wall = true;
        break;
    case RESOURCES:
        players[c.owner].stars += Rules::RESOURCES_STARS;
        break;
    case POPULATIONGROWTH:
        addPopulation(city, Rules::POPULATION_GROWTH);
        break;
    case BORDERGROWTH:
        c.borderGrown = true;
        claimBorders(city, Rules::GROWN_BORDER_RADIUS);
        if (settings.follows(RuleVersions::GAME_CONNECTIONS)) {
            connectNow(ALL_PLAYERS);
        }
        break;
    case PARK:
        c.parks++;
        break;
    case SUPERUNIT:
        spawnGiant(city);
        break;
    default:
        break;
    }
}

void Game::spawnGiant(int city) {
    int tile = cities[city].tile;
    if (tiles[tile].unit >= 0) {
        pushUnit(tiles[tile].unit, cities[city].owner);
    }
    spawnUnit(cities[city].owner, Rules::SUPER_UNIT, tile, city);
}

void Game::pushUnit(int unitIdx, int owner) {
    Unit &unit = units[unitIdx];
    int x = tileX(unit.tile), y = tileY(unit.tile);

    if (settings.follows(RuleVersions::GAME_PUSH_ORDER)) {
        // the way it faces, or away from the pusher for another player's
        // unit; then turning a little more each time, first one way, then
        // the other (game: TryPushUnit)
        int facing = unit.owner == owner ? unit.facing : (unit.facing + 4) % 8;
        // a land unit may go onto a port it could embark at, and becomes a
        // boat there
        auto sails = [&](int t) {
            return !unit.isNaval() && settings.follows(RuleVersions::PUSH_ONTO_WATER) &&
                   embarksAt(unit.owner, t);
        };
        for (int turn : {0, -1, 1, -2, 2, -3, 3, -4}) {
            const auto &[dx, dy] = Directions::OFFSETS[(facing + turn + 8) % 8];
            if (!inBounds(x + dx, y + dy)) {
                continue;
            }
            int t = index(x + dx, y + dy);
            if (tiles[t].unit >= 0) {
                continue;
            }
            if (canEnterTerrain(unit, t) || sails(t)) {
                placeUnit(unitIdx, t);
                Unit &pushed = units[unitIdx];
                if (!canEnterTerrain(pushed, t)) {
                    // it embarks, which ends its turn (PUSH_EMBARK_STOPS);
                    // a Giant stomps as a Juggernaut (PUSH_EMBARK_STOMPS)
                    if (settings.follows(RuleVersions::PUSH_EMBARK_STOMPS)) {
                        embark(unitIdx);
                    } else {
                        units[unitIdx].type = boatFor(units[unitIdx].passenger);
                    }
                    if (settings.follows(RuleVersions::PUSH_EMBARK_STOPS)) {
                        pushed.canMove = pushed.canAttack = pushed.canAct = false;
                        pushed.attacked = true;
                    }
                }
                return;
            }
        }
        killUnit(unitIdx);
        return;
    }

    // a unit that never moved goes toward the centre; a friendly one onward
    // in the direction it was moving; an enemy one backward
    int dx = unit.lastDx, dy = unit.lastDy;
    if (dx == 0 && dy == 0) {
        double centre = (size - 1) / 2.0;
        dx = (centre > x) - (centre < x);
        dy = (centre > y) - (centre < y);
        if (dx == 0 && dy == 0) {
            dy = -1;
        }
    } else if (unit.owner != owner) {
        dx = -dx;
        dy = -dy;
    }

    // y grows downward, so flip it to measure angles counter-clockwise; only
    // the neighbouring tiles, and with none free it's gone (older records:
    // any distance)
    double primary = std::atan2(-dy, dx);
    const int reach = settings.follows(RuleVersions::ADJACENT_PUSHES) ? 1 : size - 1;
    for (int radius = 1; radius <= reach; radius++) {
        std::vector<std::pair<double, int>> ring;
        forEachInRadius(unit.tile, radius, [&](int t) {
            if (distance(t, unit.tile) != radius) {
                return;
            }
            double angle = std::atan2(-(tileY(t) - y), tileX(t) - x) - primary;
            angle = std::remainder(angle, 2 * std::numbers::pi);
            // the nearest angle first; counter-clockwise before clockwise
            double key = std::abs(angle) * 2 + (angle < -1e-9 ? 1e-6 : 0);
            ring.emplace_back(key, t);
        });
        std::sort(ring.begin(), ring.end());
        for (auto [key, t] : ring) {
            if (tiles[t].unit < 0 && canEnterTerrain(unit, t)) {
                placeUnit(unitIdx, t);
                return;
            }
        }
    }
    killUnit(unitIdx);
}

// ---- end of game ----

int Game::disbandRefund(const Unit &unit) const {
    int cost = unitInfo(unit.passenger).cost;
    if (unit.isNaval()) {
        cost += unit.data().cost;
    }
    return Rules::disbandRefund(cost);
}

void Game::eliminate(int player) {
    players[player].alive = false;
    clearDiplomacy(player);
    for (std::size_t i = 0; i < units.size(); i++) {
        if (units[i].alive && units[i].owner == player) {
            if (settings.follows(RuleVersions::WIPE_PAYS_DISBAND)) {
                players[player].stars += disbandRefund(units[i]);
            }
            killUnit(static_cast<int>(i));
        }
    }
}

void Game::checkGameEnd() {
    for (std::size_t i = 0; i < players.size(); i++) {
        if (players[i].alive && cityCount(static_cast<int>(i)) == 0) {
            eliminate(static_cast<int>(i));
        }
    }

    int alive = 0, last = -1;
    for (std::size_t i = 0; i < players.size(); i++) {
        if (players[i].alive) {
            alive++;
            last = static_cast<int>(i);
        }
    }
    if (alive == 1) {
        winner = last;
        return;
    }

    if (settings.winCondition == WinConditions::MIGHT) {
        // holding as many capitals as there are players
        for (std::size_t i = 0; i < players.size(); i++) {
            auto held = std::count_if(
                cities.begin(), cities.end(), [&](const City &c) {
                    return c.capitalOf >= 0 && c.owner == static_cast<int>(i);
                });
            if (players[i].alive &&
                held == static_cast<long>(players.size())) {
                winner = static_cast<int>(i);
                return;
            }
        }
    }
}
