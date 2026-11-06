#pragma once

#include "map.h"
#include "player.h"
#include <string>
#include <vector>

class Game {
  public:
    Map map;
    // never resized after construction: units and tiles hold Player pointers
    std::vector<Player> players;
    int currentPlayerIdx = 0;
    int turnNumber = 1;

    Game(int size, MapTypes::MapType mapType, int numPlayers);
    Game(const std::string &mapFile, int numPlayers);

    // tiles, units and players all point back into the game
    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

    Player &currentPlayer();

    void endTurnAndAdvance();

  private:
    void createPlayers(int numPlayers);
};
