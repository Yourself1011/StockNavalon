// The oracle: the real game's rules, served over stdin and stdout
// (docs/oracle.md). One request a line, one JSON answer a line.

#include "dump.h"
#include "game.h"

#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace il2cpp;

// A game, its ActionManager once started, and the last legal list asked
// for, each held by GC handles so the runtime doesn't collect them
struct Held {
    GCHandle state = nullptr, manager = nullptr;
    std::vector<GCHandle> legal;
};
std::map<int, Held> games;
int nextGame = 0;

void release(std::vector<GCHandle> &handles) {
    for (GCHandle h : handles) {
        gchandle_free(h);
    }
    handles.clear();
}

Held &held(std::istream &words) {
    int id = -1;
    words >> id;
    auto found = games.find(id);
    if (found == games.end()) {
        throw std::runtime_error("no game " + std::to_string(id));
    }
    return found->second;
}

Object *game(std::istream &words) { return gchandle_get_target(held(words).state); }

std::string word(std::istream &words) {
    std::string w;
    if (!(words >> w)) {
        throw std::runtime_error("missing argument");
    }
    return w;
}

// a field of an object, by name
Object *member(Object *o, const char *name) {
    Field *f = class_get_field_from_name(object_get_class(o), name);
    return *reinterpret_cast<Object **>(reinterpret_cast<char *>(o) +
                                        field_get_offset(f));
}

std::string handle(const std::string &line) {
    std::istringstream words(line);
    std::string request = word(words);
    if (request == "new") {
        oracle::LocalGame g;
        g.map = oracle::enumValue("MapPreset", word(words));
        g.size = std::stoi(word(words));
        g.mode = oracle::enumValue("GameMode", word(words));
        g.difficulty = oracle::enumValue("BotDifficulty", word(words));
        std::string tribe;
        while (words >> tribe) {
            g.tribes.push_back(oracle::enumValue("TribeType", tribe));
        }
        // the id is used up even if the game fails, so the k-th new is
        // always game k
        int id = nextGame++;
        games[id].state = gchandle_new(oracle::createLocalGame(g), false);
        return "{\"game\":" + std::to_string(id) + "}";
    }
    if (request == "state") {
        return oracle::dump(game(words));
    }
    if (request == "map") {
        return oracle::dump(member(game(words), "<Map>k__BackingField"));
    }
    if (request == "mapsettings") {
        // the map generator's settings for this game's settings
        Object *settings = member(game(words), "<Settings>k__BackingField");
        return oracle::dump(invoke(
            findMethod(object_get_class(settings), "GetMapGeneratorSettings", 0),
            settings, nullptr));
    }
    if (request == "fairness") {
        // the map generator's score of how unequal the players' starts are
        // (MapFairnessCalculations.GetInequality): the best territory's
        // score over the worst's
        Object *state = game(words);
        const Image *logic = image("GameLogicAssembly.dll");
        void *args[] = {state};
        Object *result = invoke(
            findMethod(findClass(logic, "", "MapFairnessCalculations"), "GetInequality", 1),
            nullptr, args);
        return "{\"inequality\":" + std::to_string(*static_cast<float *>(object_unbox(result))) + "}";
    }
    if (request == "turfs") {
        // the territories the fairness score is made from
        // (MapFairnessCalculations.CreateTurfs): per player, each tile with
        // its cost from the capital
        Object *state = game(words);
        const Image *logic = image("GameLogicAssembly.dll");
        void *args[] = {state};
        Object *turfs = invoke(
            findMethod(findClass(logic, "", "MapFairnessCalculations"), "CreateTurfs", 1),
            nullptr, args);
        // a Dictionary<int, List<TurfTile>>: each player's list through
        // the dictionary's own methods, as [player, list] pairs
        Class *dict = object_get_class(turfs);
        const Method *has = findMethod(dict, "ContainsKey", 1);
        const Method *get = findMethod(dict, "get_Item", 1);
        std::string out = "[";
        for (int player = 0; player < 256; player++) {
            void *key[] = {&player};
            if (!*static_cast<bool *>(object_unbox(invoke(has, turfs, key)))) {
                continue;
            }
            out += (out.size() > 1 ? ",[" : "[") + std::to_string(player) + "," +
                   oracle::dump(invoke(get, turfs, key)) + "]";
        }
        return out + "]";
    }
    if (request == "start") {
        // every seat a person, the match started
        Held &g = held(words);
        g.manager = gchandle_new(oracle::startMatch(gchandle_get_target(g.state)), false);
        return "{}";
    }
    if (request == "legal") {
        // the current player's legal commands, numbered for apply
        Held &g = held(words);
        release(g.legal);
        std::string out = "[";
        for (Object *command : oracle::legalCommands(gchandle_get_target(g.state))) {
            out += (g.legal.empty() ? "" : ",") + oracle::dump(command);
            g.legal.push_back(gchandle_new(command, false));
        }
        return out + "]";
    }
    if (request == "apply") {
        // the k-th command of the last legal list
        Held &g = held(words);
        int k = -1;
        words >> k;
        if (g.manager == nullptr) {
            throw std::runtime_error("the match isn't started");
        }
        if (k < 0 || k >= static_cast<int>(g.legal.size())) {
            throw std::runtime_error("no legal command " + std::to_string(k));
        }
        Object *command = gchandle_get_target(g.legal[k]);
        release(g.legal);
        std::string error = oracle::execute(gchandle_get_target(g.manager), command);
        return error.empty() ? "{}" : "{\"rejected\":\"" + error + "\"}";
    }
    if (request == "free") {
        int id = -1;
        words >> id;
        auto found = games.find(id);
        if (found != games.end()) {
            release(found->second.legal);
            gchandle_free(found->second.state);
            if (found->second.manager != nullptr) {
                gchandle_free(found->second.manager);
            }
            games.erase(found);
        }
        return "{}";
    }
    throw std::runtime_error("unknown request " + request);
}

std::string quoted(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            out += c;
        }
    }
    return out + '"';
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: oracle <pinned build directory>\n");
        return 2;
    }
    try {
        int version = oracle::start(argv[1]);
        std::cout << "{\"version\":" << version << "}" << std::endl;
    } catch (const std::exception &e) {
        std::cout << "{\"error\":" << quoted(e.what()) << "}" << std::endl;
        return 1;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        // no garbage collection during a request: objects it builds are held
        // in our own memory, which the collector doesn't scan; between
        // requests everything kept is held by GC handles
        gc_disable();
        try {
            std::cout << handle(line) << std::endl;
        } catch (const std::exception &e) {
            std::cout << "{\"error\":" << quoted(e.what()) << "}" << std::endl;
        }
        gc_enable();
    }
}
