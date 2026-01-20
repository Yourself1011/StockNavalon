#include "player.h"
#include <vector>

Player::Player(int idx)
    : idx{idx}, techs{std::vector<const Tech *>()},
      units{std::vector<Unit *>()} {}
