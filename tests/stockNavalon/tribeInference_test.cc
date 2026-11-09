#include "bots/randomBot.h"
#include "doctest/doctest.h"
#include "helpers.h"
#include "mapgen.h"
#include "tribeInference.h"
#include "view.h"
#include <sstream>

using namespace TribeTypes;

namespace {
TribeSet tribes(std::initializer_list<Tribe> list) {
    TribeSet set;
    for (Tribe t : list) {
        set.set(t);
    }
    return set;
}

// an 11x11 field map, capitals at (2,2) and (8,8); the left half (x < 5)
// is player 0's climate and the rest player 1's
std::string climateMap() {
    std::string out = "11 1\n";
    for (int y = 0; y < 11; y++) {
        for (int x = 0; x < 11; x++) {
            std::string token = "F";
            if (x == 2 && y == 2) {
                token += "0";
            } else if (x == 8 && y == 8) {
                token += "1";
            }
            token += x < 5 ? "^0" : "^1";
            out += token + " ";
        }
    }
    return out;
}
} // namespace

TEST_CASE("starting scores narrow down the tribe") {
    // GAME.md section 3; a capital near a corner can start 5 lower, so a
    // few tribes one step up stay possible too
    CHECK(tribesWithStartingScore(730) == tribes({VENGIR, AIMO}));
    CHECK(tribesWithStartingScore(520) == tribes({OUMAJI}));
    CHECK(tribesWithStartingScore(515) ==
          tribes({XINXI, IMPERIUS, BARDUR, KICKOO, LUXIDOOR, OUMAJI}));
    CHECK(tribesWithStartingScore(620) == tribes({QUETZALI, HOODRICK}));
    CHECK(tribesWithStartingScore(615) ==
          tribes({ZEBASI, YADAKK, QUETZALI, HOODRICK}));
    CHECK(tribesWithStartingScore(517).none());
}

TEST_CASE("maps carry climate regions") {
    std::istringstream in(climateMap());
    MapData map = parseMap(in);
    REQUIRE(map.regions.size() == 121);
    CHECK(map.regions[0] == 0);
    CHECK(map.regions[10] == 1);

    Game game(map, {VENGIR, AIMO});
    CHECK(game.tiles[game.index(1, 1)].climate == VENGIR);
    CHECK(game.tiles[game.index(9, 1)].climate == AIMO);

    // without markers, each tile takes its nearest capital's climate
    Game nearest = makeFogGame(bigMap(), {VENGIR, AIMO});
    CHECK(nearest.tiles[nearest.index(1, 1)].climate == VENGIR);
    CHECK(nearest.tiles[nearest.index(10, 10)].climate == AIMO);

    // generated maps: every tile has one, and each capital is in its own
    for (int type = 0; type < MapTypes::MAP_TYPE_SIZE; type++) {
        std::vector<Tribe> lineup = {XINXI, KICKOO};
        MapData generated =
            generateMap({.size = 11,
                         .type = static_cast<MapTypes::MapType>(type),
                         .tribes = lineup,
                         .seed = 3,
                         .candidates = 1});
        Game g(generated, lineup);
        for (const Tile &tile : g.tiles) {
            CHECK(tile.climate >= 0);
        }
        for (std::size_t p = 0; p < lineup.size(); p++) {
            CHECK(g.tiles[generated.capitals[p]].climate == lineup[p]);
        }
    }
}

TEST_CASE("a foreign climate settles an unmet opponent's tribe in 1v1") {
    std::istringstream in(climateMap());
    Game game(parseMap(in), {VENGIR, AIMO});
    CHECK(possibleTribes(makeView(game, 0), 1) == tribes({VENGIR, AIMO}));
    CHECK(possibleTribes(makeView(game, 0), 0) == tribes({VENGIR}));
    // exploring into their climate
    game.reveal(0, game.index(5, 2));
    CHECK(possibleTribes(makeView(game, 0), 1) == tribes({AIMO}));
}

TEST_CASE("in a mirror match, your own climate proves nothing") {
    std::istringstream in(climateMap());
    Game game(parseMap(in), {VENGIR, VENGIR});
    for (int t = 0; t < game.size * game.size; t++) {
        game.reveal(0, t);
    }
    // the whole map is Vengir climate, which fits a mirror match or not
    // having found their land yet
    CHECK(possibleTribes(makeView(game, 0), 1) == tribes({VENGIR, AIMO}));
}

TEST_CASE("the real tribe is never ruled out") {
    // every matchup, mirrors included, in random games on generated maps
    int games = 0, settled = 0, checks = 0;
    for (int a = 0; a < TRIBE_SIZE; a++) {
        for (int b = 0; b < TRIBE_SIZE; b++) {
            std::vector<Tribe> lineup = {static_cast<Tribe>(a),
                                         static_cast<Tribe>(b)};
            std::uint64_t seed = a * TRIBE_SIZE + b;
            auto type = static_cast<MapTypes::MapType>(seed % MapTypes::MAP_TYPE_SIZE);
            Game game(generateMap({.size = 11,
                                   .type = type,
                                   .tribes = lineup,
                                   .seed = seed,
                                   .candidates = 1}),
                      lineup, {.seed = seed});
            RandomBot bot(seed);
            for (int turn = 0; turn < 20 && !game.gameOver(); turn++) {
                bot.playTurn(game);
                for (int viewer = 0; viewer < 2; viewer++) {
                    REQUIRE(possibleTribes(makeView(game, viewer), 1 - viewer)
                                .test(lineup[1 - viewer]));
                    checks++;
                }
            }
            games++;
            settled += possibleTribes(makeView(game, 0), 1).count() == 1;
        }
    }
    MESSAGE(games << " games, " << checks << " checks; settled by the end in "
                  << settled);
}

TEST_CASE("the opponent's first turn shows what their units could reach") {
    // A turn-0 change of +25: a Warrior that can't climb sees nothing new
    // from 1 tile out of the explored 5x5, and 5 stars can't buy +25, but a
    // Rider reaches 2 tiles out, and Climbing sees 2 tiles from a mountain.
    std::vector<Tribe> lineup = {IMPERIUS, OUMAJI};
    Game game(generateMap({.size = 11,
                           .type = MapTypes::LAKES,
                           .tribes = lineup,
                           .seed = 3}),
              lineup, {.seed = 3});
    PlayerView view = makeView(game, 0);
    REQUIRE_FALSE(view.tribes[1]);
    int start = view.scores[1];
    auto after = [&](int change) {
        PlayerView v = view;
        v.scoreHistory.push_back({1, 0, start + change});
        v.effectsInChanges.push_back(0);
        return v;
    };
    TribeSet sharing = tribes({IMPERIUS, BARDUR, KICKOO, OUMAJI, XINXI});
    REQUIRE((tribesWithStartingScore(start) & sharing) == sharing);

    PlayerView far = after(25);
    CHECK_FALSE(scoreChangesFit(far, 1, IMPERIUS));
    CHECK_FALSE(scoreChangesFit(far, 1, BARDUR));
    // a port takes a turn before its boats sail
    CHECK_FALSE(scoreChangesFit(far, 1, KICKOO));
    CHECK(scoreChangesFit(far, 1, OUMAJI));
    CHECK(scoreChangesFit(far, 1, XINXI));

    // a Warrior and a harvest
    PlayerView near = after(15);
    CHECK(scoreChangesFit(near, 1, IMPERIUS));
    CHECK(scoreChangesFit(near, 1, OUMAJI));
}

TEST_CASE("the search scores a state exactly as the engine does") {
    // The coverage test (docs/tribeInference.md section 7): after every
    // command of long random games, the search's model of the score, built
    // from the true state, must equal the engine's. Any way of scoring the
    // search doesn't know about would break it.
    int commands = 0;
    for (std::uint64_t seed = 0; seed < 24; seed++) {
        std::vector<Tribe> lineup = {static_cast<Tribe>(seed % TRIBE_SIZE),
                                     static_cast<Tribe>((seed * 5 + 3) % TRIBE_SIZE)};
        auto type = static_cast<MapTypes::MapType>(seed % MapTypes::MAP_TYPE_SIZE);
        Game game(generateMap({.size = 11, .type = type, .tribes = lineup, .seed = seed}),
                  lineup, {.seed = seed});
        RandomBot bot(seed);
        for (int step = 0; step < 4000 && !game.gameOver(); step++) {
            game.apply(bot.choose(game));
            commands++;
            for (int p = 0; p < 2; p++) {
                REQUIRE(searchModelScore(game, p) == game.score(p));
            }
        }
    }
    MESSAGE(commands << " commands");
}
