#include "doctest/doctest.h"
#include "plans.h"
#include "selfPlay.h"

using namespace CommandTypes;

namespace {
// games on `map` with side 0 forced into `plan`, the score evaluator on both
// sides
std::vector<GameRecord> forcedGames(Plan plan, int games, int planTurns, int maxTurns,
                                    MapTypes::MapType map = MapTypes::CONTINENTS) {
    SelfPlayConfig config;
    config.search.simulations = 16;
    config.search.samples = 1;
    config.quickSimulations = 4;
    config.fullShare = 1.0;
    config.fogShare = 0.0;
    config.maxTurns = maxTurns;
    config.sides = {SideKind::SCORE, SideKind::SCORE};
    config.paired = true;
    config.sidePlans = {plan, Plan::NONE};
    config.planTurns = planTurns;
    config.mapTypes = {map};
    SelfPlaySession session(config, games, games, 11);
    ScoreEvaluator evaluator;
    std::vector<Evaluation> results;
    while (!session.done()) {
        const std::vector<EvalRequest> &requests = session.pending();
        if (requests.empty()) {
            break;
        }
        evaluator.evaluate(requests, results);
        session.submit(results);
    }
    return session.takeFinished();
}

// the seat side 0 played
int forcedSeat(const GameRecord &r) { return r.seatZeroSide == 0 ? 0 : 1; }
} // namespace

TEST_CASE("plans by name") {
    CHECK(planNamed("bridge") == Plan::BRIDGE_RUSH);
    CHECK(planNamed("naval") == Plan::NAVAL);
    CHECK(planNamed(planName(Plan::NONE)) == Plan::NONE);
    CHECK_THROWS(planNamed("castle"));
}

TEST_CASE("a bridge rush on continents: the bridge's tech, the fewest bridges, then free play") {
    const int planTurns = 20;
    std::vector<GameRecord> records = forcedGames(Plan::BRIDGE_RUSH, 8, planTurns, 25);
    REQUIRE(records.size() == 8);
    const TechTypes::TechType bridgeTech = improvementInfo(ImprovementTypes::BRIDGE).tech;
    int joined = 0, bridges = 0, freeBridges = 0;
    for (const GameRecord &r : records) {
        CHECK(r.map.type == MapTypes::CONTINENTS);
        int seat = forcedSeat(r);
        Game game = r.start();
        int techTurn = -1;
        bool done = false;
        for (const RecordedMove &m : r.moves) {
            const Command &c = m.command;
            if (m.player == seat && game.turn < planTurns) {
                // no policy targets in the plan's turns
                CHECK(m.visits.empty());
                std::vector<Command> legal;
                for (const Command &l : game.legalCommands()) {
                    if (l.type != RESIGN && l.type != STAY) {
                        legal.push_back(l);
                    }
                }
                std::vector<Command> allowed = planAllowed(Plan::BRIDGE_RUSH, game, legal);
                CHECK(std::find(allowed.begin(), allowed.end(), c) != allowed.end());
                // once the landmasses are joined it constrains nothing
                if (done) {
                    CHECK(allowed.size() == legal.size());
                }
                if (c.type == BUILD && c.arg == ImprovementTypes::BRIDGE) {
                    bridges++;
                }
            } else if (c.type == BUILD && c.arg == ImprovementTypes::BRIDGE) {
                freeBridges++;
            }
            game.apply(c);
            if (techTurn < 0 && game.players[seat].hasTech(bridgeTech)) {
                techTurn = game.turn;
            }
            // joined: the plan is done for good
            if (game.currentPlayer == seat) {
                bool now = planDone(Plan::BRIDGE_RUSH, game);
                CHECK((now || !done));
                done = now;
            }
        }
        // the bridge's tech (two techs from the start) early
        CHECK(techTurn >= 0);
        CHECK(techTurn <= 6);
        joined += done;
        CHECK(r.replaysLegally());
    }
    MESSAGE("joined in " << joined << " of 8 games with " << bridges
                         << " bridges; the free side built " << freeBridges);
    CHECK(joined >= 6);
    // continents need about one crossing: no more than two a game on average
    CHECK(bridges <= 16);
}

TEST_CASE("a naval plan: the port's tech, a port, then over the sea, every move one it allows") {
    const int planTurns = 20;
    for (MapTypes::MapType map : {MapTypes::WATERWORLD, MapTypes::CONTINENTS}) {
        CAPTURE(static_cast<int>(map));
        std::vector<GameRecord> records = forcedGames(Plan::NAVAL, 8, planTurns, 25, map);
        REQUIRE(records.size() == 8);
        int ports = 0, landings = 0;
        for (const GameRecord &r : records) {
            int seat = forcedSeat(r);
            Game game = r.start();
            bool port = false, landed = false;
            for (const RecordedMove &m : r.moves) {
                const Command &c = m.command;
                if (m.player == seat && game.turn < planTurns) {
                    CHECK(m.visits.empty());
                    std::vector<Command> legal;
                    for (const Command &l : game.legalCommands()) {
                        if (l.type != RESIGN && l.type != STAY) {
                            legal.push_back(l);
                        }
                    }
                    std::vector<Command> allowed = planAllowed(Plan::NAVAL, game, legal);
                    CHECK(std::find(allowed.begin(), allowed.end(), c) != allowed.end());
                    port |= c.type == BUILD && c.arg == ImprovementTypes::PORT;
                    landed |= c.type == DISEMBARK;
                }
                game.apply(c);
            }
            ports += port;
            landings += landed;
            CHECK(r.replaysLegally());
        }
        MESSAGE("map " << static_cast<int>(map) << ": a port in " << ports
                       << " of 8 games, landed somewhere in " << landings);
        CHECK(ports >= 6);
        CHECK(landings >= 1);
    }
}
