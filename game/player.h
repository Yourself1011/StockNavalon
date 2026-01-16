#include <vector>
#ifndef PLAYER_H
#define PLAYER_H
class Unit;
class Player {
  public:
    int idx;
    std::vector<Unit *> units;
    Player(int idx);
};
#endif
