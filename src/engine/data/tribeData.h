#pragma once

#include "data/techData.h"
#include "data/terrain.h"
#include "data/unitData.h"
#include <array>
#include <string_view>
#include <vector>

namespace TribeTypes {
enum Tribe {
    XINXI,
    IMPERIUS,
    BARDUR,
    OUMAJI,
    KICKOO,
    HOODRICK,
    LUXIDOOR,
    VENGIR,
    ZEBASI,
    AIMO,
    QUETZALI,
    YADAKK,
    TRIBE_SIZE
};
} // namespace TribeTypes

using ResourceModifiers = std::array<double, ResourceTypes::RESOURCE_TYPE_SIZE>;

// GAME.md section 17
struct TribeData {
    TribeTypes::Tribe tribe;
    std::string_view name;
    // BASIC means no start tech
    TechTypes::TechType startTech;
    UnitTypes::UnitType startUnit;
    int startStars;
    int capitalLevel;
    // map generation: multiply the base chances inside the tribe's climate
    // region. The water modifier has no effect (GAME.md section 18).
    double mountainModifier = 1, forestModifier = 1, waterModifier = 1;
    ResourceModifiers resourceModifiers = {1, 1, 1, 1, 1, 1, 1};
    // added next to the capital after the other resources
    std::vector<ResourceTypes::ResourceType> startingResources;
};

extern const std::array<TribeData, TribeTypes::TRIBE_SIZE> tribeData;
