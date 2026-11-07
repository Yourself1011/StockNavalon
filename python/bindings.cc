// Python bindings for the engine (nanobind). Built by scikit-build-core:
// pip install -e .
//
// Kept coarse-grained where it matters: crossing into Python costs about as
// much as a whole game step, so loops over many games run in C++ (with the
// GIL released) rather than in Python.

#include "bots/randomBot.h"
#include "game.h"
#include "legalMoves.h"
#include "mapgen.h"
#include "view.h"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>
#include <sstream>

namespace nb = nanobind;
using namespace nb::literals;

namespace {

// bitsets as lists of the set positions
template <std::size_t N> std::vector<int> setBits(const std::bitset<N> &bits) {
    std::vector<int> out;
    for (std::size_t i = 0; i < N; i++) {
        if (bits.test(i)) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

std::vector<bool> toVector(const std::vector<bool> &v) { return v; }

std::string commandRepr(const Command &c) {
    std::ostringstream out;
    out << "Command(" << CommandTypes::commandNames[c.type] << ", from=" << c.from
        << ", to=" << c.to << ", arg=" << c.arg << ")";
    return out.str();
}

#define V(ns, name) .value(#name, ns::name)

void bindEnums(nb::module_ &m) {
    nb::enum_<TerrainTypes::TerrainType>(m, "TerrainType", nb::is_arithmetic())
        V(TerrainTypes, FIELD) V(TerrainTypes, FOREST) V(TerrainTypes, MOUNTAIN)
        V(TerrainTypes, WATER) V(TerrainTypes, OCEAN);

    nb::enum_<ResourceTypes::ResourceType>(m, "ResourceType",
                                           nb::is_arithmetic())
        V(ResourceTypes, NONE) V(ResourceTypes, FRUIT) V(ResourceTypes, CROP)
        V(ResourceTypes, GAME) V(ResourceTypes, METAL) V(ResourceTypes, FISH)
        V(ResourceTypes, STARFISH);

    nb::enum_<ImprovementTypes::ImprovementType>(m, "ImprovementType",
                                                 nb::is_arithmetic())
        V(ImprovementTypes, NONE) V(ImprovementTypes, CITY)
        V(ImprovementTypes, VILLAGE) V(ImprovementTypes, RUIN)
        V(ImprovementTypes, LIGHTHOUSE) V(ImprovementTypes, FARM)
        V(ImprovementTypes, MINE) V(ImprovementTypes, LUMBERHUT)
        V(ImprovementTypes, WINDMILL) V(ImprovementTypes, SAWMILL)
        V(ImprovementTypes, FORGE) V(ImprovementTypes, MARKET)
        V(ImprovementTypes, PORT) V(ImprovementTypes, TEMPLE)
        V(ImprovementTypes, FORESTTEMPLE) V(ImprovementTypes, MOUNTAINTEMPLE)
        V(ImprovementTypes, WATERTEMPLE) V(ImprovementTypes, ALTAROFPEACE)
        V(ImprovementTypes, EMPERORSTOMB) V(ImprovementTypes, EYEOFGOD)
        V(ImprovementTypes, GATEOFPOWER) V(ImprovementTypes, GRANDBAZAAR)
        V(ImprovementTypes, PARKOFFORTUNE) V(ImprovementTypes, TOWEROFWISDOM)
        V(ImprovementTypes, HARVESTFRUIT) V(ImprovementTypes, HUNTING)
        V(ImprovementTypes, FISHING) V(ImprovementTypes, CLEARFOREST)
        V(ImprovementTypes, BURNFOREST) V(ImprovementTypes, GROWFOREST)
        V(ImprovementTypes, ROAD) V(ImprovementTypes, BRIDGE);

    nb::enum_<UnitTypes::UnitType>(m, "UnitType", nb::is_arithmetic())
        V(UnitTypes, WARRIOR) V(UnitTypes, RIDER) V(UnitTypes, ARCHER)
        V(UnitTypes, DEFENDER) V(UnitTypes, SWORDSMAN) V(UnitTypes, MINDBENDER)
        V(UnitTypes, CATAPULT) V(UnitTypes, KNIGHT) V(UnitTypes, CLOAK)
        V(UnitTypes, DAGGER) V(UnitTypes, GIANT) V(UnitTypes, RAFT)
        V(UnitTypes, SCOUT) V(UnitTypes, RAMMER) V(UnitTypes, BOMBER)
        V(UnitTypes, JUGGERNAUT) V(UnitTypes, DINGHY) V(UnitTypes, PIRATE);

    nb::enum_<TechTypes::TechType>(m, "TechType", nb::is_arithmetic())
        V(TechTypes, BASIC) V(TechTypes, RIDING) V(TechTypes, ROADS)
        V(TechTypes, TRADE) V(TechTypes, FREESPIRIT) V(TechTypes, CHIVALRY)
        V(TechTypes, ORGANIZATION) V(TechTypes, FARMING)
        V(TechTypes, CONSTRUCTION) V(TechTypes, STRATEGY)
        V(TechTypes, DIPLOMACY) V(TechTypes, CLIMBING) V(TechTypes, MEDITATION)
        V(TechTypes, PHILOSOPHY) V(TechTypes, MINING) V(TechTypes, SMITHERY)
        V(TechTypes, FISHING) V(TechTypes, RAMMING) V(TechTypes, AQUATISM)
        V(TechTypes, SAILING) V(TechTypes, NAVIGATION) V(TechTypes, HUNTING)
        V(TechTypes, FORESTRY) V(TechTypes, MATHEMATICS) V(TechTypes, ARCHERY)
        V(TechTypes, SPIRITUALISM);

    nb::enum_<TribeTypes::Tribe>(m, "Tribe", nb::is_arithmetic())
        V(TribeTypes, XINXI) V(TribeTypes, IMPERIUS) V(TribeTypes, BARDUR)
        V(TribeTypes, OUMAJI) V(TribeTypes, KICKOO) V(TribeTypes, HOODRICK)
        V(TribeTypes, LUXIDOOR) V(TribeTypes, VENGIR) V(TribeTypes, ZEBASI)
        V(TribeTypes, AIMO) V(TribeTypes, QUETZALI) V(TribeTypes, YADAKK);

    nb::enum_<CommandTypes::CommandType>(m, "CommandType", nb::is_arithmetic())
        V(CommandTypes, MOVE) V(CommandTypes, ATTACK) V(CommandTypes, RECOVER)
        V(CommandTypes, HEALOTHERS) V(CommandTypes, PROMOTE)
        V(CommandTypes, DISBAND) V(CommandTypes, STAY)
        V(CommandTypes, DISEMBARK) V(CommandTypes, UPGRADE)
        V(CommandTypes, BUILD) V(CommandTypes, DESTROY) V(CommandTypes, HARVEST)
        V(CommandTypes, CAPTURE) V(CommandTypes, EXAMINERUINS)
        V(CommandTypes, TRAIN) V(CommandTypes, RESEARCH)
        V(CommandTypes, CITYREWARD) V(CommandTypes, PEACETREATY)
        V(CommandTypes, PEACEREQUESTRESPONSE) V(CommandTypes, BREAKPEACE)
        V(CommandTypes, ESTABLISHEMBASSY) V(CommandTypes, DESTROYEMBASSY)
        V(CommandTypes, ENDTURN) V(CommandTypes, RESIGN);

    nb::enum_<MapTypes::MapType>(m, "MapType", nb::is_arithmetic())
        V(MapTypes, DRYLANDS) V(MapTypes, LAKES) V(MapTypes, PANGEA)
        V(MapTypes, CONTINENTS) V(MapTypes, ARCHIPELAGO) V(MapTypes, WATERWORLD);

    nb::enum_<CityRewards::CityReward>(m, "CityReward", nb::is_arithmetic())
        V(CityRewards, WORKSHOP) V(CityRewards, EXPLORER)
        V(CityRewards, CITYWALL) V(CityRewards, RESOURCES)
        V(CityRewards, POPULATIONGROWTH) V(CityRewards, BORDERGROWTH)
        V(CityRewards, PARK) V(CityRewards, SUPERUNIT);

    nb::enum_<TaskTypes::Task>(m, "Task", nb::is_arithmetic())
        V(TaskTypes, PACIFIST) V(TaskTypes, WEALTH) V(TaskTypes, EXPLORER)
        V(TaskTypes, KILLER) V(TaskTypes, NETWORK) V(TaskTypes, METROPOLIS)
        V(TaskTypes, GENIUS);

    nb::enum_<WinConditions::WinCondition>(m, "WinCondition",
                                           nb::is_arithmetic())
        V(WinConditions, MIGHT) V(WinConditions, DOMINATION);
}

#undef V

void bindState(nb::module_ &m) {
    nb::class_<Command>(
        m, "Command",
        "One command, as the real game has them. from_tile and to_tile are "
        "tile indices (y * size + x), -1 when unused; arg is a unit type, "
        "improvement type, tech, city reward or player, depending on the "
        "type. See docs/python.md for each type's fields.")
        .def(
            "__init__",
            [](Command *c, CommandTypes::CommandType type, int from, int to,
               int arg) { new (c) Command{type, from, to, arg}; },
            "type"_a, "from_tile"_a = -1, "to_tile"_a = -1, "arg"_a = -1)
        .def_rw("type", &Command::type)
        .def_rw("from_tile", &Command::from)
        .def_rw("to_tile", &Command::to)
        .def_rw("arg", &Command::arg)
        .def("__eq__", [](const Command &a, const Command &b) { return a == b; })
        .def("__hash__",
             [](const Command &c) {
                 return std::hash<long long>{}(
                     (static_cast<long long>(c.type) << 48) ^
                     (static_cast<long long>(c.from + 1) << 32) ^
                     (static_cast<long long>(c.to + 1) << 16) ^ (c.arg + 1));
             })
        .def("__repr__", &commandRepr);

    nb::class_<Tile>(m, "Tile", "A map tile, as the engine sees it (no fog).")
        .def_ro("terrain", &Tile::terrain)
        .def_ro("resource", &Tile::resource)
        .def_ro("building", &Tile::building)
        .def_ro("level", &Tile::level)
        .def_ro("road", &Tile::road)
        .def_ro("unit", &Tile::unit)
        .def_ro("territory", &Tile::territory)
        .def("explored_by", &Tile::exploredBy, "player"_a);

    nb::class_<Unit>(m, "Unit",
                     "A unit, as the engine sees it. HP values are x10.")
        .def_ro("type", &Unit::type)
        .def_ro("passenger", &Unit::passenger)
        .def_ro("owner", &Unit::owner)
        .def_ro("tile", &Unit::tile)
        .def_ro("hp", &Unit::hp, "HP x10")
        .def_ro("max_hp", &Unit::maxHp, "max HP x10")
        .def_ro("home", &Unit::home)
        .def_ro("kills", &Unit::kills)
        .def_ro("veteran", &Unit::veteran)
        .def_ro("alive", &Unit::alive)
        .def_ro("hidden", &Unit::hidden)
        .def_ro("can_move", &Unit::canMove)
        .def_ro("can_attack", &Unit::canAttack)
        .def_ro("can_act", &Unit::canAct);

    nb::class_<City>(m, "City", "A city, as the engine sees it.")
        .def_ro("tile", &City::tile)
        .def_ro("owner", &City::owner)
        .def_ro("level", &City::level)
        .def_ro("population", &City::population)
        .def_ro("capital_of", &City::capitalOf)
        .def_ro("connected_for", &City::connectedFor)
        .def_ro("pending_rewards", &City::pendingRewards)
        .def_ro("parks", &City::parks)
        .def_ro("workshop", &City::workshop)
        .def_ro("wall", &City::wall);

    nb::class_<Player>(m, "Player",
                       "A player's full state, including what's hidden "
                       "from opponents.")
        .def_ro("tribe", &Player::tribe)
        .def_ro("stars", &Player::stars)
        .def_ro("capital", &Player::capital)
        .def_ro("alive", &Player::alive)
        .def_ro("kills", &Player::kills)
        .def_ro("lighthouses_found", &Player::lighthousesFound)
        .def_prop_ro("techs", [](const Player &p) { return setBits(p.techs); })
        .def_prop_ro("tasks_done",
                     [](const Player &p) { return setBits(p.tasksDone); })
        .def_prop_ro("monuments_built",
                     [](const Player &p) { return setBits(p.monumentsBuilt); })
        .def("has_met", &Player::hasMet, "player"_a);
}

void bindMaps(nb::module_ &m) {
    nb::class_<MapData>(m, "MapData",
                        "A map before the game starts: tiles and each "
                        "player's capital tile, in seat order.")
        .def_ro("size", &MapData::size)
        .def_ro("type", &MapData::type)
        .def_ro("tiles", &MapData::tiles)
        .def_ro("capitals", &MapData::capitals);

    m.def("load_map", &loadMap, "path"_a, "Loads a map file (see map.h).");
    m.def(
        "parse_map",
        [](const std::string &text) {
            std::istringstream in(text);
            return parseMap(in);
        },
        "text"_a, "Parses a map from text in the map file format.");
    m.def("ruin_count_for_size", &ruinCountForSize, "size"_a);
    m.def(
        "generate_map",
        [](std::vector<TribeTypes::Tribe> tribes, MapTypes::MapType type,
           int size, std::uint64_t seed, int candidates) {
            return generateMap({.size = size,
                                .type = type,
                                .tribes = std::move(tribes),
                                .seed = seed,
                                .candidates = candidates});
        },
        "tribes"_a, "type"_a = MapTypes::LAKES, "size"_a = 14, "seed"_a = 0,
        "candidates"_a = 10,
        "Generates a map for the tribes, in seat order (GAME.md section 18).");
}

void bindGame(nb::module_ &m) {
    nb::class_<Game>(
        m, "Game",
        "The whole game state. Sees everything, including what's hidden by "
        "fog: bots should decide from make_view(game, player) and use the "
        "Game only to list and apply commands. Copies are independent.")
        .def(
            "__init__",
            [](Game *g, const MapData &map,
               const std::vector<TribeTypes::Tribe> &tribes, std::uint64_t seed,
               WinConditions::WinCondition winCondition) {
                new (g) Game(map, tribes,
                             {.winCondition = winCondition, .seed = seed});
            },
            "map"_a, "tribes"_a, "seed"_a = 0,
            "win_condition"_a = WinConditions::MIGHT)
        .def("copy", [](const Game &g) { return Game(g); },
             "An independent copy, for branching a search.")
        .def("__copy__", [](const Game &g) { return Game(g); })
        .def("__deepcopy__", [](const Game &g, nb::handle) { return Game(g); },
             "memo"_a)

        .def_ro("size", &Game::size)
        .def_ro("map_type", &Game::mapType)
        .def_ro("current_player", &Game::currentPlayer)
        .def_ro("turn", &Game::turn)
        .def_ro("winner", &Game::winner)
        .def_prop_ro("game_over", &Game::gameOver)
        .def_ro("tiles", &Game::tiles)
        .def_prop_ro("units",
                     [](const Game &g) {
                         std::vector<Unit> alive;
                         for (const Unit &u : g.units) {
                             if (u.alive) {
                                 alive.push_back(u);
                             }
                         }
                         return alive;
                     },
                     "The units that are alive.")
        .def_ro("cities", &Game::cities)
        .def_ro("players", &Game::players)

        .def("index", &Game::index, "x"_a, "y"_a)
        .def("tile_x", &Game::tileX, "tile"_a)
        .def("tile_y", &Game::tileY, "tile"_a)
        .def("distance", &Game::distance, "a"_a, "b"_a)
        .def("tile_owner", &Game::tileOwner, "tile"_a)
        .def("score", &Game::score, "player"_a)
        .def("income", &Game::income, "player"_a)
        .def("tech_cost", &Game::techCost, "player"_a, "tech"_a)

        .def("legal_commands", &Game::legalCommands,
             "Every legal command for the current player, except Resign.")
        .def("is_legal", &Game::isLegal, "command"_a)
        .def("apply", &Game::apply, "command"_a,
             "Applies the command; raises ValueError if it isn't legal.")
        .def("apply_unchecked", &Game::applyUnchecked, "command"_a,
             "Applies a command taken from legal_commands(), without checking.")
        .def("describe", [](const Game &g, const Command &c) {
            return toString(g, c);
        }, "command"_a, "The command in words, e.g. 'move (3,4) -> (4,5)'.");
}

void bindView(nb::module_ &m) {
    nb::class_<PlayerView> view(
        m, "PlayerView",
        "What one player knows: explored tiles with their live contents, the "
        "units they can see, their own stars, techs, tasks and diplomacy. "
        "Unexplored tiles have explored=False and nothing else filled in.");

    nb::class_<PlayerView::TileView>(view, "TileView")
        .def_ro("explored", &PlayerView::TileView::explored)
        .def_ro("terrain", &PlayerView::TileView::terrain)
        .def_ro("resource", &PlayerView::TileView::resource)
        .def_ro("building", &PlayerView::TileView::building)
        .def_ro("level", &PlayerView::TileView::level)
        .def_ro("road", &PlayerView::TileView::road)
        .def_ro("owner", &PlayerView::TileView::owner)
        .def_ro("unit", &PlayerView::TileView::unit)
        .def_ro("city", &PlayerView::TileView::city);

    nb::class_<PlayerView::UnitView>(view, "UnitView")
        .def_ro("type", &PlayerView::UnitView::type)
        .def_ro("passenger", &PlayerView::UnitView::passenger)
        .def_ro("owner", &PlayerView::UnitView::owner)
        .def_ro("tile", &PlayerView::UnitView::tile)
        .def_ro("hp", &PlayerView::UnitView::hp)
        .def_ro("max_hp", &PlayerView::UnitView::maxHp)
        .def_ro("kills", &PlayerView::UnitView::kills)
        .def_ro("veteran", &PlayerView::UnitView::veteran)
        .def_ro("hidden", &PlayerView::UnitView::hidden)
        .def_ro("can_move", &PlayerView::UnitView::canMove)
        .def_ro("can_attack", &PlayerView::UnitView::canAttack)
        .def_ro("can_act", &PlayerView::UnitView::canAct)
        .def_ro("hidden_enemy_near", &PlayerView::UnitView::hiddenEnemyNear);

    nb::class_<PlayerView::CityView>(view, "CityView")
        .def_ro("tile", &PlayerView::CityView::tile)
        .def_ro("owner", &PlayerView::CityView::owner)
        .def_ro("level", &PlayerView::CityView::level)
        .def_ro("population", &PlayerView::CityView::population)
        .def_ro("capital_of", &PlayerView::CityView::capitalOf)
        .def_ro("wall", &PlayerView::CityView::wall);

    nb::class_<PlayerView::Relation>(view, "Relation")
        .def_ro("peace", &PlayerView::Relation::peace)
        .def_ro("they_asked", &PlayerView::Relation::theyAsked)
        .def_ro("we_asked", &PlayerView::Relation::weAsked)
        .def_ro("our_embassy", &PlayerView::Relation::ourEmbassy)
        .def_ro("their_embassy", &PlayerView::Relation::theirEmbassy);

    view.def_ro("player", &PlayerView::player)
        .def_ro("size", &PlayerView::size)
        .def_ro("turn", &PlayerView::turn)
        .def_ro("current_player", &PlayerView::currentPlayer)
        .def_ro("winner", &PlayerView::winner)
        .def_ro("stars", &PlayerView::stars)
        .def_ro("income", &PlayerView::income)
        .def_prop_ro("techs", [](const PlayerView &v) { return setBits(v.techs); })
        .def_ro("tribes", &PlayerView::tribes)
        .def_prop_ro("alive", [](const PlayerView &v) { return toVector(v.alive); })
        .def_prop_ro("met", [](const PlayerView &v) { return toVector(v.met); })
        .def_ro("scores", &PlayerView::scores)
        .def_prop_ro("tasks_done",
                     [](const PlayerView &v) { return setBits(v.tasksDone); })
        .def_prop_ro("monuments_built",
                     [](const PlayerView &v) { return setBits(v.monumentsBuilt); })
        .def_ro("kills", &PlayerView::kills)
        .def_ro("peaceful_turns", &PlayerView::peacefulTurns)
        .def_ro("lighthouses_found", &PlayerView::lighthousesFound)
        .def_ro("relations", &PlayerView::relations)
        .def_ro("tiles", &PlayerView::tiles)
        .def_ro("units", &PlayerView::units)
        .def_ro("cities", &PlayerView::cities);

    m.def("make_view", &makeView, "game"_a, "player"_a, "everything"_a = false,
          "What the player knows about the game. everything=True ignores fog.");
}

void bindSearch(nb::module_ &m) {
    nb::class_<LegalMoves>(
        m, "LegalMoves",
        "The current player's legal commands, updated one command at a time "
        "instead of regenerated. For search: keep one next to each Game, and "
        "apply commands through it. Always matches game.legal_commands() as a "
        "set, though not in the same order.")
        .def(nb::init<const Game &>(), "game"_a)
        .def("copy", [](const LegalMoves &l) { return LegalMoves(l); })
        .def("__copy__", [](const LegalMoves &l) { return LegalMoves(l); })
        .def_prop_ro("commands", &LegalMoves::commands)
        .def("apply", &LegalMoves::apply, "game"_a, "command"_a,
             "Applies the command to the game and updates the list.")
        .def("update", &LegalMoves::update, "before"_a, "after"_a,
             "Updates the list for a game that went from before to after.");

    nb::class_<RandomBot>(
        m, "RandomBot",
        "Plays random legal commands: a random command type, then a random "
        "command of that type.")
        .def(nb::init<std::uint64_t>(), "seed"_a = 0)
        .def("choose", &RandomBot::choose, "game"_a)
        .def("play_turn", &RandomBot::playTurn, "game"_a,
             "max_commands"_a = 200);

    m.def(
        "play_random_games",
        [](int games, std::vector<TribeTypes::Tribe> tribes,
           MapTypes::MapType type, int size, std::uint64_t seed, int maxTurns) {
            // the whole loop in C++, without the GIL
            nb::gil_scoped_release release;
            std::vector<std::tuple<int, int>> results;
            for (int i = 0; i < games; i++) {
                std::uint64_t gameSeed = seed + static_cast<std::uint64_t>(i);
                Game game(generateMap({.size = size,
                                       .type = type,
                                       .tribes = tribes,
                                       .seed = gameSeed}),
                          tribes, {.seed = gameSeed});
                RandomBot bot(gameSeed);
                while (!game.gameOver() && game.turn < maxTurns) {
                    bot.playTurn(game);
                }
                results.emplace_back(game.winner, game.turn);
            }
            return results;
        },
        "games"_a, "tribes"_a, "type"_a = MapTypes::LAKES, "size"_a = 14,
        "seed"_a = 0, "max_turns"_a = 100,
        "Plays random games on generated maps. Returns (winner, turns) for "
        "each; winner is -1 if the turn limit came first.");
}

} // namespace

NB_MODULE(_engine, m) {
    m.doc() = "The stockNavalon Polytopia engine";
    bindEnums(m);
    bindState(m);
    bindMaps(m);
    bindGame(m);
    bindView(m);
    bindSearch(m);
}
