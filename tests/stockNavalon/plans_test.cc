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
        // capitals can start on one landmass, as in the real game, and then
        // the plan has nothing to do
        bool joinedAtStart = planDone(Plan::BRIDGE_RUSH, game);
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
        if (!joinedAtStart) {
            CHECK(techTurn >= 0);
            CHECK(techTurn <= 6);
        }
        joined += done;
        CHECK(r.replaysLegally());
    }
    MESSAGE("joined in " << joined << " of 8 games with " << bridges
                         << " bridges; the free side built " << freeBridges);
    // 25 of 32 games on these maps: 8 games give 5 to 7
    CHECK(joined >= 5);
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

TEST_CASE("a plan once ahead: it starts at the side's first decision after its value reached the mark, and lasts its turns") {
    // network sides (the score evaluator answering), so the search values
    // that trigger the plan are kept
    SelfPlayConfig config;
    config.search.simulations = 16;
    config.search.samples = 1;
    config.quickSimulations = 4;
    config.fullShare = 1.0;
    config.fogShare = 0.0;
    config.maxTurns = 30;
    config.paired = true;
    config.sidePlans = {Plan::CONVERT, Plan::NONE};
    config.planTurns = 8;
    config.planWhenAhead = 0.3;
    config.mapTypes = {MapTypes::CONTINENTS};
    SelfPlaySession session(config, 8, 8, 5);
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
    std::vector<GameRecord> records = session.takeFinished();
    REQUIRE(records.size() == 8);
    int triggered = 0, crossings = 0;
    for (const GameRecord &r : records) {
        int seat = forcedSeat(r);
        Game game = r.start();
        int reached = -1, start = -1;
        for (const RecordedMove &m : r.moves) {
            const Command &c = m.command;
            if (m.player == seat) {
                // the plan starts at the first decision after the value got there
                if (reached >= 0 && start < 0) {
                    start = game.turn;
                }
                bool inPlan = start >= 0 && game.turn < start + config.planTurns;
                // a forced decision records no targets; outside the plan a full
                // search does
                CHECK(m.visits.empty() == inPlan);
                if (inPlan) {
                    std::vector<Command> legal;
                    for (const Command &l : game.legalCommands()) {
                        if (l.type != RESIGN && l.type != STAY) {
                            legal.push_back(l);
                        }
                    }
                    std::vector<Command> allowed = planAllowed(Plan::CONVERT, game, legal);
                    CHECK(std::find(allowed.begin(), allowed.end(), c) != allowed.end());
                    crossings += c.type == BUILD && (c.arg == ImprovementTypes::BRIDGE ||
                                                     c.arg == ImprovementTypes::PORT);
                }
                if (reached < 0 && !std::isnan(m.rootValue) && m.rootValue >= config.planWhenAhead) {
                    reached = game.turn;
                }
            }
            game.apply(c);
        }
        triggered += start >= 0;
    }
    MESSAGE("the plan started in " << triggered << " of 8 games; " << crossings
                                   << " bridges or ports in it");
    CHECK(triggered >= 2);
}
