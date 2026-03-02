#ifndef PLAYER_H
#define PLAYER_H

#include <vector>

class Unit;
class Tech;

class Player {
  public:
    int idx, stars, score;
    bool alive;
    std::vector<const Tech *> techs;
    std::vector<Unit *> units;

    Player(int idx);
    void startTurn();
};
#endif
