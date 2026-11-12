#include "encoding.h"
#include "game.h"
#include "mapgen.h"
#include "tribeInference.h"
#include "view.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace CommandTypes;
namespace Slots = EncodingSlots;

namespace {

// The plane layout (docs/encoding.md section 3), for P players
struct Layout {
    int players;

    // board
    static constexpr int ON_MAP = 0, EXPLORED = 1, TERRAIN = 2, RESOURCE = 7,
                         ROAD = 13, BUILDING = 14, LEVEL = 36, COUNTDOWN = 37,
                         CLIMATE = 38,
                         BOARD_END = CLIMATE + TribeTypes::TRIBE_SIZE;
    int territory() const { return BOARD_END; }
    int cityOwner() const { return territory() + players; }
    // then level, population, capital, wall
    int cityLevel() const { return cityOwner() + players; }
    int unitOwner() const { return cityLevel() + 4; }
    int unitType() const { return unitOwner() + players; }
    int passenger() const { return unitType() + UnitTypes::UNIT_TYPE_SIZE; }
    // then HP, veteran, can move, can attack, can act, hidden, hidden enemy
    int hpFraction() const { return passenger() + UnitTypes::GIANT + 1; }
    // enemy attackers, enemy damage, own attackers
    int threat() const { return hpFraction() + 8; }
    int count() const { return threat() + 3; }
};

void checkFits(const Game &game, const EncodingConfig &config) {
    if (game.size > config.mapSize) {
        throw std::invalid_argument("The map is larger than the encoding's map "
                                    "size");
    }
    if (static_cast<int>(game.players.size()) > config.players) {
        throw std::invalid_argument("The game has more players than the "
                                    "encoding");
    }
}

// whose seat q is, counting from `player`
int relative(int q, int player, int players) {
    return (q - player + players) % players;
}

// The most HP any unit can have: veterans gain HP, but only units without
// STATIC are promoted, plus the soldiers ruins make veterans
int maxHp() {
    static const int most = [] {
        int hp = 0;
        for (const UnitData &data : unitData) {
            bool veteran = !data.abilities.test(Abilities::STATIC) ||
                           data.type == Rules::RUIN_LAND_UNIT ||
                           data.type == Rules::RUIN_SEA_PASSENGER;
            hp = std::max(hp, data.maxHp + (veteran ? Rules::VETERAN_HP : 0));
        }
        return hp;
    }();
    return most;
}

bool attacksUnits(const UnitData &data) {
    return !data.abilities.test(Abilities::INFILTRATE) &&
           (data.attack > 0 || data.abilities.test(Abilities::CONVERT));
}

// Where a visible enemy unit could attack from on its next turn: its own
// tile, and with Dash anywhere it could move to. Only the viewer's knowledge
// is used, and the worst case where that's missing: the opponent owns every
// movement tech, and unexplored tiles are open ground.
std::vector<int> enemyOrigins(const PlayerView &view,
                              const PlayerView::UnitView &unit) {
    const int size = view.size;
    const UnitData &data = unitInfo(unit.type);
    const bool naval = data.abilities.test(Abilities::WATER);
    const bool creep = data.abilities.test(Abilities::CREEP);
    const bool ignoreZoc = creep || data.abilities.test(Abilities::HIDE);
    const bool passEnemies = data.abilities.test(Abilities::HIDE);

    auto neighbours = [&](int tile, auto f) {
        int cx = tile % size, cy = tile / size;
        for (int y = std::max(cy - 1, 0); y <= std::min(cy + 1, size - 1); y++) {
            for (int x = std::max(cx - 1, 0); x <= std::min(cx + 1, size - 1);
                 x++) {
                if (x != cx || y != cy) {
                    f(y * size + x);
                }
            }
        }
    };
    auto occupant = [&](int tile) -> const PlayerView::UnitView * {
        int u = view.tiles[tile].unit;
        return u < 0 ? nullptr : &view.units[u];
    };
    auto enterable = [&](int tile) {
        const PlayerView::TileView &t = view.tiles[tile];
        if (!t.explored) {
            return true;
        }
        bool water = TerrainTypes::isWater(t.terrain);
        return naval ? water : (!water || t.road);
    };
    auto roadLike = [&](int tile) {
        const PlayerView::TileView &t = view.tiles[tile];
        return t.explored && (t.road || t.city >= 0) &&
               (t.owner < 0 || t.owner == unit.owner);
    };
    auto nextToEnemy = [&](int tile) {
        bool found = false;
        neighbours(tile, [&](int n) {
            const PlayerView::UnitView *other = occupant(n);
            found |= other && other->owner != unit.owner;
        });
        return found;
    };

    std::vector<int> best(size * size, -1);
    std::vector<char> reached(size * size, 0);
    best[unit.tile] = data.movement * 2;
    std::vector<int> stack{unit.tile};
    while (!stack.empty()) {
        int from = stack.back();
        stack.pop_back();
        int remaining = best[from];
        neighbours(from, [&](int to) {
            if (!enterable(to)) {
                return;
            }
            const PlayerView::UnitView *other = occupant(to);
            bool friendly = other && other->owner == unit.owner;
            if (other && !friendly && !passEnemies) {
                return;
            }
            bool roadStep = !naval && roadLike(from) && roadLike(to);
            int cost = roadStep ? 1 : 2;
            if (remaining < cost) {
                return;
            }
            if (!other) {
                reached[to] = 1;
            }
            const PlayerView::TileView &t = view.tiles[to];
            bool stop = (t.explored && t.terrain == TerrainTypes::MOUNTAIN) ||
                        (t.explored && t.terrain == TerrainTypes::FOREST &&
                         !roadStep && !creep) ||
                        (!ignoreZoc && nextToEnemy(to));
            int left = remaining - cost;
            if (!stop && left > best[to]) {
                best[to] = left;
                stack.push_back(to);
            }
        });
    }

    std::vector<int> origins{unit.tile};
    if (data.abilities.test(Abilities::DASH)) {
        for (int t = 0; t < size * size; t++) {
            if (reached[t]) {
                origins.push_back(t);
            }
        }
    }
    return origins;
}

// marks every tile within range of the origins, other than `self`
void markInRange(int size, const std::vector<int> &origins, int range,
                 int self, std::vector<char> &hit) {
    for (int origin : origins) {
        int cx = origin % size, cy = origin / size;
        for (int y = std::max(cy - range, 0); y <= std::min(cy + range, size - 1);
             y++) {
            for (int x = std::max(cx - range, 0);
                 x <= std::min(cx + range, size - 1); x++) {
                hit[y * size + x] = 1;
            }
        }
    }
    hit[self] = 0;
}

// HP an enemy could deal at the tile: to the viewer's unit there, or else to
// a full-HP Warrior without defence bonus
double damageAt(const Game &game, const PlayerView &view,
                const PlayerView::UnitView &enemy, int tile) {
    const UnitData &data = unitInfo(enemy.type);
    if (data.attack == 0) {
        return 0; // conversion
    }
    const PlayerView::TileView &t = view.tiles[tile];
    if (t.unit >= 0 && view.units[t.unit].owner == view.player) {
        // both units are visible to the viewer, so this reads nothing hidden
        const Unit *attacker = game.unitAt(enemy.tile);
        const Unit *defender = game.unitAt(tile);
        if (attacker && defender) {
            return game.previewAttack(*attacker, *defender).damage / 10.0;
        }
    }
    const UnitData &warrior = unitInfo(UnitTypes::WARRIOR);
    return Rules::damage(data.attack, enemy.hp, enemy.maxHp, warrior.defence,
                         warrior.maxHp, warrior.maxHp, 1.0)
               .dealt /
           10.0;
}

// The temple's level when its owner last published their score, worked out
// from what's visible: its level and countdown now.
int templeLevelAt(const PlayerView::TileView &tile, int nowTurn,
                  int thenTurn) {
    if (tile.level >= Rules::MAX_TEMPLE_LEVEL) {
        return Rules::MAX_TEMPLE_LEVEL;
    }
    // level L + 1 is reached TEMPLE_TURNS_PER_LEVEL x L turns after building
    int built = nowTurn + tile.turnsToLevelUp -
                Rules::TEMPLE_TURNS_PER_LEVEL * tile.level;
    if (built > thenTurn) {
        return 0;
    }
    return Rules::templeLevel(built, thenTurn);
}

} // namespace

int planeCount(const EncodingConfig &config) {
    return Layout{config.players}.count();
}

int globalCount(const EncodingConfig &config) {
    return 58 + MapTypes::MAP_TYPE_SIZE + 15 * config.players +
           45 * (config.players - 1);
}

int actionCount(const EncodingConfig &config) {
    return Slots::PER_TILE * config.mapSize * config.mapSize +
           Slots::RESEARCH +
           Slots::DIPLOMACY_PER_OPPONENT * (config.players - 1) + 1;
}

int explainedScore(const PlayerView &view, int player) {
    // temples count at their level when the player last published
    int publishedTurn = view.turn;
    for (auto r = view.scoreHistory.rbegin(); r != view.scoreHistory.rend();
         ++r) {
        if (r->player == player) {
            publishedTurn = r->turn;
            break;
        }
    }

    int total = 0;
    for (const PlayerView::CityView &city : view.cities) {
        if (city.owner == player) {
            total += Rules::cityScore(city.level, city.population, city.parks);
        }
    }
    for (const PlayerView::UnitView &unit : view.units) {
        if (unit.owner == player) {
            total += Rules::unitScore(unitInfo(unit.passenger).cost);
        }
    }
    for (const PlayerView::TileView &tile : view.tiles) {
        if (!tile.explored || tile.owner != player) {
            continue;
        }
        total += Rules::TERRITORY_SCORE;
        if (isTemple(tile.building)) {
            total += Rules::templeScore(
                templeLevelAt(tile, view.turn, publishedTurn));
        } else if (isMonument(tile.building)) {
            total += Rules::MONUMENT_SCORE;
        }
    }
    return total;
}

void encodeState(const Game &game, int player, const EncodingConfig &config,
                 float *planes, float *globals, bool fullInformation) {
    checkFits(game, config);
    const PlayerView view = makeView(game, player, fullInformation);
    const int N = config.mapSize, P = config.players, size = view.size;
    const Layout layout{P};
    std::fill(planes, planes + layout.count() * N * N, 0.0f);
    std::fill(globals, globals + globalCount(config), 0.0f);

    auto at = [&](int plane, int tile) -> float & {
        return planes[(plane * N + tile / size) * N + tile % size];
    };
    auto rel = [&](int q) { return relative(q, player, P); };

    // ---- tiles, cities and units ----
    for (int t = 0; t < size * size; t++) {
        at(Layout::ON_MAP, t) = 1;
        const PlayerView::TileView &tile = view.tiles[t];
        if (!tile.explored) {
            continue;
        }
        // with full information every tile shows, but exploring still counts
        // for score, so this stays what the player has explored
        at(Layout::EXPLORED, t) = fullInformation ? game.explored(player, t) : 1;
        at(Layout::TERRAIN + tile.terrain, t) = 1;
        if (tile.resource != ResourceTypes::NONE) {
            at(Layout::RESOURCE + tile.resource - ResourceTypes::FRUIT, t) = 1;
        }
        at(Layout::ROAD, t) = tile.road;
        if (tile.building >= ImprovementTypes::VILLAGE &&
            tile.building <= ImprovementTypes::TOWEROFWISDOM) {
            at(Layout::BUILDING + tile.building - ImprovementTypes::VILLAGE, t) =
                1;
        }
        if (isProducer(tile.building)) {
            at(Layout::LEVEL, t) =
                tile.level / static_cast<float>(MAX_BUILDING_LEVEL);
        } else if (isTemple(tile.building)) {
            at(Layout::LEVEL, t) =
                tile.level / static_cast<float>(Rules::MAX_TEMPLE_LEVEL);
            at(Layout::COUNTDOWN, t) = tile.turnsToLevelUp / 2.0f;
        }
        if (tile.climate) {
            at(Layout::CLIMATE + *tile.climate, t) = 1;
        }
        if (tile.owner >= 0) {
            at(layout.territory() + rel(tile.owner), t) = 1;
        }

        if (tile.city >= 0) {
            const PlayerView::CityView &city = view.cities[tile.city];
            at(layout.cityOwner() + rel(city.owner), t) = 1;
            at(layout.cityLevel(), t) = city.level / 10.0f;
            at(layout.cityLevel() + 1, t) =
                static_cast<float>(city.population) / (city.level + 1);
            at(layout.cityLevel() + 2, t) = city.capitalOf >= 0;
            at(layout.cityLevel() + 3, t) = city.wall;
        }

        if (tile.unit >= 0) {
            const PlayerView::UnitView &unit = view.units[tile.unit];
            bool own = unit.owner == player;
            at(layout.unitOwner() + rel(unit.owner), t) = 1;
            at(layout.unitType() + unit.type, t) = 1;
            if (unit.passenger != unit.type &&
                unit.passenger <= UnitTypes::GIANT) {
                at(layout.passenger() + unit.passenger, t) = 1;
            }
            const int stats = layout.hpFraction();
            at(stats, t) = static_cast<float>(unit.hp) / unit.maxHp;
            at(stats + 1, t) = unit.hp / static_cast<float>(maxHp());
            at(stats + 2, t) = unit.veteran;
            if (own) {
                at(stats + 3, t) = unit.canMove;
                at(stats + 4, t) = unit.canAttack;
                at(stats + 5, t) = unit.canAct;
                at(stats + 6, t) = unit.hidden;
                at(stats + 7, t) = unit.hiddenEnemyNear;
            }
        }
    }

    // ---- threats ----
    std::vector<int> enemyAttackers(size * size, 0), ownAttackers(size * size, 0);
    std::vector<double> enemyDamage(size * size, 0);
    for (const PlayerView::UnitView &unit : view.units) {
        const UnitData &data = unitInfo(unit.type);
        if (!attacksUnits(data)) {
            continue;
        }
        std::vector<char> hit(size * size, 0);
        if (unit.owner != player) {
            markInRange(size, enemyOrigins(view, unit), data.range, unit.tile,
                        hit);
            for (int t = 0; t < size * size; t++) {
                if (hit[t]) {
                    enemyAttackers[t]++;
                    enemyDamage[t] =
                        std::max(enemyDamage[t], damageAt(game, view, unit, t));
                }
            }
        } else if (unit.canAttack) {
            std::vector<int> origins{unit.tile};
            if (unit.canMove && data.abilities.test(Abilities::DASH)) {
                for (int t : game.moveTargets(game.tiles[unit.tile].unit)) {
                    origins.push_back(t);
                }
            }
            markInRange(size, origins, data.range, unit.tile, hit);
            for (int t = 0; t < size * size; t++) {
                ownAttackers[t] += hit[t];
            }
        }
    }
    for (int t = 0; t < size * size; t++) {
        if (!view.tiles[t].explored) {
            continue;
        }
        at(layout.threat(), t) = enemyAttackers[t] / 4.0f;
        at(layout.threat() + 1, t) = static_cast<float>(enemyDamage[t] / 15.0);
        at(layout.threat() + 2, t) = ownAttackers[t] / 4.0f;
    }

    // ---- the global vector ----
    const int players = static_cast<int>(view.tribes.size());
    auto seat = [&](int k) { return (player + k) % P; };
    float *g = globals;
    *g++ = view.turn / 50.0f;
    *g++ = view.stars / 50.0f;
    *g++ = view.income / 30.0f;
    for (int k = 0; k < P; k++, g += TribeTypes::TRIBE_SIZE) {
        // all zeros for a tribe not met yet
        if (seat(k) < players && view.tribes[seat(k)]) {
            g[*view.tribes[seat(k)]] = 1;
        }
    }
    // the tribes each opponent could still be, worked out exactly
    for (int k = 1; k < P; k++, g += TribeTypes::TRIBE_SIZE) {
        if (seat(k) < players) {
            TribeSet possible = possibleTribes(view, seat(k));
            for (int t = 0; t < TribeTypes::TRIBE_SIZE; t++) {
                g[t] = possible.test(t);
            }
        }
    }
    // climates seen anywhere
    TribeSet seen = climatesSeen(view);
    for (int t = 0; t < TribeTypes::TRIBE_SIZE; t++) {
        *g++ = seen.test(t);
    }
    for (int k = 0; k < P; k++) {
        *g++ = seat(k) < players && view.alive[seat(k)];
    }
    for (int k = 0; k < P; k++) {
        *g++ = k == 0 || (seat(k) < players && view.met[seat(k)]);
    }
    for (int k = 0; k < P; k++) {
        *g++ = seat(k) < players ? view.scores[seat(k)] / 1000.0f : 0.0f;
    }

    // score features (section 4.1), per opponent
    std::vector<float> starting(P, 0), explained(P, 0), unexplained(P, 0),
        lastChange(P, 0);
    for (int k = 1; k < P; k++) {
        int q = seat(k);
        if (q >= players) {
            continue;
        }
        // the first published score is the starting score, which narrows
        // down a tribe not met yet
        for (const ScoreRecord &record : view.scoreHistory) {
            if (record.player == q) {
                starting[k] = record.score / 1000.0f;
                break;
            }
        }
        int known = explainedScore(view, q);
        explained[k] = known / 1000.0f;
        unexplained[k] =
            (view.scores[q] + view.effectsSincePublished[q] - known) / 1000.0f;
        int latest = -1, previous = -1;
        for (auto r = view.scoreHistory.rbegin(); r != view.scoreHistory.rend();
             ++r) {
            if (r->player != q) {
                continue;
            }
            if (latest < 0) {
                latest = r->score;
            } else {
                previous = r->score;
                break;
            }
        }
        if (previous >= 0) {
            lastChange[k] =
                (latest - previous - view.effectsInLastChange[q]) / 500.0f;
        }
    }
    for (const std::vector<float> *feature :
         {&starting, &explained, &unexplained, &lastChange}) {
        for (int k = 1; k < P; k++) {
            *g++ = (*feature)[k];
        }
    }

    // the opening (section 4.2), per opponent: their first changes, with the
    // player's own effects taken out, and whether each has happened yet
    for (int k = 1; k < P; k++, g += 2 * OPENING_CHANGES) {
        int q = seat(k);
        if (q >= players) {
            continue;
        }
        int last = -1, n = 0;
        for (std::size_t r = 0;
             r < view.scoreHistory.size() && n < OPENING_CHANGES; r++) {
            const ScoreRecord &record = view.scoreHistory[r];
            if (record.player != q) {
                continue;
            }
            if (last >= 0) {
                g[n] = (record.score - last - view.effectsInChanges[r]) / 500.0f;
                g[OPENING_CHANGES + n] = 1;
                n++;
            }
            last = record.score;
        }
    }
    // and for each tribe they could be, where that puts their capital: the
    // tiles they'd have started with explored, / 25 (0 if not possible)
    for (int k = 1; k < P; k++, g += TribeTypes::TRIBE_SIZE) {
        int q = seat(k);
        if (q >= players) {
            continue;
        }
        int start = -1;
        for (const ScoreRecord &record : view.scoreHistory) {
            if (record.player == q) {
                start = record.score;
                break;
            }
        }
        TribeSet possible = possibleTribes(view, q);
        for (int t = 0; t < TribeTypes::TRIBE_SIZE; t++) {
            int explored =
                start < 0 ? -1
                          : startingExplored(static_cast<TribeTypes::Tribe>(t), start);
            if (possible.test(t) && explored >= 0) {
                int side = 2 * Rules::START_EXPLORED_RADIUS + 1;
                g[t] = explored / static_cast<float>(side * side);
            }
        }
    }

    for (int tech = TechTypes::BASIC + 1; tech < TechTypes::TECH_TYPE_SIZE;
         tech++) {
        *g++ = view.techs.test(tech);
    }
    for (int task = 0; task < TaskTypes::TASK_SIZE; task++) {
        *g++ = view.tasksDone.test(task);
    }
    for (int task = 0; task < TaskTypes::TASK_SIZE; task++) {
        *g++ = view.monumentsBuilt.test(task);
    }
    // progress toward the tasks' thresholds
    *g++ = view.kills / static_cast<float>(Rules::KILLER_KILLS);
    *g++ = view.peacefulTurns / static_cast<float>(Rules::PACIFIST_TURNS);
    *g++ = view.lighthousesFound / static_cast<float>(Rules::LIGHTHOUSE_COUNT);
    for (int k = 1; k < P; k++, g += 5) {
        if (seat(k) >= players) {
            continue;
        }
        const PlayerView::Relation &r = view.relations[seat(k)];
        g[0] = r.peace;
        g[1] = r.theyAsked;
        g[2] = r.weAsked;
        g[3] = r.ourEmbassy;
        g[4] = r.theirEmbassy;
    }
    // the player's own cities, so nothing hidden
    *g++ = std::any_of(game.cities.begin(), game.cities.end(),
                       [&](const City &c) {
                           return c.owner == player && c.pendingRewards > 0;
                       });
    // the map type, public: what the unexplored tiles are likely to be
    g[view.mapType] = 1;
    g += MapTypes::MAP_TYPE_SIZE;
    if (g != globals + globalCount(config)) {
        throw std::logic_error("Global vector layout doesn't match its size");
    }
}

int encodeCommand(const Game &game, const Command &command,
                  const EncodingConfig &config) {
    const int N = config.mapSize, P = config.players;
    const int globalBase = Slots::PER_TILE * N * N;

    // per-tile slots: slot-major, then row, then column
    auto perTile = [&](int slot) {
        if (command.from < 0) {
            return -1;
        }
        int x = game.tileX(command.from), y = game.tileY(command.from);
        if (x >= N || y >= N) {
            return -1;
        }
        return (slot * N + y) * N + x;
    };
    auto diplomacy = [&](int other, int offset) {
        int k = relative(other, game.currentPlayer, P);
        if (other < 0 || other >= P || k == 0) {
            return -1;
        }
        return globalBase + Slots::RESEARCH +
               Slots::DIPLOMACY_PER_OPPONENT * (k - 1) + offset;
    };

    switch (command.type) {
    case MOVE:
    case ATTACK:
    case DISEMBARK: {
        int dx = game.tileX(command.to) - game.tileX(command.from);
        int dy = game.tileY(command.to) - game.tileY(command.from);
        if (std::abs(dx) > Slots::OFFSET_RADIUS ||
            std::abs(dy) > Slots::OFFSET_RADIUS) {
            return -1;
        }
        return perTile(Slots::TARGET +
                       (dy + Slots::OFFSET_RADIUS) * Slots::OFFSET_SIDE + dx +
                       Slots::OFFSET_RADIUS);
    }
    case RECOVER:
        return perTile(Slots::UNIT_ACTION + 0);
    case HEALOTHERS:
        return perTile(Slots::UNIT_ACTION + 1);
    case PROMOTE:
        return perTile(Slots::UNIT_ACTION + 2);
    case DISBAND:
        return perTile(Slots::UNIT_ACTION + 3);
    case CAPTURE:
        return perTile(Slots::UNIT_ACTION + 4);
    case EXAMINERUINS:
        return perTile(Slots::UNIT_ACTION + 5);
    case HARVEST:
        return perTile(Slots::UNIT_ACTION + 6);
    case UPGRADE:
        switch (command.arg) {
        case UnitTypes::SCOUT:
            return perTile(Slots::UPGRADE + 0);
        case UnitTypes::RAMMER:
            return perTile(Slots::UPGRADE + 1);
        case UnitTypes::BOMBER:
            return perTile(Slots::UPGRADE + 2);
        default:
            return -1;
        }
    case BUILD:
        if (command.arg < ImprovementTypes::FARM ||
            command.arg > ImprovementTypes::BRIDGE) {
            return -1;
        }
        return perTile(Slots::BUILD + command.arg - ImprovementTypes::FARM);
    case DESTROY:
        return perTile(Slots::DESTROY);
    case TRAIN:
        if (command.arg < 0 || command.arg > UnitTypes::CLOAK) {
            return -1;
        }
        return perTile(Slots::TRAIN + command.arg);
    case CITYREWARD: {
        int city = command.from < 0 ? -1 : game.cityAt(command.from);
        if (city < 0) {
            return -1;
        }
        const City &c = game.cities[city];
        auto options = CityRewards::rewardsForLevel(c.level - c.pendingRewards + 1);
        for (int option = 0; option < 2; option++) {
            if (options[option] == command.arg) {
                return perTile(Slots::REWARD + option);
            }
        }
        return -1;
    }
    case RESEARCH:
        if (command.arg <= TechTypes::BASIC ||
            command.arg >= TechTypes::TECH_TYPE_SIZE) {
            return -1;
        }
        return globalBase + command.arg - 1;
    case PEACETREATY:
        return diplomacy(command.arg, 0);
    case PEACEREQUESTRESPONSE:
        return diplomacy(command.arg / 2, command.arg % 2 == 1 ? 1 : 2);
    case BREAKPEACE:
        return diplomacy(command.arg, 3);
    case ESTABLISHEMBASSY:
        return diplomacy(command.arg, 4);
    case DESTROYEMBASSY:
        return diplomacy(command.arg, 5);
    case ENDTURN:
        return actionCount(config) - 1;
    default:
        return -1;
    }
}

Command decodeCommand(const Game &game, int index, const EncodingConfig &config) {
    for (const Command &command : game.legalCommands()) {
        if (encodeCommand(game, command, config) == index) {
            return command;
        }
    }
    throw std::invalid_argument("No legal command at index " +
                                std::to_string(index));
}

void legalMask(const std::vector<Command> &legal, const Game &game,
               const EncodingConfig &config, std::uint8_t *mask) {
    std::fill(mask, mask + actionCount(config), 0);
    for (const Command &command : legal) {
        int index = encodeCommand(game, command, config);
        if (index >= 0) {
            mask[index] = 1;
        }
    }
}

void legalMask(const Game &game, const EncodingConfig &config,
               std::uint8_t *mask) {
    legalMask(game.legalCommands(), game, config, mask);
}

// ---- training targets for the belief heads ----

std::vector<TargetHead> targetHeads(const EncodingConfig &config) {
    const int area = config.mapSize * config.mapSize;
    // per opponent, in this order (docs/encoding.md section 8.2)
    const std::vector<std::pair<std::string, int>> sizes = {
        {"tribe", TribeTypes::TRIBE_SIZE},
        // Basic, which everyone owns, is left out
        {"techs", TechTypes::TECH_TYPE_SIZE - 1},
        // the count itself, for a distribution over counts
        {"stars", 1},
        {"hidden_unit_here", area},
        {"hidden_unit_type", UnitTypes::UNIT_TYPE_SIZE * area},
        {"hidden_unit_count", HIDDEN_UNIT_COUNT_CAP + 1},
        {"exploration", area + 1},
    };
    std::vector<TargetHead> heads;
    int offset = 0;
    for (int k = 1; k < config.players; k++) {
        for (const auto &[name, size] : sizes) {
            bool perTile = name == "hidden_unit_here" || name == "hidden_unit_type";
            heads.push_back({name, k, offset, size, perTile ? size / area : 0});
            offset += size;
        }
    }
    // the map's, not an opponent's
    heads.push_back({"unexplored", 0, offset, UnexploredHead::LAYERS * area,
                     UnexploredHead::LAYERS});
    return heads;
}

std::vector<TargetHead> beliefOutputHeads(const EncodingConfig &config) {
    std::vector<TargetHead> heads;
    int offset = 0;
    for (TargetHead head : targetHeads(config)) {
        if (head.name == "stars") {
            head.size = 3 * STAR_COMPONENTS;
        }
        head.offset = offset;
        offset += head.size;
        heads.push_back(head);
    }
    return heads;
}

int beliefOutputCount(const EncodingConfig &config) {
    std::vector<TargetHead> heads = beliefOutputHeads(config);
    return heads.empty() ? 0 : heads.back().offset + heads.back().size;
}

namespace {
float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }
float softplus(float x) { return x > 20 ? x : std::log1p(std::exp(x)); }
// a softmax of `n` logits into out
template <typename Out>
void softmax(const float *logits, int n, Out &out) {
    float most = *std::max_element(logits, logits + n);
    double sum = 0;
    for (int i = 0; i < n; i++) {
        out[i] = std::exp(logits[i] - most);
        sum += out[i];
    }
    for (int i = 0; i < n; i++) {
        out[i] = static_cast<float>(out[i] / sum);
    }
}
} // namespace

Beliefs decodeBeliefs(const PlayerView &view, const EncodingConfig &config,
                      const float *outputs) {
    const int N = config.mapSize, size = view.size, P = config.players;
    const int players = static_cast<int>(view.tribes.size());
    Beliefs beliefs;
    beliefs.players.resize(players);
    auto padded = [&](int tile) { return (tile / size) * N + tile % size; };
    for (const TargetHead &head : beliefOutputHeads(config)) {
        int q = (view.player + head.opponent) % P;
        if (q >= players) {
            continue;
        }
        OpponentBeliefs &b = beliefs.players[q];
        const float *o = outputs + head.offset;
        if (head.name == "tribe") {
            softmax(o, head.size, b.tribe);
        } else if (head.name == "techs") {
            // Basic, left out of the head, everyone owns
            b.tech[0] = 1;
            for (int i = 0; i < head.size; i++) {
                b.tech[i + 1] = sigmoid(o[i]);
            }
        } else if (head.name == "stars") {
            // the mixture of negative binomials, as the loss reads it
            std::array<float, STAR_COMPONENTS> weight;
            softmax(o, STAR_COMPONENTS, weight);
            b.stars.assign(STAR_COUNT_CAP + 1, 0.0f);
            for (int c = 0; c < STAR_COMPONENTS; c++) {
                double mean = softplus(o[STAR_COMPONENTS + c]) + 1e-3;
                double spread = softplus(o[2 * STAR_COMPONENTS + c]) + 1e-3;
                for (int k = 0; k <= STAR_COUNT_CAP; k++) {
                    double logPmf = std::lgamma(k + spread) - std::lgamma(spread) -
                                    std::lgamma(k + 1.0) +
                                    spread * std::log(spread / (spread + mean)) +
                                    k * std::log(mean / (spread + mean));
                    b.stars[k] += static_cast<float>(weight[c] * std::exp(logPmf));
                }
            }
        } else if (head.name == "hidden_unit_count") {
            b.hiddenUnitCount.resize(head.size);
            softmax(o, head.size, b.hiddenUnitCount);
        } else if (head.name == "hidden_unit_here") {
            b.unitHere.resize(size * size);
            for (int t = 0; t < size * size; t++) {
                b.unitHere[t] = sigmoid(o[padded(t)]);
            }
        } else if (head.name == "unexplored") {
            using namespace UnexploredHead;
            beliefs.terrain.resize(size * size);
            beliefs.resource.resize(size * size);
            beliefs.feature.resize(size * size);
            for (int t = 0; t < size * size; t++) {
                auto group = [&](int first, int count, auto &out) {
                    std::array<float, LAYERS> logits;
                    for (int l = 0; l < count; l++) {
                        logits[l] = o[(first + l) * N * N + padded(t)];
                    }
                    softmax(logits.data(), count, out);
                };
                group(TERRAIN, TerrainTypes::TERRAIN_TYPE_SIZE, beliefs.terrain[t]);
                group(RESOURCE, ResourceTypes::RESOURCE_TYPE_SIZE, beliefs.resource[t]);
                group(FEATURE, static_cast<int>(TileFeature::FEATURE_SIZE), beliefs.feature[t]);
            }
        } else if (head.name == "hidden_unit_type") {
            b.unitType.resize(size * size);
            std::array<float, UnitTypes::UNIT_TYPE_SIZE> logits;
            for (int t = 0; t < size * size; t++) {
                for (int u = 0; u < UnitTypes::UNIT_TYPE_SIZE; u++) {
                    logits[u] = o[u * N * N + padded(t)];
                }
                softmax(logits.data(), UnitTypes::UNIT_TYPE_SIZE, b.unitType[t]);
            }
        }
    }
    return beliefs;
}

int sideTargetCount(const EncodingConfig &config) {
    return SideTargets::TERRITORY +
           SideTargets::TERRITORY_CLASSES * config.mapSize * config.mapSize;
}

int targetCount(const EncodingConfig &config) {
    std::vector<TargetHead> heads = targetHeads(config);
    return heads.empty() ? 0 : heads.back().offset + heads.back().size;
}

void encodeTargets(const Game &game, int player, const EncodingConfig &config,
                   float *targets, std::uint8_t *masks, bool fullInformation) {
    checkFits(game, config);
    std::fill(targets, targets + targetCount(config), 0.0f);
    std::fill(masks, masks + targetCount(config), std::uint8_t{0});
    if (fullInformation) {
        return;  // nothing hidden, nothing to learn
    }
    const PlayerView view = makeView(game, player);
    const int N = config.mapSize, size = game.size, P = config.players;
    const int players = static_cast<int>(game.players.size());
    auto padded = [&](int tile) { return (tile / size) * N + tile % size; };

    for (const TargetHead &head : targetHeads(config)) {
        int q = (player + head.opponent) % P;
        if (q >= players || !game.players[q].alive) {
            continue;
        }
        float *t = targets + head.offset;
        std::uint8_t *m = masks + head.offset;
        const Player &them = game.players[q];

        if (head.name == "tribe") {
            // only while unmet, and only over the tribes still possible
            if (!view.tribes[q]) {
                t[them.tribe] = 1;
                TribeSet possible = possibleTribes(view, q);
                for (int i = 0; i < head.size; i++) {
                    m[i] = possible.test(i);
                }
            }
        } else if (head.name == "techs") {
            for (int i = 0; i < head.size; i++) {
                t[i] = them.techs.test(i + 1);
                m[i] = 1;
            }
        } else if (head.name == "stars") {
            t[0] = static_cast<float>(them.stars);
            m[0] = 1;
        } else if (head.name == "hidden_unit_here" ||
                   head.name == "hidden_unit_type") {
            bool type = head.name == "hidden_unit_type";
            for (int tile = 0; tile < size * size; tile++) {
                // a unit the player sees is known, so nothing to learn there
                if (view.tiles[tile].unit >= 0) {
                    continue;
                }
                const Unit *unit = game.unitAt(tile);
                bool hidden = unit && unit->owner == q;
                int at = padded(tile);
                if (!type) {
                    t[at] = hidden;
                    m[at] = 1;
                } else if (hidden) {
                    t[unit->type * N * N + at] = 1;
                    for (int u = 0; u < UnitTypes::UNIT_TYPE_SIZE; u++) {
                        m[u * N * N + at] = 1;
                    }
                }
            }
        } else if (head.name == "hidden_unit_count") {
            int hidden = 0;
            for (const Unit &unit : game.units) {
                if (unit.alive && unit.owner == q &&
                    view.tiles[unit.tile].unit < 0) {
                    hidden++;
                }
            }
            t[std::min(hidden, HIDDEN_UNIT_COUNT_CAP)] = 1;
            std::fill(m, m + head.size, std::uint8_t{1});
        } else if (head.name == "unexplored") {
            // each unexplored tile's contents, except a corner (always a
            // lighthouse) and a city (determinization places those itself)
            using namespace UnexploredHead;
            for (int tile = 0; tile < size * size; tile++) {
                const Tile &real = game.tiles[tile];
                if (view.tiles[tile].explored || real.building == ImprovementTypes::CITY ||
                    isCornerTile(game.tileX(tile), game.tileY(tile), size)) {
                    continue;
                }
                int at = padded(tile);
                TileFeature feature = real.building == ImprovementTypes::VILLAGE ? TileFeature::VILLAGE
                                      : real.building == ImprovementTypes::RUIN  ? TileFeature::RUIN
                                                                                 : TileFeature::NONE;
                t[(TERRAIN + real.terrain) * N * N + at] = 1;
                t[(RESOURCE + real.resource) * N * N + at] = 1;
                t[(FEATURE + static_cast<int>(feature)) * N * N + at] = 1;
                for (int l = 0; l < LAYERS; l++) {
                    m[l * N * N + at] = 1;
                }
            }
        } else if (head.name == "exploration") {
            int explored = 0;
            for (int tile = 0; tile < size * size; tile++) {
                explored += game.explored(q, tile);
            }
            t[explored] = 1;
            std::fill(m, m + head.size, std::uint8_t{1});
        }
    }
}

// ---- symmetries ----

std::pair<int, int> transformOffset(int s, int dx, int dy) {
    if (s & 4) {
        std::swap(dx, dy);
    }
    return {s & 1 ? -dx : dx, s & 2 ? -dy : dy};
}

std::pair<int, int> transformTile(int s, int x, int y, int mapSize) {
    if (s & 4) {
        std::swap(x, y);
    }
    return {s & 1 ? mapSize - 1 - x : x, s & 2 ? mapSize - 1 - y : y};
}

namespace {
// Moves `layers` consecutive [N][N] layers' map tiles to their transformed
// places
template <typename T>
void transformLayers(int s, int mapSize, int n, int layers, T *data) {
    std::vector<T> copy(data, data + static_cast<std::size_t>(layers) * n * n);
    for (int l = 0; l < layers; l++) {
        const T *from = copy.data() + static_cast<std::size_t>(l) * n * n;
        T *to = data + static_cast<std::size_t>(l) * n * n;
        for (int y = 0; y < mapSize; y++) {
            for (int x = 0; x < mapSize; x++) {
                auto [tx, ty] = transformTile(s, x, y, mapSize);
                to[ty * n + tx] = from[y * n + x];
            }
        }
    }
}

// A command array [A]: each per-tile slot's layer moves like a plane, and a
// target offset's slot becomes the transformed offset's
template <typename T>
void transformCommands(int s, int mapSize, int n, T *data) {
    using namespace EncodingSlots;
    const std::size_t area = static_cast<std::size_t>(n) * n;
    std::vector<T> copy(data, data + PER_TILE * area);
    std::fill(data, data + PER_TILE * area, T{});
    for (int slot = 0; slot < PER_TILE; slot++) {
        int to = slot;
        if (slot < UNIT_ACTION) {
            auto [dx, dy] = transformOffset(s, slot % OFFSET_SIDE - OFFSET_RADIUS,
                                            slot / OFFSET_SIDE - OFFSET_RADIUS);
            to = (dy + OFFSET_RADIUS) * OFFSET_SIDE + (dx + OFFSET_RADIUS);
        }
        for (int y = 0; y < n; y++) {
            for (int x = 0; x < n; x++) {
                // padding has no legal commands, so it stays where it is
                auto [tx, ty] = x < mapSize && y < mapSize
                                    ? transformTile(s, x, y, mapSize)
                                    : std::pair<int, int>{x, y};
                data[to * area + ty * n + tx] = copy[slot * area + y * n + x];
            }
        }
    }
}
} // namespace

void applySymmetry(int s, int mapSize, const EncodingConfig &config,
                   float *planes, std::uint8_t *legal, float *policy,
                   float *targets, std::uint8_t *targetMasks) {
    if (s < 0 || s >= SYMMETRIES || mapSize > config.mapSize) {
        throw std::invalid_argument("No such symmetry, or the map doesn't fit");
    }
    if (s == 0) {
        return;
    }
    const int n = config.mapSize;
    if (planes) {
        transformLayers(s, mapSize, n, planeCount(config), planes);
    }
    if (legal) {
        transformCommands(s, mapSize, n, legal);
    }
    if (policy) {
        transformCommands(s, mapSize, n, policy);
    }
    for (const TargetHead &head : targetHeads(config)) {
        if (head.tileLayers == 0) {
            continue;
        }
        if (targets) {
            transformLayers(s, mapSize, n, head.tileLayers, targets + head.offset);
        }
        if (targetMasks) {
            transformLayers(s, mapSize, n, head.tileLayers, targetMasks + head.offset);
        }
    }
}

void applySymmetry(int s, int mapSize, const EncodingConfig &config, int layers,
                   float *data) {
    if (s != 0) {
        transformLayers(s, mapSize, config.mapSize, layers, data);
    }
}

void applySymmetry(int s, int mapSize, const EncodingConfig &config, int layers,
                   std::uint8_t *data) {
    if (s != 0) {
        transformLayers(s, mapSize, config.mapSize, layers, data);
    }
}
