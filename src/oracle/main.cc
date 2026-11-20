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

// games by id, each held by a GC handle so the runtime doesn't collect it
std::map<int, GCHandle> games;
int nextGame = 0;

Object *game(std::istream &words) {
    int id = -1;
    words >> id;
    auto found = games.find(id);
    if (found == games.end()) {
        throw std::runtime_error("no game " + std::to_string(id));
    }
    return gchandle_get_target(found->second);
}

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
        int id = nextGame++;
        games[id] = gchandle_new(oracle::createLocalGame(g), false);
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
    if (request == "free") {
        int id = -1;
        words >> id;
        auto found = games.find(id);
        if (found != games.end()) {
            gchandle_free(found->second);
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
        try {
            std::cout << handle(line) << std::endl;
        } catch (const std::exception &e) {
            std::cout << "{\"error\":" << quoted(e.what()) << "}" << std::endl;
        }
    }
}
