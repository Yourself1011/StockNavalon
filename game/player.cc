#include "player.h"
#include <vector>

Player::Player(int idx) : idx{idx}, units{std::vector<Unit *>()} {}
