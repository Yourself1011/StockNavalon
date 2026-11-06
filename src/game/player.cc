#include "player.h"
#include "tech.h"
#include "unit.h"
#include "utils.h"
#include <algorithm>

Player::Player(int idx)
    : idx{idx}, buyableTechs{makeBitset<TechTypes::TECH_TYPE_SIZE>(
                    {TechTypes::RIDING, TechTypes::ORGANIZATION,
                     TechTypes::CLIMBING, TechTypes::FISHING,
                     TechTypes::HUNTING})} {
    techs.set(TechTypes::BASIC);
}

Player::~Player() = default;
Player::Player(Player &&) noexcept = default;
Player &Player::operator=(Player &&) noexcept = default;

void Player::startTurn() {
    for (const std::unique_ptr<Unit> &unit : units) {
        unit->canMove = true;
        unit->canAttack = true;
        unit->canUseAbility = true;
    }
}

Unit &Player::addUnit(std::unique_ptr<Unit> unit) {
    units.push_back(std::move(unit));
    return *units.back();
}

void Player::removeUnit(const Unit *unit) {
    std::erase_if(units, [unit](const std::unique_ptr<Unit> &owned) {
        return owned.get() == unit;
    });
}

int Player::techCost(TechTypes::TechTypes tech) const {
    // return techData.at(tech).cost * cities.size() + 4;
    return techData.at(tech).cost + 4;
}

bool Player::canBuyTech(TechTypes::TechTypes tech) const {
    return buyableTechs.test(tech) && techCost(tech) <= stars;
}

void Player::buyTech(TechTypes::TechTypes tech) {
    stars -= techCost(tech);
    techs.set(tech);
    buyableTechs |= techData.at(tech).techUnlocks;
    buyableTechs.reset(tech);
}
