#pragma once

#include "city.h"
#include "command.h"
#include "data/improvementData.h"
#include "data/techData.h"
#include "data/tribeData.h"
#include "data/unitData.h"
#include "map.h"
#include "player.h"
#include "rules.h"
#include "tile.h"
#include "unit.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace WinConditions {
enum WinCondition { MIGHT, DOMINATION };
} // namespace WinConditions

// A score as other players see it: published when its player ends a turn
struct ScoreRecord {
    int player;
    // the turn that just ended
    int turn;
    int score;

    bool operator==(const ScoreRecord &) const = default;
};

// From a turn on, a player's capital earns this much more (a handicap added
// partway through a game, GameSettings::incomeBonuses)
struct IncomeBonus {
    int player = 0, fromTurn = 0, extra = 0;
    bool operator==(const IncomeBonus &) const = default;
};

// The engine's rule fixes, in order. Each fix to how the engine plays the
// game gets the next version, and the engine follows each rule from its
// version on (GameSettings::follows), so a game recorded under older rules
// replays as it was played: its record keeps the version. Otherwise a replay
// can drift into states that never happened (a meeting at a different time,
// different stars), and training on it goes wrong.
namespace RuleVersions {
// an embassy or a peace offer introduces the sender to the other tribe
constexpr int DIPLOMACY_MEETS = 1;
// a pushed unit (a Giant, or a ruin's unit appearing where it stands) only
// goes to one of the 8 neighbouring tiles, and with none free it's destroyed:
// the "houdini" (before: anywhere on the map)
constexpr int ADJACENT_PUSHES = 2;
// enemies in fog exert zone of control (before: only units the mover saw);
// hidden units (Hide) never do
constexpr int UNSEEN_ZONE_OF_CONTROL = 3;
// a Giant embarking stomps as the Juggernaut it becomes
constexpr int EMBARK_STOMP = 4;
// only a unit that hasn't moved or attacked this turn can be disbanded
constexpr int DISBAND_UNMOVED = 5;
// only the player whose turn it is meets tribes: at the start of the turn,
// as it explores, and a peace offer's receiver at the start of its turn
// (before: every player, as soon as they saw a tribe or received its offer)
constexpr int TURN_PLAYER_MEETS = 6;
// in a two-player game, meeting the player who moves first is worth more
constexpr int FIRST_PLAYER_MEETING_BONUS = 7;
// a road can go on a ruin or a lighthouse (before: on no map feature)
constexpr int ROADS_ON_RUINS_AND_LIGHTHOUSES = 8;
// monuments can be destroyed like other buildings
constexpr int DESTROY_MONUMENTS = 9;
// a village counts as a road for movement, as a city does
constexpr int VILLAGE_ROADS = 10;
// a peace offer has to be answered before anything else
constexpr int PEACE_ANSWER_FIRST = 11;
// a move takes the route the game's own path finder picks among routes of
// equal cost (searchPath), which decides what it reveals on the way
constexpr int GAME_PATHS = 12;
// breaking peace starts a cooldown for the other side: it can't attack the
// breaker in its next turn (before: no cooldown)
constexpr int PEACE_COOLDOWN = 13;
// a temple levels up at its owner's turn start (Rules::templeTurn; before:
// the first player's)
constexpr int TEMPLE_OWNER_TURNS = 14;
// a city below zero population loses STARVATION_SCORE per point from its
// score, down to 0
constexpr int CITY_STARVATION = 15;
// a building keeps the resource it stands on (a farm its crop, a port its
// starfish), so destroying it leaves the resource (before: built over)
constexpr int BUILDINGS_KEEP_RESOURCES = 16;
// an embassy costs more for each one the builder already has
constexpr int GROWING_EMBASSY_COST = 17;
// a unit scores by its own cost plus its passenger's (before: a boat by its
// passenger's alone)
constexpr int FULL_UNIT_SCORE = 18;
// a pushed unit tries the 8 directions in the game's order, from the one it
// faces (pushUnit)
constexpr int GAME_PUSH_ORDER = 19;
// no move ends on the city of a player at peace with the mover, or one the
// mover broke peace with and is still cooling down from
constexpr int NO_STOP_ON_PEACEFUL_CITIES = 20;
// a bridge can be destroyed
constexpr int DESTROY_BRIDGES = 21;
// city connections as the game finds them (connectedCities): from the
// original capital over roads, bridges, cities, villages and ports, a step
// into another player's land only where its owner and the land stepped from
// are at peace, fog or not; and recomputed when the game does, at the end of
// a command that built a road, bridge or port, destroyed something, examined
// a ruin, grew borders, answered a peace offer or explored a tile (CAPTURE_CONNECTS adds captures; before:
// at once, after every change, and over explored roads only)
constexpr int GAME_CONNECTIONS = 22;
// there's no Destroy Embassy: nothing in the game or its client makes the
// command (only loading a saved game does)
constexpr int NO_DESTROY_EMBASSY = 23;
// a Creep unit takes no road steps (Mover::roads)
constexpr int CREEP_NO_ROADS = 24;
// a bridge can't be entered straight from its side (Game::bridgeSideStep)
constexpr int BRIDGE_SIDES = 25;
// a unit heals inside a peace partner's borders as inside its own's
constexpr int ALLIED_HEALING = 26;
// Pacifist: an attack sets the count back to 0 there and then, and every
// turn's end adds one, that turn's too (before: a turn with an attack ended
// at 0)
constexpr int PACIFIST_RESET_AT_ATTACK = 27;
// capturing a city recomputes connections (GAME_CONNECTIONS otherwise only
// did if the capture explored a tile)
constexpr int CAPTURE_CONNECTS = 28;
// a pushed land unit can land on a port it could embark at, as the boat it
// becomes there
constexpr int PUSH_ONTO_WATER = 29;
// two players are at war from either's attack on the other until the
// attacker's next turn ends (Game::atWar), and embassies between them (built
// since: the attack destroyed the others) pay nothing meanwhile
constexpr int EMBASSIES_AT_WAR = 30;
// a boat on a bridge can disembark where it is ({DISEMBARK, tile, tile}),
// after moving there too
constexpr int DISEMBARK_ON_BRIDGE = 31;
// within a turn, a tribe is met only as a tile showing one of its units or
// cities is explored, not by a unit of theirs already in view (the whole
// explored map is looked over at the turn's start)
constexpr int MEET_ON_EXPLORE = 32;
// a unit pushed onto a port embarks, which ends its turn
constexpr int PUSH_EMBARK_STOPS = 33;
// harvesting starfish takes a boat that hasn't moved or attacked this turn
constexpr int HARVEST_NEEDS_ACTION = 34;
// a turn's income is paid once a reward or peace offer waiting at its start
// is answered, so a Workshop picked then already counts
constexpr int INCOME_AFTER_CHOICES = 35;
// a converted unit's score goes to its new owner (before: it stayed with
// the first), and the two face each other as after an attack
constexpr int CONVERSION_TAKES_SCORE = 36;
// population comes a point at a time in the game's action order, and a
// level-up holds back the rest until its reward is chosen (game: the
// ActionStack stops at a pending CityLevelUp trigger)
constexpr int POPULATION_STACK = 37;
// a bridge ends water routes too, to routers at most a tile away from it
// and over shallow water only
constexpr int BRIDGE_ROUTES = 38;
// a water route leaves a city's land only onto a port or a bridge
constexpr int ROUTE_LANDINGS = 39;
// connections are recomputed only for the players something concerned:
// the one acting, both sides of a peace answer, everyone for border growth
// (game: UpdateCityConnectionsAction's players)
constexpr int ACTING_CONNECTIONS = 40;
// splash and stomp: half of what an attack on each victim would deal,
// floored to whole HP; they spare units the attacker has a cooldown with;
// splash victims are hit after the main kill, and turn as in an attack
// (before: half the main damage, or half unrounded for a stomp)
constexpr int AREA_DAMAGE_FLOORED = 41;
// kills count toward promotion only for units that aren't boats or Static
// (game: not IsVehicle)
constexpr int BOATS_GET_NO_KILLS = 42;
// a unit doesn't land on such a city either (NO_STOP_ON_PEACEFUL_CITIES)
constexpr int NO_LANDING_ON_PEACEFUL_CITIES = 43;
// harvesting starfish ends the boat's turn
constexpr int HARVEST_ENDS_TURN = 44;
// Pacifist starts over only at an attack that deals damage: a stomp's too,
// but not a conversion or an infiltration (game: shouldResetPacifistTask)
constexpr int PACIFIST_NEEDS_DAMAGE = 45;
// a player wiped out gets each of its units' disband refund (game:
// WipePlayerAction disbands them, paid)
constexpr int WIPE_PAYS_DISBAND = 46;
// a Raft upgrades inside a peace partner's borders too
constexpr int UPGRADE_IN_ALLIED_BORDERS = 47;
// a melee boat that kills a unit on land lands its passenger there (game:
// the move into the tile, then WillUnitEmbark)
constexpr int KILL_LANDS = 48;
// Persist gives another attack only with another target in range
constexpr int PERSIST_NEEDS_TARGET = 49;
// embarking takes the tech that unlocks the Port (game: CanPlayerEmbark)
constexpr int EMBARK_NEEDS_PORT_TECH = 50;
// income tile by tile as the game pays it (TileDataExtensions.CalculateWork):
// any other player's unit on a city stops it, a peace partner's too; the
// capital bonus and the embassies hosted come with the original capital,
// inside its starvation clamp; markets pay on their own, unclamped; an
// embassy built pays only while its host holds its own capital with no one
// else's unit on it
constexpr int GAME_INCOME = 51;
// breaking peace puts both sides in a cooldown (game: BreakPeaceAction),
// each lasting until its holder's turn ends: the breaker's own turn, the
// other side's next (measured); nor can either capture the other's cities
// in it
constexpr int GAME_COOLDOWN = 52;
// a capture recomputes the old owner's connections, then the capturer's;
// nobody else's (game: CaptureCommand)
constexpr int CAPTURE_CONNECTS_BOTH = 53;
// a boat upgraded after moving can still attack if the new boat has Dash,
// as long as it hasn't attacked (game: CanAttack reads moved and attacked)
constexpr int UPGRADE_DASHES = 54;
// infiltration Daggers face the city they came out of (game:
// InfiltrationRewardAction's look target); the one on the city tile faces
// no way, which pushes as SW (0)
constexpr int DAGGERS_FACE_CITY = 55;
// a melee land unit that kills a unit on a port it could embark at (its
// own or a peace partner's, with Fishing) moves onto it and embarks (game:
// the move into the tile, then WillUnitEmbark)
constexpr int KILL_EMBARKS = 56;
// a capture re-checks the producers (windmill, forge, sawmill...) next to
// the tiles that changed hands, which count only neighbours of their own
// owner (game: CheckSurroundingArea)
constexpr int CAPTURE_REFRESHES_PRODUCERS = 57;
// a capture explores every tile within the city's border radius, not only
// its territory (game: CaptureCityAction's ExploreActions)
constexpr int CAPTURE_REVEALS_AREA = 58;
// a ruin's soldier counts toward its city's unit cap (before: it didn't)
constexpr int RUIN_UNIT_COUNTS = 59;
// a ruin's soldier belongs to the city whose land the ruin is on (none on
// unclaimed land), not the examining unit's (game:
// GetHomeCoordinateForTrainedUnit)
constexpr int RUIN_UNIT_HOME_TILE = 60;
// no infiltrating a city one of your own units (or an ally's) stands on
constexpr int NO_INFILTRATE_OWN_OCCUPIED = 61;
// a Giant pushed onto a port embarks as a move would: a Juggernaut, which
// stomps as it arrives
constexpr int PUSH_EMBARK_STOMPS = 62;
// ruins can't be destroyed
constexpr int NO_DESTROY_RUINS = 63;
// infiltration as the game does it (InfiltrateAction, InfiltrationRewardAction):
// the defender on the city is hit as by a fresh Cloak, with no retaliation;
// the infiltrator takes the city's base production in stars and robs it (no
// income, no infiltrating it again, until its owner's turn ends); Daggers go
// on explored tiles of the city's border area they can stand on, best first
// (the city, then defensible terrain and resources, with a little randomness)
constexpr int GAME_INFILTRATION = 64;
// a boat can be promoted for its passenger's kills: Static goes by the
// passenger (game: promotion is the carried unit's)
constexpr int BOATS_PROMOTE_PASSENGER = 65;
// damage in the game's integer arithmetic (Rules::damage)
constexpr int INTEGER_DAMAGE = 66;
// a move that bumps into a hidden enemy turns the unit toward it (game:
// RevealAction)
constexpr int BUMP_TURNS = 67;
// the unit actions that use its turn (Recover, Heal Others, Stay, Capture,
// Examine Ruins) set the game's "attacked" too, so an upgrade to a Dash
// boat afterwards gives no attack back (UPGRADE_DASHES)
constexpr int ACTIONS_SET_ATTACKED = 68;
// a boat can't land a Hide unit (a Cloak) on an enemy city either
constexpr int NO_HIDDEN_LANDING_ON_CITIES = 69;
// Persist is decided before the unit moves in: targets from the dead
// unit's tile as seen before the move reveals more, and none if it embarks
// or lands there (which ends its turn) (game)
constexpr int PERSIST_BEFORE_MOVE_IN = 70;
constexpr int LATEST = 70;
} // namespace RuleVersions

struct GameSettings {
    WinConditions::WinCondition winCondition = WinConditions::MIGHT;
    // seeds Game::rng
    std::uint64_t seed = 0;
    // Peace treaties can be proposed, accepted and broken (GAME.md section
    // 15). Off only for training a bot before it can use them; the rules are
    // otherwise the same, and embassies stay.
    bool peaceTreaties = true;
    // The rules the game is played by (RuleVersions): the latest, or an
    // older version to replay a game recorded under it
    int rulesVersion = RuleVersions::LATEST;
    bool follows(int rule) const { return rulesVersion >= rule; }
    // Extra stars each player starts with, on top of their tribe's: a
    // handicap for training, public to every player. Empty for none.
    std::vector<int> headStart;
    // Each player's capital income bonus (Rules::CAPITAL_INCOME for everyone
    // when empty): the real game's per-player handicap, which its built-in
    // bots' difficulty sets. Public.
    std::vector<int> capitalIncome;
    // more capital income from a given turn on, on top of capitalIncome: a
    // handicap for training given to a side that fell behind. Public.
    std::vector<IncomeBonus> incomeBonuses;
};

// The whole game state. Everything refers to everything else by index, so a
// Game is a plain value: copy it to branch a search.
//
// Bots only need legalCommands() and apply(). The rest is public so tests and
// tools can inspect and set up positions.
class Game {
    // builds its lists from the same generators as legalCommands()
    friend class LegalMoves;

  public:
    int size;
    MapTypes::MapType mapType;
    GameSettings settings;
    // index y * size + x
    std::vector<Tile> tiles;
    // dead units stay in place with alive = false, and their slot is reused
    std::vector<Unit> units;
    std::vector<City> cities;
    std::vector<Player> players;
    int currentPlayer = 0;
    // the first round is turn 0
    int turn = 0;
    int winner = -1;
    // Every score other players have seen, in order: the starting scores,
    // then one record each time a player ends their turn. Other players'
    // scores only update then; your own you see live. One flat list, so a
    // copy of the game is one allocation more, not one per player.
    std::vector<ScoreRecord> scoreHistory;
    // What each player's own actions visibly did to other players' scores
    // (their units seen dying, cities captured), for reading score changes.
    // [0][p][q]: by p to q since q last published; [1][p][q]: by p to q
    // within the change q last published. Flat, players x players each.
    std::vector<int> scoreEffects;
    // For each scoreHistory record, what each player had visibly done to the
    // publisher's score within that change: players entries per record.
    std::vector<int> scoreEffectHistory;
    // The first turn one of each player's units or cities stood on each tile,
    // -1 for never: players x tiles, filled in as they happen. Others meet a
    // player by seeing those, so the player can bound when that could have
    // been.
    std::vector<int> firstOccupied;
    // whether the map had roads before anyone built one
    bool startedWithRoads = false;
    // random number state, part of the game so copies stay deterministic
    std::uint64_t rng;
    // Outcomes of random events fixed by the caller instead of drawn, to
    // follow a game played elsewhere (src/oracle/mirror.py). Each is used by
    // the next such event and then cleared.
    // the players whose city connections are due to be recomputed at the
    // end of the command, a bit each (GAME_CONNECTIONS)
    std::uint32_t connectionsDue = 0;
    // population points (city, +1 or -1) held back by a level-up waiting on
    // its reward, in the order they come (POPULATION_STACK)
    std::vector<std::pair<int, int>> populationQueue;
    // the tiles on a water route between two ports, anyone's, as of the last
    // time connections were recomputed (GAME_CONNECTIONS; the game's
    // hasRoute)
    std::vector<char> waterRoute;
    struct Forced {
        // the next ruin's reward (RuinReward) and, for a tech, which
        int ruinReward = -1;
        int ruinTech = -1;
        // the tiles the next Explorer reveals, instead of its walk
        std::optional<std::vector<int>> explored;
        // the city (by tile) the next converted unit belongs to
        int convertedHome = -1;
        // where the next infiltration's Daggers end up, and which way each
        // faces (GAME_INFILTRATION), instead of their random order
        std::optional<std::vector<std::pair<int, int>>> daggers;
    } forced;

    Game(const MapData &map, const std::vector<TribeTypes::Tribe> &tribes,
         GameSettings settings = {});

    // ---- geometry (game.cc) ----
    int index(int x, int y) const { return y * size + x; }
    int tileX(int tile) const { return tile % size; }
    int tileY(int tile) const { return tile / size; }
    bool inBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < size && y < size;
    }
    // diagonal steps count the same as straight ones
    int distance(int a, int b) const;

    // calls f(tile) for every tile within radius of tile, excluding tile
    template <typename F> void forEachInRadius(int tile, int radius, F f) const {
        int cx = tileX(tile), cy = tileY(tile);
        for (int y = std::max(cy - radius, 0);
             y <= std::min(cy + radius, size - 1); y++) {
            for (int x = std::max(cx - radius, 0);
                 x <= std::min(cx + radius, size - 1); x++) {
                if (x != cx || y != cy) {
                    f(index(x, y));
                }
            }
        }
    }
    template <typename F> void forEachNeighbour(int tile, F f) const {
        forEachInRadius(tile, 1, f);
    }

    // ---- queries (game.cc) ----
    // the player whose borders contain the tile, -1 if none
    int tileOwner(int tile) const;
    // the alive unit on the tile, or nullptr
    const Unit *unitAt(int tile) const;
    Unit *unitAt(int tile);
    int cityAt(int tile) const;
    int cityCount(int player) const;
    // The seat whose capital (a tile per seat, -1 for none) is nearest the
    // tile, the lowest seat on a tie: the climate region of a tile a map
    // doesn't place
    int nearestCapitalSeat(int tile, const std::vector<int> &capitals) const;
    bool besieged(int city) const;
    // units supported by the city, toward its cap (Rules::unitCap)
    int unitCount(int city) const;
    int techCost(int player, TechTypes::TechType tech) const;
    // owns the parent or a child; ignores the price
    bool canResearch(int player, TechTypes::TechType tech) const;
    int income(int player) const;
    int cityIncome(int city) const;
    // the player's capital income bonus (GameSettings::capitalIncome)
    int capitalIncome(int player) const {
        int income = player >= 0 && player < static_cast<int>(settings.capitalIncome.size())
                         ? settings.capitalIncome[player]
                         : Rules::CAPITAL_INCOME;
        for (const IncomeBonus &b : settings.incomeBonuses) {
            income += b.player == player && turn >= b.fromTurn ? b.extra : 0;
        }
        return income;
    }
    int marketIncome(int tile) const;
    bool gameOver() const { return winner >= 0; }
    // uniform in [0, n)
    int randomInt(int n);

    // ---- commands (game.cc) ----
    // every legal command for the current player, except Resign, which is
    // always legal and left out so random play never picks it
    std::vector<Command> legalCommands() const;
    bool isLegal(const Command &command) const;
    // throws std::invalid_argument if the command isn't legal
    void apply(const Command &command);
    // for callers that took the command from legalCommands()
    void applyUnchecked(const Command &command);

    // ---- units (movement.cc) ----
    // tiles a Move can end on, including embarking onto a Port
    std::vector<int> moveTargets(int unit) const;
    // land tiles a boat can Disembark onto
    std::vector<int> disembarkTargets(int unit) const;
    bool canEnterTerrain(const Unit &unit, int tile) const;
    // the same for any land or naval unit of `player`
    bool canEnterTerrain(int player, bool naval, int tile) const;
    // whether a land unit of `player` stepping onto the tile embarks: a
    // port in borders not hostile to it, with the Port's tech
    // (EMBARK_NEEDS_PORT_TECH)
    bool embarksAt(int player, int tile) const;
    // counts as a road for the unit's owner: a road, bridge or city, outside
    // enemy borders
    bool isRoadFor(int tile, int player) const;
    bool nextToEnemy(int tile, int player) const;
    // the unit must be alive, the tile free; returns the unit's index
    int spawnUnit(int player, UnitTypes::UnitType type, int tile, int home);

    // ---- combat (combat.cc) ----
    struct AttackResult {
        // x10, like HP
        int damage, retaliation;
    };
    // what a normal attack would deal, before range and skill checks
    AttackResult previewAttack(const Unit &attacker,
                               const Unit &defender) const;
    double defenceBonus(const Unit &unit) const;
    // the player's techs make this terrain defensive (Archery, Climbing,
    // Aquatism)
    bool terrainDefended(int player, TerrainTypes::TerrainType terrain) const;
    // from `from` (its own tile if -1), as it stands
    std::vector<int> attackTargets(int unit, int from = -1) const;

    // ---- cities (cities.cc) ----
    void addPopulation(int city, int amount);
    // adds the queued population until a level-up waits on its reward
    void runPopulation();
    // recomputes connections now under POPULATION_STACK, so their
    // population comes before what follows; else at the command's end
    void connectNow(std::uint32_t players);
    void connectNow() { connectNow(std::uint32_t{1} << currentPlayer); }
    int healAmount(const Unit &unit) const;
    // creates a city on the tile and claims its borders; returns its index
    int foundCity(int tile, int owner);
    static constexpr std::uint32_t ALL_PLAYERS = ~std::uint32_t{0};
    // recomputes the water routes, and these players' capital connections
    void updateAllConnections(std::uint32_t players = ALL_PLAYERS);
    // something that can change these players' connections happened (by
    // default the one acting; everyone's before ACTING_CONNECTIONS):
    // recompute them now, or at the end of the command (GAME_CONNECTIONS)
    void connectionsChanged(std::uint32_t players);
    void connectionsChanged() { connectionsChanged(std::uint32_t{1} << currentPlayer); }
    void updateConnections(int player);
    // recomputes waterRoute (game: UpdateRoutes)
    void updateWaterRoutes();
    // cities whose road/port network reaches the player's capital. With
    // newOnly, shared neutral roads the player has no claim to are left out,
    // since they can't form new connections.
    std::vector<bool> connectedCities(int player, bool newOnly = false) const;
    // neutral road tiles the player can't use for new connections: their
    // network touches several players' borders and the player isn't first in
    // seat order among them
    std::vector<char> sharedRoadsBlocked(int player) const;

    // ---- fog of war (fog.cc) ----
    // at most this many players, one explored bit each
    static constexpr int MAX_PLAYERS = 32;
    bool explored(int player, int tile) const {
        return tiles[tile].exploredBy(player);
    }
    // the player can see the unit: its tile is explored and it isn't hidden
    // from them
    bool canSee(int player, const Unit &unit) const;
    // 2 for Scout units and units on mountains, otherwise 1
    int sightRange(const Unit &unit) const;
    // resources only show once the tech that uses them is researchable or
    // owned
    bool resourceVisible(int player, ResourceTypes::ResourceType resource) const;
    // an enemy hidden unit is next to the tile; the game shows the player a
    // warning but not where it is
    bool hiddenEnemyNear(int player, int tile) const;
    // explores the tile for the player; finding a lighthouse gives the
    // capital a population
    void reveal(int player, int tile);

    // ---- setting up a position (tests, src/oracle/scenarios.py) ----
    // builds as the player would, without paying, with everything that
    // follows (population, connections at the end of the next command)
    void placeBuilding(int player, int tile, ImprovementTypes::ImprovementType type);
    void revealAround(int player, int tile, int radius);
    void revealForUnit(int unit);

    // ---- score and meeting tribes (score.cc) ----
    // GAME.md section 3. The live score, which only its player sees.
    int score(int player) const;
    // the score other players see: from the end of the player's last turn
    int publishedScore(int player) const;
    // records the current player's score for everyone to see
    void publishScore(int player);
    // what `by` has visibly done to `to`'s score since `to` last published
    int effectsSincePublished(int by, int to) const;
    // ... and within the change `to` last published
    int effectsInLastChange(int by, int to) const;
    int unitScore(const Unit &unit) const;
    int cityScore(const City &city) const;
    // what the player's next embassy costs
    int embassyCost(int player) const;
    // what an owned tile adds to its owner's score: the territory, and a
    // temple or monument on it
    int tileScore(int tile) const;
    int templeLevel(int tile) const;
    // the turn a temple's level counts by: its owner's (TEMPLE_OWNER_TURNS)
    int templeTurn(int tile) const;
    // turns until the temple gains its next level, 0 at the top level
    int templeTurnsToLevelUp(int tile) const;
    // meets every tribe the player can now see; called after every command
    void checkMeetings();
    // Capital Vision, after every command
    void revealMetCapitals();
    // `player` meets `other`, with the meeting stars, unless they already
    // have (or either is gone)
    void meet(int player, int other);

    // ---- tasks and monuments (tasks.cc) ----
    // whether the task's goal holds right now (GAME.md section 14)
    bool taskMet(int player, TaskTypes::Task task) const;
    // marks every task whose goal holds; called after every command
    void updateTasks();

    // ---- diplomacy (diplomacy.cc) ----
    bool atPeace(int a, int b) const {
        return a != b && a >= 0 && b >= 0 && (players[a].peace >> b & 1);
    }
    // different players without a peace treaty: they can attack each other,
    // and only their units exert zone of control
    bool hostile(int a, int b) const { return a != b && !atPeace(a, b); }
    // a recent attack between them (EMBASSIES_AT_WAR)
    bool atWar(int a, int b) const;
    // stars a turn from embassies, built and hosted
    int embassyIncome(int player) const;
    // (GAME_INCOME) what one tribe's embassy in another's land pays a turn,
    // and the embassies hosted by the player, paid with its original capital
    int embassyPays(int builder, int host) const;
    int hostedEmbassyIncome(int player) const;

    // ---- ruins (ruins.cc) ----
    enum RuinReward { RUIN_TECH, RUIN_STARS, RUIN_POPULATION, RUIN_EXPLORER,
                      RUIN_UNIT, RUIN_REWARD_SIZE };
    // the rewards whose conditions hold for this unit on this ruin
    std::vector<RuinReward> ruinRewards(int unit) const;
    void giveRuinReward(int unit, RuinReward reward);

    // ---- building (build.cc) ----
    // checkStars false: everything but whether the player can afford it
    bool canBuild(int player, int tile, ImprovementTypes::ImprovementType type,
                  bool checkStars = true) const;
    bool canDestroy(int player, int tile) const;
    // whether a bridge on the tile would span water between land on
    // opposite sides (its shape only: not who may build it)
    bool bridgeSpans(int tile) const;
    // a step onto a bridge straight from its side, which a player without
    // shallow-water movement can't take (BRIDGE_SIDES); diagonal steps are
    // fine
    bool bridgeSideStep(int player, int from, int to) const;

  private:
    // game.cc
    // checkStars false: also the commands the player can't afford yet
    // (LegalMoves keeps those, and filters them by stars itself)
    void addUnitCommands(int unit, std::vector<Command> &out,
                         bool checkStars = true) const;
    void addCityCommands(int city, std::vector<Command> &out) const;
    void addTileCommands(int tile, std::vector<Command> &out,
                         bool checkStars = true) const;
    void addResearchCommands(std::vector<Command> &out) const;
    // the current player's first city with a reward to choose, -1 if none
    int pendingRewardCity() const;
    // the player whose peace offer the current player has to answer first
    // (PEACE_ANSWER_FIRST), -1 if none
    int pendingPeaceOffer() const;
    void startTurn();
    void endTurn();
    void research(TechTypes::TechType tech);
    void applyCommand(const Command &command);

    // movement.cc
    // With `parents`, also records for each tile the tile it's best reached
    // from (the route leaving the most movement), for revealing along a path.
    void reachable(int unit, std::vector<int> *moves,
                   std::vector<int> *disembarks,
                   std::vector<int> *parents = nullptr) const;
    // reveals around every tile the unit passes on its way to `to`; returns
    // the tile its last step is from
    int revealAlongPath(int unit, int to);
    void moveUnit(int unit, int to);
    void disembark(int unit, int to);
    // the land unit on a port becomes a boat carrying it, which ends its turn
    void embark(int unit);
    // puts the unit on the tile, facing the way it came from `from` (its
    // own tile if -1), and applies Stomp
    void placeUnit(int unit, int to, int from = -1);
    void markOccupied(int player, int tile);
    void stomp(int unit);
    void killUnit(int unit);

    // combat.cc
    void attack(int unit, int target);
    void convert(int unit, int target);
    void infiltrate(int unit, int target);
    void infiltrateOld(int unit, int target);
    // damage x10 from a source that isn't retaliated; credits the kill
    void splashDamage(int attacker, int target, int amount);
    // the area damage (splash or stomp) a unit deals to a victim, and the
    // units next to a tile it hits: in the game's order, the attacker ends
    // facing the first
    int areaDamage(const Unit &attacker, const Unit &victim) const;
    std::vector<int> areaVictims(const Unit &attacker, int tile) const;

    // cities.cc
    void capture(int unit);
    void train(int city, UnitTypes::UnitType type);
    void chooseReward(int city, CityRewards::CityReward reward);
    void rewardEffect(int city, CityRewards::CityReward reward);
    void claimBorders(int city, int radius);
    void spawnGiant(int city);
    // moves the unit off its tile for a new unit (a Giant, or a ruin's
    // reward), or destroys it if no tile fits. owner is the new unit's owner.
    void pushUnit(int unit, int owner);
    void eliminate(int player);
    void checkGameEnd();

    // ruins.cc
    void examineRuins(int unit);

    // tasks.cc
    // a kill by the unit, for its veteran count and its owner's Killer task
    void creditKill(int unit);
    // an attack by one player on another: Pacifist starts over (if it does
    // damage, after PACIFIST_NEEDS_DAMAGE), and the embassies between them
    // are destroyed
    void onAttack(int attacker, int defender, bool damaging = true);
    // the stars disbanding the unit gives back: half of what it and its
    // passenger cost
    int disbandRefund(const Unit &unit) const;

    // diplomacy.cc
    void addDiplomacyCommands(std::vector<Command> &out) const;
    void applyDiplomacy(const Command &command);
    // removes every diplomatic tie to the player
    void clearDiplomacy(int player);

    // fog.cc
    void revealTerritory(int city);
    // the Explorer: 12 automatic steps toward unexplored tiles
    void runExplorer(int player, int start);

    // build.cc
    void build(int tile, ImprovementTypes::ImprovementType type);
    void destroy(int tile);
    // a bridge: a road on shallow water
    bool isBridge(int tile) const {
        return tiles[tile].road && TerrainTypes::isWater(tiles[tile].terrain);
    }
    void refreshProducer(int tile);
    void refreshProducersAround(int tile);
};
