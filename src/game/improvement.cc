#include "improvement.h"

Improvement::Improvement(Player *player, Tile *tile, ImprovementData &improvementData)
    : level{0}, currentPopulation{0}, totalPopulation{0}, production{0}, baseScore{0}, borderSize{1}, upgradeCount{0}, connectedPlayerCapital{false}, underRebellion{false}, foundedTribe{player} {
}