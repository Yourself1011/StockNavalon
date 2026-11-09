#include "game.h"

bool Game::canSee(int player, const Unit &unit) const {
    return unit.owner == player ||
           (explored(player, unit.tile) && !unit.hidden);
}

int Game::sightRange(const Unit &unit) const {
    return unit.has(Abilities::SCOUT) ||
                   tiles[unit.tile].terrain == TerrainTypes::MOUNTAIN
               ? Rules::FAR_SIGHT
               : Rules::UNIT_SIGHT;
}

bool Game::resourceVisible(int player,
                           ResourceTypes::ResourceType resource) const {
    TechTypes::TechType tech;
    switch (resource) {
    case ResourceTypes::FRUIT:
        tech = TechTypes::ORGANIZATION;
        break;
    case ResourceTypes::CROP:
        tech = TechTypes::FARMING;
        break;
    case ResourceTypes::GAME:
        tech = TechTypes::HUNTING;
        break;
    case ResourceTypes::METAL:
        tech = TechTypes::MINING;
        break;
    case ResourceTypes::FISH:
        tech = TechTypes::FISHING;
        break;
    case ResourceTypes::STARFISH:
        tech = TechTypes::NAVIGATION;
        break;
    default:
        return true;
    }
    return players[player].hasTech(tech) || canResearch(player, tech);
}

bool Game::hiddenEnemyNear(int player, int tile) const {
    bool found = false;
    forEachNeighbour(tile, [&](int n) {
        const Unit *other = unitAt(n);
        found |= other && hostile(player, other->owner) && other->hidden;
    });
    return found;
}

void Game::reveal(int player, int tile) {
    Tile &t = tiles[tile];
    if (t.exploredBy(player)) {
        return;
    }
    t.explored |= std::uint32_t{1} << player;

    if (t.building == ImprovementTypes::LIGHTHOUSE) {
        Player &p = players[player];
        p.lighthousesFound++;
        if (p.capital >= 0 && cities[p.capital].owner == player) {
            addPopulation(p.capital, Rules::LIGHTHOUSE_POPULATION);
        }
    }
}

void Game::revealAround(int player, int tile, int radius) {
    reveal(player, tile);
    forEachInRadius(tile, radius, [&](int t) { reveal(player, t); });
}

void Game::revealForUnit(int unit) {
    const Unit &u = units[unit];
    revealAround(u.owner, u.tile, sightRange(u));
}

void Game::revealTerritory(int city) {
    int owner = cities[city].owner;
    reveal(owner, cities[city].tile);
    forEachInRadius(cities[city].tile, Rules::MAX_BORDER_RADIUS, [&](int t) {
        if (tiles[t].territory == city) {
            reveal(owner, t);
        }
    });
}

void Game::runExplorer(int player, int start) {
    const Player &p = players[player];
    // the same terrain units can move on: Climbing for mountains, Fishing for
    // shallow water, Sailing for ocean
    auto canEnter = [&](int t) {
        if (tiles[t].road) {
            return true;
        }
        for (const TechData &tech : techData) {
            if (p.hasTech(tech.type) &&
                tech.movementUnlocks.test(tiles[t].terrain)) {
                return true;
            }
        }
        return false;
    };

    constexpr int UNSCORED = 10000;
    // steps it already took are a little worse, so it avoids backtracking
    constexpr int BACKTRACK_PENALTY = 1;
    std::vector<char> visited(tiles.size(), 0);
    visited[start] = 1;
    int at = start;

    for (int step = 0; step < Rules::EXPLORER_STEPS; step++) {
        // Lower is better. A tile scores by the fog standing on it would clear
        // (sight 1: climbing a mountain doesn't count), capped at 4 tiles,
        // and a lighthouse in that fog is worth a little over 3 tiles.
        std::vector<int> score(tiles.size(), UNSCORED);
        std::vector<int> frontier;
        for (int t = 0; t < size * size; t++) {
            if (!canEnter(t)) {
                continue;
            }
            int fog = 0;
            bool lighthouse = false;
            auto count = [&](int n) {
                if (!explored(player, n)) {
                    fog++;
                    lighthouse |=
                        tiles[n].building == ImprovementTypes::LIGHTHOUSE;
                }
            };
            count(t);
            forEachNeighbour(t, count);
            if (fog > 0) {
                score[t] = (lighthouse ? 150 : 183) - 10 * std::min(fog, 4);
                frontier.push_back(t);
            }
        }
        // spread 3 steps outward, +100 a step, so the closest fog wins first
        for (int iteration = 0; iteration < 3; iteration++) {
            std::vector<int> next;
            for (int t : frontier) {
                forEachNeighbour(t, [&](int n) {
                    if (canEnter(n) && score[t] + 100 < score[n]) {
                        score[n] = score[t] + 100;
                        next.push_back(n);
                    }
                });
            }
            frontier = std::move(next);
        }

        // step to the best neighbour; any unit may be stepped over
        std::vector<int> best;
        int bestScore = 0;
        forEachNeighbour(at, [&](int n) {
            if (!canEnter(n)) {
                return;
            }
            int s = score[n] + (visited[n] ? BACKTRACK_PENALTY : 0);
            if (best.empty() || s < bestScore) {
                best.clear();
                bestScore = s;
            }
            if (s == bestScore) {
                best.push_back(n);
            }
        });
        if (best.empty()) {
            // nowhere to go: it disappears
            return;
        }
        at = best[randomInt(static_cast<int>(best.size()))];
        visited[at] = 1;
        revealAround(player, at,
                     tiles[at].terrain == TerrainTypes::MOUNTAIN ? 2 : 1);
    }
}
