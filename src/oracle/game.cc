#include "game.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace oracle {
namespace {

using namespace il2cpp;

const Image *logic = nullptr;
const Image *backend = nullptr;

Class *logicClass(const std::string &ns, const std::string &name) {
    return findClass(logic, ns, name);
}

std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("can't read " + path);
    }
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// a new object through its parameterless constructor
Object *construct(Class *klass) {
    Object *o = object_new(klass);
    runtime_object_init(o);
    return o;
}

char *fieldAddress(Object *o, const char *name) {
    Field *f = class_get_field_from_name(object_get_class(o), name);
    if (f == nullptr) {
        throw std::runtime_error(std::string("no field ") + name);
    }
    return reinterpret_cast<char *>(o) + field_get_offset(f);
}
template <typename T> void setField(Object *o, const char *name, T value) {
    *reinterpret_cast<T *>(fieldAddress(o, name)) = value;
}
template <typename T> T getField(Object *o, const char *name) {
    return *reinterpret_cast<T *>(fieldAddress(o, name));
}

int unboxInt(Object *o) { return *static_cast<int *>(object_unbox(o)); }

// a static method's int result
int staticInt(Class *klass, const char *method, void **args = nullptr,
              int params = 0) {
    return unboxInt(invoke(findMethod(klass, method, params), nullptr, args));
}

// a static Dictionary<int, T> field's set_Item(key, value)
void putStatic(Class *klass, const char *field, int key, Object *value) {
    Object *dict = nullptr;
    field_static_get_value(class_get_field_from_name(klass, field), &dict);
    void *args[] = {&key, value};
    invoke(findMethod(object_get_class(dict), "set_Item", 2), dict, args);
}

// parses a rules data file into a new GameLogicData or AvatarData
Object *parseData(Class *klass, const std::string &path) {
    Object *data = construct(klass);
    void *args[] = {string_new(readFile(path).c_str())};
    invoke(findMethod(klass, "Parse", 1), data, args);
    return data;
}

// A new List<PlayerState>. The runtime can't make a generic class by name,
// so the class comes from a GameState's own list.
Object *playerList(const std::vector<Object *> &players) {
    Object *state = construct(logicClass("", "GameState"));
    Class *listClass = object_get_class(
        getField<Object *>(state, "<PlayerStates>k__BackingField"));
    Object *list = construct(listClass);
    const Method *add = findMethod(listClass, "Add", 1);
    for (Object *player : players) {
        void *args[] = {player};
        invoke(add, list, args);
    }
    return list;
}

} // namespace

int start(const std::string &buildDir) {
    load(buildDir + "/GameAssembly-arm64.dylib", buildDir);
    logic = image("GameLogicAssembly.dll");
    backend = image("PolytopiaBackendBase.dll");

    Class *versions = logicClass("", "VersionManager");
    int gameVersion = staticInt(versions, "get_GameVersion");
    void *versionArg[] = {&gameVersion};
    int dataVersion = staticInt(
        versions, "GetGameLogicDataVersionFromGameVersion", versionArg, 1);
    int avatarVersion = staticInt(versions, "get_AvatarDataVersion");

    // The app's data provider reads Unity assets; instead the manager's
    // caches get the extracted files, so it never asks a provider.
    Class *manager = logicClass("", "PolytopiaDataManager");
    runtime_class_init(manager);
    putStatic(manager, "gameLogicDatas", dataVersion,
              parseData(logicClass("Polytopia.Data", "GameLogicData"),
                        buildDir + "/gamedata/GameLogicData" +
                            std::to_string(dataVersion) + ".txt"));
    putStatic(manager, "avatarDatas", avatarVersion,
              parseData(logicClass("Polytopia.Data", "AvatarData"),
                        buildDir + "/gamedata/AvatarData.txt"));
    return gameVersion;
}

int enumValue(const std::string &enumType, const std::string &name) {
    static std::map<std::string, std::map<std::string, int>> cache;
    auto [values, fresh] = cache.try_emplace(enumType);
    if (fresh) {
        Class *klass = nullptr;
        for (const char *ns :
             {"PolytopiaBackendBase.Game", "PolytopiaBackendBase.Common"}) {
            klass = class_from_name(backend, ns, enumType.c_str());
            if (klass != nullptr) {
                break;
            }
        }
        if (klass == nullptr || !class_is_enum(klass)) {
            cache.erase(enumType);
            throw std::runtime_error("no enum " + enumType);
        }
        void *iter = nullptr;
        while (Field *f = class_get_fields(klass, &iter)) {
            if (field_get_flags(f) & 0x10) { // static: the enum's values
                int v = 0;
                field_static_get_value(f, &v);
                values->second[lower(field_get_name(f))] = v;
            }
        }
    }
    auto found = values->second.find(lower(name));
    if (found == values->second.end()) {
        throw std::runtime_error("no " + enumType + " " + name);
    }
    return found->second;
}

Object *createLocalGame(const LocalGame &game) {
    if (game.tribes.size() < 2) {
        throw std::runtime_error("a game needs at least two tribes");
    }
    int version = staticInt(logicClass("", "VersionManager"), "get_GameVersion");

    Class *settingsClass = logicClass("", "GameSettings");
    Object *settings = construct(settingsClass);
    auto call = [&](const char *method, int value) {
        void *args[] = {&value};
        invoke(findMethod(settingsClass, method, 1), settings, args);
    };
    int gameType = enumValue("GameType", "SinglePlayer");
    int mode = game.mode;
    void *typeArgs[] = {&gameType, &mode};
    invoke(findMethod(settingsClass, "ApplyGameTypeDefaults", 2), settings,
           typeArgs);
    setField<int>(settings, "mapPreset", game.map);
    call("set_MapSize", game.size);
    call("set_OpponentCount", static_cast<int>(game.tribes.size()) - 1);
    call("set_Difficulty", game.difficulty);
    int difficulty = game.difficulty;
    void *difficultyArg[] = {&difficulty};
    int handicap = staticInt(settingsClass, "HandicapFromDifficulty",
                             difficultyArg, 1);

    // the own player, as the app's ClientBase.CreateSession sets it up
    Object *own = construct(logicClass("", "PlayerState"));
    setField<uint8_t>(own, "<Id>k__BackingField", 1);
    setField<int>(own, "tribe", game.tribes[0]);
    setField<bool>(own, "hasChosenTribe", true);

    // the game's AI, with the chosen tribes
    Class *utils = logicClass("", "GameStateUtils");
    const Method *createAI = findMethod(utils, "CreateAIOpponent", 5);
    std::vector<Object *> ais;
    for (size_t i = 1; i < game.tribes.size(); i++) {
        int id = static_cast<int>(i) + 1;
        int tribe = game.tribes[i];
        int skin = 0, climate = 0;
        void *args[] = {&id, &tribe, &handicap, &skin, &climate};
        ais.push_back(invoke(createAI, nullptr, args));
    }

    void *args[] = {&version, settings, own, playerList(ais)};
    return invoke(findMethod(utils, "CreateLocalGame", 4), nullptr, args);
}

} // namespace oracle
