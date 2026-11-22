#include "game.h"

#include <algorithm>
#include <array>
#include <cstdio>
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

// a method by name on the object's class or a parent
const Method *methodOf(Class *klass, const char *name, int params) {
    for (Class *c = klass; c != nullptr; c = class_get_parent(c)) {
        if (const Method *m = class_get_method_from_name(c, name, params)) {
            return m;
        }
    }
    throw std::runtime_error(std::string("no method ") + name);
}
Object *call(Object *o, const char *name, int params, void **args) {
    return invoke(methodOf(object_get_class(o), name, params), o, args);
}
Object *callStatic(Class *klass, const char *name, int params, void **args) {
    return invoke(findMethod(klass, name, params), nullptr, args);
}
// a new object through the constructor with this many parameters
Object *constructWith(Class *klass, int params, void **args) {
    Object *o = object_new(klass);
    invoke(findMethod(klass, ".ctor", params), o, args);
    return o;
}
// a List<T>'s or array's elements: for a reference type the objects, for a
// value type the address of each element's data
std::vector<void *> elements(Object *collection) {
    std::vector<void *> out;
    if (collection == nullptr) {
        return out;
    }
    Object *array = collection;
    int count = 0;
    Class *klass = object_get_class(collection);
    if (std::string(class_get_name(klass)) == "List`1") {
        array = *reinterpret_cast<Object **>(reinterpret_cast<char *>(collection) +
                                             field_get_offset(class_get_field_from_name(klass, "_items")));
        count = *reinterpret_cast<int *>(reinterpret_cast<char *>(collection) +
                                         field_get_offset(class_get_field_from_name(klass, "_size")));
    } else {
        count = static_cast<int>(array_length(reinterpret_cast<Array *>(array)));
    }
    Class *arrayClass = object_get_class(array);
    Class *element = class_get_element_class(arrayClass);
    bool value = class_is_valuetype(element);
    // (not il2cpp_class_array_element_size, which is wrong for some structs)
    uint32_t align = 0;
    int size = value ? class_value_size(element, &align) : static_cast<int>(sizeof(void *));
    char *data = reinterpret_cast<char *>(array) + 0x20;
    for (int i = 0; i < count; i++) {
        char *at = data + static_cast<size_t>(i) * size;
        out.push_back(value ? static_cast<void *>(at) : *reinterpret_cast<Object **>(at));
    }
    return out;
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

namespace {
// an enum's value by name, ignoring case
int enumOf(Class *klass, const std::string &name) {
    void *iter = nullptr;
    while (Field *f = class_get_fields(klass, &iter)) {
        if ((field_get_flags(f) & 0x10) && lower(field_get_name(f)) == lower(name)) {
            int v = 0;
            field_static_get_value(f, &v);
            return v;
        }
    }
    throw std::runtime_error(std::string("no ") + class_get_name(klass) + " " + name);
}
} // namespace

std::string runAction(Object *state, Object *manager, const std::string &name,
                      const std::vector<std::string> &args) {
    Class *klass = logicClass("", name);
    int count = static_cast<int>(args.size());
    const Method *ctor = findMethod(klass, ".ctor", count);
    // each argument as its parameter's type wants it
    std::vector<std::array<char, 16>> values(args.size());
    std::vector<void *> pointers;
    for (int i = 0; i < count; i++) {
        const Type *type = method_get_param(ctor, i);
        Class *param = class_from_type(type);
        char *v = values[i].data();
        std::string paramName = class_get_name(param);
        if (class_is_enum(param)) {
            *reinterpret_cast<int *>(v) = enumOf(param, args[i]);
        } else if (paramName == "WorldCoordinates") {
            int x = 0, y = 0;
            if (std::sscanf(args[i].c_str(), "%d,%d", &x, &y) != 2) {
                throw std::runtime_error("coordinates are x,y: " + args[i]);
            }
            reinterpret_cast<int *>(v)[0] = x;
            reinterpret_cast<int *>(v)[1] = y;
        } else if (paramName == "Byte") {
            *reinterpret_cast<uint8_t *>(v) = static_cast<uint8_t>(std::stoi(args[i]));
        } else if (paramName == "Boolean") {
            *reinterpret_cast<bool *>(v) = args[i] == "1" || lower(args[i]) == "true";
        } else if (paramName == "Int32" || paramName == "UInt32" || paramName == "Int16") {
            *reinterpret_cast<int *>(v) = std::stoi(args[i]);
        } else {
            throw std::runtime_error("can't pass a " + paramName);
        }
        pointers.push_back(v);
    }
    Object *action = object_new(klass);
    invoke(ctor, action, pointers.data());
    // queued as a command's actions are, and run to the end
    void *addArg[] = {action};
    call(getField<Object *>(state, "<ActionStack>k__BackingField"), "Add", 1, addArg);
    for (int i = 0; i < 1000; i++) {
        call(manager, "ForceUpdate", 0, nullptr);
        if (!*static_cast<bool *>(object_unbox(call(manager, "get_IsProcessing", 0, nullptr)))) {
            break;
        }
    }
    return "";
}

void setInt(Object *target, const std::string &field, int value) {
    Field *f = class_get_field_from_name(object_get_class(target), field.c_str());
    if (f == nullptr) {
        throw std::runtime_error("no field " + field);
    }
    char *at = reinterpret_cast<char *>(target) + field_get_offset(f);
    std::string type = class_get_name(class_from_type(field_get_type(f)));
    Class *klass = class_from_type(field_get_type(f));
    if (type == "Byte" || type == "Boolean") {
        *reinterpret_cast<uint8_t *>(at) = static_cast<uint8_t>(value);
    } else if (type == "UInt16" || type == "Int16") {
        *reinterpret_cast<uint16_t *>(at) = static_cast<uint16_t>(value);
    } else if (type == "Int32" || type == "UInt32" || class_is_enum(klass)) {
        *reinterpret_cast<int *>(at) = value;
    } else {
        throw std::runtime_error("can't set a " + type);
    }
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

namespace oracle {

Object *startMatch(Object *state, bool keepHandicaps) {
    for (void *p : elements(getField<Object *>(state, "<PlayerStates>k__BackingField"))) {
        Object *player = static_cast<Object *>(p);
        if (getField<uint8_t>(player, "<Id>k__BackingField") == 255) {
            continue;  // nature
        }
        setField<bool>(player, "<AutoPlay>k__BackingField", false);
        if (!keepHandicaps) {
            setField<int>(player, "handicap", 1);
        }
    }
    void *stateArg[] = {state};
    Object *manager = constructWith(logicClass("", "ActionManager"), 1, stateArg);
    uint8_t first = 1;
    void *playerArg[] = {&first};
    std::string error =
        execute(manager, constructWith(logicClass("", "StartMatchCommand"), 1, playerArg));
    if (!error.empty()) {
        throw std::runtime_error("StartMatchCommand: " + error);
    }
    return manager;
}

Object *aiMove(Object *state) {
    uint8_t id = *static_cast<uint8_t *>(object_unbox(call(state, "get_CurrentPlayer", 0, nullptr)));
    Object *player = nullptr;
    void *find[] = {&id, &player};
    call(state, "TryGetPlayer", 2, find);
    if (player == nullptr) {
        throw std::runtime_error("no current player");
    }
    // as the game plays a bot's turn (GameStateUtils.ExecuteCommands): a
    // pending trigger's answer first (a city reward, a peace offer), else
    // its best move
    Object *trigger = nullptr;
    void *triggerArgs[] = {state, &trigger};
    if (*static_cast<bool *>(object_unbox(
            callStatic(logicClass("", "CommandTriggerUtils"), "TryGetTriggerCommand", 2, triggerArgs))) &&
        trigger != nullptr) {
        return trigger;
    }
    int specific = 0;  // CommandType.None: whatever it likes best
    void *args[] = {state, player, &specific};
    return callStatic(logicClass("", "AI"), "GetMove", 3, args);
}

std::vector<Object *> legalCommands(Object *state) {
    std::vector<Object *> candidates;
    auto add = [&](Object *list) {
        for (void *c : elements(list)) {
            candidates.push_back(static_cast<Object *>(c));
        }
    };
    uint8_t id = *static_cast<uint8_t *>(object_unbox(call(state, "get_CurrentPlayer", 0, nullptr)));
    Object *player = nullptr;
    void *find[] = {&id, &player};
    call(state, "TryGetPlayer", 2, find);
    if (player == nullptr) {
        return {};
    }
    Class *utils = logicClass("", "CommandUtils");
    Class *units = logicClass("", "UnitDataExtensions");
    Class *moveClass = logicClass("", "MoveCommand"), *attackClass = logicClass("", "AttackCommand");
    Object *map = getField<Object *>(state, "<Map>k__BackingField");
    bool no = false;
    for (void *t : elements(getField<Object *>(map, "tiles"))) {
        Object *tile = static_cast<Object *>(t);
        // the client only acts on tiles the player has explored (the
        // commands' own checks would allow a road in the fog)
        void *explorer[] = {&id};
        if (!*static_cast<bool *>(object_unbox(call(tile, "GetExplored", 1, explorer)))) {
            continue;
        }
        void *perTile[] = {state, player, tile, &no};
        add(callStatic(utils, "GetUnitActions", 4, perTile));
        add(callStatic(utils, "GetTrainableUnits", 4, perTile));
        add(callStatic(utils, "GetBuildableImprovements", 4, perTile));
        add(callStatic(utils, "GetImprovementAbilities", 4, perTile));
        Object *unit = getField<Object *>(tile, "<unit>k__BackingField");
        if (unit == nullptr || getField<uint8_t>(unit, "owner") != id) {
            continue;
        }
        void *unitArgs[] = {unit, state};
        int movement = unboxInt(callStatic(units, "GetMovement", 2, unitArgs));
        void *moveArgs[] = {unit, state, &movement};
        for (void *to : elements(callStatic(units, "GetMovementOptions", 3, moveArgs))) {
            void *args[] = {&id, unit, to};
            candidates.push_back(constructWith(moveClass, 3, args));
        }
        int range = unboxInt(callStatic(units, "GetRange", 2, unitArgs));
        void *attackArgs[] = {unit, state, &range, &no};
        for (void *to : elements(callStatic(units, "GetAttackOptions", 4, attackArgs))) {
            void *args[] = {&id, unit, to};
            candidates.push_back(constructWith(attackClass, 3, args));
        }
    }
    // research: every tech of the player's tribe
    Object *data = call(state, "get_GameLogicData", 0, nullptr);
    int tribe = getField<int>(player, "tribe");
    void *tribeArg[] = {&tribe};
    Class *research = logicClass("", "ResearchCommand");
    for (void *tech : elements(invoke(methodOf(object_get_class(data), "GetAllTechForTribe", 1),
                                      data, tribeArg))) {
        int type = unboxInt(call(static_cast<Object *>(tech), "get_type", 0, nullptr));
        void *args[] = {&id, &type};
        candidates.push_back(constructWith(research, 2, args));
    }
    // diplomacy with each other player
    for (void *p : elements(getField<Object *>(state, "<PlayerStates>k__BackingField"))) {
        uint8_t other = getField<uint8_t>(static_cast<Object *>(p), "<Id>k__BackingField");
        if (other == id || other == 255) {
            continue;
        }
        void *args[] = {state, player, &other, &no};
        add(callStatic(utils, "GetDiplomacyCommandsForOpponent", 4, args));
    }
    // a city that levelled up waits for its reward
    struct Trigger {
        uint8_t player, opponent;
        int type;
        int x, y;
    } trigger{};
    void *triggerArgs[] = {&id, &trigger};
    bool pending = *static_cast<bool *>(object_unbox(call(state, "TryGetPendingCommandTrigger", 2, triggerArgs)));
    if (pending && trigger.type == 1) {
        int coords[2] = {trigger.x, trigger.y};
        void *tileArgs[] = {map, coords};
        Object *tile = callStatic(logicClass("", "MapDataExtensions"), "GetTile", 2, tileArgs);
        Object *improvement = getField<Object *>(tile, "improvement");
        int city = 1;
        void *cityArg[] = {&city};
        Object *cityData = invoke(methodOf(object_get_class(data), "GetImprovementData", 1), data, cityArg);
        Class *rewardClass = logicClass("", "CityRewardCommand");
        // the rewards for the levels around the city's: IsValid keeps the
        // ones that match it
        int level = getField<uint16_t>(improvement, "level");
        for (int l = level - 1; l <= level + 1; l++) {
            void *rewardArgs[] = {cityData, &l};
            for (void *reward : elements(callStatic(logicClass("", "ImprovementDataExtensions"),
                                                    "GetCityRewardsForLevel", 2, rewardArgs))) {
                void *args[] = {&id, reward, coords};
                candidates.push_back(constructWith(rewardClass, 3, args));
            }
        }
    }
    // a peace offer waits for an answer: accept or decline
    if (pending && trigger.type == 2) {
        Class *responseClass = logicClass("", "PeaceRequestResponseCommand");
        for (bool accepted : {true, false}) {
            uint8_t opponent = trigger.opponent;
            void *args[] = {&id, &opponent, &accepted};
            candidates.push_back(constructWith(responseClass, 3, args));
        }
    }
    void *endArg[] = {&id};
    candidates.push_back(constructWith(logicClass("", "EndTurnCommand"), 1, endArg));

    std::vector<Object *> legal;
    for (Object *command : candidates) {
        String *error = nullptr;
        void *args[] = {state, &error};
        if (*static_cast<bool *>(object_unbox(call(command, "IsValid", 2, args)))) {
            legal.push_back(command);
        }
    }
    return legal;
}

std::string execute(Object *actionManager, Object *command) {
    String *error = nullptr;
    void *args[] = {command, &error};
    bool ok = *static_cast<bool *>(object_unbox(call(actionManager, "ExecuteCommand", 2, args)));
    // the actions a command queues may not all have run yet
    for (int i = 0; i < 1000 && *static_cast<bool *>(object_unbox(call(actionManager, "get_IsProcessing", 0, nullptr))); i++) {
        call(actionManager, "ForceUpdate", 0, nullptr);
    }
    return ok ? "" : (error ? toString(error) : "rejected");
}

} // namespace oracle
