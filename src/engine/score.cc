#include "game.h"

int Game::unitScore(const Unit &unit) const {
    // by the carried unit's cost; boat upgrades add nothing
    return Rules::unitScore(unitInfo(unit.passenger).cost);
}

int Game::templeLevel(int tile) const {
    return Rules::templeLevel(tiles[tile].builtTurn, turn);
}

int Game::templeTurnsToLevelUp(int tile) const {
    int level = templeLevel(tile);
    if (level >= Rules::MAX_TEMPLE_LEVEL) {
        return 0;
    }
    // level n is reached on turn builtTurn + TEMPLE_TURNS_PER_LEVEL (n - 1)
    return tiles[tile].builtTurn + Rules::TEMPLE_TURNS_PER_LEVEL * level - turn;
}

int Game::tileScore(int tile) const {
    int total = Rules::TERRITORY_SCORE;
    ImprovementTypes::ImprovementType building = tiles[tile].building;
    if (isTemple(building)) {
        total += Rules::templeScore(templeLevel(tile));
    } else if (isMonument(building)) {
        total += Rules::MONUMENT_SCORE;
    }
    return total;
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
            total += Rules::EXPLORED_SCORE;
        }
        if (tileOwner(t) == player) {
            total += tileScore(t);
        }
    }
    for (const City &city : cities) {
        if (city.owner == player) {
            total += Rules::cityScore(city.level, city.population, city.parks);
        }
    }
    for (const TechData &tech : techData) {
        if (players[player].hasTech(tech.type)) {
            total += Rules::techScore(tech.tier);
        }
    }
    return total;
}

int Game::publishedScore(int player) const {
    for (auto record = scoreHistory.rbegin(); record != scoreHistory.rend();
         ++record) {
        if (record->player == player) {
            return record->score;
        }
    }
    return 0;
}

void Game::publishScore(int player) {
    scoreHistory.push_back({player, turn, score(player)});
    // the effects so far belong to the change just published
    int count = static_cast<int>(players.size());
    for (int by = 0; by < count; by++) {
        scoreEffectHistory.push_back(scoreEffects[by * count + player]);
    }
    for (int by = 0; by < count; by++) {
        scoreEffects[count * count + by * count + player] =
            scoreEffects[by * count + player];
        scoreEffects[by * count + player] = 0;
    }
}

int Game::effectsSincePublished(int by, int to) const {
    return scoreEffects[by * static_cast<int>(players.size()) + to];
}

int Game::effectsInLastChange(int by, int to) const {
    int count = static_cast<int>(players.size());
    return scoreEffects[count * count + by * count + to];
}

void Game::meet(int p, int other) {
    Player &player = players[p];
    if (other < 0 || other == p || player.hasMet(other) || !player.alive ||
        !players[other].alive) {
        return;
    }
    player.met |= std::uint32_t{1} << other;
    player.stars += Rules::meetingBonus(score(other));
}

void Game::checkMeetings() {
    int count = static_cast<int>(players.size());
    for (int p = 0; p < count; p++) {
        if (!players[p].alive) {
            continue;
        }
        // meeting a tribe means seeing one of its units or one of its cities;
        // a border tile alone doesn't count
        for (int t = 0; t < size * size; t++) {
            if (!explored(p, t)) {
                continue;
            }
            int city = cityAt(t);
            if (city >= 0) {
                meet(p, cities[city].owner);
            }
            const Unit *unit = unitAt(t);
            if (unit && canSee(p, *unit)) {
                meet(p, unit->owner);
            }
        }
    }

    // Capital Vision: Diplomacy shows the capitals of tribes you've met
    for (int p = 0; p < count; p++) {
        if (!players[p].alive || !players[p].hasTech(TechTypes::DIPLOMACY)) {
            continue;
        }
        for (int q = 0; q < count; q++) {
            if (players[p].hasMet(q) && players[q].capital >= 0) {
                reveal(p, cities[players[q].capital].tile);
            }
        }
    }
}
