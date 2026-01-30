#ifndef PLAYER_H
#define PLAYER_H

#include <vector>

class Unit;
class Tech;
class Player {
  public:
    int idx;
    std::vector<const Tech *> techs;
    Player(int idx);
};
#endif
