#include "player.h"
#include "tech.h"
#include "unit.h"

Player::Player(int idx)
    : idx{idx}, techs{}, units{}, stars{5}, score{0}, alive{true} {
    techs.push_back(&Techs::basic);
}

void Player::startTurn() {
    for (Unit *unit : units) {
        unit->canMove = true;
        unit->canAttack = true;
        unit->canUseAbility = true;
    }
}
