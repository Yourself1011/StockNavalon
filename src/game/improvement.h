#pragma once

#include "data/improvementData.h"
#include <vector>

class Player;
class Tile;

class Improvement {
    int level = 0, currentPopulation = 0, totalPopulation = 0, production = 0,
        baseScore = 0, borderSize = 1, upgradeCount = 0, createdTurn = 0;
    // there is also a rebellionBuffer field, unsure why this is useful
    bool connectedPlayerCapital = false, underRebellion = false;
    Player *foundedPlayer;
    std::vector<Tile *> tiles;
    Tile *centre;

  public:
    const ImprovementData &staticData;
    Improvement(Player &player, Tile &tile,
                const ImprovementData &improvementData);
    void addPopulation(int amt);
};
