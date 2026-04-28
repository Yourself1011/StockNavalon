#include "player.h"
#include "tech.h"
#include "unit.h"
#include "utils.h"

Player::Player(int idx)
    : idx{idx}, techs{},
      buyableTechs{*vectorToBitset<TechTypes::TECH_TYPE_SIZE>(
          {TechTypes::RIDING, TechTypes::ORGANIZATION, TechTypes::CLIMBING,
           TechTypes::FISHING, TechTypes::HUNTING})},
      units{}, stars{5}, score{0}, alive{true} {
    techs.set(TechTypes::BASIC);
}

void Player::startTurn() {
    for (Unit *unit : units) {
        unit->canMove = true;
        unit->canAttack = true;
        unit->canUseAbility = true;
    }
}

void Player::buyTech(TechTypes::TechTypes techType) {
    const Tech &tech = techData.at(techType);
    techs.set(techType);
    buyableTechs |= tech.techUnlocks;
    buyableTechs.reset(techType);
    // stars -= tech.cost * cities.size() + 4;
    stars -= tech.cost + 4;
}
