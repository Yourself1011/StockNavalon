#include "game.h"
#include "unit.h"

Game::Game(int size, MapTypes::MapType mapType, int numPlayers)
    : map{this, size, mapType} {
    createPlayers(numPlayers);
}

Game::Game(const std::string &mapFile, int numPlayers) : map{this, mapFile} {
    createPlayers(numPlayers);
}

void Game::createPlayers(int numPlayers) {
    players.reserve(numPlayers);
    for (int i = 0; i < numPlayers; i++) {
        players.emplace_back(i);
    }
}

Player &Game::currentPlayer() { return players.at(currentPlayerIdx); }

void Game::endTurnAndAdvance() {
    currentPlayerIdx = (currentPlayerIdx + 1) % players.size();
    if (currentPlayerIdx == 0) {
        turnNumber++;
    }
    currentPlayer().startTurn();
}
