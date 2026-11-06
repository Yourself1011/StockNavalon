#pragma once

#include "tech.h"
#include <bitset>
#include <memory>
#include <vector>

class Unit;
class Improvement;

class Player {
  public:
    int idx;
    int stars = 5, score = 0;
    bool alive = true;
    std::bitset<TechTypes::TECH_TYPE_SIZE> techs;
    std::bitset<TechTypes::TECH_TYPE_SIZE> buyableTechs;
    std::vector<std::unique_ptr<Unit>> units;
    std::vector<Improvement *> cities; // owned by their tiles

    explicit Player(int idx);
    // defined in player.cc, where Unit is a complete type
    ~Player();
    Player(Player &&) noexcept;
    Player &operator=(Player &&) noexcept;

    void startTurn();

    Unit &addUnit(std::unique_ptr<Unit> unit);
    // destroys the unit
    void removeUnit(const Unit *unit);

    int techCost(TechTypes::TechTypes tech) const;
    bool canBuyTech(TechTypes::TechTypes tech) const;
    void buyTech(TechTypes::TechTypes tech);
};
