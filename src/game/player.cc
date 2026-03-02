#include "player.h"
#include <vector>

Player::Player(int idx)
    : idx{idx}, stars{0}, techs{std::vector<const Tech *>()} {}
