#include "map.h"
#include <vector>

#ifndef GAME_H
#define GAME_H

class Tile;
class Player;

class Game {
  public:
    Map map;
    std::vector<Player> players;
    Game(int size, MapType mapType);
};

#endif
