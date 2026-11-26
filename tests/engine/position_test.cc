#include "bots/randomBot.h"
#include "doctest/doctest.h"
#include "helpers.h"
#include "mapgen.h"
#include <unordered_map>

using namespace CommandTypes;
using namespace UnitTypes;

namespace {
// The same position, field by field, whatever order things were made in:
// both games canonicalized (units and cities by tile), then compared with
// the state's own operator== wherever it has one. Written apart from
// positionHash, so a field one of them forgets shows up.
bool samePosition(Game a, Game b) {
    a.canonicalize();
    b.canonicalize();
    return a.tiles == b.tiles && a.units == b.units && a.cities == b.cities &&
           a.players == b.players && a.currentPlayer == b.currentPlayer && a.turn == b.turn &&
           a.winner == b.winner && a.scoreHistory == b.scoreHistory &&
           a.scoreEffects == b.scoreEffects && a.scoreEffectHistory == b.scoreEffectHistory &&
           a.firstOccupied == b.firstOccupied && a.waterRoute == b.waterRoute &&
           a.populationQueue == b.populationQueue && a.connectionsDue == b.connectionsDue &&
           a.rng == b.rng && a.forced.ruinReward == b.forced.ruinReward &&
           a.forced.ruinTech == b.forced.ruinTech && a.forced.explored == b.forced.explored &&
           a.forced.convertedHome == b.forced.convertedHome &&
           a.forced.daggers == b.forced.daggers;
}

Game after(Game game, const std::vector<Command> &commands) {
    for (const Command &c : commands) {
        game.apply(c);
    }
    return game;
}
} // namespace

TEST_CASE("the same commands in either order give the same position hash") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    addUnit(game, 0, WARRIOR, 3, 3);
    addUnit(game, 0, RIDER, 5, 5);
    game.players[0].stars = 20;
    Command a{MOVE, game.index(3, 3), game.index(3, 4)};
    Command b{MOVE, game.index(5, 5), game.index(6, 5)};
    REQUIRE(game.isLegal(a));
    REQUIRE(game.isLegal(b));
    Game ab = after(game, {a, b}), ba = after(game, {b, a});
    CHECK(samePosition(ab, ba));
    CHECK(ab.positionHash() == ba.positionHash());
    CHECK(ab.positionHash() != game.positionHash());

    SUBCASE("units made in a different order: by tile, not index") {
        Game x = ab, y = ba;
        int u = x.spawnUnit(0, WARRIOR, x.index(6, 6), -1);
        x.spawnUnit(0, WARRIOR, x.index(7, 7), -1);
        y.spawnUnit(0, WARRIOR, y.index(7, 7), -1);
        int v = y.spawnUnit(0, WARRIOR, y.index(6, 6), -1);
        REQUIRE(u != v);
        CHECK(x.positionHash() == y.positionHash());
    }
}

TEST_CASE("a position differing in any part of the state hashes differently") {
    Game game = makeGame(fieldMap());
    clearUnits(game);
    int w = addUnit(game, 0, WARRIOR, 3, 3);
    int city = game.players[0].capital;
    std::uint64_t base = game.positionHash();
    auto differs = [&](auto change) {
        Game copy = game;
        change(copy);
        return copy.positionHash() != base;
    };
    CHECK(differs([&](Game &g) { g.units[w].facing = (g.units[w].facing + 1) % 8; }));
    CHECK(differs([&](Game &g) { g.units[w].hp -= 10; }));
    CHECK(differs([&](Game &g) { g.units[w].attacked = !g.units[w].attacked; }));
    CHECK(differs([&](Game &g) { g.units[w].canMove = !g.units[w].canMove; }));
    CHECK(differs([&](Game &g) { g.units[w].home = g.units[w].home == city ? -1 : city; }));
    CHECK(differs([&](Game &g) { g.cities[city].population++; }));
    CHECK(differs([&](Game &g) { g.cities[city].robbed = true; }));
    CHECK(differs([&](Game &g) { g.players[0].stars++; }));
    CHECK(differs([&](Game &g) { g.players[1].peacefulTurns++; }));
    CHECK(differs([&](Game &g) { g.tiles[g.index(7, 7)].explored ^= 1; }));
    CHECK(differs([&](Game &g) { g.tiles[g.index(7, 7)].roadBuilder = 1; }));
    CHECK(differs([&](Game &g) { g.rng++; }));
    CHECK(differs([&](Game &g) { g.populationQueue.push_back({city, 1}); }));
    CHECK(differs([&](Game &g) { g.forced.ruinReward = 0; }));
    CHECK(differs([&](Game &g) { g.scoreEffects[1]++; }));
    CHECK(differs([&](Game &g) { g.firstOccupied[0] = 5; }));
}

TEST_CASE("random games: equal hashes exactly when the positions are the same") {
    // Pairs of commands tried in both orders all through random games: the
    // same position must hash the same (the order left out), and an equal
    // hash must be the same position (no part of the state left out). Every
    // position seen is also checked against every other with its hash.
    std::unordered_map<std::uint64_t, Game> seen;
    int transpositions = 0, checked = 0;
    for (std::uint64_t seed = 0; seed < 6; seed++) {
        std::vector<TribeTypes::Tribe> lineup = {static_cast<TribeTypes::Tribe>(seed % 12),
                                                 static_cast<TribeTypes::Tribe>((seed + 5) % 12)};
        Game game(generateMap({.size = 11,
                               .type = static_cast<MapTypes::MapType>(seed % 6),
                               .tribes = lineup,
                               .seed = seed}),
                  lineup, {.seed = seed});
        RandomBot bot(seed);
        for (int step = 0; step < 300 && !game.gameOver(); step++) {
            std::vector<Command> legal = game.legalCommands();
            for (std::size_t i = 0; i + 1 < legal.size() && i < 6; i += 2) {
                const Command &a = legal[i], &b = legal[i + 1];
                if (a.type == ENDTURN || b.type == ENDTURN) {
                    continue;
                }
                Game ga = game, gb = game;
                ga.apply(a);
                gb.apply(b);
                if (!ga.isLegal(b) || !gb.isLegal(a)) {
                    continue;
                }
                ga.apply(b);
                gb.apply(a);
                bool same = samePosition(ga, gb);
                bool equalHash = ga.positionHash() == gb.positionHash();
                CHECK(same == equalHash);
                transpositions += same;
                checked++;
            }
            for (const Game *g : {&game}) {
                auto [it, added] = seen.try_emplace(g->positionHash(), *g);
                if (!added) {
                    CHECK(samePosition(it->second, *g));
                }
            }
            game.apply(bot.choose(game));
        }
    }
    MESSAGE(checked << " pairs tried both ways, " << transpositions << " the same position");
    CHECK(transpositions > 20);
    CHECK(checked > transpositions);
}
