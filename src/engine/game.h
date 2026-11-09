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

struct GameSettings {
    WinConditions::WinCondition winCondition = WinConditions::MIGHT;
    // seeds Game::rng
    std::uint64_t seed = 0;
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
    std::vector<int> attackTargets(int unit) const;

    // ---- cities (cities.cc) ----
    void addPopulation(int city, int amount);
    int healAmount(const Unit &unit) const;
    // creates a city on the tile and claims its borders; returns its index
    int foundCity(int tile, int owner);
    // recomputes every player's capital connections
    void updateAllConnections();
    void updateConnections(int player);
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
    // what an owned tile adds to its owner's score: the territory, and a
    // temple or monument on it
    int tileScore(int tile) const;
    int templeLevel(int tile) const;
    // turns until the temple gains its next level, 0 at the top level
    int templeTurnsToLevelUp(int tile) const;
    // meets every tribe the player can now see; called after every command
    void checkMeetings();

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
    // stars a turn from embassies, built and hosted
    int embassyIncome(int player) const;

    // ---- ruins (ruins.cc) ----
    enum RuinReward { RUIN_TECH, RUIN_STARS, RUIN_POPULATION, RUIN_EXPLORER,
                      RUIN_UNIT, RUIN_REWARD_SIZE };
    // the rewards whose conditions hold for this unit on this ruin
    std::vector<RuinReward> ruinRewards(int unit) const;
    void giveRuinReward(int unit, RuinReward reward);

    // ---- building (build.cc) ----
    bool canBuild(int player, int tile,
                  ImprovementTypes::ImprovementType type) const;
    bool canDestroy(int player, int tile) const;

  private:
    // game.cc
    void addUnitCommands(int unit, std::vector<Command> &out) const;
    void addCityCommands(int city, std::vector<Command> &out) const;
    void addTileCommands(int tile, std::vector<Command> &out) const;
    void addResearchCommands(std::vector<Command> &out) const;
    // the current player's first city with a reward to choose, -1 if none
    int pendingRewardCity() const;
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
    // reveals around every tile the unit passes on its way to `to`
    void revealAlongPath(int unit, const std::vector<int> &parents, int to);
    void moveUnit(int unit, int to);
    void disembark(int unit, int to);
    // puts the unit on the tile and applies Stomp
    void placeUnit(int unit, int to);
    void markOccupied(int player, int tile);
    void stomp(int unit);
    void killUnit(int unit);

    // combat.cc
    void attack(int unit, int target);
    void convert(int unit, int target);
    void infiltrate(int unit, int target);
    // damage x10 from a source that isn't retaliated; credits the kill
    void splashDamage(int attacker, int target, int amount);

    // cities.cc
    void capture(int unit);
    void train(int city, UnitTypes::UnitType type);
    void chooseReward(int city, CityRewards::CityReward reward);
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
    // an attack by one player on another: Pacifist starts over, and the
    // embassies between them are destroyed
    void onAttack(int attacker, int defender);

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
    void refreshProducer(int tile);
    void refreshProducersAround(int tile);
};
