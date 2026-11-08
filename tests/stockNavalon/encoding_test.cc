#include "bots/randomBot.h"
#include "doctest/doctest.h"
#include "encoding.h"
#include "helpers.h"
#include "legalMoves.h"
#include "mapgen.h"
#include "view.h"
#include <set>
#include <sstream>

using namespace UnitTypes;

namespace {

struct Encoded {
    std::vector<float> planes, globals;
};

Encoded encode(const Game &game, int player, const EncodingConfig &config) {
    Encoded out{std::vector<float>(planeCount(config) * config.mapSize *
                                   config.mapSize),
                std::vector<float>(globalCount(config))};
    encodeState(game, player, config, out.planes.data(), out.globals.data());
    return out;
}

// an 11x11 field map with capitals at (2,2) and (8,8), in that seat order or
// swapped
std::string tinyMap(bool swapped = false) {
    std::vector<std::string> tokens(121, "F");
    tokens[2 * 11 + 2] = swapped ? "F1" : "F0";
    tokens[8 * 11 + 8] = swapped ? "F0" : "F1";
    std::string out = "11 1\n";
    for (const std::string &t : tokens) {
        out += t + " ";
    }
    return out;
}

// Changes everything `viewer` can't know: unexplored tiles, enemy units they
// can't see, every opponent's stars, techs and exploration.
Game scrambleHidden(const Game &game, int viewer) {
    Game other = game;
    for (int t = 0; t < other.size * other.size; t++) {
        Tile &tile = other.tiles[t];
        if (tile.exploredBy(viewer)) {
            continue;
        }
        if (tile.unit < 0 && tile.building == ImprovementTypes::NONE) {
            tile.terrain = tile.terrain == TerrainTypes::FOREST
                               ? TerrainTypes::MOUNTAIN
                               : TerrainTypes::FOREST;
        }
        tile.resource = tile.resource == ResourceTypes::NONE
                            ? ResourceTypes::METAL
                            : ResourceTypes::NONE;
        tile.climate = (tile.climate + 1) % TribeTypes::TRIBE_SIZE;
    }
    for (Unit &unit : other.units) {
        if (unit.alive && unit.owner != viewer && !other.canSee(viewer, unit)) {
            unit.hp = std::max(1, unit.hp - 5);
            unit.kills += 2;
        }
    }
    for (std::size_t p = 0; p < other.players.size(); p++) {
        if (static_cast<int>(p) == viewer) {
            continue;
        }
        Player &player = other.players[p];
        player.stars += 17;
        player.techs.flip();
        player.techs.set(TechTypes::BASIC);
        player.kills += 3;
        // a tribe not met yet is hidden too (its climate stays put)
        if (!other.players[viewer].hasMet(static_cast<int>(p))) {
            player.tribe = player.tribe == TribeTypes::XINXI
                               ? TribeTypes::LUXIDOOR
                               : TribeTypes::XINXI;
        }
        for (Tile &tile : other.tiles) {
            tile.explored |= std::uint32_t{1} << p;
        }
    }
    return other;
}

} // namespace

TEST_CASE("encoding sizes follow the spec") {
    EncodingConfig config;
    CHECK(planeCount(config) == 100);
    CHECK(globalCount(config) == 109);
    CHECK(actionCount(config) == 26410);
    CHECK(EncodingSlots::PER_TILE == 218);
}

TEST_CASE("every legal command has its own slot, and decodes back") {
    struct Case {
        int size, mapSize;
    };
    // Tiny maps at their own size, Small ones padded to 16
    for (Case c : {Case{11, 11}, Case{14, 16}}) {
        EncodingConfig config{.mapSize = c.mapSize};
        for (int type = 0; type < MapTypes::MAP_TYPE_SIZE; type++) {
            CAPTURE(c.size);
            CAPTURE(type);
            std::vector<TribeTypes::Tribe> tribes = {
                static_cast<TribeTypes::Tribe>(type * 2 % TribeTypes::TRIBE_SIZE),
                static_cast<TribeTypes::Tribe>((type * 2 + 5) %
                                               TribeTypes::TRIBE_SIZE)};
            Game game(generateMap({.size = c.size,
                                   .type = static_cast<MapTypes::MapType>(type),
                                   .tribes = tribes,
                                   .seed = static_cast<std::uint64_t>(type),
                                   .candidates = 1}),
                      tribes, {.seed = static_cast<std::uint64_t>(type)});
            LegalMoves legal(game);
            RandomBot bot(type);
            std::vector<std::uint8_t> mask(actionCount(config)),
                fromLegalMoves(actionCount(config));
            for (int step = 0; step < 400 && !game.gameOver(); step++) {
                // a richer game reaches more of the commands
                if (step % 50 == 0) {
                    Game before = game;
                    for (Player &p : game.players) {
                        p.stars += 20;
                    }
                    legal.update(before, game);
                }
                std::vector<Command> commands = game.legalCommands();
                std::set<int> indices;
                for (const Command &command : commands) {
                    int index = encodeCommand(game, command, config);
                    REQUIRE(index >= 0);
                    REQUIRE(index < actionCount(config));
                    REQUIRE(indices.insert(index).second);
                    REQUIRE(decodeCommand(game, index, config) == command);
                }
                legalMask(game, config, mask.data());
                CHECK(std::count(mask.begin(), mask.end(), 1) ==
                      static_cast<long>(commands.size()));
                legalMask(legal.commands(), game, config, fromLegalMoves.data());
                CHECK(mask == fromLegalMoves);
                legal.apply(game, bot.choose(game));
            }
        }
    }
}

TEST_CASE("the encoding only depends on what the player knows") {
    EncodingConfig config;
    for (std::uint64_t seed = 0; seed < 6; seed++) {
        CAPTURE(seed);
        std::vector<TribeTypes::Tribe> tribes = {TribeTypes::IMPERIUS,
                                                 TribeTypes::BARDUR};
        auto type = static_cast<MapTypes::MapType>(seed);
        Game game(generateMap({.size = 11,
                               .type = type,
                               .tribes = tribes,
                               .seed = seed,
                               .candidates = 1}),
                  tribes, {.seed = seed});
        RandomBot bot(seed);
        for (int step = 0; step < 600 && !game.gameOver(); step++) {
            if (step % 25 == 0) {
                for (int viewer = 0; viewer < 2; viewer++) {
                    Encoded real = encode(game, viewer, config);
                    Encoded scrambled =
                        encode(scrambleHidden(game, viewer), viewer, config);
                    REQUIRE(real.planes == scrambled.planes);
                    REQUIRE(real.globals == scrambled.globals);
                }
            }
            game.applyUnchecked(bot.choose(game));
        }
    }
}

TEST_CASE("features land where they should") {
    EncodingConfig config;
    Game game = makeGame(tinyMap());
    const int N = config.mapSize;
    Encoded start = encode(game, 0, config);
    auto plane = [&](const Encoded &e, int index, int x, int y) {
        return e.planes[(index * N + y) * N + x];
    };
    // board: everything explored, field, the cities' tiles
    CHECK(plane(start, 0, 10, 10) == 1);   // on map
    CHECK(plane(start, 1, 5, 5) == 1);     // explored
    CHECK(plane(start, 2, 5, 5) == 1);     // field
    // climate: (2,2) is Imperius country, (8,8) Bardur
    CHECK(plane(start, 38 + TribeTypes::IMPERIUS, 2, 2) == 1);
    CHECK(plane(start, 38 + TribeTypes::BARDUR, 8, 8) == 1);
    // territory: (2,2) is ours, (8,8) theirs
    CHECK(plane(start, 50 + 0, 2, 2) == 1);
    CHECK(plane(start, 50 + 1, 8, 8) == 1);

    // an enemy Rider at (5,3)
    clearUnits(game);
    addUnit(game, 1, RIDER, 5, 3);
    Encoded e = encode(game, 0, config);
    const int unitOwner = 50 + 2 + 2 + 4;
    CHECK(plane(e, unitOwner + 1, 5, 3) == 1);
    CHECK(plane(e, unitOwner + 2 + RIDER, 5, 3) == 1);
    const int threat = planeCount(config) - 3;
    // it can move 2 and attack 1 further, but not 4 away
    CHECK(plane(e, threat, 2, 3) == doctest::Approx(0.25));
    CHECK(plane(e, threat, 5, 3) == 0);
    CHECK(plane(e, threat, 10, 10) == 0);
    // against a full-HP Warrior: 2 vs 2, so 5 HP
    CHECK(plane(e, threat + 1, 4, 4) == doctest::Approx(5.0 / 15));

    // our Warrior next to it: it can attack the Rider
    addUnit(game, 0, WARRIOR, 4, 3);
    e = encode(game, 0, config);
    CHECK(plane(e, threat + 2, 5, 3) == doctest::Approx(0.25));
}

TEST_CASE("score features split what the player can account for") {
    EncodingConfig config;
    // Bardur starts at 515: a capital (100), 9 border tiles (180), a Warrior
    // (10), Hunting (100) and 25 explored tiles (125)
    Game game = makeGame(tinyMap());
    PlayerView view = makeView(game, 0);
    CHECK(view.scores[1] == 515);
    CHECK(explainedScore(view, 1) == 100 + 180 + 10);
    Encoded e = encode(game, 0, config);
    const int scoreFeatures = 3 + 12 * 2 + 12 + 12 + 3 * 2;
    CHECK(e.globals[scoreFeatures] == doctest::Approx(0.515));        // starting
    CHECK(e.globals[scoreFeatures + 1] == doctest::Approx(0.29));     // explained
    CHECK(e.globals[scoreFeatures + 2] == doctest::Approx(0.225));    // unexplained
    CHECK(e.globals[scoreFeatures + 3] == 0);                         // last change

    // killing their Warrior: still in their published score, but no longer
    // unexplained
    clearUnits(game);
    addUnit(game, 1, WARRIOR, 5, 5);
    int attacker = addUnit(game, 0, KNIGHT, 4, 5);
    game.units[game.tiles[game.index(5, 5)].unit].hp = 10;
    game.apply({CommandTypes::ATTACK, game.index(4, 5), game.index(5, 5)});
    REQUIRE_FALSE(game.unitAt(game.index(5, 5))->owner == 1);
    view = makeView(game, 0);
    CHECK(view.effectsSincePublished[1] == -10);
    e = encode(game, 0, config);
    CHECK(e.globals[scoreFeatures + 2] == doctest::Approx(0.225));
    (void)attacker;
}

TEST_CASE("an opponent's tribe is unknown until met") {
    EncodingConfig config;
    Game game = makeFogGame(tinyMap(), {TribeTypes::VENGIR, TribeTypes::AIMO});
    PlayerView view = makeView(game, 0);
    CHECK(view.tribes[0] == TribeTypes::VENGIR);
    CHECK_FALSE(view.tribes[1].has_value());
    // their starting score is public: 730 means Vengir or Ai-Mo
    CHECK(view.scoreHistory[1].player == 1);
    CHECK(view.scoreHistory[1].score == 730);
    Encoded e = encode(game, 0, config);
    const int opponentTribe = 3 + TribeTypes::TRIBE_SIZE;
    for (int t = 0; t < TribeTypes::TRIBE_SIZE; t++) {
        CHECK(e.globals[opponentTribe + t] == 0);
    }
    game.players[0].met = ~std::uint32_t{0};
    CHECK(makeView(game, 0).tribes[1] == TribeTypes::AIMO);
    CHECK(encode(game, 0, config).globals[opponentTribe + TribeTypes::AIMO] == 1);
}

TEST_CASE("one encoding serves both seats") {
    EncodingConfig config;
    std::vector<TribeTypes::Tribe> tribes = {TribeTypes::OUMAJI,
                                             TribeTypes::KICKOO};
    Game a = makeFogGame(tinyMap(), tribes);
    // the same position with the seats swapped
    Game b = makeFogGame(tinyMap(true), {tribes[1], tribes[0]});
    b.apply({CommandTypes::ENDTURN});
    REQUIRE(b.currentPlayer == 1);
    Encoded fromA = encode(a, 0, config), fromB = encode(b, 1, config);
    CHECK(fromA.planes == fromB.planes);
    CHECK(fromA.globals == fromB.globals);
}

TEST_CASE("a smaller map is padded into the top-left corner") {
    Game game = makeFogGame(tinyMap());
    Encoded small = encode(game, 0, {.mapSize = 11});
    Encoded big = encode(game, 0, {.mapSize = 16});
    const int planes = planeCount({});
    for (int p = 0; p < planes; p++) {
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                float value = big.planes[(p * 16 + y) * 16 + x];
                if (x < 11 && y < 11) {
                    REQUIRE(value == small.planes[(p * 11 + y) * 11 + x]);
                } else {
                    REQUIRE(value == 0);
                }
            }
        }
    }
    CHECK(small.globals == big.globals);
    CHECK_THROWS_AS(encode(makeFogGame(bigMap()), 0, {.mapSize = 11}),
                    std::invalid_argument);
}
