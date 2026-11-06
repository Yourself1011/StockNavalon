#include "improvement.h"

Improvement::Improvement(Player &player, Tile &tile,
                         const ImprovementData &improvementData)
    : foundedPlayer{&player}, centre{&tile}, staticData{improvementData} {}

void Improvement::addPopulation(int amt) {
    if (currentPopulation + amt >= totalPopulation) {
        currentPopulation = currentPopulation + amt - totalPopulation;
        totalPopulation++;
        level++;
    } else {
        currentPopulation += amt;
    }
}
