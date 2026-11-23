#include "record.h"
#include "parallel.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

Game GameRecord::start() const {
    return Game(generateMap(map), map.tribes,
                {.seed = gameSeed, .peaceTreaties = peaceTreaties,
                 .rulesVersion = rulesVersion,
                 .headStart = headStart, .capitalIncome = capitalIncome,
                 .incomeBonuses = incomeBonuses});
}

bool GameRecord::replaysLegally() const {
    Game game = start();
    for (const RecordedMove &m : moves) {
        if (game.gameOver() || game.currentPlayer != m.player || !game.isLegal(m.command)) {
            return false;
        }
        game.applyUnchecked(m.command);
        for (const Player &p : game.players) {
            if (p.stars < 0) {
                return false;
            }
        }
    }
    return true;
}

float GameRecord::cityValue(int player) const {
    if (player >= static_cast<int>(finalCities.size())) {
        return 0;
    }
    int best = 0;
    for (std::size_t p = 0; p < finalCities.size(); p++) {
        if (static_cast<int>(p) != player) {
            best = std::max(best, finalCities[p]);
        }
    }
    int sum = finalCities[player] + best;
    return sum > 0 ? static_cast<float>(finalCities[player] - best) / sum : 0.0f;
}

float GameRecord::result(int player) const {
    return winner < 0 ? 0.0f : winner == player ? 1.0f : -1.0f;
}

float GameRecord::value(int player) const {
    if (winner >= 0 || player >= static_cast<int>(finalValues.size()) ||
        std::isnan(finalValues[player])) {
        return result(player);
    }
    return finalValues[player];
}

float GameRecord::scoreValue(int player, double scale) const {
    int best = 0;
    for (std::size_t p = 0; p < finalScores.size(); p++) {
        if (static_cast<int>(p) != player) {
            best = std::max(best, finalScores[p]);
        }
    }
    return static_cast<float>(std::tanh((finalScores.at(player) - best) / scale));
}

namespace {
void writeCommand(std::ostream &out, const Command &c) {
    out << ' ' << c.type << ' ' << c.from << ' ' << c.to << ' ' << c.arg;
}
Command readCommand(std::istream &in) {
    int type;
    Command c{CommandTypes::ENDTURN};
    in >> type >> c.from >> c.to >> c.arg;
    c.type = static_cast<CommandTypes::CommandType>(type);
    return c;
}
} // namespace

// Format, per game:
//   game <size> <map type> <map seed> <candidates> <tribes> <tribe>...
//        <game seed> <winner> <turns> <scores> <score>...
//        [<values> <value>... <tiles> <owner>... <scores> <score>... <fog>
//         <peace treaties> <cities> <city count>... <root values>
//         <diplomacy meets> <n> <turn stars>... <n> <turn income>...
//         <n> <turn values>... <adjacent pushes> <n> <head start>...
//         <n> <capital income>... <n> (<player> <from turn> <extra>)...
//         <catch-up> <pool opponent seat> rules <rules version>]
//        (final values, final territory, the score table, whether it was
//        played with fog and peace treaties, the final city counts, whether
//        move lines end with a root value, and whether diplomacy introduced
//        the sender; absent in older files, which replay without it)
//   <moves>
//   <player> <command> <visits> (<command> <count>)...   one line per move
// where a command is <type> <from> <to> <arg>. The map is regenerated from
// its settings, with the generator's defaults for everything else.
namespace {
void writeRecords(std::ostream &out, const std::vector<GameRecord> &records) {
    for (const GameRecord &r : records) {
        out << "game " << r.map.size << ' ' << r.map.type << ' ' << r.map.seed
            << ' ' << r.map.candidates << ' ' << r.map.tribes.size();
        for (TribeTypes::Tribe t : r.map.tribes) {
            out << ' ' << t;
        }
        out << ' ' << r.gameSeed << ' ' << r.winner << ' ' << r.turns << ' '
            << r.finalScores.size();
        for (int s : r.finalScores) {
            out << ' ' << s;
        }
        out << ' ' << r.finalValues.size();
        for (float v : r.finalValues) {
            out << ' ' << v;
        }
        out << ' ' << r.finalTerritory.size();
        for (int owner : r.finalTerritory) {
            out << ' ' << owner;
        }
        out << ' ' << r.turnScores.size();
        for (int score : r.turnScores) {
            out << ' ' << score;
        }
        out << ' ' << r.fog << ' ' << r.peaceTreaties << ' ' << r.finalCities.size();
        for (int cities : r.finalCities) {
            out << ' ' << cities;
        }
        // each move line ends with its root value
        // the first two rule fixes, where older records kept them as flags
        // (rulesVersion, saved at the end, supersedes them)
        out << " 1 " << (r.rulesVersion >= RuleVersions::DIPLOMACY_MEETS);
        for (const std::vector<int> *table : {&r.turnStars, &r.turnIncome}) {
            out << ' ' << table->size();
            for (int v : *table) {
                out << ' ' << v;
            }
        }
        out << ' ' << r.turnValues.size();
        for (float v : r.turnValues) {
            out << ' ' << v;
        }
        out << ' ' << (r.rulesVersion >= RuleVersions::ADJACENT_PUSHES) << ' '
            << r.headStart.size();
        for (int stars : r.headStart) {
            out << ' ' << stars;
        }
        out << ' ' << r.capitalIncome.size();
        for (int stars : r.capitalIncome) {
            out << ' ' << stars;
        }
        out << ' ' << r.incomeBonuses.size();
        for (const IncomeBonus &b : r.incomeBonuses) {
            out << ' ' << b.player << ' ' << b.fromTurn << ' ' << b.extra;
        }
        out << ' ' << r.catchUp << ' ' << r.poolOpponentSeat << " rules " << r.rulesVersion;
        out << '\n' << r.moves.size() << '\n';
        for (const RecordedMove &m : r.moves) {
            out << m.player;
            writeCommand(out, m.command);
            out << ' ' << m.visits.size();
            for (const auto &[command, count] : m.visits) {
                writeCommand(out, command);
                out << ' ' << count;
            }
            out << ' ' << m.rootValue << '\n';
        }
    }
}
} // namespace

namespace {
// `path` names the source in error messages
std::vector<GameRecord> readRecords(std::istream &in, const std::string &path) {
    std::vector<GameRecord> records;
    std::string word;
    while (in >> word) {
        if (word != "game") {
            throw std::runtime_error("Bad record file " + path);
        }
        GameRecord r;
        int type;
        std::size_t tribes, scores, values, moves;
        // the header is one line, its final values optional
        std::string line;
        std::getline(in, line);
        std::istringstream header(line);
        header >> r.map.size >> type >> r.map.seed >> r.map.candidates >> tribes;
        r.map.type = static_cast<MapTypes::MapType>(type);
        for (std::size_t i = 0; i < tribes; i++) {
            int t;
            header >> t;
            r.map.tribes.push_back(static_cast<TribeTypes::Tribe>(t));
        }
        header >> r.gameSeed >> r.winner >> r.turns >> scores;
        r.finalScores.resize(scores);
        for (int &s : r.finalScores) {
            header >> s;
        }
        if (!header) {
            throw std::runtime_error("Bad record header in " + path);
        }
        if (header >> values) {
            r.finalValues.resize(values);
            for (float &v : r.finalValues) {
                // "nan" doesn't parse as a float, so read words
                std::string word;
                header >> word;
                v = word == "nan" ? std::nanf("") : std::stof(word);
            }
        }
        std::size_t count;
        if (header >> count) {
            r.finalTerritory.resize(count);
            for (int &owner : r.finalTerritory) {
                header >> owner;
            }
        }
        if (header >> count) {
            r.turnScores.resize(count);
            for (int &score : r.turnScores) {
                header >> score;
            }
        }
        if (int fog; header >> fog) {
            r.fog = fog != 0;
        }
        if (int peace; header >> peace) {
            r.peaceTreaties = peace != 0;
        }
        if (header >> count) {
            r.finalCities.resize(count);
            for (int &cities : r.finalCities) {
                header >> cities;
            }
        }
        bool rootValues = false;
        if (int flag; header >> flag) {
            rootValues = flag != 0;
        }
        // the rules version, where older records only had flags for the
        // first fixes: recorded before one, replayed without it
        r.rulesVersion = 0;
        if (int meets; header >> meets && meets) {
            r.rulesVersion = RuleVersions::DIPLOMACY_MEETS;
        }
        for (std::vector<int> *table : {&r.turnStars, &r.turnIncome}) {
            if (header >> count) {
                table->resize(count);
                for (int &v : *table) {
                    header >> v;
                }
            }
        }
        if (header >> count) {
            r.turnValues.resize(count);
            for (float &v : r.turnValues) {
                std::string word;
                header >> word;
                v = word == "nan" ? std::nanf("") : std::stof(word);
            }
        }
        if (int adjacent; header >> adjacent && adjacent) {
            r.rulesVersion = RuleVersions::ADJACENT_PUSHES;
        }
        if (header >> count) {
            r.headStart.resize(count);
            for (int &stars : r.headStart) {
                header >> stars;
            }
        }
        if (header >> count) {
            r.capitalIncome.resize(count);
            for (int &stars : r.capitalIncome) {
                header >> stars;
            }
        }
        if (header >> count) {
            r.incomeBonuses.resize(count);
            for (IncomeBonus &b : r.incomeBonuses) {
                header >> b.player >> b.fromTurn >> b.extra;
            }
        }
        if (int catchUp; header >> catchUp) {
            r.catchUp = catchUp != 0;
        }
        if (int seat; header >> seat) {
            r.poolOpponentSeat = seat;
        }
        // "rules <version>" since versions were saved; records written just
        // before have the third and fourth fixes as flags instead
        if (std::string word; header >> word) {
            if (word == "rules") {
                header >> r.rulesVersion;
            } else {
                if (word == "1") {
                    r.rulesVersion = RuleVersions::UNSEEN_ZONE_OF_CONTROL;
                }
                if (int stomps; header >> stomps && stomps) {
                    r.rulesVersion = RuleVersions::EMBARK_STOMP;
                }
            }
        }
        if (!header && !header.eof()) {
            throw std::runtime_error("Bad record header in " + path);
        }
        in >> moves;
        r.moves.resize(moves);
        for (RecordedMove &m : r.moves) {
            std::size_t visits;
            in >> m.player;
            m.command = readCommand(in);
            in >> visits;
            m.visits.resize(visits);
            for (auto &[command, count] : m.visits) {
                command = readCommand(in);
                in >> count;
            }
            if (rootValues) {
                std::string word;
                in >> word;
                m.rootValue = word == "nan" ? std::nanf("") : std::stof(word);
            }
        }
        if (!in) {
            throw std::runtime_error("Truncated record file " + path);
        }
        records.push_back(std::move(r));
    }
    return records;
}
} // namespace

void saveRecords(const std::vector<GameRecord> &records, const std::string &path,
                 bool append) {
    std::ofstream out(path, append ? std::ios::app : std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Can't write " + path);
    }
    writeRecords(out, records);
}

std::vector<GameRecord> loadRecords(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Can't read " + path);
    }
    return readRecords(in, path);
}

std::string recordsToText(const std::vector<GameRecord> &records) {
    std::ostringstream out;
    writeRecords(out, records);
    return out.str();
}

std::vector<GameRecord> recordsFromText(const std::string &text) {
    std::istringstream in(text);
    return readRecords(in, "(text)");
}

void encodeExamples(const std::vector<GameRecord> &records,
                    const std::vector<std::pair<int, int>> &positions,
                    const EncodingConfig &config, const ExampleArrays &out,
                    int threads, const std::vector<int> &symmetries,
                    const ValueTarget &value) {
    if (!symmetries.empty() && symmetries.size() != positions.size()) {
        throw std::invalid_argument("Need one symmetry per position");
    }
    const std::size_t n = config.mapSize,
                      planes = planeCount(config) * n * n,
                      globals = globalCount(config),
                      actions = actionCount(config),
                      targets = targetCount(config),
                      side = sideTargetCount(config);
    // each record replayed once, in order, stopping at the positions asked for
    std::map<int, std::vector<std::pair<int, std::size_t>>> byRecord;
    for (std::size_t i = 0; i < positions.size(); i++) {
        auto [record, move] = positions[i];
        if (record < 0 || record >= static_cast<int>(records.size()) ||
            move < 0 ||
            move >= static_cast<int>(records[record].moves.size())) {
            throw std::out_of_range("No such recorded position");
        }
        byRecord[record].push_back({move, i});
    }
    std::vector<std::pair<int, std::vector<std::pair<int, std::size_t>>>> work(
        byRecord.begin(), byRecord.end());
    // each game replayed on its own thread; they write disjoint rows
    parallelFor(static_cast<int>(work.size()), threads, [&](int w) {
        auto &[index, wanted] = work[w];
        const GameRecord &record = records[index];
        std::sort(wanted.begin(), wanted.end());
        Game game = record.start();
        int at = 0;
        for (auto [move, i] : wanted) {
            // recorded commands were legal when played (the replay test
            // checks), so skip checking them again
            for (; at < move; at++) {
                game.applyUnchecked(record.moves[at].command);
            }
            const RecordedMove &m = record.moves[move];
            int player = game.currentPlayer;
            if (player != m.player) {
                throw std::runtime_error("Record doesn't replay: wrong player");
            }
            // a game played without fog is seen the way it was played
            encodeState(game, player, config, out.planes + i * planes,
                        out.globals + i * globals, !record.fog);
            legalMask(game, config, out.legal + i * actions);
            float *policy = out.policy + i * actions;
            std::fill(policy, policy + actions, 0.0f);
            double total = 0;
            const std::uint8_t *legal = out.legal + i * actions;
            for (const auto &[command, count] : m.visits) {
                int slot = encodeCommand(game, command, config);
                // A move the search found legal in a determinized game can
                // be illegal in the real one (a hidden unit in the way).
                // Gumbel search's target weighs every root move, so such
                // moves would put weight where the policy can't go.
                if (slot >= 0 && legal[slot]) {
                    policy[slot] += static_cast<float>(count);
                    total += count;
                }
            }
            if (total > 0) {
                for (std::size_t a = 0; a < actions; a++) {
                    policy[a] = static_cast<float>(policy[a] / total);
                }
            }
            // the result, or a lambda-return of the player's later turns'
            // search values ending in it; partly the position's own search
            // value
            double result = lambdaReturn(record, player, game.turn + 1, value.lambda,
                                         record.valueEstimates);
            if (value.searchWeight > 0 && !std::isnan(m.rootValue)) {
                result = (1 - value.searchWeight) * result + value.searchWeight * m.rootValue;
            }
            out.value[i] = static_cast<float>(
                (1 - value.cityWeight) * result +
                value.cityWeight * record.cityValue(player));
            out.valueWeight[i] = !record.valueCounts(game.turn) ? 0.0f
                                 : record.winner >= 0           ? 1.0f
                                                    : static_cast<float>(value.cutOffWeight);
            if (out.turnPositions) {
                // a player's turn is a run of their moves (two players
                // alternate; with more, a run is still one turn)
                int count = 0;
                for (int k = move; k >= 0 && record.moves[k].player == player; k--) {
                    count += !record.moves[k].visits.empty();
                }
                for (int k = move + 1; k < static_cast<int>(record.moves.size()) &&
                                       record.moves[k].player == player;
                     k++) {
                    count += !record.moves[k].visits.empty();
                }
                out.turnPositions[i] = static_cast<float>(count);
            }
            encodeTargets(game, player, config, out.targets + i * targets,
                          out.targetMasks + i * targets, !record.fog);
            if (out.sideTargets) {
                encodeSideTargets(record, game.turn, player, config,
                                  out.sideTargets + i * side, out.sideMasks + i * side);
                // they're about how the game went too
                if (!record.valueCounts(game.turn)) {
                    std::fill(out.sideMasks + i * side, out.sideMasks + (i + 1) * side,
                              std::uint8_t{0});
                }
            }
            if (!symmetries.empty()) {
                applySymmetry(symmetries[i], game.size, config,
                              out.planes + i * planes, out.legal + i * actions,
                              policy, out.targets + i * targets,
                              out.targetMasks + i * targets);
                if (out.sideTargets) {
                    using namespace SideTargets;
                    applySymmetry(symmetries[i], game.size, config, TERRITORY_CLASSES,
                                  out.sideTargets + i * side + TERRITORY);
                    applySymmetry(symmetries[i], game.size, config, TERRITORY_CLASSES,
                                  out.sideMasks + i * side + TERRITORY);
                }
            }
        }
    });
}

double lambdaReturn(const GameRecord &record, int player, int from, double lambda,
                    const std::vector<float> &values) {
    double ret = record.value(player);
    const int players = static_cast<int>(record.finalScores.size());
    if (lambda >= 1 || values.empty() || players == 0) {
        return ret;
    }
    int rows = static_cast<int>(values.size()) / players;
    for (int t = rows - 1; t >= std::max(from, 0); t--) {
        float v = values[static_cast<std::size_t>(t) * players + player];
        if (!std::isnan(v)) {
            ret = (1 - lambda) * v + lambda * ret;
        }
    }
    return ret;
}

void thinPolicyTargets(std::vector<GameRecord> &records, double share,
                       std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::bernoulli_distribution keep(share);
    for (GameRecord &r : records) {
        for (RecordedMove &m : r.moves) {
            if (!m.visits.empty() && !keep(rng)) {
                m.visits.clear();
            }
        }
    }
}

void encodeSideTargets(const GameRecord &record, int turn, int player,
                       const EncodingConfig &config, float *targets,
                       std::uint8_t *masks) {
    using namespace SideTargets;
    const int n = config.mapSize, count = sideTargetCount(config),
              players = static_cast<int>(record.finalScores.size());
    std::fill(targets, targets + count, 0.0f);
    std::fill(masks, masks + count, std::uint8_t{0});
    // the player's lead over the best opponent, squashed
    auto lead = [&](const int *scores) {
        int best = std::numeric_limits<int>::min();
        for (int q = 0; q < players; q++) {
            if (q != player) {
                best = std::max(best, scores[q]);
            }
        }
        return static_cast<float>(std::tanh((scores[player] - best) / SCORE_SCALE));
    };
    targets[SCORE_END] = lead(record.finalScores.data());
    masks[SCORE_END] = 1;
    if (!record.turnScores.empty()) {
        int rows = static_cast<int>(record.turnScores.size()) / players;
        int soon = turn + SOON_TURNS;
        targets[SCORE_SOON] = soon < rows ? lead(&record.turnScores[soon * players])
                                          : targets[SCORE_END];
        masks[SCORE_SOON] = 1;
    }
    // the player's own stars and income ECONOMY_TURNS later: their latest
    // turn by then, or their last one if the game ended first
    if (!record.turnStars.empty() && record.turnStars.size() == record.turnIncome.size()) {
        int rows = static_cast<int>(record.turnStars.size()) / players;
        for (int t = std::min(turn + ECONOMY_TURNS, rows - 1); t >= 0; t--) {
            std::size_t at = static_cast<std::size_t>(t) * players + player;
            if (record.turnStars[at] >= 0) {
                targets[STARS_AHEAD] = static_cast<float>(record.turnStars[at] / STARS_SCALE);
                targets[INCOME_AHEAD] = static_cast<float>(record.turnIncome[at] / INCOME_SCALE);
                masks[STARS_AHEAD] = masks[INCOME_AHEAD] = 1;
                break;
            }
        }
    }
    // short-term values: lambda-returns of the player's own search values,
    // from the game's value at the end back to this turn; a turn without one
    // passes the return on
    if (!record.turnValues.empty()) {
        for (std::size_t h = 0; h < SHORT_TERM_HORIZONS.size(); h++) {
            targets[SHORT_TERM_VALUE + h] = static_cast<float>(lambdaReturn(
                record, player, turn, 1 - 1 / SHORT_TERM_HORIZONS[h], record.turnValues));
            masks[SHORT_TERM_VALUE + h] = 1;
        }
    }
    // turns left, only where the game really ended: a cut-off game says
    // nothing about when its end would have come
    if (record.winner >= 0) {
        double left = std::min<double>(std::max(0, record.turns - turn), TURNS_LEFT_CAP);
        targets[TURNS_LEFT] = static_cast<float>(left / TURNS_LEFT_SCALE);
        masks[TURNS_LEFT] = 1;
    }
    const int size = record.map.size;
    if (static_cast<int>(record.finalTerritory.size()) == size * size && size <= n) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int owner = record.finalTerritory[y * size + x];
                int cls = owner == player ? 0 : owner < 0 ? 2 : 1;
                targets[TERRITORY + cls * n * n + y * n + x] = 1;
                for (int c = 0; c < TERRITORY_CLASSES; c++) {
                    masks[TERRITORY + c * n * n + y * n + x] = 1;
                }
            }
        }
    }
}

namespace {
std::size_t searchedMoves(const GameRecord &r) {
    return std::count_if(r.moves.begin(), r.moves.end(),
                         [](const RecordedMove &m) { return !m.visits.empty(); });
}
} // namespace

void ReplayBuffer::add(std::vector<GameRecord> games) {
    std::lock_guard<std::mutex> lock(mutex);
    for (GameRecord &g : games) {
        std::size_t moves = searchedMoves(g);
        searched += moves;
        total += moves;
        records.push_back(std::move(g));
    }
    // the oldest games out, keeping at least one
    std::size_t drop = 0;
    while (drop + 1 < records.size() && searched > maxPositions) {
        searched -= searchedMoves(records[drop]);
        drop++;
    }
    records.erase(records.begin(), records.begin() + drop);
}

void ReplayBuffer::save(const std::string &path) const {
    std::lock_guard<std::mutex> lock(mutex);
    saveRecords(records, path, false);
}

std::size_t ReplayBuffer::gameCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return records.size();
}

std::size_t ReplayBuffer::positions() const {
    std::lock_guard<std::mutex> lock(mutex);
    return searched;
}

std::size_t ReplayBuffer::added() const {
    std::lock_guard<std::mutex> lock(mutex);
    return total;
}

double ReplayBuffer::decidedShare() const {
    std::lock_guard<std::mutex> lock(mutex);
    if (records.empty()) {
        return 0;
    }
    auto decided = std::count_if(records.begin(), records.end(),
                                 [](const GameRecord &r) { return r.winner >= 0; });
    return static_cast<double>(decided) / static_cast<double>(records.size());
}

std::vector<std::pair<int, int>> ReplayBuffer::sample(int count,
                                                      std::uint64_t seed,
                                                      int perGame) const {
    std::lock_guard<std::mutex> lock(mutex);
    return sampleUnlocked(count, seed, perGame);
}

void ReplayBuffer::encode(const std::vector<std::pair<int, int>> &positions,
                          const EncodingConfig &config, const ExampleArrays &out,
                          int threads, const ValueTarget &value) const {
    std::lock_guard<std::mutex> lock(mutex);
    encodeExamples(records, positions, config, out, threads, {}, value);
}

void ReplayBuffer::encodeSample(int count, std::uint64_t seed, int perGame,
                                const EncodingConfig &config,
                                const ExampleArrays &out, int threads,
                                bool augment, const ValueTarget &value,
                                std::vector<std::pair<std::uint64_t, int>> *drawn) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::pair<int, int>> positions = sampleUnlocked(count, seed, perGame);
    if (drawn) {
        drawn->clear();
        for (auto [record, move] : positions) {
            drawn->push_back({records[record].gameSeed, move});
        }
    }
    std::vector<int> symmetries;
    if (augment) {
        // a different stream from the sample's own
        std::mt19937_64 rng(seed ^ 0x9e3779b97f4a7c15ULL);
        std::uniform_int_distribution<int> pick(0, SYMMETRIES - 1);
        for (std::size_t i = 0; i < positions.size(); i++) {
            symmetries.push_back(pick(rng));
        }
    }
    encodeExamples(records, positions, config, out, threads, symmetries, value);
}

std::vector<std::uint64_t> ReplayBuffer::gameSeeds(bool withoutEstimates) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::uint64_t> out;
    for (const GameRecord &r : records) {
        if (!withoutEstimates || r.valueEstimates.empty()) {
            out.push_back(r.gameSeed);
        }
    }
    return out;
}

std::vector<ReplayBuffer::TurnStart> ReplayBuffer::encodeTurnStarts(
    const std::vector<std::uint64_t> &seeds, const EncodingConfig &config,
    std::vector<float> &planes, std::vector<float> &globals, int threads) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < records.size(); i++) {
        index[records[i].gameSeed] = i;
    }
    // each game's turn starts: its first move, and every move by a player
    // other than the last move's
    std::vector<std::size_t> games, first;
    std::vector<TurnStart> starts;
    for (std::uint64_t seed : seeds) {
        auto it = index.find(seed);
        if (it == index.end()) {
            continue;
        }
        const GameRecord &r = records[it->second];
        games.push_back(it->second);
        first.push_back(starts.size());
        for (std::size_t m = 0; m < r.moves.size(); m++) {
            if (m == 0 || r.moves[m].player != r.moves[m - 1].player) {
                starts.push_back({seed, 0, r.moves[m].player});
            }
        }
    }
    const std::size_t n = config.mapSize, p = planeCount(config) * n * n,
                      g = globalCount(config);
    planes.assign(starts.size() * p, 0.0f);
    globals.assign(starts.size() * g, 0.0f);
    parallelFor(static_cast<int>(games.size()), threads, [&](int k) {
        const GameRecord &r = records[games[k]];
        Game game = r.start();
        std::size_t at = first[k];
        for (std::size_t m = 0; m < r.moves.size(); m++) {
            if (m == 0 || r.moves[m].player != r.moves[m - 1].player) {
                starts[at].turn = game.turn;
                encodeState(game, game.currentPlayer, config, planes.data() + at * p,
                            globals.data() + at * g, !r.fog);
                at++;
            }
            game.applyUnchecked(r.moves[m].command);
        }
    });
    return starts;
}

void ReplayBuffer::setValueEstimates(const std::vector<TurnStart> &starts,
                                     const std::vector<float> &values) {
    if (values.size() != starts.size()) {
        throw std::invalid_argument("Need one value per turn start");
    }
    std::lock_guard<std::mutex> lock(mutex);
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < records.size(); i++) {
        index[records[i].gameSeed] = i;
    }
    for (std::size_t i = 0; i < starts.size(); i++) {
        auto it = index.find(starts[i].gameSeed);
        if (it == index.end()) {
            continue;
        }
        GameRecord &r = records[it->second];
        const std::size_t players = r.finalScores.size();
        const std::size_t rows = static_cast<std::size_t>(r.turns) + 1;
        if (r.valueEstimates.size() != rows * players) {
            r.valueEstimates.assign(rows * players, std::nanf(""));
        }
        std::size_t at = static_cast<std::size_t>(starts[i].turn) * players + starts[i].player;
        if (at < r.valueEstimates.size()) {
            r.valueEstimates[at] = values[i];
        }
    }
}

std::vector<std::pair<int, int>> ReplayBuffer::sampleUnlocked(int count,
                                                              std::uint64_t seed,
                                                              int perGame) const {
    std::vector<std::pair<int, int>> out;
    if (searched == 0) {
        return out;
    }
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, searched - 1);
    // the k-th searched move overall, found by game
    std::vector<std::size_t> starts;
    std::size_t total = 0;
    for (const GameRecord &r : records) {
        starts.push_back(total);
        total += searchedMoves(r);
    }
    perGame = std::max(1, perGame);
    while (static_cast<int>(out.size()) < count) {
        // a game, in proportion to its positions, then its positions
        std::size_t k = pick(rng);
        int game = static_cast<int>(
            std::upper_bound(starts.begin(), starts.end(), k) - starts.begin() - 1);
        const GameRecord &r = records[game];
        std::vector<int> searchedHere;
        for (std::size_t m = 0; m < r.moves.size(); m++) {
            if (!r.moves[m].visits.empty()) {
                searchedHere.push_back(static_cast<int>(m));
            }
        }
        std::uniform_int_distribution<std::size_t> within(0, searchedHere.size() - 1);
        for (int j = 0; j < perGame && static_cast<int>(out.size()) < count; j++) {
            out.push_back({game, searchedHere[within(rng)]});
        }
    }
    return out;
}

