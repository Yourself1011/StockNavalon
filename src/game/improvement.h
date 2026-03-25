#ifndef IMPROVEMENT_H
#define IMPROVEMENT_H
#include "data/improvementData.h"

class Player;
class Tile;

class Improvement {
    int level, currentPopulation, totalPopulation, production, baseScore,
        borderSize, upgradeCount, createdTurn;
    // there is also a rebellionBuffer field, unsure why this is useful
    bool connectedPlayerCapital, underRebellion;
    Player *foundedPlayer;

  public:
    Improvement(Player *player, Tile *tile, ImprovementData &improvementData);
    void addPopulation(int amt);
};

#endif // !IMPROVEMENT_H
