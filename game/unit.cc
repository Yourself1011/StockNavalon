#include "player.h"
#include "tile.h"
#include "unit.h"
#include <algorithm>
#include <vector>

Unit::Unit(Player *player, Tile *tile, std::string type, int idx, bool hidden,
           int cost, int maxHealth, int defence, int movement, int range,
           int attack, int kills, std::string id,
           std::vector<Ability> abilities)
    : player{player}, tile{tile}, type{type}, idx{idx}, hidden{hidden},
      cost{cost}, maxHealth{maxHealth}, health{maxHealth}, defence{defence},
      movement{movement}, range{range}, attack{attack}, kills{kills}, id{id},
      abilities{abilities}, canAttack{false}, canMove{false},
      canUseAbility{false} {};

void Unit::move(Tile *to) {
    tile->unit = nullptr;
    tile = to;
    to->unit = this;
    canMove = false;
    canUseAbility = false;

    bool hasDash = false;
    for (Ability ability : abilities) {
        if (ability == DASH) {
            hasDash = true;
        }
    }
    if (!hasDash) {
        canAttack = false;
    }
}
