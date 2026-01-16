#include "allUnits.h"
#include "game.h"
#include "player.h"
#include <iostream>

int main() {
    Game game = Game(14, DRYLANDS);
    Warrior warrior = Warrior(&game.players.at(0), game.map.at(1, 1), 0);
    warrior.canMove = true;
    warrior.canAttack = true;
    game.players.at(0).units.push_back(&warrior);
    game.map.at(1, 1)->unit = &warrior;

    while (true) {
        game.map.print();
        std::string cmd;
        std::cin >> cmd;
        if (cmd == "m") {
            int fromx, fromy, tox, toy;
            std::cin >> fromx >> fromy >> tox >> toy;

            Unit *unit = game.map.at(fromx, fromy)->unit;
            // TODO: also check for move validity
            if (unit) {
                unit->move(game.map.at(tox, toy));
            }
        }
    }
}
