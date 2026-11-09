#pragma once

#include "data/terrain.h"
#include <array>
#include <bitset>
#include <string_view>

namespace TechTypes {
enum TechType {
    BASIC,
    RIDING,
    ROADS,
    TRADE,
    FREESPIRIT,
    CHIVALRY,
    ORGANIZATION,
    FARMING,
    CONSTRUCTION,
    STRATEGY,
    DIPLOMACY,
    CLIMBING,
    MEDITATION,
    PHILOSOPHY,
    MINING,
    SMITHERY,
    FISHING,
    RAMMING,
    AQUATISM,
    SAILING,
    NAVIGATION,
    HUNTING,
    FORESTRY,
    MATHEMATICS,
    ARCHERY,
    SPIRITUALISM,
    TECH_TYPE_SIZE
};
} // namespace TechTypes

using TechSet = std::bitset<TechTypes::TECH_TYPE_SIZE>;
using TerrainSet = std::bitset<TerrainTypes::TERRAIN_TYPE_SIZE>;

// GAME.md section 5. Units and improvements name their own tech in their data.
struct TechData {
    TechTypes::TechType type;
    std::string_view name;
    int tier;
    TechTypes::TechType parent;
    TerrainSet movementUnlocks;
    TerrainSet defenceBonuses;
};

extern const std::array<TechData, TechTypes::TECH_TYPE_SIZE> techData;

// Whether a tech can be researched with these techs owned, ignoring the
// price: its parent owned, or a child owned (backwards research)
bool researchableWith(const TechSet &owned, TechTypes::TechType tech);
