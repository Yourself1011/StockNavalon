#include "game.h"
#include "player.h"

Game::Game(int size, MapType mapType) : map{Map(size, mapType)} {
    players.push_back(Player(0));
}
