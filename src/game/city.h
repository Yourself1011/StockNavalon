#pragma once

#include <array>
#include <string_view>

namespace CityRewards {
// GAME.md section 7, level-up rewards
enum CityReward {
    WORKSHOP,
    EXPLORER,
    CITYWALL,
    RESOURCES,
    POPULATIONGROWTH,
    BORDERGROWTH,
    PARK,
    SUPERUNIT,
    CITY_REWARD_SIZE
};

const std::array<std::string_view, CITY_REWARD_SIZE> rewardNames = {
    "workshop",      "explorer", "city wall", "resources", "population growth",
    "border growth", "park",     "super unit"};

// the two options offered on reaching a level
std::array<CityReward, 2> rewardsForLevel(int level);
} // namespace CityRewards

struct City {
    int tile;
    int owner;
    int level = 1;
    // progress toward the next level; level n -> n+1 needs n+1. Can go
    // negative.
    int population = 0;
    // the player whose original capital this is, -1 if none
    int capitalOf = -1;
    // the player whose capital this city is connected to, -1 if none
    int connectedFor = -1;
    // level-up rewards still to be chosen
    int pendingRewards = 0;
    int parks = 0;
    bool workshop = false, wall = false, borderGrown = false;
};
