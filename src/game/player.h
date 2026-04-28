#ifndef PLAYER_H
#define PLAYER_H

#include "tech.h"
#include <vector>

class Unit;

class Player {
  public:
    int idx, stars, score;
    bool alive;
    std::bitset<TechTypes::TECH_TYPE_SIZE> techs;
    std::bitset<TechTypes::TECH_TYPE_SIZE> buyableTechs;
    std::vector<Unit *> units;

    Player(int idx);
    void startTurn();

    std::bitset<TechTypes::TECH_TYPE_SIZE> *validTechs();
    void buyTech(TechTypes::TechTypes tech);
};
#endif
