#include "game.h"
#include <cmath>
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
    City &c = cities[city];
    c.population += amount;
    // level n -> n + 1 needs n + 1 population; leftovers carry over
    while (c.population >= Rules::populationToLevelUp(c.level)) {
        c.population -= Rules::populationToLevelUp(c.level);
        c.level++;
        c.pendingRewards++;
    }
}

int Game::healAmount(const Unit &unit) const {
    return tileOwner(unit.tile) == unit.owner ? Rules::HEAL_IN_TERRITORY
                                              : Rules::HEAL_ELSEWHERE;
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
    std::vector<char> blocked;
    if (newOnly) {
        blocked = sharedRoadsBlocked(player);
    }
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
            if (!seen[n] && onNetwork(n)) {
                seen[n] = 1;
                stack.push_back(n);
            }
        });

        if (!ownPort(from)) {
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
    for (std::size_t i = 0; i < cities.size(); i++) {
        City &city = cities[i];
        int idx = static_cast<int>(i);
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

void Game::updateAllConnections() {
    for (std::size_t i = 0; i < players.size(); i++) {
        updateConnections(static_cast<int>(i));
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
    if (tiles[tile].building == ImprovementTypes::VILLAGE) {
        city = foundCity(tile, owner);
    } else {
        city = tiles[tile].territory;
        // the previous owner's score loses the city, its borders, and the
        // temples and monuments in them: all visible once it's captured
        int previous = cities[city].owner;
        const City &c = cities[city];
        int loss = Rules::cityScore(c.level, c.population, c.parks);
        forEachInRadius(tile, Rules::MAX_BORDER_RADIUS, [&](int t) {
            if (tiles[t].territory == city) {
                loss += tileScore(t);
            }
        });
        scoreEffects[owner * players.size() + previous] -= loss;

        cities[city].owner = owner;
        markOccupied(owner, cities[city].tile);
        revealTerritory(city);
        // embassies only stand in a capital its owner still holds
        int capitalOf = cities[city].capitalOf;
        if (capitalOf >= 0) {
            for (Player &p : players) {
                p.embassies &= ~(std::uint32_t{1} << capitalOf);
            }
        }
        cities[city].pendingRewards = 0;
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
    updateAllConnections();
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

    // y grows downward, so flip it to measure angles counter-clockwise
    double primary = std::atan2(-dy, dx);
    for (int radius = 1; radius < size; radius++) {
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

void Game::eliminate(int player) {
    players[player].alive = false;
    clearDiplomacy(player);
    for (std::size_t i = 0; i < units.size(); i++) {
        if (units[i].alive && units[i].owner == player) {
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
