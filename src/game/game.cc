#include "game.h"

Game::Game(int size, MapTypes::MapType mapType) : map{Map(size, mapType)} {}

Player &Game::currentPlayer() { 
    return players.at(currentPlayerIdx);
}

void Game::endTurnAndAdvance() {
    currentPlayerIdx = (currentPlayerIdx + 1) % players.size();
    if (currentPlayerIdx == 0) {
        turnNumber++;
    }
    currentPlayer().startTurn();
}
