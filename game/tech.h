#ifndef TECH_H
#define TECH_H

#include <string>
#include <vector>

class Unit;
class Tech {
    int cost;
    std::vector<Tech> techUnlocks;
    // improvementUnlocks
    std::vector<Unit> unitUnlocks;
    // abilityUnlocks
    // taskUnlocks
};

#endif // !TECH_H
