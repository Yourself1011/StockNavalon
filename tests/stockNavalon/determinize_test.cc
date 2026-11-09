#include "bots/randomBot.h"
#include "determinize.h"
#include "doctest/doctest.h"
#include "encoding.h"
#include "helpers.h"
#include "mapgen.h"
#include "view.h"

using namespace TribeTypes;

namespace {
struct Encoded {
    std::vector<float> planes, globals;
    bool operator==(const Encoded &) const = default;
};
Encoded encode(const Game &game, int player) {
    EncodingConfig config{.mapSize = std::max(11, game.size)};
    Encoded out{std::vector<float>(planeCount(config) * config.mapSize *
                                   config.mapSize),
                std::vector<float>(globalCount(config))};
    encodeState(game, player, config, out.planes.data(), out.globals.data());
    return out;
}

// everything in the game, for comparing two samples
bool sameGame(const Game &a, const Game &b) {
    bool players = a.players.size() == b.players.size();
    for (std::size_t i = 0; players && i < a.players.size(); i++) {
        const Player &p = a.players[i], &o = b.players[i];
        players &= p.tribe == o.tribe && p.stars == o.stars &&
                   p.techs == o.techs && p.capital == o.capital &&
                   p.alive == o.alive && p.lighthousesFound == o.lighthousesFound &&
                   p.met == o.met && p.tasksDone == o.tasksDone &&
                   p.monumentsBuilt == o.monumentsBuilt && p.kills == o.kills &&
                   p.peacefulTurns == o.peacefulTurns &&
                   p.attackedThisTurn == o.attackedThisTurn &&
                   p.attackedOnTurns == o.attackedOnTurns && p.peace == o.peace &&
                   p.peaceRequests == o.peaceRequests && p.embassies == o.embassies;
    }
    return players && a.tiles == b.tiles && a.units == b.units &&
           a.cities == b.cities && a.rng == b.rng &&
           a.settings.seed == b.settings.seed &&
           a.firstOccupied == b.firstOccupied &&
           a.scoreHistory == b.scoreHistory && a.turn == b.turn &&
           a.currentPlayer == b.currentPlayer;
}

Game randomGame(std::uint64_t seed, int commands) {
    std::vector<Tribe> lineup = {static_cast<Tribe>(seed % TRIBE_SIZE),
                                 static_cast<Tribe>((seed * 5 + 2) % TRIBE_SIZE)};
    Game game(generateMap({.size = 11,
                           .type = static_cast<MapTypes::MapType>(seed % 6),
                           .tribes = lineup,
                           .seed = seed}),
              lineup, {.seed = seed});
    RandomBot bot(seed);
    for (int i = 0; i < commands && !game.gameOver(); i++) {
        game.apply(bot.choose(game));
    }
    return game;
}
} // namespace

TEST_CASE("a sample agrees with everything the player knows") {
    int samples = 0, boats = 0;
    DeterminizeStats stats;
    for (std::uint64_t seed = 0; seed < 24; seed++) {
        for (int commands : {0, 40, 150, 400}) {
            Game real = randomGame(seed, commands);
            if (real.gameOver()) {
                continue;
            }
            int player = real.currentPlayer, q = 1 - player;
            Beliefs beliefs = priorBeliefs(makeView(real, player));
            for (Detail detail : {Detail::TILES_AND_UNITS, Detail::EVERYTHING}) {
                std::optional<Game> sample = determinize(
                    real, player, beliefs, seed * 7 + commands, detail, 50, &stats);
                if (!sample) {
                    continue;
                }
                samples++;
                checkInvariants(*sample);
                PlayerView sampleView = makeView(*sample, player);
                for (const Unit &u : sample->units) {
                    boats += u.alive && u.owner == q && u.isNaval() &&
                             !sampleView.tiles[u.tile].explored;
                }
                // the player's input can't tell the sample from the real game
                CHECK(encode(*sample, player) == encode(real, player));
                if (detail == Detail::EVERYTHING) {
                    CHECK(sample->score(q) == real.publishedScore(q) +
                                                  real.effectsSincePublished(player, q));
                }
                // and it plays on like any game
                RandomBot bot(seed);
                for (int i = 0; i < 100 && !sample->gameOver(); i++) {
                    sample->apply(bot.choose(*sample));
                    checkInvariants(*sample);
                }
            }
        }
    }
    MESSAGE(samples << " samples; " << stats.rejected << " of " << stats.tries
                    << " draws rejected; " << boats << " boats sampled in the fog");
    CHECK(samples > 100);
}

TEST_CASE("a sample doesn't depend on anything the player can't see") {
    int compared = 0;
    for (std::uint64_t seed = 0; seed < 24; seed++) {
        Game real = randomGame(seed, 150);
        if (real.gameOver()) {
            continue;
        }
        int player = real.currentPlayer, q = 1 - player;
        PlayerView view = makeView(real, player);
        Game other = real;

        // their stars and techs
        other.players[q].stars += 7;
        for (int t = 1; t < TechTypes::TECH_TYPE_SIZE; t++) {
            if (!other.players[q].techs.test(t)) {
                other.players[q].techs.set(t);
                break;
            }
        }
        // unexplored terrain, and one of their units moved within the fog
        bool moved = false;
        for (int t = 0; t < real.size * real.size; t++) {
            if (view.tiles[t].explored) {
                continue;
            }
            Tile &tile = other.tiles[t];
            if (tile.unit < 0 && tile.building == ImprovementTypes::NONE &&
                tile.terrain == TerrainTypes::FIELD) {
                tile.terrain = TerrainTypes::FOREST;
            }
        }
        for (Unit &u : other.units) {
            if (moved || !u.alive || u.owner != q || view.tiles[u.tile].explored) {
                continue;
            }
            for (int t = 0; t < real.size * real.size && !moved; t++) {
                if (!view.tiles[t].explored && other.tiles[t].unit < 0 &&
                    other.canEnterTerrain(u, t)) {
                    other.tiles[t].unit = other.tiles[u.tile].unit;
                    other.tiles[u.tile].unit = -1;
                    u.tile = t;
                    moved = true;
                }
            }
        }

        Beliefs beliefs = priorBeliefs(view);
        for (Detail detail : {Detail::TILES_AND_UNITS, Detail::EVERYTHING}) {
            std::optional<Game> a = determinize(real, player, beliefs, seed, detail, 50);
            std::optional<Game> b = determinize(other, player, beliefs, seed, detail, 50);
            REQUIRE(a.has_value() == b.has_value());
            if (a) {
                CHECK(sameGame(*a, *b));
                compared++;
            }
        }
    }
    MESSAGE(compared << " pairs compared");
    CHECK(compared > 20);
}

TEST_CASE("a hidden unit the player was warned of is resampled next to the warning") {
    // 12x12, capitals at (2,2) and (9,9): player 0 has explored x, y <= 4
    Game real = makeFogGame(bigMap());
    clearUnits(real);
    addUnit(real, 0, UnitTypes::WARRIOR, 3, 3);
    real.revealAround(0, real.index(3, 3), 1);
    real.players[1].techs.set(TechTypes::DIPLOMACY);
    real.players[1].techs.set(TechTypes::FREESPIRIT);
    auto withCloakAt = [&](int x, int y) {
        Game g = real;
        int cloak = addUnit(g, 1, UnitTypes::CLOAK, x, y);
        g.units[cloak].hidden = true;
        return g;
    };
    Game a = withCloakAt(4, 4), b = withCloakAt(2, 4);
    REQUIRE(makeView(a, 0).tiles[a.index(4, 4)].explored);
    REQUIRE(makeView(a, 0).tiles[a.index(4, 4)].unit < 0);
    auto warned = [](const PlayerView &view) {
        return std::any_of(view.units.begin(), view.units.end(),
                           [&](const PlayerView::UnitView &u) {
                               return u.owner == view.player && u.hiddenEnemyNear;
                           });
    };
    REQUIRE(warned(makeView(a, 0)));

    Beliefs beliefs = priorBeliefs(makeView(a, 0));
    for (std::uint64_t seed = 0; seed < 20; seed++) {
        std::optional<Game> sa =
            determinize(a, 0, beliefs, seed, Detail::TILES_AND_UNITS);
        std::optional<Game> sb =
            determinize(b, 0, beliefs, seed, Detail::TILES_AND_UNITS);
        REQUIRE(sa);
        REQUIRE(sb);
        // the warning stays, wherever the unit went
        checkInvariants(*sa);
        CHECK(warned(makeView(*sa, 0)));
        CHECK(encode(*sa, 0) == encode(a, 0));
        // and where it really was doesn't matter
        CHECK(sameGame(*sa, *sb));
    }
}

TEST_CASE("only what nothing else explains proves a tech") {
    using namespace UnitTypes;
    // player 0 has explored x, y <= 4; their units there are visible
    auto game = [] {
        Game g = makeFogGame(bigMap(), {IMPERIUS, BARDUR});
        clearUnits(g);
        return g;
    };
    auto add = [](Game &g, UnitType type, int x, int y, bool veteran) {
        int unit = addUnit(g, 1, type, x, y);
        g.units[unit].veteran = veteran;
        REQUIRE(makeView(g, 0).tiles[g.index(x, y)].unit >= 0);
    };
    // how many of 40 samples give them the tech
    auto owning = [](const Game &g, TechTypes::TechType tech) {
        Beliefs beliefs = priorBeliefs(makeView(g, 0));
        int count = 0;
        for (std::uint64_t seed = 0; seed < 40; seed++) {
            std::optional<Game> s =
                determinize(g, 0, beliefs, seed, Detail::TILES_AND_UNITS);
            REQUIRE(s);
            count += s->players[1].techs.test(tech);
        }
        return count;
    };
    const TechTypes::TechType archery = unitInfo(ARCHER).tech,
                              smithery = unitInfo(SWORDSMAN).tech;

    SUBCASE("an Archer of theirs proves Archery, veteran or not") {
        Game g = game();
        add(g, ARCHER, 3, 4, true);
        CHECK(owning(g, archery) == 40);
    }
    SUBCASE("unless the player lost one to conversion") {
        Game g = game();
        add(g, ARCHER, 3, 4, false);
        g.players[0].convertedAway.push_back({ARCHER, ARCHER});
        CHECK(owning(g, archery) < 40);
        // but not two
        add(g, ARCHER, 4, 4, false);
        CHECK(owning(g, archery) == 40);
    }
    SUBCASE("veteran Swordsmen prove Smithery once there are more than ruins") {
        Game g = game();
        add(g, SWORDSMAN, 3, 4, true);
        CHECK(owning(g, smithery) < 40);
        // a plain one was trained
        Game plain = game();
        add(plain, SWORDSMAN, 3, 4, false);
        CHECK(owning(plain, smithery) == 40);
        // more veterans than ruins on the map
        Game many = game();
        int ruins = ruinCountForSize(many.size);
        for (int i = 0; i <= ruins; i++) {
            add(many, SWORDSMAN, i % 5, 3 + i / 5, true);
        }
        CHECK(owning(many, smithery) == 40);
    }
    SUBCASE("a road the player didn't build proves Roads") {
        const TechTypes::TechType roads =
            improvementData[ImprovementTypes::ROAD].tech;
        Game g = game();
        g.tiles[g.index(4, 4)].road = true;
        g.tiles[g.index(4, 4)].roadBuilder = 1;
        CHECK(owning(g, roads) == 40);
        Game own = game();
        own.tiles[own.index(4, 4)].road = true;
        own.tiles[own.index(4, 4)].roadBuilder = 0;
        CHECK(owning(own, roads) < 40);
    }
}
