// Python bindings for the engine (nanobind). Built by scikit-build-core:
// pip install -e .
//
// Kept coarse-grained where it matters: crossing into Python costs about as
// much as a whole game step, so loops over many games run in C++ (with the
// GIL released) rather than in Python.

#include "bots/randomBot.h"
#include "cli.h"
#include "encoding.h"
#include "game.h"
#include "legalMoves.h"
#include "mapgen.h"
#include "record.h"
#include "selfPlay.h"
#include "profile.h"
#include "tribeInference.h"
#include "view.h"
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
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

// a new NumPy array of the given shape, zeroed, owned by Python
template <typename T>
nb::ndarray<nb::numpy, T> newArray(std::initializer_list<std::size_t> shape) {
    std::size_t count = 1;
    for (std::size_t s : shape) {
        count *= s;
    }
    T *data = new T[count]();
    nb::capsule owner(data, [](void *p) noexcept { delete[] static_cast<T *>(p); });
    return nb::ndarray<nb::numpy, T>(data, shape, owner);
}

// An evaluator that hands each batch to a Python function, the network:
// evaluate(planes [B, C, N, N], globals [B, G], rows [K], slots [K],
// belief_rows [R]) returns (logits[rows, slots] [K], values [B]), plus the
// raw belief outputs of the belief rows [R, beliefOutputCount] when R > 0, as
// with SelfPlaySession's pending_slots, pending_belief_rows and
// submit_move_logits. Called with the GIL held.
class PythonEvaluator : public Evaluator {
  public:
    PythonEvaluator(const EncodingConfig &config, nb::callable evaluate)
        : config(config), function(std::move(evaluate)) {}

    void evaluate(const std::vector<EvalRequest> &requests,
                  std::vector<Evaluation> &results) override {
        using Floats = nb::ndarray<const float, nb::c_contig, nb::device::cpu>;
        std::size_t b = requests.size(), n = config.mapSize;
        auto planes = newArray<float>({b, static_cast<std::size_t>(planeCount(config)), n, n});
        auto globals = newArray<float>({b, static_cast<std::size_t>(globalCount(config))});
        encodeRequests(requests, config, planes.data(), globals.data(), nullptr);
        std::vector<int> rows, slots;
        requestSlots(requests, config, rows, slots);
        auto r = newArray<std::int64_t>({rows.size()});
        auto k = newArray<std::int64_t>({slots.size()});
        for (std::size_t i = 0; i < rows.size(); i++) {
            r.data()[i] = rows[i];
            // -1 (no slot) reads slot 0; its logit is ignored
            k.data()[i] = std::max(0, slots[i]);
        }
        std::vector<std::int64_t> wanting;
        for (std::size_t i = 0; i < b; i++) {
            if (requests[i].wantsBeliefs) {
                wanting.push_back(static_cast<std::int64_t>(i));
            }
        }
        auto beliefRows = newArray<std::int64_t>({wanting.size()});
        std::copy(wanting.begin(), wanting.end(), beliefRows.data());
        auto out = nb::cast<nb::tuple>(function(planes, globals, r, k, beliefRows));
        auto logits = nb::cast<Floats>(out[0]);
        auto values = nb::cast<Floats>(out[1]);
        if (logits.size() != slots.size() || values.size() != b) {
            throw std::invalid_argument(
                "evaluate must return one logit per move and one value per position");
        }
        // an optional fourth output: tie-breaks [B, 2]
        std::optional<Floats> tieBreaks;
        if (out.size() >= 4 && !out[3].is_none()) {
            tieBreaks = nb::cast<Floats>(out[3]);
            if (tieBreaks->size() != 2 * b) {
                throw std::invalid_argument("evaluate's tie-breaks must be [B, 2]");
            }
        }
        evaluationsFromMoveLogits(requests, slots, logits.data(), values.data(), results,
                                  tieBreaks ? tieBreaks->data() : nullptr);
        if (!wanting.empty()) {
            if (out.size() < 3) {
                throw std::invalid_argument("evaluate must return beliefs for belief_rows");
            }
            auto beliefs = nb::cast<Floats>(out[2]);
            if (beliefs.size() != wanting.size() * beliefOutputCount(config)) {
                throw std::invalid_argument(
                    "evaluate must return [R, belief_output_count] beliefs");
            }
            attachBeliefs(requests, config, beliefs.data(), results);
        }
    }

  private:
    EncodingConfig config;
    nb::callable function;
};

// The training example arrays (ExampleArrays) as a dict of NumPy arrays,
// filled by `fill` with the GIL released
// annotated in the stubs as a dict of arrays by name
using ExampleDict = nb::typed<nb::dict, nb::str, nb::ndarray<nb::numpy>>;

template <typename Fill>
ExampleDict exampleDict(const EncodingConfig &c, std::size_t b, Fill fill) {
    std::size_t n = c.mapSize, a = actionCount(c), t = targetCount(c);
    auto planes = newArray<float>({b, static_cast<std::size_t>(planeCount(c)), n, n});
    auto globals = newArray<float>({b, static_cast<std::size_t>(globalCount(c))});
    auto legal = newArray<std::uint8_t>({b, a});
    auto policy = newArray<float>({b, a});
    auto value = newArray<float>({b});
    auto valueWeight = newArray<float>({b});
    auto targets = newArray<float>({b, t});
    auto masks = newArray<std::uint8_t>({b, t});
    std::size_t side = sideTargetCount(c);
    auto sideTargets = newArray<float>({b, side});
    auto sideMasks = newArray<std::uint8_t>({b, side});
    auto turnPositions = newArray<float>({b});
    ExampleArrays out{planes.data(),      globals.data(),     legal.data(),
                      policy.data(),      value.data(),       valueWeight.data(),
                      targets.data(),     masks.data(),       sideTargets.data(),
                      sideMasks.data(),   turnPositions.data()};
    {
        nb::gil_scoped_release release;
        fill(out);
    }
    ExampleDict d;
    d["planes"] = planes;
    d["globals"] = globals;
    d["legal"] = legal;
    d["policy"] = policy;
    d["value"] = value;
    d["value_weight"] = valueWeight;
    d["targets"] = targets;
    d["target_masks"] = masks;
    d["side_targets"] = sideTargets;
    d["side_masks"] = sideMasks;
    d["turn_positions"] = turnPositions;
    return d;
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
        V(ResourceTypes, STARFISH) V(ResourceTypes, AQUACROP);

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

    nb::enum_<Game::RuinReward>(m, "RuinReward", nb::is_arithmetic())
        .value("TECH", Game::RUIN_TECH).value("STARS", Game::RUIN_STARS)
        .value("POPULATION", Game::RUIN_POPULATION)
        .value("EXPLORER", Game::RUIN_EXPLORER).value("UNIT", Game::RUIN_UNIT);

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
        .def("__eq__", [](const Command &a, nb::handle other) {
            // anything can be compared; only an equal Command is equal
            Command b;
            return nb::try_cast(other, b) && a == b;
        }, "other"_a)
        .def("__hash__",
             [](const Command &c) {
                 return std::hash<long long>{}(
                     (static_cast<long long>(c.type) << 48) ^
                     (static_cast<long long>(c.from + 1) << 32) ^
                     (static_cast<long long>(c.to + 1) << 16) ^ (c.arg + 1));
             })
        .def("__repr__", &commandRepr);

    nb::class_<ScoreRecord>(
        m, "ScoreRecord",
        "A score as other players saw it: published when its player ended a "
        "turn (or the starting score).")
        .def_ro("player", &ScoreRecord::player)
        .def_ro("turn", &ScoreRecord::turn, "the turn that had just ended")
        .def_ro("score", &ScoreRecord::score)
        .def("__eq__",
             [](const ScoreRecord &a, nb::handle other) {
                 ScoreRecord b;
                 return nb::try_cast(other, b) && a == b;
             }, "other"_a)
        .def("__repr__", [](const ScoreRecord &r) {
            return "ScoreRecord(player=" + std::to_string(r.player) +
                   ", turn=" + std::to_string(r.turn) +
                   ", score=" + std::to_string(r.score) + ")";
        });

    nb::class_<Tile>(m, "Tile", "A map tile, as the engine sees it (no fog).")
        .def_ro("terrain", &Tile::terrain)
        .def_ro("resource", &Tile::resource)
        .def_ro("building", &Tile::building)
        .def_ro("level", &Tile::level)
        .def_ro("road", &Tile::road)
        .def_ro("road_builder", &Tile::roadBuilder,
                "The player who built the road, -1 for one the map started with.")
        .def_ro("unit", &Tile::unit)
        .def_ro("territory", &Tile::territory)
        .def_ro("climate", &Tile::climate,
                "The tribe whose climate region the tile is in (as an int), "
                "-1 if none")
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
        .def_ro("can_act", &Unit::canAct)
        .def_ro("facing", &Unit::facing, "The way it faces, as the game numbers directions: SW, W, NW, N, NE, E, SE, S (pushes go that way).");

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
        .def("has_met", &Player::hasMet, "player"_a)
        .def("has_embassy_with", &Player::hasEmbassyWith, "player"_a,
             "Whether the player has an embassy in that player's capital.")
        .def("cooling_down_with", [](const Player &p, int other) { return (p.coolDown >> other & 1) != 0; },
             "other"_a, "Whether peace with them was broken and the player can't attack them yet.");
}

void bindMaps(nb::module_ &m) {
    nb::class_<MapData>(m, "MapData",
                        "A map before the game starts: tiles and each "
                        "player's capital tile, in seat order.")
        .def_ro("size", &MapData::size)
        .def_ro("type", &MapData::type)
        .def_ro("tiles", &MapData::tiles)
        .def_ro("capitals", &MapData::capitals)
        .def_ro("regions", &MapData::regions,
                "The seat whose climate region each tile is in; empty if the "
                "map doesn't say");

    m.def("load_map", &loadMap, "path"_a, "Loads a map file (see map.h).");
    m.def(
        "parse_map",
        [](const std::string &text) {
            std::istringstream in(text);
            return parseMap(in);
        },
        "text"_a, "Parses a map from text in the map file format.");
    m.def("ruin_count_for_size", &ruinCountForSize, "size"_a);
    // the map file format's characters (map.h), by TerrainType and
    // ResourceType value
    m.attr("TERRAIN_CHARS") = std::string(TerrainTypes::terrainChars.begin(),
                                          TerrainTypes::terrainChars.end());
    m.attr("RESOURCE_CHARS") = std::string(ResourceTypes::resourceChars.begin(),
                                           ResourceTypes::resourceChars.end());
    m.def("start_inequality", &startInequality, "map"_a,
          "How unequal the players' starts are, as the real game scores it "
          "(GAME.md section 18): the best territory's score over the worst's.");
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
        "candidates"_a = MapGenSettings{}.candidates,
        "Generates a map for the tribes, in seat order (GAME.md section 18): up "
        "to `candidates` of them, keeping the fairest.");
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
               WinConditions::WinCondition winCondition, const std::vector<int> &capitalIncome) {
                new (g) Game(map, tribes,
                             {.winCondition = winCondition, .seed = seed,
                              .capitalIncome = capitalIncome});
            },
            "map"_a, "tribes"_a, "seed"_a = 0,
            "win_condition"_a = WinConditions::MIGHT, "capital_income"_a = std::vector<int>{},
            "capital_income: each seat's capital income bonus (the game's handicap: "
            "1 for people, 4 for Crazy bots); empty for 1 each.")
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
        .def("score", &Game::score, "player"_a,
             "The live score, which only its own player sees.")
        .def("published_score", &Game::publishedScore, "player"_a,
             "The score other players see: from the end of the player's last "
             "turn.")
        .def_ro("score_history", &Game::scoreHistory,
                "Every published score so far, oldest first.")
        .def("income", &Game::income, "player"_a)
        .def("city_income", &Game::cityIncome, "city"_a, "What the city (an index into cities) earns a turn.")
        .def("at_peace", &Game::atPeace, "a"_a, "b"_a)
        .def("embassy_income", &Game::embassyIncome, "player"_a)
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
        }, "command"_a, "The command in words, e.g. 'move (3,4) -> (4,5)'.")
        // random outcomes fixed by the caller, to follow a game played
        // elsewhere (src/oracle/mirror.py)
        .def("force_ruin", [](Game &g, Game::RuinReward reward, int tech) {
            g.forced.ruinReward = reward;
            g.forced.ruinTech = tech;
        }, "reward"_a, "tech"_a = -1,
           "The next ruin gives this reward (and, for a tech, this tech) instead of a random one.")
        .def("force_explorer", [](Game &g, std::vector<int> tiles) {
            g.forced.explored = std::move(tiles);
        }, "tiles"_a, "The next Explorer reveals exactly these tiles instead of walking.")
        .def("force_reveal", [](Game &g, int player, std::vector<int> tiles) {
            for (int t : tiles) {
                g.reveal(player, t);
            }
            // what the command that finished the walk would have checked
            g.updateTasks();
        }, "player"_a, "tiles"_a,
           "Explores these tiles for the player, as the rest of an outcome fixed earlier (an Explorer the game finished later), and checks tasks again.")
        // setting up a position (src/oracle/scenarios.py)
        .def("set_stars", [](Game &g, int player, int stars) { g.players[player].stars = stars; },
             "player"_a, "stars"_a)
        .def("give_tech", [](Game &g, int player, TechTypes::TechType tech) {
            g.players[player].techs.set(tech);
        }, "player"_a, "tech"_a)
        .def("spawn_unit", [](Game &g, int player, UnitTypes::UnitType type, int tile) {
            int unit = g.spawnUnit(player, type, tile, -1);
            Unit &u = g.units[unit];
            u.canMove = u.canAttack = u.canAct = false;
            return unit;
        }, "player"_a, "type"_a, "tile"_a,
           "A unit of no city, as if trained there: it can't act until a later turn.")
        .def("set_unit", [](Game &g, int unit, UnitTypes::UnitType passenger, int hp) {
            Unit &u = g.units[unit];
            u.passenger = passenger;
            u.maxHp = std::max(u.maxHp, unitInfo(passenger).maxHp);
            u.hp = hp;
        }, "unit"_a, "passenger"_a, "hp"_a, "Sets what a boat carries and the unit's HP (x10).")
        .def("found_city", [](Game &g, int tile, int owner) {
            int city = g.foundCity(tile, owner);
            g.updateAllConnections();
            return city;
        }, "tile"_a, "owner"_a, "Makes the village there the player's city, as a capture does.")
        .def("set_unit_ready", [](Game &g, int unit) {
            Unit &u = g.units[unit];
            u.canMove = u.canAttack = u.canAct = true;
            u.attacked = false;
        }, "unit"_a)
        .def("set_met", [](Game &g, int player, int other, bool met) {
            std::uint32_t bit = std::uint32_t{1} << other;
            g.players[player].met = met ? g.players[player].met | bit : g.players[player].met & ~bit;
        }, "player"_a, "other"_a, "met"_a)
        .def("place_building", &Game::placeBuilding, "player"_a, "tile"_a, "type"_a,
             "Builds as the player would, without paying.")
        .def("add_population", [](Game &g, int tile, int amount) {
            g.addPopulation(g.cityAt(tile), amount);
        }, "city_tile"_a, "amount"_a)
        .def("set_unit_hp", [](Game &g, int tile, int hp) { g.units[g.tiles[tile].unit].hp = hp; },
             "tile"_a, "hp"_a)
        .def("set_tile", [](Game &g, int tile, TerrainTypes::TerrainType terrain,
                            ResourceTypes::ResourceType resource) {
            g.tiles[tile].terrain = terrain;
            g.tiles[tile].resource = resource;
        }, "tile"_a, "terrain"_a, "resource"_a)
        .def("add_embassy", [](Game &g, int builder, int host) {
            g.players[builder].embassies |= std::uint32_t{1} << host;
        }, "builder"_a, "host"_a)
        .def("set_city_level", [](Game &g, int tile, int level) { g.cities[g.cityAt(tile)].level = level; },
             "city_tile"_a, "level"_a)
        .def("make_peace", [](Game &g, int a, int b) {
            g.players[a].peace |= std::uint32_t{1} << b;
            g.players[b].peace |= std::uint32_t{1} << a;
            g.updateAllConnections();
        }, "a"_a, "b"_a)
        .def("force_converted_home", [](Game &g, int tile) {
            g.forced.convertedHome = tile;
        }, "tile"_a, "The next converted unit belongs to the city on this tile instead of a random one.")
        .def("force_daggers", [](Game &g, std::vector<std::pair<int, int>> daggers) {
            g.forced.daggers = std::move(daggers);
        }, "daggers"_a,
           "The next infiltration's Daggers, as (tile, facing) each, instead of their random order.");
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
        .def_ro("level", &PlayerView::TileView::level,
                "Windmill, Sawmill or Forge level, or a temple's level")
        .def_ro("turns_to_level_up", &PlayerView::TileView::turnsToLevelUp,
                "Temples: turns until the next level, 0 at the top level")
        .def_ro("climate", &PlayerView::TileView::climate,
                "The tribe whose climate the tile is in")
        .def_ro("road", &PlayerView::TileView::road)
        .def_ro("own_road", &PlayerView::TileView::ownRoad,
                "A road this player built themselves.")
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
        .def_ro("wall", &PlayerView::CityView::wall)
        .def_ro("parks", &PlayerView::CityView::parks);

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
        .def_ro("tribes", &PlayerView::tribes,
                "Each player's tribe: your own and those you've met; None "
                "until then. Starting scores (the first records in "
                "score_history) narrow an unmet tribe down.")
        .def_prop_ro("alive", [](const PlayerView &v) { return toVector(v.alive); })
        .def_prop_ro("met", [](const PlayerView &v) { return toVector(v.met); })
        .def_ro("scores", &PlayerView::scores,
                "Every player's score, met or not, as this player sees it: "
                "their own live, everyone else's from the end of their last "
                "turn.")
        .def_ro("score_history", &PlayerView::scoreHistory,
                "Every published score so far (public), oldest first.")
        .def_ro("effects_since_published", &PlayerView::effectsSincePublished,
                "Per player: what this player's own actions visibly did to "
                "their score since they last published (negative for losses).")
        .def_ro("effects_in_last_change", &PlayerView::effectsInLastChange,
                "Per player: the same, within the change they last published.")
        .def_ro("effects_in_changes", &PlayerView::effectsInChanges,
                "Per score_history record: what this player's own actions "
                "visibly did to the publisher's score within that change.")
        .def_ro("converted_away", &PlayerView::convertedAway,
                "This player's units taken by conversion: (type, passenger).")
        .def_ro("attacked_on_turns", &PlayerView::attackedOnTurns,
                "The turns this player's units or cities were attacked.")
        .def_ro("first_occupied", &PlayerView::firstOccupied,
                "Per tile: the first turn one of this player's units or "
                "cities stood on it, -1 for never.")
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

SideKind sideKind(const std::string &kind) {
    if (kind == "network") {
        return SideKind::NETWORK;
    }
    if (kind == "score") {
        return SideKind::SCORE;
    }
    if (kind == "random") {
        return SideKind::RANDOM;
    }
    throw std::invalid_argument("A side is network, score or random");
}

std::string sideName(SideKind kind) {
    return kind == SideKind::NETWORK ? "network" : kind == SideKind::SCORE ? "score" : "random";
}

// The settings objects every search and self-play entry point takes, their
// fields and defaults the C++ structs' (one place for each setting)
void bindConfigs(nb::module_ &m) {
    nb::enum_<SearchConfig::Replies>(m, "Replies",
                                     "Turn search: how the opponent's replies are drawn "
                                     "(search.h, SearchConfig::replies).")
        .value("SAMPLED", SearchConfig::Replies::SAMPLED)
        .value("LIKELY_FIRST", SearchConfig::Replies::LIKELY_FIRST)
        .value("DISCREPANCY", SearchConfig::Replies::DISCREPANCY);
    nb::enum_<SearchConfig::Backup>(m, "Backup",
                                    "How a node's value comes from its children's "
                                    "(search.h, SearchConfig::backup).")
        .value("MEAN", SearchConfig::Backup::MEAN)
        .value("PBAR", SearchConfig::Backup::PBAR)
        .value("PBAR_OPPONENT", SearchConfig::Backup::PBAR_OPPONENT);
    nb::class_<SearchConfig>(m, "SearchConfig",
                             "A search's settings (search.h, SearchConfig), defaults "
                             "as there. polyengine.search_config(**fields) makes one.")
        .def(nb::init<>())
        .def("copy", [](const SearchConfig &c) { return SearchConfig(c); })
        .def("__copy__", [](const SearchConfig &c) { return SearchConfig(c); })
        .def_rw("simulations", &SearchConfig::simulations,
                "per determinized game (network evaluations with count_evaluations)")
        .def_rw("samples", &SearchConfig::samples, "determinized games per decision")
        .def_rw("exploration", &SearchConfig::exploration)
        .def_rw("fpu_reduction", &SearchConfig::fpuReduction)
        .def_rw("first_play_urgency", &SearchConfig::firstPlayUrgency)
        .def_rw("horizon", &SearchConfig::horizon,
                "turn changes a line may cross (UNLIMITED_HORIZON: no cutoff)")
        .def_rw("root_noise", &SearchConfig::rootNoise)
        .def_rw("noise_alpha", &SearchConfig::noiseAlpha)
        .def_rw("noise_weight", &SearchConfig::noiseWeight)
        .def_rw("determinize_tries", &SearchConfig::determinizeTries)
        .def_rw("full_information", &SearchConfig::fullInformation)
        .def_rw("gumbel", &SearchConfig::gumbel)
        .def_rw("gumbel_candidates", &SearchConfig::gumbelCandidates)
        .def_rw("gumbel_visit_scale", &SearchConfig::gumbelVisitScale)
        .def_rw("gumbel_value_scale", &SearchConfig::gumbelValueScale)
        .def_rw("explore_candidates", &SearchConfig::exploreCandidates)
        .def_rw("score_utility", &SearchConfig::scoreUtility)
        .def_rw("win_sooner", &SearchConfig::winSooner)
        .def_rw("turn_search", &SearchConfig::turnSearch)
        .def_rw("command_visits", &SearchConfig::commandVisits)
        .def_rw("turn_widening", &SearchConfig::turnWidening)
        .def_rw("turn_widening_power", &SearchConfig::turnWideningPower)
        .def_rw("turn_temperature", &SearchConfig::turnTemperature)
        .def_rw("max_turn_commands", &SearchConfig::maxTurnCommands)
        .def_rw("count_evaluations", &SearchConfig::countEvaluations)
        .def_rw("replies", &SearchConfig::replies,
                "turn search: how the opponent's replies are drawn (Replies)")
        .def_rw("backup", &SearchConfig::backup,
                "how a node's value comes from its children's (Backup)");
    m.def("regularized_policy", &regularizedPolicy, "prior"_a, "q"_a, "lambda_"_a,
          "The policy PUCT heads toward (search.h, regularizedPolicy).");
    nb::class_<SelfPlayConfig>(m, "SelfPlayConfig",
                               "Self-play's or a match's settings (selfPlay.h, "
                               "SelfPlayConfig), defaults as there. "
                               "polyengine.self_play_config(**fields) makes one.")
        .def(nb::init<>())
        .def("copy", [](const SelfPlayConfig &c) { return SelfPlayConfig(c); })
        .def("__copy__", [](const SelfPlayConfig &c) { return SelfPlayConfig(c); })
        .def_rw("search", &SelfPlayConfig::search)
        .def_prop_rw(
            "side_search",
            [](const SelfPlayConfig &c) {
                return std::vector<std::optional<SearchConfig>>(c.sideSearch.begin(),
                                                                c.sideSearch.end());
            },
            [](SelfPlayConfig &c, const std::vector<std::optional<SearchConfig>> &v) {
                if (!v.empty() && v.size() != 2) {
                    throw std::invalid_argument("side_search: one per side, or none");
                }
                c.sideSearch = {v.empty() ? std::nullopt : v[0],
                                v.empty() ? std::nullopt : v[1]};
            },
            "per side, a whole search instead of `search` (None: `search`)")
        .def_rw("map_size", &SelfPlayConfig::mapSize)
        .def_rw("max_turns", &SelfPlayConfig::maxTurns)
        .def_rw("stall_turns", &SelfPlayConfig::stallTurns)
        .def_rw("fog_share", &SelfPlayConfig::fogShare)
        .def_rw("peace_treaties", &SelfPlayConfig::peaceTreaties)
        .def_rw("tree_reuse", &SelfPlayConfig::treeReuse)
        .def_rw("network_beliefs", &SelfPlayConfig::networkBeliefs)
        .def_rw("temperature_turns", &SelfPlayConfig::temperatureTurns)
        .def_rw("full_share", &SelfPlayConfig::fullShare)
        .def_rw("quick_simulations", &SelfPlayConfig::quickSimulations)
        .def_rw("threads", &SelfPlayConfig::threads)
        .def_prop_rw(
            "sides",
            [](const SelfPlayConfig &c) {
                return std::vector<std::string>{sideName(c.sides[0]), sideName(c.sides[1])};
            },
            [](SelfPlayConfig &c, const std::vector<std::string> &v) {
                if (v.size() != 2) {
                    throw std::invalid_argument("Need two sides");
                }
                c.sides = {sideKind(v[0]), sideKind(v[1])};
            },
            "who plays each side: network, score or random")
        .def_prop_rw(
            "side_plans",
            [](const SelfPlayConfig &c) {
                return std::vector<std::string>{planName(c.sidePlans[0]), planName(c.sidePlans[1])};
            },
            [](SelfPlayConfig &c, const std::vector<std::string> &v) {
                if (v.size() != 2) {
                    throw std::invalid_argument("Need two sides");
                }
                c.sidePlans = {planNamed(v[0]), planNamed(v[1])};
            },
            "each side's forced plan (plans.h): none, bridge, naval or convert")
        .def_rw("plan_turns", &SelfPlayConfig::planTurns,
                "turns a forced plan lasts, from its start")
        .def_rw("plan_when_ahead", &SelfPlayConfig::planWhenAhead,
                "NaN: plans from the start; else from a side's first decision with "
                "its own search value at least this")
        .def_rw("map_types", &SelfPlayConfig::mapTypes,
                "the map types games are drawn from, evenly; empty for every type")
        .def_rw("paired", &SelfPlayConfig::paired)
        .def_rw("random_seats", &SelfPlayConfig::randomSeats)
        .def_prop_rw(
            "train_sides",
            [](const SelfPlayConfig &c) {
                return std::vector<bool>{c.trainSides[0], c.trainSides[1]};
            },
            [](SelfPlayConfig &c, const std::vector<bool> &v) {
                if (v.size() != 2) {
                    throw std::invalid_argument("train_sides: one flag per side");
                }
                c.trainSides = {v[0], v[1]};
            })
        .def_rw("handicap_share", &SelfPlayConfig::handicapShare)
        .def_rw("handicap_income_share", &SelfPlayConfig::handicapIncomeShare)
        .def_rw("handicap_income", &SelfPlayConfig::handicapIncome)
        .def_rw("handicap_stars", &SelfPlayConfig::handicapStars)
        .def_rw("handicap_catch_up_share", &SelfPlayConfig::handicapCatchUpShare)
        .def_rw("catch_up_from_turn", &SelfPlayConfig::catchUpFromTurn)
        .def_rw("catch_up_below", &SelfPlayConfig::catchUpBelow)
        .def_rw("trailing_search_factor", &SelfPlayConfig::trailingSearchFactor)
        .def_rw("explore_candidates", &SelfPlayConfig::exploreCandidates)
        .def_rw("failures_path", &SelfPlayConfig::failuresPath);
}

void bindSearch(nb::module_ &m) {
    bindConfigs(m);
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

    nb::class_<GameRecord>(m, "GameRecord",
                           "A recorded game: its setup and commands, replayed "
                           "to rebuild any position (docs/stockNavalon.md "
                           "section 5).")
        .def_prop_ro("map_size", [](const GameRecord &r) { return r.map.size; })
        .def_prop_ro("map_type", [](const GameRecord &r) { return r.map.type; })
        .def_prop_ro("tribes", [](const GameRecord &r) { return r.map.tribes; })
        .def_ro("game_seed", &GameRecord::gameSeed,
                "The game's random seed: identifies it among the others.")
        .def_rw("rules_version", &GameRecord::rulesVersion,
                "The rules the game was played by (GameSettings::rulesVersion; "
                "RULES_VERSION is the latest): older records replay by theirs.")
        .def_ro("head_start", &GameRecord::headStart,
                "Each player's head start in stars, in a handicap game; empty "
                "for an ordinary one.")
        .def_ro("capital_income", &GameRecord::capitalIncome,
                "Each player's capital income bonus, in a handicap game; empty "
                "for the usual.")
        .def_prop_ro("income_bonuses", [](const GameRecord &r) {
                std::vector<std::tuple<int, int, int>> out;
                for (const IncomeBonus &b : r.incomeBonuses) {
                    out.emplace_back(b.player, b.fromTurn, b.extra);
                }
                return out;
            }, "A catch-up game's boosts: (player, from turn, extra income).")
        .def_ro("pool_opponent_seat", &GameRecord::poolOpponentSeat,
                "In an opponent-pool game, the seat the earlier network "
                "played; -1 otherwise.")
        .def_ro("catch_up", &GameRecord::catchUp,
                "A catch-up game, whether or not its boost was given: its "
                "value targets count only from the boost on.")
        .def_prop_ro("handicap_leader", [](const GameRecord &r) {
                // the player given the edge, -1 in an ordinary game; in a
                // catch-up game, the one the boosted side had fallen behind
                if (!r.incomeBonuses.empty() && r.map.tribes.size() == 2) {
                    return 1 - r.incomeBonuses[0].player;
                }
                for (std::size_t p = 0; p < r.headStart.size(); p++) {
                    if (r.headStart[p] > 0) {
                        return static_cast<int>(p);
                    }
                }
                for (std::size_t p = 0; p < r.capitalIncome.size(); p++) {
                    if (r.capitalIncome[p] > Rules::CAPITAL_INCOME) {
                        return static_cast<int>(p);
                    }
                }
                return -1;
            }, "In a handicap game, the player given the edge; -1 otherwise.")
        .def("replays_legally", &GameRecord::replaysLegally,
             "Whether every command is legal when replayed, and no player's "
             "stars go below zero.")
        .def_ro("winner", &GameRecord::winner, "-1 if the turn limit came first")
        .def_ro("turns", &GameRecord::turns)
        .def_ro("final_scores", &GameRecord::finalScores)
        .def_ro("seat_zero_side", &GameRecord::seatZeroSide,
                "In a match, the side that played seat 0.")
        .def_prop_ro("move_count",
                     [](const GameRecord &r) { return r.moves.size(); })
        .def_prop_ro(
            "searched",
            [](const GameRecord &r) {
                std::vector<int> out;
                for (std::size_t i = 0; i < r.moves.size(); i++) {
                    if (!r.moves[i].visits.empty()) {
                        out.push_back(static_cast<int>(i));
                    }
                }
                return out;
            },
            "The moves the search chose, which have a policy target")
        .def("start", &GameRecord::start, "The game before any command.")
        .def_prop_ro("commands",
                     [](const GameRecord &r) {
                         std::vector<Command> out;
                         for (const RecordedMove &m : r.moves) {
                             out.push_back(m.command);
                         }
                         return out;
                     },
                     "Every command played, in order, from start().")
        .def("result", &GameRecord::result, "player"_a,
             "+1 or -1 for a win or loss, 0 for a game cut off by the turn limit.")
        .def_ro("final_territory", &GameRecord::finalTerritory,
                "Each tile's owner when the game ended (-1 for nobody); empty "
                "in older records.")
        .def_ro("turn_scores", &GameRecord::turnScores,
                "Each player's latest published score at each turn, flattened "
                "turns x players; empty in older records.")
        .def_ro("peace_treaties", &GameRecord::peaceTreaties,
                "Whether peace treaties were allowed in the game.")
        .def_ro("final_cities", &GameRecord::finalCities,
                "Each player's cities when the game ended; empty in older records.")
        .def("city_value", &GameRecord::cityValue, "player"_a,
             "The final city lead, (mine - the best opponent's) / their sum.")
        .def_ro("fog", &GameRecord::fog,
                "Played with fog; without it, its positions are encoded with "
                "full information.")
        .def_ro("stalled", &GameRecord::stalled,
                "Ended by the stalemate cutoff (stall_turns) rather than the "
                "turn limit; not saved with the record.")
        .def_prop_ro(
            "root_values",
            [](const GameRecord &r) {
                std::vector<float> out;
                for (const RecordedMove &m : r.moves) {
                    out.push_back(m.rootValue);
                }
                return out;
            },
            "Each move's search value for its player (NaN without one).")
        .def_ro("final_values", &GameRecord::finalValues,
                "For a game cut off by the turn limit, each seat's last search "
                "value with the network (NaN if none); empty otherwise.")
        .def("value", &GameRecord::value, "player"_a,
             "The value target: the result, or for a game cut off by the turn "
             "limit the player's final value (0 without one).")
        .def("score_value", &GameRecord::scoreValue, "player"_a,
             "scale"_a = 500.0, "The final score lead, squashed into [-1, 1].");
    m.def(
        "play_games",
        [](const SelfPlayConfig &config, int count, std::uint64_t seed, int threads) {
            nb::gil_scoped_release release;
            return playGames(config, count, seed, threads);
        },
        "config"_a, "count"_a, "seed"_a = 0, "threads"_a = 1,
        "Self-play games with these settings, every side scored by the score "
        "lead (no network): the bootstrap before there's one.");
    using Floats = nb::ndarray<const float, nb::c_contig, nb::device::cpu>;
    nb::class_<SelfPlaySession>(
        m, "SelfPlaySession",
        "Many self-play games at once, their waiting positions evaluated "
        "together by the caller's network:\n\n"
        "    while not session.done():\n"
        "        planes, globals_, legal = session.pending_inputs(enc)\n"
        "        logits, values = model(planes, globals_)\n"
        "        session.submit_network(enc, logits, values)\n"
        "    records = session.take_finished()")
        .def(nb::init<const SelfPlayConfig &, int, int, std::uint64_t, int>(), "config"_a,
             "parallel"_a, "games"_a, "seed"_a = 0, "first"_a = 0,
             "`games` games with these settings, `parallel` at a time, numbered "
             "from `first`: with the same seed, sessions of games [0, n) and "
             "[n, m) play the same games as one of [0, m) (n even for paired "
             "games).")
        .def("done", &SelfPlaySession::done)
        .def("clear_cache", &SelfPlaySession::clearCache,
             "Forget the cached evaluations: after the network changes.")
        .def(
            "cache_stats",
            [](SelfPlaySession &s) {
                SelfPlaySession::CacheStats c = s.takeCacheStats();
                return std::make_tuple(c.lookups, c.hits);
            },
            "(lookups, hits) of the evaluation cache since the last call.")
        .def(
            "depth_stats",
            [](const SelfPlaySession &s) {
                nb::list out;
                for (const BatchedSearch::DepthStats &d : s.depthStats()) {
                    nb::dict side;
                    side["simulations"] = d.simulations;
                    side["mean_depth"] = d.simulations ? static_cast<double>(d.depth) / d.simulations : 0.0;
                    side["reached_opponent"] =
                        d.simulations ? static_cast<double>(d.reachedOpponent) / d.simulations : 0.0;
                    side["mean_turns"] = d.simulations ? static_cast<double>(d.turns) / d.simulations : 0.0;
                    side["evaluations"] = d.evaluations;
                    out.append(side);
                }
                return out;
            },
            "Per side: simulations so far, their mean depth in moves below "
            "the root (a sampled turn is one), the share that reached the "
            "opponent's turn, their mean turn changes (turn search reaches "
            "further), and the network evaluations asked for.")
        .def_prop_ro("finished_games", &SelfPlaySession::finishedGames)
        .def(
            "pending_inputs",
            [](SelfPlaySession &s, const EncodingConfig &c, bool withLegal) {
                std::size_t b, n = c.mapSize;
                {
                    nb::gil_scoped_release release;
                    // repeats are answered by the cache: only the rest come
                    // back, and none only when no game has anything pending
                    b = s.pendingForNetwork(c);
                }
                auto planes = newArray<float>(
                    {b, static_cast<std::size_t>(planeCount(c)), n, n});
                auto globals = newArray<float>(
                    {b, static_cast<std::size_t>(globalCount(c))});
                auto legal = newArray<std::uint8_t>(
                    {withLegal ? b : 0, static_cast<std::size_t>(actionCount(c))});
                {
                    nb::gil_scoped_release release;
                    s.copyPrepared(planes.data(), globals.data());
                    if (withLegal) {
                        const std::size_t a = actionCount(c);
                        for (std::size_t i = 0; i < b; i++) {
                            legalMask(*s.waiting()[i].game, c, legal.data() + i * a);
                        }
                    }
                }
                return std::make_tuple(planes, globals, legal);
            },
            "encoding"_a, "legal"_a = true,
            "Advances every game to its next waiting positions and encodes "
            "them: (planes [B, C, N, N], globals [B, G], legal [B, A]). With "
            "legal=False the masks are skipped (empty): pending_slots gives "
            "what the search needs.")
        .def(
            "submit_network",
            [](SelfPlaySession &s, const EncodingConfig &c, Floats logits,
               Floats values) {
                std::size_t b = s.waiting().size();
                if (logits.ndim() != 2 || logits.shape(0) != b ||
                    logits.shape(1) != static_cast<std::size_t>(actionCount(c)) ||
                    values.size() != b) {
                    throw std::invalid_argument(
                        "Need logits [B, A] and values [B] for the pending positions");
                }
                nb::gil_scoped_release release;
                std::vector<Evaluation> results;
                evaluationsFromNetwork(s.waiting(), c, logits.data(),
                                       values.data(), results);
                s.submitServed(results);
            },
            "encoding"_a, "logits"_a, "values"_a,
            "The network's policy logits [B, A] and values [B] for the "
            "pending positions.")
        .def(
            "pending_sides",
            [](SelfPlaySession &s) {
                const std::vector<int> &sides = s.waitingSides();
                auto out = newArray<std::int64_t>({sides.size()});
                std::copy(sides.begin(), sides.end(), out.data());
                return out;
            },
            "For each pending position, the side (0 or 1) whose network "
            "evaluates it: in a match, only network sides' positions are "
            "pending. Call after pending_inputs.")
        .def(
            "pending_slots",
            [](SelfPlaySession &s, const EncodingConfig &c) {
                std::vector<int> rows, slots;
                {
                    nb::gil_scoped_release release;
                    requestSlots(s.waiting(), c, rows, slots);
                }
                auto r = newArray<std::int64_t>({rows.size()});
                auto k = newArray<std::int64_t>({slots.size()});
                for (std::size_t i = 0; i < rows.size(); i++) {
                    r.data()[i] = rows[i];
                    // -1 (no slot) reads slot 0; its logit is ignored
                    k.data()[i] = std::max(0, slots[i]);
                }
                return std::make_tuple(r, k);
            },
            "encoding"_a,
            "For the pending positions' moves, flattened: (rows, slots) to "
            "gather from the policy logits, logits[rows, slots], so only those "
            "leave the GPU. Call after pending_inputs.")
        .def(
            "pending_belief_rows",
            [](const SelfPlaySession &s) {
                std::vector<std::int64_t> rows;
                for (std::size_t i = 0; i < s.waiting().size(); i++) {
                    if (s.waiting()[i].wantsBeliefs) {
                        rows.push_back(static_cast<std::int64_t>(i));
                    }
                }
                auto out = newArray<std::int64_t>({rows.size()});
                std::copy(rows.begin(), rows.end(), out.data());
                return out;
            },
            "The pending positions that want the network's beliefs "
            "(network_beliefs): give their raw belief outputs to "
            "submit_move_logits. Call after pending_inputs.")
        .def(
            "submit_move_logits",
            [](SelfPlaySession &s, const EncodingConfig &c, Floats moveLogits,
               Floats values, std::optional<Floats> beliefs, std::optional<Floats> tieBreaks) {
                std::vector<int> rows, slots;
                requestSlots(s.waiting(), c, rows, slots);
                if (moveLogits.size() != slots.size() ||
                    values.size() != s.waiting().size()) {
                    throw std::invalid_argument(
                        "Need one logit per pending move and one value per position");
                }
                std::size_t wanted = std::count_if(
                    s.waiting().begin(), s.waiting().end(),
                    [](const EvalRequest &r) { return r.wantsBeliefs; });
                if (beliefs && beliefs->size() != wanted * beliefOutputCount(c)) {
                    throw std::invalid_argument(
                        "Need belief outputs [R, belief_output_count] for the "
                        "pending_belief_rows");
                }
                if (tieBreaks && tieBreaks->size() != 2 * s.waiting().size()) {
                    throw std::invalid_argument("Need tie_breaks [B, 2]");
                }
                nb::gil_scoped_release release;
                s.submitMoveLogits(c, moveLogits.data(), values.data(),
                                   tieBreaks ? tieBreaks->data() : nullptr,
                                   beliefs && wanted > 0 ? beliefs->data() : nullptr);
            },
            "encoding"_a, "move_logits"_a, "values"_a, "beliefs"_a = nb::none(),
            "tie_breaks"_a = nb::none(),
            "The logits gathered at pending_slots, the values [B], and for "
            "the pending_belief_rows their raw belief outputs [R, "
            "belief_output_count] (without them, those take priorBeliefs). "
            "tie_breaks [B, 2]: each position's predicted final score lead and "
            "turns left, for the search's tie-breaks (score_utility, win_sooner).")
        .def(
            "submit_scores",
            [](SelfPlaySession &s) {
                nb::gil_scoped_release release;
                ScoreEvaluator evaluator;
                std::vector<Evaluation> results;
                evaluator.evaluate(s.waiting(), results);
                s.submit(results);
            },
            "Evaluates the pending positions with the score evaluator instead.")
        .def("finish_now", &SelfPlaySession::finishNow,
             "Ends every game still being played, scored by the lead now, and "
             "starts no more. Call after submitting, before the next pending.")
        .def("set_fog_share", &SelfPlaySession::setFogShare, "share"_a,
             "The share of games started from now on that have fog.")
        .def("finish_after", &SelfPlaySession::finishAfter, "moves"_a,
             "Gives every game still being played this many more commands, "
             "then ends it scored by the lead; starts no more games.")
        .def("take_finished", &SelfPlaySession::takeFinished,
             "The games finished since the last call.");
    nb::class_<ReplayBuffer>(
        m, "ReplayBuffer",
        "The recent games training draws from, held in C++: random positions "
        "across all of them, the oldest games dropped past max_positions.")
        .def(nb::init<std::size_t>(), "max_positions"_a)
        .def("add", &ReplayBuffer::add, "games"_a)
        .def_prop_ro("game_count", &ReplayBuffer::gameCount)
        .def_prop_ro("positions", &ReplayBuffer::positions,
                     "Positions with a policy target (moves the search chose).")
        .def_prop_ro("added", &ReplayBuffer::added,
                     "Positions ever added, dropped ones included.")
        .def_prop_ro("decided_share", &ReplayBuffer::decidedShare,
                     "The share of games decided before the turn limit.")
        .def("sample", &ReplayBuffer::sample, "count"_a, "seed"_a,
             "per_game"_a = 1,
             "Random (game, move) positions with a policy target, in groups "
             "of per_game from the same game (replayed once for the group).")
        .def(
            "encode",
            [](const ReplayBuffer &buffer, const EncodingConfig &c,
               const std::vector<std::pair<int, int>> &positions,
               int threads, double cityWeight, double cutOffWeight, double searchWeight,
               double valueLambda) {
                return exampleDict(c, positions.size(), [&](const ExampleArrays &out) {
                    buffer.encode(positions, c, out, threads,
                                  {.lambda = valueLambda, .cityWeight = cityWeight,
                                   .searchWeight = searchWeight, .cutOffWeight = cutOffWeight});
                });
            },
            "encoding"_a, "positions"_a, "threads"_a = 8, "city_weight"_a = 0.0,
            "cut_off_weight"_a = 1.0, "search_weight"_a = 0.0, "value_lambda"_a = 1.0,
            nb::sig("def encode(self, encoding: Encoding, positions: "
                    "collections.abc.Sequence[tuple[int, int]], threads: int = 8, "
                    "city_weight: float = 0.0, cut_off_weight: float = 1.0, search_weight: float = 0.0, "
                    "value_lambda: float = 1.0) "
                    "-> dict[str, numpy.typing.NDArray[typing.Any]]"),
            "Training examples at those positions, like Encoding.encode_examples. "
            "The positions only hold until the next add(): from another "
            "thread than the one adding, use sample_examples.")
        .def(
            "sample_examples",
            [](const ReplayBuffer &buffer, const EncodingConfig &c, int count,
               std::uint64_t seed, int perGame, int threads, bool augment,
               double cityWeight, double cutOffWeight, double searchWeight,
               bool positions, double valueLambda) {
                std::vector<std::pair<std::uint64_t, int>> drawn;
                ExampleDict d = exampleDict(c, static_cast<std::size_t>(count),
                                            [&](const ExampleArrays &out) {
                    buffer.encodeSample(count, seed, perGame, c, out, threads, augment,
                                        {.lambda = valueLambda,
                                         .cityWeight = cityWeight,
                                         .searchWeight = searchWeight,
                                         .cutOffWeight = cutOffWeight},
                                        positions ? &drawn : nullptr);
                });
                if (positions) {
                    auto seeds = newArray<std::uint64_t>({drawn.size()});
                    auto moves = newArray<std::int64_t>({drawn.size()});
                    for (std::size_t i = 0; i < drawn.size(); i++) {
                        seeds.data()[i] = drawn[i].first;
                        moves.data()[i] = drawn[i].second;
                    }
                    d["game_seeds"] = seeds;
                    d["moves"] = moves;
                }
                return d;
            },
            "encoding"_a, "count"_a, "seed"_a, "per_game"_a = 4, "threads"_a = 8,
            "augment"_a = false, "city_weight"_a = 0.0, "cut_off_weight"_a = 1.0, "search_weight"_a = 0.0,
            "positions"_a = false, "value_lambda"_a = 1.0,
            nb::sig("def sample_examples(self, encoding: Encoding, count: int, "
                    "seed: int, per_game: int = 4, threads: int = 8, "
                    "augment: bool = False, city_weight: float = 0.0, "
                    "cut_off_weight: float = 1.0, search_weight: float = 0.0, "
                    "positions: bool = False, value_lambda: float = 1.0) "
                    "-> dict[str, numpy.typing.NDArray[typing.Any]]"),
            "Random training examples, sampled and encoded at once, so safe "
            "while another thread adds games. With augment, each is seen "
            "through a random one of the map's 8 symmetries (rotations and "
            "reflections). city_weight blends the value target with the "
            "final city lead (early in a run); cut_off_weight is the "
            "value weight of positions from games cut off by the turn limit or "
            "a stalemate. With positions, the dict also has game_seeds and "
            "moves [B]: which positions were drawn, to encode again later "
            "(Encoding.encode_examples).")
        .def("game_seeds", &ReplayBuffer::gameSeeds, "without_estimates"_a = false,
             "The seeds of the games held: all, or with without_estimates only "
             "those not reanalysed yet (set_value_estimates).")
        .def(
            "turn_start_inputs",
            [](const ReplayBuffer &b, const EncodingConfig &c,
               const std::vector<std::uint64_t> &seeds, int threads) {
                std::vector<float> planes, globals;
                std::vector<ReplayBuffer::TurnStart> starts;
                {
                    nb::gil_scoped_release release;
                    starts = b.encodeTurnStarts(seeds, c, planes, globals, threads);
                }
                const std::size_t count = starts.size(), n = c.mapSize;
                auto p = newArray<float>({count, static_cast<std::size_t>(planeCount(c)), n, n});
                auto g = newArray<float>({count, static_cast<std::size_t>(globalCount(c))});
                std::copy(planes.begin(), planes.end(), p.data());
                std::copy(globals.begin(), globals.end(), g.data());
                auto s = newArray<std::uint64_t>({count});
                auto t = newArray<std::int32_t>({count});
                auto q = newArray<std::int32_t>({count});
                for (std::size_t i = 0; i < count; i++) {
                    s.data()[i] = starts[i].gameSeed;
                    t.data()[i] = starts[i].turn;
                    q.data()[i] = starts[i].player;
                }
                return std::make_tuple(p, g, s, t, q);
            },
            "encoding"_a, "seeds"_a, "threads"_a = 8,
            "Reanalysis: the start of each player turn of those games still "
            "held, encoded for the player to move: (planes [B, C, N, N], "
            "globals [B, G], game seeds, turns, players [B]). Safe while "
            "another thread adds games.")
        .def(
            "set_value_estimates",
            [](ReplayBuffer &b, const std::vector<std::uint64_t> &seeds,
               const std::vector<int> &turns, const std::vector<int> &players,
               const std::vector<float> &values) {
                if (turns.size() != seeds.size() || players.size() != seeds.size()) {
                    throw std::invalid_argument("Need a turn and player per seed");
                }
                std::vector<ReplayBuffer::TurnStart> starts;
                for (std::size_t i = 0; i < seeds.size(); i++) {
                    starts.push_back({seeds[i], turns[i], players[i]});
                }
                b.setValueEstimates(starts, values);
            },
            "seeds"_a, "turns"_a, "players"_a, "values"_a,
            "Reanalysis: the network's values of those turn starts "
            "(turn_start_inputs), for the lambda-return value target "
            "(value_lambda). Games no longer held are skipped.")
        .def("save", [](const ReplayBuffer &b, const std::string &path) {
            nb::gil_scoped_release release;
            b.save(path);
        }, "path"_a, "Writes exactly the buffer's games to a record file, "
                     "replacing it.");
    m.def("save_records", &saveRecords, "records"_a, "path"_a, "append"_a = true,
          "Appends records to a text file, or with append=False replaces it.");
    m.def("load_records", &loadRecords, "path"_a);
    m.def("records_to_text", &recordsToText, "records"_a,
          "The records as save_records writes them, as a string.");
    m.def("records_from_text", &recordsFromText, "text"_a,
          "Records from records_to_text's string.");
    m.def(
        "thin_policy_targets",
        [](std::vector<GameRecord> records, double share, std::uint64_t seed) {
            thinPolicyTargets(records, share, seed);
            return records;
        },
        "records"_a, "share"_a, "seed"_a = 0,
        "Copies of the records keeping the search's visits (the policy "
        "target, and what makes a move a training position) on a random "
        "share of their moves: a match's games at self-play's density.");
    m.def(
        "keep_side_policy_targets",
        [](std::vector<GameRecord> records, int side) {
            keepSidePolicyTargets(records, side);
            return records;
        },
        "records"_a, "side"_a,
        "Copies of a match's records with the search's visits (the policy target, "
        "and what makes a move a training position) only on `side`'s moves (0 or 1).");
    m.def(
        "run_cli",
        [](std::vector<std::string> args, const EncodingConfig &encoding,
           nb::callable evaluate, const SearchConfig &config, bool networkBeliefs,
           bool explain, const std::vector<std::optional<SearchConfig>> &seatSearch,
           const std::vector<std::string> &seatPlans, int planTurns, double planWhenAhead,
           nb::object start, nb::object beforeCommand, std::vector<int> outsideSeats,
           nb::object outsideTurn) {
            CliHooks hooks;
            if (!start.is_none()) {
                hooks.start = [start](Game &game) { game = nb::cast<Game>(start()); };
            }
            std::function<void(Game &, const Command &)> before;
            if (!beforeCommand.is_none()) {
                before = [beforeCommand](Game &game, const Command &command) {
                    beforeCommand(nb::cast(&game, nb::rv_policy::reference), command);
                };
            }
            hooks.beforeCommand = before;
            hooks.outsideSeats = outsideSeats;
            if (!outsideTurn.is_none()) {
                hooks.outsideTurn = [outsideTurn](Game &game) {
                    return BotResult{.commands = nb::cast<std::vector<Command>>(
                                         outsideTurn(nb::cast(&game, nb::rv_policy::reference)))};
                };
            }
            std::vector<Plan> plans;
            for (const std::string &name : seatPlans) {
                plans.push_back(planNamed(name));
            }
            // per seat: the turn its plan started (-1 before), and its last
            // evaluation, for planWhenAhead
            auto planStart = std::make_shared<std::vector<int>>(
                plans.size(), std::isnan(planWhenAhead) ? 0 : -1);
            auto lastValue = std::make_shared<std::vector<float>>(plans.size(), std::nanf(""));
            args.insert(args.begin(), "stocknavalon.play");
            std::vector<char *> argv;
            for (std::string &a : args) {
                argv.push_back(a.data());
            }
            auto evaluator = std::make_shared<PythonEvaluator>(encoding, evaluate);
            return runCli(static_cast<int>(argv.size()), argv.data(),
                          [config, seatSearch, evaluator, networkBeliefs, explain, plans,
                           planTurns, planWhenAhead, planStart,
                           lastValue, before](std::uint64_t seed) -> BotTurn {
                auto rng = std::make_shared<std::mt19937_64>(seed);
                return [config, seatSearch, evaluator, rng, networkBeliefs, explain, plans,
                        planTurns, planWhenAhead, planStart, lastValue, before](Game &game) {
                    int player = game.currentPlayer;
                    // this seat's own search, if it has one
                    const SearchConfig &search =
                        player < static_cast<int>(seatSearch.size()) && seatSearch[player]
                            ? *seatSearch[player]
                            : config;
                    float value;
                    std::string decisions;
                    // this seat's forced plan, if any, from the start or once its
                    // last evaluation was ahead enough
                    Plan plan = Plan::NONE;
                    int planUntil = 0;
                    if (player < static_cast<int>(plans.size())) {
                        int &start = (*planStart)[player];
                        if (start < 0 && (*lastValue)[player] >= planWhenAhead) {
                            start = game.turn;
                        }
                        plan = start >= 0 ? plans[player] : Plan::NONE;
                        planUntil = start + planTurns;
                    }
                    BotResult result{.commands = playTurn(game, *evaluator, search, *rng,
                                                          networkBeliefs, &value,
                                                          explain ? &decisions : nullptr,
                                                          plan, planUntil, before)};
                    if (player < static_cast<int>(lastValue->size())) {
                        (*lastValue)[player] = value;
                    }
                    if (!std::isnan(value)) {
                        // as the search sees it before ending the turn: the
                        // value, and as a chance of winning
                        char note[128], which[40] = "";
                        // which search, when seats differ
                        if (!seatSearch.empty()) {
                            if (search.turnSearch) {
                                std::snprintf(which, sizeof which, " (turn search %d)",
                                              search.commandVisits);
                            } else {
                                std::snprintf(which, sizeof which, " (command search)");
                            }
                        }
                        std::snprintf(note, sizeof note,
                                      "player %d%s's evaluation: %+.2f (%.0f%% to win)", player,
                                      which, value, 50.0 * (value + 1));
                        result.note = note;
                    }
                    if (explain) {
                        // each decision, indented under the note
                        std::string text;
                        for (std::size_t at = 0; at < decisions.size();) {
                            std::size_t end = decisions.find('\n', at);
                            text += "\n    " + decisions.substr(at, end - at);
                            at = end + 1;
                        }
                        result.note += text;
                    }
                    return result;
                };
            }, hooks);
        },
        "args"_a, "encoding"_a, "evaluate"_a, "search"_a, "network_beliefs"_a = true,
        "explain"_a = false,
        "seat_search"_a = std::vector<std::optional<SearchConfig>>{},
        "seat_plans"_a = std::vector<std::string>{}, "plan_turns"_a = 20,
        "plan_when_ahead"_a = std::nan(""), "start"_a = nb::none(),
        "before_command"_a = nb::none(), "outside_seats"_a = std::vector<int>{},
        "outside_turn"_a = nb::none(),
        "The engine's terminal game (README \"Run\"), with the seats given by "
        "--bot in `args` played by the search, its positions evaluated by "
        "evaluate(planes, globals, rows, slots, belief_rows) -> (move logits, "
        "values, raw belief outputs of belief_rows or None), as with "
        "SelfPlaySession.submit_move_logits. With network_beliefs, each bot "
        "turn samples the hidden state from the network's beliefs (asked once "
        "per turn), else from the fixed priors. seat_search gives seats their "
        "own search (None, or seats past it: `search`); seat_plans their forced "
        "plans (plans.h: none, bridge, naval or convert) for plan_turns turns, from "
        "the start or (plan_when_ahead) once the seat's evaluation reaches it. With "
        "explain, each bot turn "
        "also lists its decisions: the command played and the search's "
        "leading moves, with priors, visits and values. For a game played alongside "
        "another (stocknavalon.play --crazy): start() returns the starting Game; "
        "before_command(game, command) is called before every command a person or "
        "a bot plays; outside_seats are played by outside_turn(game), which plays the "
        "turn on the game itself and returns its commands. Returns the exit code.");
    m.def(
        "search",
        [](const Game &game, const EncodingConfig &encoding, nb::callable evaluate,
           const SearchConfig &config, bool networkBeliefs, std::uint64_t seed) {
            PythonEvaluator evaluator(encoding, evaluate);
            int me = game.currentPlayer;
            std::optional<Beliefs> beliefs;
            if (networkBeliefs) {
                std::vector<Evaluation> results;
                evaluator.evaluate({EvalRequest{.game = &game, .player = me, .moves = nullptr,
                                                .wantsBeliefs = true}},
                                   results);
                beliefs = std::move(results.at(0).beliefs);
            }
            if (!beliefs) {
                beliefs = priorBeliefs(makeView(game, me));
            }
            SearchResult r = search(game, *beliefs, evaluator, config, seed);
            // per move, per leaf kind: (count, mean value for the searcher)
            std::vector<std::vector<std::pair<int, double>>> leaves;
            for (std::size_t i = 0; i < r.moves.size(); i++) {
                auto &row = leaves.emplace_back();
                for (int k = 0; k < LEAF_KINDS; k++) {
                    int n = r.leafCounts[i][k];
                    row.push_back({n, n ? r.leafValues[i][k] / n : 0.0});
                }
            }
            return std::make_tuple(r.moves, r.visits, r.values, r.priors, r.chosen, leaves);
        },
        "game"_a, "encoding"_a, "evaluate"_a, "search"_a, "network_beliefs"_a = true,
        "seed"_a = 0,
        "One search for the player to move, as the CLI's bot does, `evaluate` as "
        "for run_cli, with these settings (SearchConfig). Returns (moves, visits, values, priors, chosen index, "
        "leaves): leaves per move are (count, mean value for the searcher) of "
        "where its simulations ended: in the searcher's own turn, an "
        "opponent's, the searcher's later turns, the game's end.");
    m.def(
        "profile_start",
        [] {
            profiling::reset();
            profiling::enabled = true;
        },
        "Starts timing self-play's phases (profile.h), from zero.");
    m.def(
        "profile_stop",
        [] {
            profiling::enabled = false;
            nb::dict out;
            for (int p = 0; p < profiling::PHASES; p++) {
                nb::dict phase;
                phase["seconds"] = profiling::nanoseconds[p].load() / 1e9;
                phase["count"] = profiling::counts[p].load();
                out[profiling::NAMES[p]] = phase;
            }
            return out;
        },
        "Stops timing; per phase, the thread time spent (summed over threads) "
        "and how many times it ran.");
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

void bindEncoding(nb::module_ &m) {
    m.attr("ENCODING_VERSION") = ENCODING_VERSION;
    m.attr("RULES_VERSION") = RuleVersions::LATEST;
    m.attr("MAP_TYPE_COUNT") = static_cast<int>(MapTypes::MAP_TYPE_SIZE);
    m.attr("OPENING_CHANGES") = OPENING_CHANGES;
    // command slots per tile; the global slots come after them
    m.attr("SLOTS_PER_TILE") = EncodingSlots::PER_TILE;
    // the side targets' layout (docs/encoding.md section 8.5)
    m.attr("STAR_COMPONENTS") = STAR_COMPONENTS;
    // the unexplored head's layers: terrain, resource and feature groups
    m.attr("UNEXPLORED_TERRAIN") = UnexploredHead::TERRAIN;
    m.attr("UNEXPLORED_RESOURCE") = UnexploredHead::RESOURCE;
    m.attr("UNEXPLORED_FEATURE") = UnexploredHead::FEATURE;
    m.attr("UNEXPLORED_LAYERS") = UnexploredHead::LAYERS;
    m.attr("SIDE_SCORE_SOON") = SideTargets::SCORE_SOON;
    m.attr("SIDE_SCORE_END") = SideTargets::SCORE_END;
    m.attr("SIDE_STARS_AHEAD") = SideTargets::STARS_AHEAD;
    m.attr("SIDE_INCOME_AHEAD") = SideTargets::INCOME_AHEAD;
    m.attr("SIDE_SHORT_TERM_VALUE") = SideTargets::SHORT_TERM_VALUE;
    m.attr("SHORT_TERM_HORIZONS") = std::vector<double>(SideTargets::SHORT_TERM_HORIZONS.begin(),
                                                        SideTargets::SHORT_TERM_HORIZONS.end());
    m.attr("UNLIMITED_HORIZON") = UNLIMITED_HORIZON;
    m.attr("SIDE_TURNS_LEFT") = SideTargets::TURNS_LEFT;
    m.attr("TURNS_LEFT_SCALE") = SideTargets::TURNS_LEFT_SCALE;
    m.attr("TURNS_LEFT_CAP") = SideTargets::TURNS_LEFT_CAP;
    m.attr("SIDE_TERRITORY") = SideTargets::TERRITORY;
    m.attr("TERRITORY_CLASSES") = SideTargets::TERRITORY_CLASSES;
    m.attr("HIDDEN_UNIT_COUNT_CAP") = HIDDEN_UNIT_COUNT_CAP;

    nb::class_<EncodingConfig>(
        m, "Encoding",
        "States and commands as arrays for the network (docs/encoding.md). "
        "Arrays are NumPy: planes float32 [C, N, N], globals float32 [G], "
        "masks uint8 [A]. The batched forms run in C++ without the GIL.")
        .def(
            "__init__",
            [](EncodingConfig *c, int mapSize, int players) {
                new (c) EncodingConfig{.mapSize = mapSize, .players = players};
            },
            "map_size"_a = 11, "players"_a = 2)
        .def_ro("map_size", &EncodingConfig::mapSize)
        .def_ro("players", &EncodingConfig::players)
        .def_prop_ro("plane_count", &planeCount, "C")
        .def_prop_ro("global_count", &globalCount, "G")
        .def_prop_ro("action_count", &actionCount, "A")
        .def_prop_ro(
            "belief_output_heads",
            [](const EncodingConfig &c) {
                std::vector<std::tuple<std::string, int, int, int>> out;
                for (const TargetHead &h : beliefOutputHeads(c)) {
                    out.emplace_back(h.name, h.opponent, h.offset, h.size);
                }
                return out;
            },
            "Where each belief head's raw outputs go in the array "
            "submit_move_logits takes: (name, opponent, offset, size), like "
            "target_heads but the stars as the mixture's 3 x STAR_COMPONENTS "
            "raw parameters (docs/encoding.md section 8.4).")
        .def_prop_ro("belief_output_count", &beliefOutputCount)
        .def_prop_ro("side_target_count", &sideTargetCount,
                     "The side targets' length: the score lead soon and at the "
                     "end, then [3][N][N] final territory (docs/encoding.md "
                     "section 8.5)")
        .def_prop_ro("target_count", &targetCount,
                     "The belief targets' length (docs/encoding.md section 8)")
        .def_prop_ro(
            "target_heads",
            [](const EncodingConfig &c) {
                std::vector<std::tuple<std::string, int, int, int>> out;
                for (const TargetHead &h : targetHeads(c)) {
                    out.emplace_back(h.name, h.opponent, h.offset, h.size);
                }
                return out;
            },
            "Each belief head's part of the targets: (name, opponent, offset, "
            "size), opponents counted from 1.")
        .def(
            "encode_state",
            [](const EncodingConfig &c, const Game &game, int player,
               bool fullInformation) {
                std::size_t n = c.mapSize;
                auto planes = newArray<float>(
                    {static_cast<std::size_t>(planeCount(c)), n, n});
                auto globals = newArray<float>(
                    {static_cast<std::size_t>(globalCount(c))});
                encodeState(game, player, c, planes.data(), globals.data(),
                            fullInformation);
                return std::make_tuple(planes, globals);
            },
            "game"_a, "player"_a, "full_information"_a = false,
            "The player's input: (planes [C, N, N], globals [G]). With "
            "full_information, from the full view (every tile and opponent "
            "shown, `explored` still the player's own).")
        .def(
            "encode_states",
            [](const EncodingConfig &c, const std::vector<const Game *> &games,
               const std::vector<int> &players) {
                if (games.size() != players.size()) {
                    throw std::invalid_argument("Need one player per game");
                }
                std::size_t b = games.size(), n = c.mapSize,
                            planeSize = planeCount(c) * n * n,
                            globalSize = globalCount(c);
                auto planes = newArray<float>(
                    {b, static_cast<std::size_t>(planeCount(c)), n, n});
                auto globals = newArray<float>({b, globalSize});
                float *p = planes.data(), *g = globals.data();
                {
                    nb::gil_scoped_release release;
                    for (std::size_t i = 0; i < b; i++) {
                        encodeState(*games[i], players[i], c, p + i * planeSize,
                                    g + i * globalSize);
                    }
                }
                return std::make_tuple(planes, globals);
            },
            "games"_a, "players"_a,
            "A batch: (planes [B, C, N, N], globals [B, G]).")
        .def(
            "legal_mask",
            [](const EncodingConfig &c, const Game &game) {
                auto mask = newArray<std::uint8_t>(
                    {static_cast<std::size_t>(actionCount(c))});
                legalMask(game, c, mask.data());
                return mask;
            },
            "game"_a, "1 for each legal command's slot: [A].")
        .def(
            "legal_masks",
            [](const EncodingConfig &c, const std::vector<const Game *> &games) {
                std::size_t b = games.size(),
                            a = static_cast<std::size_t>(actionCount(c));
                auto masks = newArray<std::uint8_t>({b, a});
                std::uint8_t *out = masks.data();
                {
                    nb::gil_scoped_release release;
                    for (std::size_t i = 0; i < b; i++) {
                        legalMask(*games[i], c, out + i * a);
                    }
                }
                return masks;
            },
            "games"_a, "A batch of masks: [B, A].")
        .def(
            "encode_targets",
            [](const EncodingConfig &c, const std::vector<const Game *> &games,
               const std::vector<int> &players) {
                if (games.size() != players.size()) {
                    throw std::invalid_argument("Need one player per game");
                }
                std::size_t b = games.size(),
                            n = static_cast<std::size_t>(targetCount(c));
                auto targets = newArray<float>({b, n});
                auto masks = newArray<std::uint8_t>({b, n});
                float *t = targets.data();
                std::uint8_t *m = masks.data();
                {
                    nb::gil_scoped_release release;
                    for (std::size_t i = 0; i < b; i++) {
                        encodeTargets(*games[i], players[i], c, t + i * n,
                                      m + i * n);
                    }
                }
                return std::make_tuple(targets, masks);
            },
            "games"_a, "players"_a,
            "The belief targets from the true games, and where each is trained: "
            "(targets [B, T], masks [B, T]). The value target is the game's "
            "result, added once it's over.")
        .def(
            "encode_examples",
            [](const EncodingConfig &c, const std::vector<GameRecord> &records,
               const std::vector<std::pair<int, int>> &positions) {
                return exampleDict(c, positions.size(), [&](const ExampleArrays &out) {
                    encodeExamples(records, positions, c, out);
                });
            },
            "records"_a, "positions"_a,
            nb::sig("def encode_examples(self, records: "
                    "collections.abc.Sequence[GameRecord], positions: "
                    "collections.abc.Sequence[tuple[int, int]]) "
                    "-> dict[str, numpy.typing.NDArray[typing.Any]]"),
            "Training examples at (record, move) positions, each the position "
            "just before that move, for the player to move: a dict of planes "
            "[B, C, N, N], globals [B, G], legal [B, A], policy [B, A] (the "
            "search's visits, normalized; all 0 for a move it didn't choose), "
            "value and value_weight [B] (GameRecord.value: the result, or for a "
            "game cut off by the turn limit the player's final value; weights "
            "all 1), targets and target_masks [B, T], side_targets and "
            "side_masks [B, S]. "
            "Copies the records: for training, use ReplayBuffer.encode.")
        .def(
            "encode_command",
            [](const EncodingConfig &c, const Game &game, const Command &command) {
                return encodeCommand(game, command, c);
            },
            "game"_a, "command"_a,
            "The command's slot, or -1 if it has none (Resign, Stay).")
        .def(
            "decode_command",
            [](const EncodingConfig &c, const Game &game, int index) {
                return decodeCommand(game, index, c);
            },
            "game"_a, "index"_a,
            "The legal command in that slot; raises ValueError if none is.");

    auto tribeList = [](const TribeSet &set) {
        std::vector<TribeTypes::Tribe> out;
        for (int t = 0; t < TribeTypes::TRIBE_SIZE; t++) {
            if (set.test(t)) {
                out.push_back(static_cast<TribeTypes::Tribe>(t));
            }
        }
        return out;
    };
    TribeInferenceConfig defaults;
    m.def(
        "determinize",
        [](const Game &game, int player, std::uint64_t seed, const EncodingConfig *config,
           std::optional<nb::ndarray<const float, nb::ndim<1>, nb::c_contig>> beliefs)
            -> std::optional<Game> {
            PlayerView view = makeView(game, player);
            Beliefs b = beliefs && config ? decodeBeliefs(view, *config, beliefs->data())
                                          : priorBeliefs(view);
            return determinize(game, player, b, seed);
        },
        "game"_a, "player"_a, "seed"_a, "encoding"_a = nb::none(), "beliefs"_a = nb::none(),
        "A game agreeing with everything `player` knows of `game`, the rest "
        "sampled as the search does: from the network's raw belief outputs "
        "for the player's position (with `encoding`), else the fixed priors. "
        "None if no sample fit.");
    m.def(
        "possible_tribes",
        [tribeList](const PlayerView &view, int opponent, int turns,
                    int maxStates, int maxWork) {
            return tribeList(possibleTribes(view, opponent,
                                            {turns, maxStates, maxWork}));
        },
        "view"_a, "opponent"_a, "turns"_a = defaults.turns,
        "max_states"_a = defaults.maxStates, "max_work"_a = defaults.maxWork,
        "The tribes the opponent could be, from their starting score, the "
        "climates seen and their first `turns` score changes "
        "(docs/tribeInference.md). Always includes the real one; the limits "
        "only bound the time.");
    m.def(
        "score_changes_fit",
        [](const PlayerView &view, int opponent, TribeTypes::Tribe tribe,
           int turns, int maxStates, int maxWork) {
            return scoreChangesFit(view, opponent, tribe,
                                   {turns, maxStates, maxWork});
        },
        "view"_a, "opponent"_a, "tribe"_a, "turns"_a = defaults.turns,
        "max_states"_a = defaults.maxStates, "max_work"_a = defaults.maxWork,
        "Whether a player of `tribe` could have made the opponent's published "
        "score changes so far. Two-player games only; otherwise True.");
    m.def(
        "climates_seen",
        [tribeList](const PlayerView &view) {
            return tribeList(climatesSeen(view));
        },
        "view"_a, "The tribes whose climate the player has seen.");
    m.def(
        "tribes_with_starting_score",
        [tribeList](int score) { return tribeList(tribesWithStartingScore(score)); },
        "score"_a, "The tribes that could start the game with this score.");
    m.def("starting_explored", &startingExplored, "tribe"_a, "score"_a,
          "How many tiles a player of this tribe with this starting score "
          "started with explored (9 to 25), or -1 if none fits. 25 means "
          "their capital is at least 2 tiles from every edge.");

    m.def("explained_score", &explainedScore, "view"_a, "player"_a,
          "The part of a player's published score the viewer can account "
          "for from what they see.");
}

} // namespace

NB_MODULE(_engine, m) {
    m.doc() = "polyengine, a Polytopia engine";
    bindEnums(m);
    bindState(m);
    bindMaps(m);
    bindGame(m);
    bindView(m);
    bindSearch(m);
    bindEncoding(m);
}
