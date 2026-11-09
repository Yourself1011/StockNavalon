#include "game.h"

namespace {
bool onWater(const Tile &tile) {
    return TerrainTypes::isWater(tile.terrain) && !tile.road;
}
} // namespace

std::vector<Game::RuinReward> Game::ruinRewards(int unitIdx) const {
    const Unit &unit = units[unitIdx];
    int player = unit.owner;
    std::vector<RuinReward> rewards{RUIN_STARS};

    bool researchable = std::any_of(
        techData.begin(), techData.end(),
        [&](const TechData &tech) { return canResearch(player, tech.type); });
    if (researchable) {
        rewards.push_back(RUIN_TECH);
    }
    int capital = players[player].capital;
    if (capital >= 0 && cities[capital].owner == player) {
        rewards.push_back(RUIN_POPULATION);
    }
    // only if something in the 5x5 around the ruin is still unexplored
    bool fog = false;
    forEachInRadius(unit.tile, Rules::RUIN_EXPLORER_FOG_RADIUS,
                    [&](int t) { fog |= !explored(player, t); });
    if (fog) {
        rewards.push_back(RUIN_EXPLORER);
    }
    rewards.push_back(RUIN_UNIT);
    std::sort(rewards.begin(), rewards.end());
    return rewards;
}

void Game::giveRuinReward(int unitIdx, RuinReward reward) {
    // copied, since spawning a unit can move the units vector
    const Unit unit = units[unitIdx];
    Player &player = players[unit.owner];

    switch (reward) {
    case RUIN_TECH: {
        std::vector<TechTypes::TechType> options;
        for (const TechData &tech : techData) {
            if (canResearch(unit.owner, tech.type)) {
                options.push_back(tech.type);
            }
        }
        player.techs.set(options[randomInt(static_cast<int>(options.size()))]);
        break;
    }
    case RUIN_STARS:
        player.stars += Rules::RUIN_STARS;
        break;
    case RUIN_POPULATION:
        addPopulation(player.capital, Rules::RUIN_POPULATION);
        break;
    case RUIN_EXPLORER:
        runExplorer(unit.owner, unit.tile);
        break;
    case RUIN_UNIT: {
        // it appears on the ruin and pushes the examining unit off, by the
        // same rules as a Giant
        int spot = unit.tile;
        bool water = onWater(tiles[spot]);
        pushUnit(unitIdx, unit.owner);
        int idx = spawnUnit(unit.owner,
                            water ? Rules::RUIN_SEA_BOAT : Rules::RUIN_LAND_UNIT,
                            spot, unit.home);
        Unit &spawned = units[idx];
        // the Rammer carries a Warrior; either way the soldier is a veteran
        spawned.passenger =
            water ? Rules::RUIN_SEA_PASSENGER : Rules::RUIN_LAND_UNIT;
        spawned.maxHp = unitInfo(spawned.passenger).maxHp + Rules::VETERAN_HP;
        spawned.hp = spawned.maxHp;
        spawned.veteran = true;
        spawned.freeOfCap = true;
        break;
    }
    default:
        break;
    }
}

void Game::examineRuins(int unitIdx) {
    std::vector<RuinReward> rewards = ruinRewards(unitIdx);
    RuinReward reward = rewards[randomInt(static_cast<int>(rewards.size()))];
    Unit &unit = units[unitIdx];
    tiles[unit.tile].building = ImprovementTypes::NONE;
    // before the reward: a pushed unit can be destroyed, freeing its slot
    unit.canMove = unit.canAttack = unit.canAct = false;
    giveRuinReward(unitIdx, reward);
}
