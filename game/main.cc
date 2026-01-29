#include "game.h"
#include "player.h"
#include "tech.h"
#include "units.h"
#include <iostream>
#include <vector>

int main() {
    Game game = Game(14, MapTypes::DRYLANDS);
    game.map = Map("map.txt");
    Unit *unit1 = Units::rider(&game, &game.players.at(0), game.map.at(1, 1));
    unit1->canMove = true;
    unit1->canAttack = true;
    game.map.at(1, 1)->unit = unit1;

    game.players.at(0).techs.push_back(&Techs::basic);

    Unit *unit2 = Units::warrior(&game, &game.players.at(1), game.map.at(2, 1));
    unit2->canMove = true;
    unit2->canAttack = true;
    game.map.at(2, 1)->unit = unit2;

    game.players.at(1).techs.push_back(&Techs::basic);

    while (true) {
        game.map.print();
        std::string cmd;
        std::cin >> cmd;
        if (cmd == "l") {
            int x, y;
            std::cin >> x >> y;
            Unit *unit = game.map.at(x, y)->unit;
            if (unit == nullptr) {
                std::cout << "No unit on that tile" << std::endl;
                continue;
            }

            if (unit->canMove) {
                std::vector<Tile> *moves = unit->validMoves();

                std::cout << "Valid Moves: ";
                for (Tile tile : *moves) {
                    std::cout << "(" << tile.x << "," << tile.y << ") ";
                }
                std::cout << std::endl;

                delete moves;
            }

            if (unit->canAttack) {
                std::vector<Tile> *attacks = unit->validAttacks();

                std::cout << "Valid Attacks: ";
                for (Tile tile : *attacks) {
                    std::cout << "(" << tile.x << "," << tile.y << ") ";
                }
                std::cout << std::endl;

                delete attacks;
            }
        } else if (cmd == "m") {
            int fromx, fromy, tox, toy;
            std::cin >> fromx >> fromy >> tox >> toy;

            Unit *unit = game.map.at(fromx, fromy)->unit;

            if (unit == nullptr) {
                std::cout << "No unit on that tile" << std::endl;
                continue;
            }

            if (!unit->canMove) {
                std::cout << "Unit out of moves this turn" << std::endl;
                continue;
            }

            bool isValid = false;
            std::vector<Tile> *moves = unit->validMoves();
            for (Tile tile : *moves) {
                if (tile.x == tox && tile.y == toy) {
                    isValid = true;
                    break;
                }
            }
            delete moves;
            if (!isValid) {
                std::cout << "Invalid move" << std::endl;
                continue;
            }

            unit->move(game.map.at(tox, toy));
        } else if (cmd == "a") {
            int fromx, fromy, tox, toy;
            std::cin >> fromx >> fromy >> tox >> toy;

            Unit *attacker = game.map.at(fromx, fromy)->unit;
            Unit *defender = game.map.at(tox, toy)->unit;

            if (attacker == nullptr) {
                std::cout << "No attacker on that tile" << std::endl;
                continue;
            }

            if (defender == nullptr) {
                std::cout << "No defender on that tile" << std::endl;
                continue;
            }

            if (!attacker->canAttack) {
                std::cout << "Unit out of moves this turn" << std::endl;
                continue;
            }

            bool isValid = false;
            std::vector<Tile> *attacks = attacker->validAttacks();
            for (Tile tile : *attacks) {
                if (tile.x == tox && tile.y == toy) {
                    isValid = true;
                    break;
                }
            }
            delete attacks;

            if (!isValid) {
                std::cout << "Invalid attack" << std::endl;
                continue;
            }

            attacker->attackUnit(defender);
        } else if (cmd == "i") {
            int x, y;
            std::cin >> x >> y;
            Unit *unit = game.map.at(x, y)->unit;
            if (unit == nullptr) {
                std::cout << "No unit on that tile" << std::endl;
                continue;
            }

            std::cout << "Player " << unit->player->idx << "'s " << unit->type
                      << "\nHealth: " << unit->health << "/" << unit->maxHealth
                      << "\nAttack: " << unit->attack
                      << "\nDefence: " << unit->defence;
            if (unit->getDefenceBonus() != 1) {
                std::cout << "(" << unit->getDefenceBonus() << ")";
            }
            std::cout << "\nMovement: " << unit->movement
                      << "\nRange: " << unit->range;
            if (!unit->abilities.test(Abilities::STATIC)) {
                std::cout << "\n" << unit->kills << "/3 to become a veteran";
            }
            std::cout << "\n" << (unit->canMove ? "Can move" : "Cannot move");
            std::cout << "\n"
                      << (unit->canAttack ? "Can attack" : "Cannot attack");
            std::cout << "\n"
                      << (unit->canUseAbility ? "Can use ability"
                                              : "Cannot use ability");
            std::cout << std::endl;
        }
    }
}
