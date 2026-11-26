#include "game.h"
#include <cstdlib>
#include <stdexcept>

using namespace CommandTypes;

Game::Game(const MapData &map, const std::vector<TribeTypes::Tribe> &tribes,
           GameSettings settings)
    : size{map.size}, mapType{map.type}, settings{settings}, tiles{map.tiles},
      rng{settings.seed} {
    if (tribes.size() > MAX_PLAYERS) {
        throw std::invalid_argument("Too many players");
    }
    if (tribes.size() != map.capitals.size()) {
        throw std::invalid_argument("Map has " +
                                    std::to_string(map.capitals.size()) +
                                    " capitals but " +
                                    std::to_string(tribes.size()) +
                                    " tribes were given");
    }

    for (std::size_t i = 0; i < tribes.size(); i++) {
        const TribeData &tribe = tribeData[tribes[i]];
        Player &player = players.emplace_back();
        player.tribe = tribe.tribe;
        player.stars = tribe.startStars +
                       (i < settings.headStart.size() ? settings.headStart[i] : 0);
        player.techs.set(TechTypes::BASIC);
        player.techs.set(tribe.startTech);
        player.lastAttackTurn.assign(tribes.size(), -100);
    }
    firstOccupied.assign(players.size() * size * size, -1);
    startedWithRoads = std::any_of(tiles.begin(), tiles.end(),
                                   [](const Tile &t) { return t.road; });

    for (std::size_t i = 0; i < tribes.size(); i++) {
        const TribeData &tribe = tribeData[tribes[i]];
        int player = static_cast<int>(i);
        int tile = map.capitals[i];
        int city = foundCity(tile, player);
        cities[city].capitalOf = player;
        cities[city].level = tribe.capitalLevel;
        players[i].capital = city;
        spawnUnit(player, tribe.startUnit, tile, city);

        // the 5x5 around the capital starts explored, except a lighthouse,
        // which still has to be found
        tiles[tile].explored |= std::uint32_t{1} << player;
        forEachInRadius(tile, Rules::START_EXPLORED_RADIUS, [&](int t) {
            if (tiles[t].building != ImprovementTypes::LIGHTHOUSE) {
                tiles[t].explored |= std::uint32_t{1} << player;
            }
        });
    }

    // each tile's climate: from the map, or else its nearest capital's (the
    // lowest seat on a tie)
    for (int t = 0; t < size * size; t++) {
        int seat = map.regions.empty() ? -1 : map.regions[t];
        if (seat < 0) {
            seat = nearestCapitalSeat(t, map.capitals);
        }
        if (seat >= 0 && seat < static_cast<int>(tribes.size())) {
            tiles[t].climate = tribes[seat];
        }
    }

    updateAllConnections();
    connectionsDue = 0;
    scoreEffects.assign(2 * players.size() * players.size(), 0);
    // everyone sees the starting scores
    for (std::size_t i = 0; i < players.size(); i++) {
        publishScore(static_cast<int>(i));
    }
    checkMeetings();
    startTurn();
}

int Game::distance(int a, int b) const {
    return std::max(std::abs(tileX(a) - tileX(b)),
                    std::abs(tileY(a) - tileY(b)));
}

int Game::tileOwner(int tile) const {
    int city = tiles[tile].territory;
    return city < 0 ? -1 : cities[city].owner;
}

const Unit *Game::unitAt(int tile) const {
    int unit = tiles[tile].unit;
    return unit < 0 ? nullptr : &units[unit];
}

Unit *Game::unitAt(int tile) {
    int unit = tiles[tile].unit;
    return unit < 0 ? nullptr : &units[unit];
}

int Game::cityAt(int tile) const {
    return tiles[tile].building == ImprovementTypes::CITY
               ? tiles[tile].territory
               : -1;
}

int Game::nearestCapitalSeat(int tile, const std::vector<int> &capitals) const {
    int seat = -1, nearest = size;
    for (std::size_t p = 0; p < capitals.size(); p++) {
        if (capitals[p] < 0) {
            continue;
        }
        int d = distance(tile, capitals[p]);
        if (d < nearest) {
            nearest = d;
            seat = static_cast<int>(p);
        }
    }
    return seat;
}

int Game::cityCount(int player) const {
    return static_cast<int>(
        std::count_if(cities.begin(), cities.end(),
                      [player](const City &c) { return c.owner == player; }));
}

bool Game::besieged(int city) const {
    const Unit *unit = unitAt(cities[city].tile);
    // an allied unit on the city doesn't stop it
    return unit && hostile(unit->owner, cities[city].owner);
}

int Game::unitCount(int city) const {
    return static_cast<int>(
        std::count_if(units.begin(), units.end(), [this, city](const Unit &unit) {
            return unit.alive && unit.home == city && !unit.freeOfCap &&
                   (!unit.has(Abilities::INDEPENDENT) ||
                    settings.follows(RuleVersions::GAME_UNIT_HOMES));
        }));
}

int Game::techCost(int player, TechTypes::TechType tech) const {
    // Literacy comes from Philosophy
    return Rules::techCost(techData[tech].tier, cityCount(player),
                           players[player].hasTech(TechTypes::PHILOSOPHY));
}

bool Game::canResearch(int player, TechTypes::TechType tech) const {
    return researchableWith(players[player].techs, tech);
}

int Game::marketIncome(int tile) const {
    int total = 0;
    int owner = tileOwner(tile);
    bool counts = settings.follows(RuleVersions::MARKET_COUNTS_PRODUCERS);
    forEachNeighbour(tile, [&](int n) {
        if (isProducer(tiles[n].building) && tileOwner(n) == owner) {
            total += counts ? std::max(tiles[n].level, 1) : tiles[n].level;
        }
    });
    return std::min(total, MAX_BUILDING_LEVEL);
}

int Game::cityIncome(int city) const {
    const City &c = cities[city];
    if (c.robbed) {
        return 0;  // infiltrated (GAME_INFILTRATION)
    }
    if (settings.follows(RuleVersions::GAME_INCOME)) {
        // anyone else's unit on it stops it (game)
        const Unit *unit = unitAt(c.tile);
        if (unit && unit->owner != c.owner) {
            return 0;
        }
        bool home = c.capitalOf == c.owner;
        int stars = Rules::cityIncome(c.level, home, c.workshop, c.parks, capitalIncome(c.owner));
        if (home) {
            stars += hostedEmbassyIncome(c.owner);
        }
        return std::max(0, stars + std::min(0, c.population));
    }
    if (besieged(city)) {
        return 0;
    }
    int stars = Rules::cityIncome(c.level, players[c.owner].capital == city,
                                  c.workshop, c.parks, capitalIncome(c.owner));
    forEachInRadius(c.tile, Rules::MAX_BORDER_RADIUS, [&](int t) {
        if (tiles[t].territory == city &&
            tiles[t].building == ImprovementTypes::MARKET) {
            stars += marketIncome(t);
        }
    });
    // each point of negative population costs a star
    return std::max(0, stars + std::min(0, c.population));
}

int Game::income(int player) const {
    int total = 0;
    for (std::size_t i = 0; i < cities.size(); i++) {
        if (cities[i].owner == player) {
            total += cityIncome(static_cast<int>(i));
        }
    }
    if (!settings.follows(RuleVersions::GAME_INCOME)) {
        return total + embassyIncome(player);
    }
    // markets on the player's land, each on its own
    for (int t = 0; t < size * size; t++) {
        if (tiles[t].building == ImprovementTypes::MARKET && tileOwner(t) == player) {
            total += marketIncome(t);
        }
    }
    // its embassies, each paid at its host's capital while the host holds
    // it and no one else's unit is on it (game)
    for (const City &c : cities) {
        const Unit *unit = unitAt(c.tile);
        if (c.capitalOf >= 0 && c.capitalOf == c.owner && c.owner != player &&
            !(unit && unit->owner != c.owner)) {
            total += embassyPays(player, c.owner);
        }
    }
    return total;
}

int Game::randomInt(int n) {
    // splitmix64
    std::uint64_t z = (rng += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    z ^= z >> 31;
    return static_cast<int>(z % static_cast<std::uint64_t>(n));
}

// ---- turns ----

void Game::startTurn() {
    Player &player = players[currentPlayer];
    // the starting stars are the first turn's stars; since
    // INCOME_AFTER_CHOICES, the income comes at the end of the command, once
    // nothing waits for an answer (payIncomeIfDue)
    if (turn > 0) {
        if (settings.follows(RuleVersions::INCOME_AFTER_CHOICES)) {
            player.incomeDue = true;
        } else {
            player.stars += income(currentPlayer);
        }
    }

    for (Unit &unit : units) {
        if (unit.alive && unit.owner == currentPlayer) {
            unit.canMove = unit.canAttack = unit.canAct = true;
            unit.attacked = false;
        }
    }
    // the player's temples level up by their age (TEMPLE_LEVEL_KEPT)
    if (settings.follows(RuleVersions::TEMPLE_LEVEL_KEPT)) {
        for (int t = 0; t < size * size; t++) {
            if (isTemple(tiles[t].building) && tileOwner(t) == currentPlayer) {
                tiles[t].level = Rules::templeLevel(tiles[t].builtTurn, turn);
            }
        }
    }
}

void Game::endTurn() {
    // the score other players see updates when its player's turn ends
    publishScore(currentPlayer);

    // Pacifist counts the turns without an attack since Meditation
    Player &player = players[currentPlayer];
    if (player.hasTech(Rules::PACIFIST_TECH)) {
        player.peacefulTurns =
            player.attackedThisTurn && !settings.follows(RuleVersions::PACIFIST_RESET_AT_ATTACK)
                ? 0
                : player.peacefulTurns + 1;
    }
    player.attackedThisTurn = false;
    // a cooldown after someone broke peace with the player lasts its next
    // turn (game)
    player.coolDown = 0;
    // its robbed cities recover (game: EndTurnAction)
    for (City &c : cities) {
        if (c.owner == currentPlayer) {
            c.robbed = false;
        }
    }

    // units that could still use an ability recover automatically
    for (Unit &unit : units) {
        if (unit.alive && unit.owner == currentPlayer && unit.canAct) {
            unit.hp = std::min(unit.maxHp, unit.hp + healAmount(unit));
        }
    }

    int count = static_cast<int>(players.size());
    do {
        currentPlayer = (currentPlayer + 1) % count;
        if (currentPlayer == 0) {
            turn++;
        }
    } while (!players[currentPlayer].alive);
    startTurn();
}

void Game::research(TechTypes::TechType tech) {
    Player &player = players[currentPlayer];
    player.stars -= techCost(currentPlayer, tech);
    player.techs.set(tech);
    if (tech == Rules::PACIFIST_TECH) {
        player.peacefulTurns = 0;
    }
}

// ---- legal commands ----

int Game::pendingRewardCity() const {
    for (std::size_t i = 0; i < cities.size(); i++) {
        if (cities[i].owner == currentPlayer && cities[i].pendingRewards > 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Game::pendingPeaceOffer() const {
    if (!settings.peaceTreaties || !settings.follows(RuleVersions::PEACE_ANSWER_FIRST)) {
        return -1;
    }
    const Player &me = players[currentPlayer];
    for (std::size_t i = 0; i < players.size(); i++) {
        int other = static_cast<int>(i);
        if (players[i].alive && me.hasMet(other) && (me.peaceRequests >> other & 1) &&
            !atPeace(currentPlayer, other)) {
            return other;
        }
    }
    return -1;
}

void Game::addUnitCommands(int unitIdx, std::vector<Command> &out, bool checkStars) const {
    const Unit &unit = units[unitIdx];
    const Player &player = players[unit.owner];
    const Tile &tile = tiles[unit.tile];
    int from = unit.tile;

    if (unit.canMove) {
        std::vector<int> moves, disembarks;
        reachable(unitIdx, &moves, unit.isNaval() ? &disembarks : nullptr);
        for (int to : moves) {
            out.push_back({MOVE, from, to});
        }
        for (int to : disembarks) {
            out.push_back({DISEMBARK, from, to});
        }
    }
    // a boat on a bridge can step out onto it there and then, moved or not
    if (unit.isNaval() && unit.passenger != unit.type && isBridge(from) &&
        settings.follows(RuleVersions::DISEMBARK_ON_BRIDGE)) {
        out.push_back({DISEMBARK, from, from});
    }
    if (unit.canAttack) {
        for (int to : attackTargets(unitIdx)) {
            out.push_back({ATTACK, from, to});
        }
    }
    if (unit.canAct && unit.hp < unit.maxHp) {
        out.push_back({RECOVER, from});
    }
    if (unit.canAct && unit.has(Abilities::HEAL)) {
        bool anyHurt = false;
        forEachNeighbour(from, [&](int n) {
            const Unit *other = unitAt(n);
            anyHurt |= other && other->owner == unit.owner &&
                       other->hp < other->maxHp;
        });
        if (anyHurt) {
            out.push_back({HEALOTHERS, from});
        }
    }
    bool byPassenger = unit.isNaval() && settings.follows(RuleVersions::BOATS_PROMOTE_PASSENGER);
    bool fixed = byPassenger ? unitInfo(unit.passenger).abilities.test(Abilities::STATIC)
                             : unit.has(Abilities::STATIC);
    if (unit.kills >= Rules::VETERAN_KILLS && !unit.veteran && !fixed) {
        out.push_back({PROMOTE, from});
    }
    // only a unit that hasn't moved or attacked this turn
    if (player.hasTech(Rules::DISBAND_TECH) &&
        (!settings.follows(RuleVersions::DISBAND_UNMOVED) || (unit.canMove && unit.canAttack))) {
        out.push_back({DISBAND, from});
    }
    int owner = tileOwner(from);
    bool home = owner == unit.owner ||
                (owner >= 0 && settings.follows(RuleVersions::UPGRADE_IN_ALLIED_BORDERS) &&
                 atPeace(unit.owner, owner));
    if (unit.type == Rules::UPGRADABLE_BOAT && home) {
        for (UnitTypes::UnitType boat :
             {UnitTypes::SCOUT, UnitTypes::RAMMER, UnitTypes::BOMBER}) {
            int price = std::max(0, unitInfo(boat).cost - unit.data().cost);
            if (boat != unit.type && player.hasTech(unitInfo(boat).tech) &&
                (!checkStars || player.stars >= price)) {
                out.push_back({UPGRADE, from, -1, boat});
            }
        }
    }
    if (unit.canAct && !unit.isNaval()) {
        bool village = tile.building == ImprovementTypes::VILLAGE;
        // an ally's city can't be captured
        bool enemyCity = tile.building == ImprovementTypes::CITY &&
                         hostile(unit.owner, tileOwner(from)) &&
                         !(settings.follows(RuleVersions::GAME_COOLDOWN) &&
                           (player.coolDown >> tileOwner(from) & 1));
        if (village || enemyCity) {
            out.push_back({CAPTURE, from});
        }
    }
    // like capturing, only a unit that started its turn on the ruin
    if (unit.canAct && tile.building == ImprovementTypes::RUIN) {
        out.push_back({EXAMINERUINS, from});
    }
    // not under a building or bridge (BUILDINGS_KEEP_RESOURCES), and by a
    // boat that hasn't used its turn (HARVEST_NEEDS_ACTION: the game's
    // "manual" improvement)
    if (unit.isNaval() && tile.resource == ResourceTypes::STARFISH &&
        tile.building == ImprovementTypes::NONE && !tile.road &&
        player.hasTech(Rules::STARFISH_TECH) &&
        (unit.canAct || !settings.follows(RuleVersions::HARVEST_NEEDS_ACTION))) {
        out.push_back({HARVEST, from});
    }
}

void Game::addCityCommands(int city, std::vector<Command> &out) const {
    const City &c = cities[city];
    const Player &player = players[c.owner];
    if (besieged(city) || tiles[c.tile].unit >= 0 ||
        unitCount(city) >= Rules::unitCap(c.level)) {
        return;
    }
    for (const UnitData &data : unitData) {
        if (data.trainable && player.hasTech(data.tech) &&
            player.stars >= data.cost) {
            out.push_back({TRAIN, c.tile, -1, data.type});
        }
    }
}

namespace {
// The improvements that can go on each terrain, so most are never checked.
// Built on first use: improvementData lives in another file, and globals
// across files have no set order of initialization.
const std::vector<ImprovementTypes::ImprovementType> &
buildableOn(TerrainTypes::TerrainType terrain) {
    static const auto table = [] {
        std::array<std::vector<ImprovementTypes::ImprovementType>,
                   TerrainTypes::TERRAIN_TYPE_SIZE>
            out;
        for (int type = ImprovementTypes::FARM;
             type < ImprovementTypes::IMPROVEMENT_TYPE_SIZE; type++) {
            for (int t = 0; t < TerrainTypes::TERRAIN_TYPE_SIZE; t++) {
                if (improvementData[type].terrain.test(t)) {
                    out[t].push_back(
                        static_cast<ImprovementTypes::ImprovementType>(type));
                }
            }
        }
        return out;
    }();
    return table[terrain];
}
} // namespace

void Game::addTileCommands(int tile, std::vector<Command> &out, bool checkStars) const {
    // nothing is ever built on unexplored tiles or inside other players'
    // borders, so most tiles stop here
    int owner = tileOwner(tile);
    if (!explored(currentPlayer, tile) ||
        (owner >= 0 && owner != currentPlayer)) {
        return;
    }
    const Player &player = players[currentPlayer];
    for (ImprovementTypes::ImprovementType type :
         buildableOn(tiles[tile].terrain)) {
        const ImprovementData &data = improvementInfo(type);
        // cheap checks first; canBuild has the final say
        if ((owner < 0 && !data.freelance) || !player.hasTech(data.tech) ||
            (checkStars && player.stars < data.cost)) {
            continue;
        }
        if (canBuild(currentPlayer, tile, type, checkStars)) {
            out.push_back({BUILD, tile, -1, type});
        }
    }
    if (canDestroy(currentPlayer, tile)) {
        out.push_back({DESTROY, tile});
    }
}

void Game::addResearchCommands(std::vector<Command> &out) const {
    for (const TechData &tech : techData) {
        if (canResearch(currentPlayer, tech.type) &&
            players[currentPlayer].stars >= techCost(currentPlayer, tech.type)) {
            out.push_back({RESEARCH, -1, -1, tech.type});
        }
    }
}

std::vector<Command> Game::legalCommands() const {
    std::vector<Command> out;
    if (gameOver()) {
        return out;
    }

    // a level-up reward has to be chosen before anything else
    int rewardCity = pendingRewardCity();
    if (rewardCity >= 0) {
        const City &city = cities[rewardCity];
        int level = city.level - city.pendingRewards + 1;
        for (CityRewards::CityReward reward :
             CityRewards::rewardsForLevel(level)) {
            out.push_back({CITYREWARD, city.tile, -1, reward});
        }
        return out;
    }
    // so does a peace offer, one at a time
    int proposer = pendingPeaceOffer();
    if (proposer >= 0) {
        // arg: the proposer times 2, plus 1 to accept
        out.push_back({PEACEREQUESTRESPONSE, -1, -1, proposer * 2 + 1});
        out.push_back({PEACEREQUESTRESPONSE, -1, -1, proposer * 2});
        return out;
    }

    addResearchCommands(out);
    addDiplomacyCommands(out);
    for (std::size_t i = 0; i < cities.size(); i++) {
        if (cities[i].owner == currentPlayer) {
            addCityCommands(static_cast<int>(i), out);
        }
    }
    for (std::size_t i = 0; i < units.size(); i++) {
        if (units[i].alive && units[i].owner == currentPlayer) {
            addUnitCommands(static_cast<int>(i), out);
        }
    }
    for (int tile = 0; tile < size * size; tile++) {
        addTileCommands(tile, out);
    }
    out.push_back({ENDTURN});
    return out;
}

bool Game::isLegal(const Command &command) const {
    if (gameOver()) {
        return false;
    }
    if (command.type == RESIGN) {
        return true;
    }

    // only the commands that could match are generated
    std::vector<Command> candidates;
    bool validTile =
        command.from >= 0 && command.from < static_cast<int>(tiles.size());
    if (pendingRewardCity() >= 0 || command.type == CITYREWARD || pendingPeaceOffer() >= 0) {
        candidates = legalCommands();
    } else if (command.type == ENDTURN) {
        return true;
    } else if (command.type >= PEACETREATY &&
               command.type <= DESTROYEMBASSY) {
        addDiplomacyCommands(candidates);
    } else if (command.type == RESEARCH) {
        addResearchCommands(candidates);
    } else if (!validTile) {
        return false;
    } else if (command.type == TRAIN) {
        int city = cityAt(command.from);
        if (city >= 0 && cities[city].owner == currentPlayer) {
            addCityCommands(city, candidates);
        }
    } else if (command.type == BUILD || command.type == DESTROY) {
        addTileCommands(command.from, candidates);
    } else {
        const Unit *unit = unitAt(command.from);
        if (unit && unit->owner == currentPlayer) {
            addUnitCommands(tiles[command.from].unit, candidates);
        }
    }
    return std::find(candidates.begin(), candidates.end(), command) !=
           candidates.end();
}

void Game::apply(const Command &command) {
    if (!isLegal(command)) {
        throw std::invalid_argument("Illegal command: " +
                                    toString(*this, command));
    }
    applyUnchecked(command);
}

void Game::applyUnchecked(const Command &command) {
    int actor = command.from >= 0 ? tiles[command.from].unit : -1;
    applyCommand(command);
    if (actor >= 0 && settings.follows(RuleVersions::ACTIONS_SET_ATTACKED)) {
        switch (command.type) {
        case RECOVER:
        case HEALOTHERS:
        case STAY:
        case CAPTURE:
        case EXAMINERUINS:
            units[actor].attacked = true;
            break;
        case HARVEST:
            if (settings.follows(RuleVersions::SPENT_UNITS_ATTACKED)) {
                units[actor].attacked = true;
            }
            break;
        default:
            break;
        }
    }
    if (connectionsDue) {
        std::uint32_t due = connectionsDue;
        connectionsDue = 0;
        updateAllConnections(due);
    }
    if (!gameOver()) {
        // the game looks over everything explored only at a turn's start;
        // within the turn, a tribe is met as a tile showing it is explored
        // (MEET_ON_EXPLORE, in reveal)
        if (!settings.follows(RuleVersions::MEET_ON_EXPLORE) || command.type == ENDTURN) {
            checkMeetings();
        } else {
            revealMetCapitals();
        }
        // a turn's income, once a reward or peace offer waiting at its
        // start is answered (game: the turn's start waits for them)
        Player &player = players[currentPlayer];
        if (player.incomeDue && pendingRewardCity() < 0 && pendingPeaceOffer() < 0) {
            player.incomeDue = false;
            player.stars += income(currentPlayer);
        }
        updateTasks();
    }
}

void Game::applyCommand(const Command &command) {
    Player &player = players[currentPlayer];
    int unit = command.from >= 0 ? tiles[command.from].unit : -1;

    switch (command.type) {
    case MOVE:
        moveUnit(unit, command.to);
        break;
    case DISEMBARK:
        if (command.to == command.from) {
            // on a bridge: the unit leaves its boat where it is
            land(unit);
            Unit &u = units[unit];
            u.canMove = u.canAttack = u.canAct = false;
        } else {
            disembark(unit, command.to);
        }
        break;
    case ATTACK:
        attack(unit, command.to);
        break;
    case RECOVER: {
        Unit &u = units[unit];
        u.hp = std::min(u.maxHp, u.hp + healAmount(u));
        u.canMove = u.canAttack = u.canAct = false;
        break;
    }
    case HEALOTHERS: {
        Unit &u = units[unit];
        forEachNeighbour(u.tile, [&](int n) {
            Unit *other = unitAt(n);
            if (other && other->owner == u.owner) {
                other->hp = std::min(other->maxHp, other->hp + Rules::HEAL_OTHERS);
            }
        });
        u.canMove = u.canAttack = u.canAct = false;
        break;
    }
    case PROMOTE: {
        Unit &u = units[unit];
        u.veteran = true;
        u.maxHp += Rules::VETERAN_HP;
        u.hp = u.maxHp;
        break;
    }
    case DISBAND:
        player.stars += disbandRefund(units[unit]);
        killUnit(unit);
        break;
    case UPGRADE: {
        Unit &u = units[unit];
        auto boat = static_cast<UnitTypes::UnitType>(command.arg);
        player.stars -= std::max(0, unitInfo(boat).cost - u.data().cost);
        u.type = boat;
        if (settings.follows(RuleVersions::UPGRADE_DASHES) && !u.canMove && !u.attacked &&
            u.has(Abilities::DASH)) {
            u.canAttack = true;
        }
        revealForUnit(unit);
        break;
    }
    case BUILD:
        build(command.from,
              static_cast<ImprovementTypes::ImprovementType>(command.arg));
        break;
    case DESTROY:
        destroy(command.from);
        break;
    case HARVEST:
        player.stars += Rules::STARFISH_STARS;
        tiles[command.from].resource = ResourceTypes::NONE;
        if (settings.follows(RuleVersions::HARVEST_ENDS_TURN)) {
            units[unit].canMove = units[unit].canAttack = units[unit].canAct = false;
        }
        if (settings.follows(RuleVersions::HARVEST_REVEALS)) {
            units[unit].hidden = false;
        }
        break;
    case CAPTURE:
        capture(unit);
        break;
    case EXAMINERUINS:
        examineRuins(unit);
        break;
    case TRAIN:
        train(cityAt(command.from),
              static_cast<UnitTypes::UnitType>(command.arg));
        break;
    case RESEARCH:
        research(static_cast<TechTypes::TechType>(command.arg));
        break;
    case CITYREWARD:
        chooseReward(cityAt(command.from),
                     static_cast<CityRewards::CityReward>(command.arg));
        break;
    case PEACETREATY:
    case PEACEREQUESTRESPONSE:
    case BREAKPEACE:
    case ESTABLISHEMBASSY:
    case DESTROYEMBASSY:
        applyDiplomacy(command);
        break;
    case ENDTURN:
        endTurn();
        break;
    case STAY:
        // the unit is done for the turn (game: StayCommand sets moved and
        // attacked); the game's bots play it, players never get it
        units[unit].canMove = units[unit].canAttack = units[unit].canAct = false;
        break;
    case RESIGN:
        eliminate(currentPlayer);
        checkGameEnd();
        if (!gameOver()) {
            endTurn();
        }
        break;
    default:
        throw std::invalid_argument("Command not implemented yet: " +
                                    toString(*this, command));
    }
}
