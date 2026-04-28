#include "improvement.h"

Improvement::Improvement(Player *player, Tile *tile,
                         ImprovementData &improvementData)
    : level{0}, currentPopulation{0}, totalPopulation{0}, production{0},
      baseScore{0}, borderSize{1}, upgradeCount{0}, createdTurn{0},
      connectedPlayerCapital{false}, underRebellion{false},
      foundedPlayer{player} {}

void Improvement::addPopulation(int amt) {
    if (currentPopulation + amt >= totalPopulation) {
        currentPopulation = currentPopulation + amt - totalPopulation;
        totalPopulation++;
        level++;
    } else {
        currentPopulation += amt;
    }
}
