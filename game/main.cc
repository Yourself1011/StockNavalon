#include "game.h"
#include "player.h"
#include "tech.h"
#include "units.h"
#include <iostream>
#include <vector>

int main() {
    Game game = Game(14, MapTypes::DRYLANDS);
    game.map.at(2, 2)->terrainType = TerrainTypes::FOREST;
    game.map.at(4, 5)->terrainType = TerrainTypes::MOUNTAIN;
    Unit *warrior = Units::rider(&game, &game.players.at(0), game.map.at(1, 1));
    warrior->canMove = true;
    warrior->canAttack = true;
    game.players.at(0).units.push_back(warrior);
    game.map.at(1, 1)->unit = warrior;

    game.players.at(0).techs.push_back(&Techs::basic);

    while (true) {
        game.map.print();
        std::string cmd;
        std::cin >> cmd;
        if (cmd == "l") {
            std::vector<Tile> *moves = warrior->validMoves();

            for (Tile tile : *moves) {
                std::cout << "(" << tile.x << "," << tile.y << ") ";
            }
            std::cout << std::endl;

            delete moves;
        } else if (cmd == "m") {
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
