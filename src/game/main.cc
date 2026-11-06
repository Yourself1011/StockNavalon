#include "data/improvementData.h"
#include "game.h"
#include "player.h"
#include "tech.h"
#include "unit.h"
#include "utils.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool containsTile(const std::vector<Tile *> &tiles, int x, int y) {
    return std::any_of(tiles.begin(), tiles.end(), [x, y](const Tile *tile) {
        return tile->x == x && tile->y == y;
    });
}

void printTiles(const std::string &label, const std::vector<Tile *> &tiles) {
    std::cout << label << ": ";
    for (const Tile *tile : tiles) {
        std::cout << "(" << tile->x << "," << tile->y << ") ";
    }
    std::cout << '\n';
}

// returns the current player's unit on (x, y), or nullptr after printing why
Unit *ownUnitAt(Game &game, int x, int y) {
    Unit *unit = game.map.at(x, y)->unit;
    if (unit == nullptr) {
        std::cout << "No unit on that tile\n";
        return nullptr;
    }
    if (unit->player->idx != game.currentPlayerIdx) {
        std::cout << "Not your unit\n";
        return nullptr;
    }
    return unit;
}

void printUnit(const Unit &unit) {
    std::cout << "Player " << unit.player->idx << "'s " << unit.type
              << "\nHealth: " << unit.health << "/" << unit.maxHealth
              << "\nAttack: " << unit.attack << "\nDefence: " << unit.defence;
    if (unit.getDefenceBonus() != 1) {
        std::cout << "(" << unit.getDefenceBonus() << ")";
    }
    std::cout << "\nMovement: " << unit.movement << "\nRange: " << unit.range;
    if (!unit.abilities.test(Abilities::STATIC)) {
        std::cout << "\n" << unit.kills << "/3 to become a veteran";
    }
    std::cout << "\n" << (unit.canMove ? "Can move" : "Cannot move") << "\n"
              << (unit.canAttack ? "Can attack" : "Cannot attack") << "\n"
              << (unit.canUseAbility ? "Can use ability" : "Cannot use ability")
              << '\n';
}

// runs one command; returns false once input is exhausted
bool runCommand(Game &game) {
    Player &currentPlayer = game.currentPlayer();
    std::string cmd;
    if (!(std::cin >> cmd)) {
        return false;
    }

    if (cmd == "e") {
        game.endTurnAndAdvance();
    } else if (cmd == "lt") {
        std::cout << "Unlocked Techs:\n";
        for (const Tech *tech :
             BitsetIndex{currentPlayer.techs, TechDataAccessor{}}) {
            std::cout << tech->type << '\n';
        }
        std::cout << "\nUnlockable Techs:\n";
        for (const Tech *tech :
             BitsetIndex{currentPlayer.buyableTechs, TechDataAccessor{}}) {
            std::cout << tech->type << '\n';
        }
    } else if (cmd == "t") {
        int buy;
        if (!(std::cin >> buy)) {
            return false;
        }
        if (buy < 0 || buy >= TechTypes::TECH_TYPE_SIZE) {
            std::cout << "No such tech\n";
            return true;
        }
        auto tech = static_cast<TechTypes::TechTypes>(buy);
        if (!currentPlayer.buyableTechs.test(tech)) {
            std::cout << "Not buyable\n";
        } else if (!currentPlayer.canBuyTech(tech)) {
            std::cout << "Too expensive\n";
        } else {
            currentPlayer.buyTech(tech);
            std::cout << "Success\n";
        }
    } else if (cmd == "l") {
        int x, y;
        if (!(std::cin >> x >> y)) {
            return false;
        }
        Unit *unit = ownUnitAt(game, x, y);
        if (unit == nullptr) {
            return true;
        }
        if (unit->canMove) {
            printTiles("Valid Moves", unit->validMoves());
        }
        if (unit->canAttack) {
            printTiles("Valid Attacks", unit->validAttacks());
        }
    } else if (cmd == "m") {
        int fromx, fromy, tox, toy;
        if (!(std::cin >> fromx >> fromy >> tox >> toy)) {
            return false;
        }
        Unit *unit = ownUnitAt(game, fromx, fromy);
        if (unit == nullptr) {
            return true;
        }
        if (!unit->canMove) {
            std::cout << "Unit out of moves this turn\n";
        } else if (!containsTile(unit->validMoves(), tox, toy)) {
            std::cout << "Invalid move\n";
        } else {
            unit->move(game.map.at(tox, toy));
        }
    } else if (cmd == "a") {
        int fromx, fromy, tox, toy;
        if (!(std::cin >> fromx >> fromy >> tox >> toy)) {
            return false;
        }
        Unit *attacker = ownUnitAt(game, fromx, fromy);
        if (attacker == nullptr) {
            return true;
        }
        Unit *defender = game.map.at(tox, toy)->unit;
        if (defender == nullptr) {
            std::cout << "No defender on that tile\n";
        } else if (!attacker->canAttack) {
            std::cout << "Unit out of moves this turn\n";
        } else if (!containsTile(attacker->validAttacks(), tox, toy)) {
            std::cout << "Invalid attack\n";
        } else {
            attacker->attackUnit(defender);
        }
    } else if (cmd == "i") {
        int x, y;
        if (!(std::cin >> x >> y)) {
            return false;
        }
        const Unit *unit = game.map.at(x, y)->unit;
        if (unit == nullptr) {
            std::cout << "No unit on that tile\n";
        } else {
            printUnit(*unit);
        }
    } else {
        std::cout << "Unknown command\n";
    }
    return true;
}

} // namespace

int main() {
    Game game("./src/game/map.txt", 2);

    game.map.at(1, 1)->spawnUnit(game.players.at(0), Units::rider);
    game.map.at(4, 1)->spawnUnit(game.players.at(1), Units::warrior);
    game.map.at(1, 2)->improve(game.players.at(0),
                               ImprovementMap.at(ImprovementTypes::CITY));

    game.currentPlayer().startTurn();

    while (true) {
        std::cout << "Player " << game.currentPlayerIdx << "'s turn (Round "
                  << game.turnNumber << ")\n"
                  << "Stars: " << game.currentPlayer().stars << '\n';
        game.map.print();
        std::cout << std::flush;

        try {
            if (!runCommand(game)) {
                return 0;
            }
        } catch (const std::out_of_range &e) {
            std::cout << e.what() << '\n';
        }
    }
}
