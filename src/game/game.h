#include "map.h"
#include "player.h"
#include <vector>

#ifndef GAME_H
#define GAME_H

class Tile;

class Game {
  public:
    Map map;
    std::vector<Player> players;
    int currentPlayerIdx = 0;
    int turnNumber = 1;

    Game(int size, MapTypes::MapType mapType);
    Player &currentPlayer();

    void endTurnAndAdvance();
};

#endif
