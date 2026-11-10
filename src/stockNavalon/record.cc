#include "record.h"
#include "parallel.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <random>
#include <stdexcept>

Game GameRecord::start() const {
    return Game(generateMap(map), map.tribes, {.seed = gameSeed});
}

float GameRecord::result(int player) const {
    return winner < 0 ? 0.0f : winner == player ? 1.0f : -1.0f;
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
//   <moves>
//   <player> <command> <visits> (<command> <count>)...   one line per move
// where a command is <type> <from> <to> <arg>. The map is regenerated from
// its settings, with the generator's defaults for everything else.
void saveRecords(const std::vector<GameRecord> &records, const std::string &path,
                 bool append) {
    std::ofstream out(path, append ? std::ios::app : std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Can't write " + path);
    }
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
        out << '\n' << r.moves.size() << '\n';
        for (const RecordedMove &m : r.moves) {
            out << m.player;
            writeCommand(out, m.command);
            out << ' ' << m.visits.size();
            for (const auto &[command, count] : m.visits) {
                writeCommand(out, command);
                out << ' ' << count;
            }
            out << '\n';
        }
    }
}

std::vector<GameRecord> loadRecords(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Can't read " + path);
    }
    std::vector<GameRecord> records;
    std::string word;
    while (in >> word) {
        if (word != "game") {
            throw std::runtime_error("Bad record file " + path);
        }
        GameRecord r;
        int type;
        std::size_t tribes, scores, moves;
        in >> r.map.size >> type >> r.map.seed >> r.map.candidates >> tribes;
        r.map.type = static_cast<MapTypes::MapType>(type);
        for (std::size_t i = 0; i < tribes; i++) {
            int t;
            in >> t;
            r.map.tribes.push_back(static_cast<TribeTypes::Tribe>(t));
        }
        in >> r.gameSeed >> r.winner >> r.turns >> scores;
        r.finalScores.resize(scores);
        for (int &s : r.finalScores) {
            in >> s;
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
        }
        if (!in) {
            throw std::runtime_error("Truncated record file " + path);
        }
        records.push_back(std::move(r));
    }
    return records;
}

void encodeExamples(const std::vector<GameRecord> &records,
                    const std::vector<std::pair<int, int>> &positions,
                    const EncodingConfig &config, const ValueTarget &value,
                    const ExampleArrays &out, int threads) {
    const std::size_t n = config.mapSize,
                      planes = planeCount(config) * n * n,
                      globals = globalCount(config),
                      actions = actionCount(config),
                      targets = targetCount(config);
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
            encodeState(game, player, config, out.planes + i * planes,
                        out.globals + i * globals);
            legalMask(game, config, out.legal + i * actions);
            float *policy = out.policy + i * actions;
            std::fill(policy, policy + actions, 0.0f);
            double total = 0;
            for (const auto &[command, count] : m.visits) {
                int slot = encodeCommand(game, command, config);
                if (slot >= 0) {
                    policy[slot] += static_cast<float>(count);
                    total += count;
                }
            }
            if (total > 0) {
                for (std::size_t a = 0; a < actions; a++) {
                    policy[a] = static_cast<float>(policy[a] / total);
                }
            }
            // a game cut off by the turn limit has a result of 0
            out.value[i] = static_cast<float>(
                value.scoreWeight * record.scoreValue(player, value.scale) +
                (1 - value.scoreWeight) * record.result(player));
            out.valueWeight[i] = 1;
            encodeTargets(game, player, config, out.targets + i * targets,
                          out.targetMasks + i * targets);
        }
    });
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
                          const EncodingConfig &config, const ValueTarget &value,
                          const ExampleArrays &out, int threads) const {
    std::lock_guard<std::mutex> lock(mutex);
    encodeExamples(records, positions, config, value, out, threads);
}

void ReplayBuffer::encodeSample(int count, std::uint64_t seed, int perGame,
                                const EncodingConfig &config,
                                const ValueTarget &value, const ExampleArrays &out,
                                int threads) const {
    std::lock_guard<std::mutex> lock(mutex);
    encodeExamples(records, sampleUnlocked(count, seed, perGame), config, value,
                   out, threads);
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

