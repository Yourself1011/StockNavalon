#include "game.h"

int Game::unitScore(const Unit &unit) const {
    // 5 a star of the carried unit's cost; boat upgrades add nothing. A Giant
    // costs 10, so it's worth 50.
    return 5 * unitInfo(unit.passenger).cost;
}

int Game::templeLevel(int tile) const {
    // level 1 when built, one more every 2 turns, up to 5
    return std::min(MAX_TEMPLE_LEVEL, 1 + (turn - tiles[tile].builtTurn) / 2);
}

int Game::score(int player) const {
    int total = 0;
    for (const Unit &unit : units) {
        if (unit.alive && unit.scoreOwner == player) {
            total += unitScore(unit);
        }
    }
    for (int t = 0; t < size * size; t++) {
        if (explored(player, t)) {
            total += 5;
        }
        if (tileOwner(t) != player) {
            continue;
        }
        total += 20;
        ImprovementTypes::ImprovementType building = tiles[t].building;
        if (building >= ImprovementTypes::TEMPLE &&
            building <= ImprovementTypes::WATERTEMPLE) {
            total += 100 * templeLevel(t);
        }
    }
    for (const City &city : cities) {
        if (city.owner == player) {
            // 100 at level 1; each level above is 50 (5 a population point
            // plus a 50 - 5n bonus for reaching level n); 5 a population
            // point toward the next; 250 a park
            total += 100 + 50 * (city.level - 1) + 5 * city.population +
                     250 * city.parks;
        }
    }
    for (const TechData &tech : techData) {
        if (players[player].hasTech(tech.type)) {
            total += 100 * tech.tier;
        }
    }
    return total;
}

int Game::meetingBonus(int score) {
    int thousands = score <= 0 ? 0 : (score + 999) / 1000;
    return std::min(11, 1 + 2 * thousands);
}

void Game::checkMeetings() {
    int count = static_cast<int>(players.size());
    for (int p = 0; p < count; p++) {
        Player &player = players[p];
        if (!player.alive) {
            continue;
        }
        auto meet = [&](int other) {
            if (other < 0 || other == p || player.hasMet(other) ||
                !players[other].alive) {
                return;
            }
            player.met |= std::uint32_t{1} << other;
            player.stars += meetingBonus(score(other));
        };
        // meeting a tribe means seeing one of its units or one of its cities;
        // a border tile alone doesn't count
        for (int t = 0; t < size * size; t++) {
            if (!explored(p, t)) {
                continue;
            }
            int city = cityAt(t);
            if (city >= 0) {
                meet(cities[city].owner);
            }
            const Unit *unit = unitAt(t);
            if (unit && canSee(p, *unit)) {
                meet(unit->owner);
            }
        }
    }
}
