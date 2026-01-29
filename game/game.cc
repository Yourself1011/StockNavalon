#include "game.h"
#include "player.h"

Game::Game(int size, MapTypes::MapType mapType) : map{Map(size, mapType)} {
    players.push_back(Player(0));
    players.push_back(Player(1));
}
