#include "bots/randomBot.h"
#include "game.h"
#include "mapgen.h"
#include "view.h"
#include <algorithm>
#include <cctype>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

TribeTypes::Tribe parseTribe(const std::string &name) {
    for (const TribeData &tribe : tribeData) {
        if (lower(std::string{tribe.name}) == lower(name)) {
            return tribe.tribe;
        }
    }
    throw std::invalid_argument("Unknown tribe " + name);
}

// Two lines per row. Top: terrain, then the unit and its owner. Bottom: the
// building or resource, then the owner of the tile. Unexplored tiles are
// dots.
void printMapAscii(const PlayerView &view) {
    std::cout << "   ";
    for (int x = 0; x < view.size; x++) {
        std::cout << (x < 10 ? " " : "") << x << "   ";
    }
    std::cout << '\n';
    for (int y = 0; y < view.size; y++) {
        std::string top = (y < 10 ? " " : "") + std::to_string(y) + ' ';
        std::string bottom = "   ";
        for (int x = 0; x < view.size; x++) {
            const PlayerView::TileView &t = view.tiles[y * view.size + x];
            if (!t.explored) {
                top += ".... ";
                bottom += ".... ";
                continue;
            }
            top += TerrainTypes::terrainChars[t.terrain];
            if (t.unit >= 0) {
                const PlayerView::UnitView &unit = view.units[t.unit];
                top += std::string{unitInfo(unit.type).id} +
                       static_cast<char>('0' + unit.owner);
            } else {
                top += "   ";
            }
            top += ' ';

            if (t.building != ImprovementTypes::NONE) {
                bottom += improvementInfo(t.building).id;
            } else if (t.road) {
                bottom += "==";
            } else {
                bottom += ' ';
                bottom += ResourceTypes::resourceChars[t.resource];
            }
            bottom += t.owner < 0 ? ' ' : static_cast<char>('0' + t.owner);
            bottom += "  ";
        }
        std::cout << top << '\n' << bottom << '\n';
    }
}

// Emoji view. Each tile is a square of 2 x 2 emoji slots (4 columns, 2 rows):
//   top:    building or terrain, then resource or road
//   bottom: the unit, then its HP
// Tiles alternate between two shades so they stay distinct; inside a player's
// borders the shades are that player's colour. A unit and its HP sit on its
// owner's brighter colour.
namespace Emoji {
using namespace std::string_view_literals;

const std::array<std::string_view, TerrainTypes::TERRAIN_TYPE_SIZE> terrain = {
    "  "sv, "🌲"sv, "🗻"sv, "🟦"sv, "🌊"sv};

const std::array<std::string_view, ResourceTypes::RESOURCE_TYPE_SIZE>
    resources = {""sv, "🍒"sv, "🌾"sv, "🦌"sv, "💎"sv, "🐟"sv, "⭐"sv};

// indexed by ImprovementType; actions are never on a tile
const std::array<std::string_view, ImprovementTypes::IMPROVEMENT_TYPE_SIZE>
    buildings = {""sv,   "🏠"sv, "⛺"sv, "🗿"sv, "🗼"sv, "🚜"sv, "🔨"sv,
                 "🪓"sv, "🌀"sv, "🪚"sv, "🔥"sv, "💰"sv, "⚓"sv, "🛕"sv,
                 "🛕"sv, "🛕"sv, "🛕"sv,
                 // monuments
                 "🗽"sv, "🗽"sv, "🗽"sv, "🗽"sv, "🗽"sv, "🗽"sv, "🗽"sv,
                 // actions, never on a tile
                 ""sv, ""sv, ""sv, ""sv, ""sv, ""sv, ""sv, ""sv};

const std::array<std::string_view, UnitTypes::UNIT_TYPE_SIZE> units = {
    "🤺"sv, "🏇"sv, "🏹"sv, "🪖"sv, "💂"sv, "🧙"sv, "🎯"sv, "🐴"sv, "🥷"sv,
    "🔪"sv, "👹"sv, "🛶"sv, "⛵"sv, "🚤"sv, "🚢"sv, "🐋"sv, "🛟"sv, "🦜"sv};

// 256-colour backgrounds: two alternating shades for neutral tiles and for
// each player's borders, and a bright one for each player's units
const std::array<int, 2> neutralColours = {235, 238};
const std::array<std::array<int, 2>, 6> borderColours = {
    {{52, 88}, {17, 18}, {58, 94}, {53, 89}, {23, 24}, {22, 28}}};
const std::array<int, 6> unitColours = {160, 27, 136, 127, 37, 34};

std::string background(int colour, std::string_view text) {
    return "\033[48;5;" + std::to_string(colour) + "m" + std::string{text} +
           "\033[0m";
}

// unexplored tiles: a dim cloud texture on near-black
const std::string fog = "\033[48;5;233;38;5;237m░░░░\033[0m";
} // namespace Emoji

void printMap(const PlayerView &view) {
    std::cout << "   ";
    for (int x = 0; x < view.size; x++) {
        std::cout << (x < 10 ? "  " : " ") << x << ' ';
    }
    std::cout << '\n';

    for (int y = 0; y < view.size; y++) {
        std::string top = (y < 10 ? " " : "") + std::to_string(y) + ' ';
        std::string bottom = "   ";
        for (int x = 0; x < view.size; x++) {
            const PlayerView::TileView &t = view.tiles[y * view.size + x];
            int shade = (x + y) % 2;
            if (!t.explored) {
                top += Emoji::fog;
                bottom += Emoji::fog;
                continue;
            }
            int colour = t.owner < 0 ? Emoji::neutralColours[shade]
                                     : Emoji::borderColours[t.owner % 6][shade];

            std::string_view base = Emoji::terrain[t.terrain];
            if (t.city >= 0 && view.cities[t.city].capitalOf >= 0) {
                base = "🏰";
            } else if (t.building != ImprovementTypes::NONE) {
                base = Emoji::buildings[t.building];
            }
            std::string_view extra = "  ";
            if (t.unit >= 0 && view.units[t.unit].hiddenEnemyNear) {
                extra = "👀";
            } else if (t.resource != ResourceTypes::NONE) {
                extra = Emoji::resources[t.resource];
            } else if (t.road && t.building != ImprovementTypes::CITY) {
                extra = TerrainTypes::isWater(t.terrain) ? "🌉" : "🟫";
            }
            top += Emoji::background(colour, std::string{base} +
                                                 std::string{extra});

            if (t.unit >= 0) {
                const PlayerView::UnitView &unit = view.units[t.unit];
                // whole HP, rounded up like the in-game map
                int hp = (unit.hp + 9) / 10;
                std::string hpText = (hp < 10 ? " " : "") + std::to_string(hp);
                bottom += Emoji::background(Emoji::unitColours[unit.owner % 6],
                                            std::string{Emoji::units[unit.type]} +
                                                hpText);
            } else {
                bottom += Emoji::background(colour, "    ");
            }
        }
        std::cout << top << '\n' << bottom << '\n';
    }
}

void printKey() {
    std::cout << "Terrain:   (blank) field  🌲 forest  🗻 mountain  🟦 shallow "
                 "water  🌊 ocean\n"
                 "Resources: 🍒 fruit  🌾 crop  🦌 game  💎 metal  🐟 fish  ⭐ "
                 "starfish\n"
                 "Places:    🏰 capital  🏠 city  ⛺ village  🗿 ruin  🗼 "
                 "lighthouse  🟫 road  🌉 bridge\n"
                 "Buildings: 🚜 farm  🔨 mine  🪓 lumber hut  🌀 windmill  🪚 "
                 "sawmill  🔥 forge  💰 market  ⚓ port  🛕 temple  🗽 monument\n"
                 "Units:     🤺 warrior  🏇 rider  🏹 archer  🪖 defender  💂 "
                 "swordsman  🧙 mind bender  🎯 catapult\n"
                 "           🐴 knight  🥷 cloak  🔪 dagger  👹 giant\n"
                 "Boats:     🛶 raft  ⛵ scout  🚤 rammer  🚢 bomber  🐋 "
                 "juggernaut  🛟 dinghy  🦜 pirate\n"
                 "Tiles:     top row: building or terrain, then resource or "
                 "road; bottom row: unit, then its HP\n"
                 "Colours:   a tile's tint shows whose borders it's in, a "
                 "unit's background its owner\n"
                 "Fog:       "
              << Emoji::fog
              << " unexplored  👀 a hidden enemy is next to this unit\n";
    for (int player = 0; player < 2; player++) {
        std::cout << "           player " << player << ": "
                  << Emoji::background(Emoji::borderColours[player][0], "borders")
                  << ' '
                  << Emoji::background(Emoji::unitColours[player], "units")
                  << '\n';
    }
}

void printStatus(const Game &game) {
    const Player &player = game.players[game.currentPlayer];
    std::cout << "Turn " << game.turn << ", player " << game.currentPlayer
              << " (" << tribeData[player.tribe].name << ")\n"
              << "Stars: " << player.stars << " (+"
              << game.income(game.currentPlayer) << "), score "
              << game.score(game.currentPlayer) << "\nTechs:";
    for (const TechData &tech : techData) {
        if (tech.type != TechTypes::BASIC && player.hasTech(tech.type)) {
            std::cout << ' ' << tech.name;
        }
    }
    std::cout << '\n';
    for (const City &city : game.cities) {
        if (city.owner == game.currentPlayer) {
            std::cout << "City (" << game.tileX(city.tile) << ","
                      << game.tileY(city.tile) << ") level " << city.level
                      << ", population " << city.population << "/"
                      << city.level + 1 << '\n';
        }
    }
}

// what the viewer knows about the tile, unless everything is shown
void printTile(const Game &game, int tile, int viewer, bool everything) {
    if (!everything && !game.explored(viewer, tile)) {
        std::cout << "Unexplored\n";
        return;
    }
    const Tile &t = game.tiles[tile];
    std::cout << "Terrain " << TerrainTypes::terrainChars[t.terrain]
              << ", building " << improvementInfo(t.building).name
              << (t.road ? ", road" : "") << ", owner "
              << game.tileOwner(tile) << '\n';
    const Unit *unit = game.unitAt(tile);
    if (unit && (everything || game.canSee(viewer, *unit))) {
        std::cout << "Player " << unit->owner << "'s " << unit->data().name;
        if (unit->passenger != unit->type) {
            std::cout << " carrying a " << unitInfo(unit->passenger).name;
        }
        std::cout << "\nHP " << unit->hp / 10.0 << "/" << unit->maxHp / 10.0
                  << ", attack " << unit->data().attack / 10.0 << ", defence "
                  << unit->data().defence / 10.0 << " (x"
                  << game.defenceBonus(*unit) << "), kills " << unit->kills
                  << (unit->veteran ? ", veteran" : "")
                  << '\n';
        if (everything || unit->owner == viewer) {
            std::cout << "Can move: " << unit->canMove
                      << ", can attack: " << unit->canAttack
                      << ", can act: " << unit->canAct << '\n';
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    // --ascii switches to the letter map, --reveal turns fog off, --seed and
    // --size set up a generated map, --bot N makes seat N a random bot; the
    // rest is positional
    bool ascii = false, reveal = false;
    std::vector<int> botSeats;
    MapGenSettings generate;
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--ascii") {
            ascii = true;
        } else if (arg == "--reveal") {
            reveal = true;
        } else if (arg == "--bot" && i + 1 < argc) {
            botSeats.push_back(std::stoi(argv[++i]));
        } else if ((arg == "--seed" || arg == "--size") && i + 1 < argc) {
            unsigned long long value = std::stoull(argv[++i]);
            if (arg == "--seed") {
                generate.seed = value;
            } else {
                generate.size = static_cast<int>(value);
            }
        } else {
            args.push_back(argv[i]);
        }
    }
    std::string mapFile = args.empty() ? "./src/game/map.txt" : args[0];
    std::vector<TribeTypes::Tribe> tribes;
    for (std::size_t i = 1; i < args.size(); i++) {
        tribes.push_back(parseTribe(args[i]));
    }

    // a map type's name generates a map; anything else is a map file
    const std::array<std::string, MapTypes::MAP_TYPE_SIZE> typeNames = {
        "drylands", "lakes", "pangea", "continents", "archipelago",
        "waterworld"};
    auto typeName =
        std::find(typeNames.begin(), typeNames.end(), lower(mapFile));
    MapData map;
    if (typeName != typeNames.end()) {
        generate.type =
            static_cast<MapTypes::MapType>(typeName - typeNames.begin());
        if (tribes.empty()) {
            tribes = {TribeTypes::IMPERIUS, TribeTypes::BARDUR};
        }
        generate.tribes = tribes;
        map = generateMap(generate);
    } else {
        map = loadMap(mapFile);
    }
    if (tribes.empty()) {
        tribes.assign(map.capitals.size(), TribeTypes::IMPERIUS);
    }
    Game game(map, tribes, {.seed = generate.seed});
    for (int seat : botSeats) {
        if (seat < 0 || seat >= static_cast<int>(tribes.size())) {
            std::cerr << "No seat " << seat << " for a bot\n";
            return 1;
        }
    }
    std::vector<char> isBot(tribes.size(), 0);
    for (int seat : botSeats) {
        isBot[seat] = 1;
    }
    bool allBots = std::all_of(isBot.begin(), isBot.end(),
                               [](char bot) { return bot; });
    RandomBot bot(generate.seed);

    // the current player's view, so hot-seat players only see their own.
    // With only bots playing, everything is shown.
    auto show = [&] {
        PlayerView view =
            makeView(game, game.currentPlayer, reveal || allBots);
        ascii ? printMapAscii(view) : printMap(view);
    };
    // plays every bot turn until a person's turn or the end of the game.
    // A bot's commands are only listed with --reveal, since they'd give away
    // what's in the fog.
    // random games can go on forever, so bot-only games stop at a turn limit
    constexpr int BOT_GAME_TURNS = 300;
    auto runBots = [&] {
        bool played = false;
        while (!game.gameOver() && isBot[game.currentPlayer] &&
               game.turn < BOT_GAME_TURNS) {
            int player = game.currentPlayer;
            std::vector<Command> commands = bot.playTurn(game);
            std::cout << "Player " << player << " (bot) played "
                      << commands.size() << " commands\n";
            if (reveal || allBots) {
                for (const Command &command : commands) {
                    std::cout << "  " << toString(game, command) << '\n';
                }
            }
            played = true;
        }
        return played;
    };

    if (allBots) {
        runBots();
        show();
        if (game.gameOver()) {
            std::cout << "Player " << game.winner << " wins on turn "
                      << game.turn << '\n';
        } else {
            std::cout << "No winner after " << BOT_GAME_TURNS << " turns\n";
        }
        return 0;
    }
    runBots();
    printStatus(game);
    show();
    std::string cmd;
    while (!game.gameOver() && std::cout << "> " << std::flush &&
           std::cin >> cmd) {
        try {
            std::vector<Command> legal = game.legalCommands();
            if (cmd == "l") {
                for (std::size_t i = 0; i < legal.size(); i++) {
                    std::cout << i << ": " << toString(game, legal[i])
                              << '\n';
                }
            } else if (cmd == "c" || cmd == "e") {
                Command command{CommandTypes::ENDTURN};
                if (cmd == "c") {
                    std::size_t n;
                    if (!(std::cin >> n)) {
                        break;
                    }
                    if (n >= legal.size()) {
                        std::cout << "No such command\n";
                        continue;
                    }
                    command = legal[n];
                }
                game.apply(command);
                runBots();
                if (!game.gameOver()) {
                    printStatus(game);
                    show();
                }
            } else if (cmd == "i") {
                int x, y;
                if (!(std::cin >> x >> y)) {
                    break;
                }
                if (!game.inBounds(x, y)) {
                    std::cout << "Off the map\n";
                    continue;
                }
                printTile(game, game.index(x, y), game.currentPlayer, reveal);
            } else if (cmd == "s") {
                printStatus(game);
            } else if (cmd == "p") {
                show();
            } else if (cmd == "k") {
                printKey();
            } else if (cmd == "q") {
                return 0;
            } else {
                std::cout << "Unknown command\n";
            }
        } catch (const std::exception &e) {
            std::cout << e.what() << '\n';
        }
    }
    if (game.gameOver()) {
        std::cout << "Player " << game.winner << " wins\n";
    }
}
