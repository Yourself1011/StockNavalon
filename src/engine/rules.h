#pragma once

// Every rule of the game that isn't a table in data/: amounts, formulas,
// radii and thresholds. The engine reads them from here, and so does
// everything built on it (stockNavalon's tribe inference and encoding), so a
// change to the game is one edit here or in data/. Nothing outside these two
// places should write a rule's number.

#include "data/unitData.h"
#include <algorithm>
#include <cmath>

namespace Rules {

// ---- score (GAME.md section 3) ----

constexpr int TECH_SCORE_PER_TIER = 100;
// a unit is worth this per star of its cost plus its passenger's
constexpr int UNIT_SCORE_PER_STAR = 5;
constexpr int CITY_SCORE = 100;
// per level above 1: a population point's worth each, plus a bonus for the
// level, adding up to this
constexpr int LEVEL_SCORE = 50;
constexpr int POPULATION_SCORE = 5;
constexpr int PARK_SCORE = 250;
constexpr int TERRITORY_SCORE = 20;
constexpr int EXPLORED_SCORE = 5;
constexpr int TEMPLE_SCORE_PER_LEVEL = 100;
constexpr int MONUMENT_SCORE = 400;

constexpr int techScore(int tier) { return TECH_SCORE_PER_TIER * tier; }
constexpr int unitScore(int cost) { return UNIT_SCORE_PER_STAR * cost; }
// below zero population a city loses this much per point, down to 0 (game:
// the score sheet's starvation penalty)
constexpr int STARVATION_SCORE = 100;
constexpr int cityScore(int level, int population, int parks, bool starvation = true) {
    int score = CITY_SCORE + LEVEL_SCORE * (level - 1) +
                POPULATION_SCORE * population + PARK_SCORE * parks;
    if (starvation && population < 0) {
        score = std::max(score + STARVATION_SCORE * population, 0);
    }
    return score;
}

// a temple gains a level every few turns after it's built
constexpr int MAX_TEMPLE_LEVEL = 5;
constexpr int TEMPLE_TURNS_PER_LEVEL = 2;
constexpr int templeLevel(int builtTurn, int turn) {
    return std::min(MAX_TEMPLE_LEVEL,
                    1 + (turn - builtTurn) / TEMPLE_TURNS_PER_LEVEL);
}
constexpr int templeScore(int level) { return TEMPLE_SCORE_PER_LEVEL * level; }
// the turn a temple of `owner`'s levels by: the turns its owner has started
// (game, measured). `turn` counts the first player's turns.
constexpr int templeTurn(int turn, int currentPlayer, int owner) {
    return currentPlayer >= owner ? turn : turn - 1;
}

// ---- stars ----

constexpr int TECH_COST_BASE = 4;
// tier x cities + 4; Literacy (from Philosophy) makes it a third cheaper,
// rounded up
constexpr int techCost(int tier, int cities, bool literacy) {
    int cost = tier * cities + TECH_COST_BASE;
    return literacy ? (cost * 2 + 2) / 3 : cost;
}

// a city's income: its level, +1 for the capital, +1 for a workshop and per
// park (markets and sieges come on top, in Game::cityIncome). The capital's
// +1 is a per-player handicap in the game: humans and Normal bots 1, Easy 0,
// Hard 2, Crazy 4 (GameSettings::capitalIncome)
constexpr int CAPITAL_INCOME = 1;
constexpr int WORKSHOP_INCOME = 1;
constexpr int PARK_INCOME = 1;
constexpr int cityIncome(int level, bool capital, bool workshop, int parks,
                         int capitalIncome = CAPITAL_INCOME) {
    return level + capitalIncome * capital + WORKSHOP_INCOME * workshop +
           PARK_INCOME * parks;
}

constexpr TechTypes::TechType EMBASSY_TECH = TechTypes::DIPLOMACY;
// an embassy costs this, plus EMBASSY_COST_STEP per embassy the builder
// already has (game; before GROWING_EMBASSY_COST: a flat 5)
constexpr int EMBASSY_COST = 4;
constexpr int EMBASSY_COST_STEP = 2;
constexpr int OLD_EMBASSY_COST = 5;
constexpr int embassyCost(int embassies) { return EMBASSY_COST + EMBASSY_COST_STEP * embassies; }
// income from each embassy, either way round, and with a peace treaty
constexpr int EMBASSY_INCOME = 2;
constexpr int EMBASSY_INCOME_AT_PEACE = 4;

constexpr int CLEAR_FOREST_REFUND = 1;
// the tech that lets units be disbanded, for half their cost back
constexpr TechTypes::TechType DISBAND_TECH = TechTypes::FREESPIRIT;
constexpr int STARFISH_STARS = 8;
// the tech that lets boats harvest starfish
constexpr TechTypes::TechType STARFISH_TECH = TechTypes::NAVIGATION;
constexpr int disbandRefund(int cost) { return cost / 2; }

// stars for meeting a tribe, by its score then: 1, +2 per thousand, and 2
// more for meeting the first player of a two-player game (the second
// player's compensation for moving second)
constexpr int MEETING_FIRST_PLAYER_BONUS = 2;
constexpr int meetingBonus(int score, bool firstOfTwo = false) {
    int thousands = score <= 0 ? 0 : (score + 999) / 1000;
    return std::min(11, 1 + 2 * thousands) + (firstOfTwo ? MEETING_FIRST_PLAYER_BONUS : 0);
}

// ---- cities (GAME.md section 7) ----

// population needed to go up a level from `level`
constexpr int populationToLevelUp(int level) { return level + 1; }
// the units a city supports
constexpr int unitCap(int level) { return level + 1; }
// the furthest a city's territory reaches from it
constexpr int MAX_BORDER_RADIUS = 2;
// level-up rewards (the choices are CityRewards::rewardsForLevel)
constexpr int RESOURCES_STARS = 5;
constexpr UnitTypes::UnitType SUPER_UNIT = UnitTypes::GIANT;
constexpr int POPULATION_GROWTH = 3;
constexpr int CITY_BORDER_RADIUS = 1;
constexpr int GROWN_BORDER_RADIUS = 2;
// a port connects to ports and cities at most this many water tiles away
constexpr int PORT_CONNECTION_RANGE = 5;
// a bridge's: a water route from it reaches this far, over shallow water
constexpr int BRIDGE_CONNECTION_RANGE = 1;
// a connection to the capital: this much in the city and in the capital
constexpr int CONNECTION_POPULATION = 1;
// finding a lighthouse: this much in the capital
constexpr int LIGHTHOUSE_POPULATION = 1;
// one on each corner of the map
constexpr int LIGHTHOUSE_COUNT = 4;

// ---- boats ----

// the only boat that upgrades (to the rest of its family, isRaftFamily),
// inside its owner's or a peace partner's borders; the upgrade is final and
// doesn't use the unit's turn
constexpr UnitTypes::UnitType UPGRADABLE_BOAT = UnitTypes::RAFT;

// ---- ruins (GAME.md section 13) ----

constexpr int RUIN_STARS = 10;
// a ruin's soldier: on land, a veteran of this type; on water, this boat
// carrying a veteran of that type
constexpr UnitTypes::UnitType RUIN_LAND_UNIT = UnitTypes::SWORDSMAN;
constexpr UnitTypes::UnitType RUIN_SEA_BOAT = UnitTypes::RAMMER;
constexpr UnitTypes::UnitType RUIN_SEA_PASSENGER = UnitTypes::WARRIOR;
constexpr int RUIN_POPULATION = 3;
// a ruin's soldier is a veteran (VETERAN_HP)
// an Explorer from a ruin needs fog within this radius of it
constexpr int RUIN_EXPLORER_FOG_RADIUS = 2;

// ---- movement (GAME.md section 9) ----

// movement points per step, counted in half steps: a road step costs half
constexpr int STEP_COST = 2;
constexpr int ROAD_STEP_COST = 1;

// ---- sight and exploration (GAME.md section 16) ----

// the square around a capital that starts explored
constexpr int START_EXPLORED_RADIUS = 2;
constexpr int UNIT_SIGHT = 1;
// with the Scout skill, or from a mountain
constexpr int FAR_SIGHT = 2;
// how many steps an Explorer takes
constexpr int EXPLORER_STEPS = 12;

// ---- combat (GAME.md section 10) ----

// HP (x10) healed: resting in your own territory, elsewhere, and by a
// Mind Bender's Heal Others
constexpr int HEAL_IN_TERRITORY = 40;
constexpr int HEAL_ELSEWHERE = 20;
constexpr int HEAL_OTHERS = 40;
// splash damage is this fraction of what an attack on each would deal
constexpr int SPLASH_DIVISOR = 2;
// a Cloak's infiltration brings out daggers: the city's level, up to this
constexpr int INFILTRATION_MAX_DAGGERS = 5;

// a unit becomes a veteran after this many kills, gaining max HP (x10)
constexpr int VETERAN_KILLS = 3;
constexpr int VETERAN_HP = 50;

// defence multipliers: a Fortify unit in its own city, behind a wall, and on
// terrain its owner's techs make defensive
constexpr double FORTIFY_BONUS = 1.5;
constexpr double WALL_BONUS = 4.0;
constexpr double TERRAIN_BONUS = 1.5;
constexpr double DAMAGE_SCALE = 4.5;

struct Damage {
    // HP x10, as stored: what the attacker deals, and the retaliation
    int dealt, returned;
};
// An attack, from the units' attack and defence (x10), HP, and the
// defender's defence multiplier. Damage rounds to whole HP, halves up.
// With `integer`, in the game's own integer arithmetic (BattleHelpers:
// attack and defence x100, the bonus x10, each division truncated before
// the rounding), which can round differently from the exact value: a
// retaliation of 1.5 comes out as 1.4999, so 1
inline Damage damage(int attack, int attackerHp, int attackerMaxHp,
                     int defence, int defenderHp, int defenderMaxHp,
                     double defenceBonus, bool integer = true) {
    if (integer) {
        long long a = attack * 10LL;
        long long bonus = std::llround(defenceBonus * 10);
        long long d = defence * 10LL * bonus / 10;
        long long attackForce = a * attackerHp * 100 / attackerMaxHp;
        long long defenceForce = d * defenderHp * 100 / defenderMaxHp;
        long long total = attackForce + defenceForce;
        if (total <= 0) {
            return {0, 0};
        }
        long long dealt = a * attackForce * 450 / (total * 1000);
        long long returned = defenceForce * d * 4500 / (total * 1000 * bonus);
        auto round = [](long long tenths) { return static_cast<int>((tenths + 5) / 10 * 10); };
        return {round(dealt), round(returned)};
    }
    double a = attack / 10.0, d = defence / 10.0;
    double attackForce = a * attackerHp / attackerMaxHp;
    double defenceForce = d * defenderHp / defenderMaxHp * defenceBonus;
    double total = attackForce + defenceForce;
    if (total <= 0) {
        return {0, 0};
    }
    auto round = [](double hp) {
        return static_cast<int>(std::floor(hp + 0.5 + 1e-9)) * 10;
    };
    return {round(attackForce / total * a * DAMAGE_SCALE),
            round(defenceForce / total * d * DAMAGE_SCALE)};
}

// ---- tasks (GAME.md section 14) ----

// Pacifist: this many turns without attacking, counted from this tech
constexpr TechTypes::TechType PACIFIST_TECH = TechTypes::MEDITATION;
constexpr int PACIFIST_TURNS = 5;
// Wealth: this tech, and this many stars at once
constexpr TechTypes::TechType WEALTH_TECH = TechTypes::TRADE;
constexpr int WEALTH_STARS = 100;
constexpr int KILLER_KILLS = 10;
constexpr int NETWORK_CITIES = 5;
constexpr int METROPOLIS_LEVEL = 5;

} // namespace Rules
