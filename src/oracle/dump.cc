#include "dump.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace oracle {
namespace {

using namespace il2cpp;

// Il2CppTypeEnum
enum : int {
    T_BOOLEAN = 0x02,
    T_CHAR = 0x03,
    T_I1 = 0x04,
    T_U1 = 0x05,
    T_I2 = 0x06,
    T_U2 = 0x07,
    T_I4 = 0x08,
    T_U4 = 0x09,
    T_I8 = 0x0a,
    T_U8 = 0x0b,
    T_R4 = 0x0c,
    T_R8 = 0x0d,
    T_STRING = 0x0e,
    T_VALUETYPE = 0x11,
    T_CLASS = 0x12,
    T_ARRAY = 0x14,
    T_GENERICINST = 0x15,
    T_OBJECT = 0x1c,
    T_SZARRAY = 0x1d,
};
constexpr int FIELD_STATIC = 0x10;
constexpr int FIELD_LITERAL = 0x40;
// the object header; a value type's field offsets include it
constexpr size_t HEADER = 0x10;
// an array's elements start after the header, bounds and length
constexpr size_t ARRAY_DATA = 0x20;

// Classes never dumped: the rules data (huge, and the same for every game)
// and delegates.
bool skipped(Class *klass) {
    for (Class *c = klass; c != nullptr; c = class_get_parent(c)) {
        std::string_view name = class_get_name(c);
        if (name == "GameLogicData" || name == "AvatarData" ||
            name == "MulticastDelegate") {
            return true;
        }
    }
    return false;
}

// The size of an array's elements. For value types the element type's own
// size: il2cpp_class_array_element_size gives 8 for the entry structs of
// generic dictionaries, which are bigger.
int elementSize(Class *arrayClass) {
    Class *element = class_get_element_class(arrayClass);
    if (!class_is_valuetype(element)) {
        return static_cast<int>(sizeof(void *));
    }
    uint32_t align = 0;
    return class_value_size(element, &align);
}

struct Field {
    std::string name;
    size_t offset;
    const Type *type;
};

class Dumper {
  public:
    std::string out;

    void object(Object *o) {
        if (o == nullptr) {
            out += "null";
            return;
        }
        Class *klass = object_get_class(o);
        std::string_view name = class_get_name(klass);
        if (name == "String") {
            string(toString(reinterpret_cast<String *>(o)));
            return;
        }
        if (skipped(klass) || !seen.insert(o).second) {
            out += "{\"$ref\":";
            string(name);
            out += '}';
            return;
        }
        if (name.ends_with("[]")) {
            array(o, klass, array_length(reinterpret_cast<Array *>(o)));
        } else if (name == "List`1") {
            Object *items = refField(o, klass, "_items");
            array(items, object_get_class(items), intField(o, klass, "_size"));
        } else if (name == "Dictionary`2" || name == "HashSet`1") {
            entries(o, klass, name == "Dictionary`2");
        } else if (class_is_valuetype(klass)) {
            // boxed
            value(class_get_type(klass), object_unbox(o));
        } else {
            out += "{\"$type\":";
            string(name);
            fields(klass, reinterpret_cast<char *>(o), 0);
            out += '}';
        }
    }

  private:
    std::unordered_set<Object *> seen;
    std::map<Class *, std::vector<Field>> fieldCache;
    std::map<Class *, std::map<long long, std::string>> enumCache;

    // instance fields, parents' first
    const std::vector<Field> &fieldsOf(Class *klass) {
        auto found = fieldCache.find(klass);
        if (found != fieldCache.end()) {
            return found->second;
        }
        std::vector<Field> list;
        std::vector<Class *> chain;
        for (Class *c = klass; c != nullptr; c = class_get_parent(c)) {
            chain.push_back(c);
        }
        for (auto c = chain.rbegin(); c != chain.rend(); ++c) {
            void *iter = nullptr;
            while (il2cpp::Field *f = class_get_fields(*c, &iter)) {
                if (field_get_flags(f) & (FIELD_STATIC | FIELD_LITERAL)) {
                    continue;
                }
                list.push_back(
                    {field_get_name(f), field_get_offset(f), field_get_type(f)});
            }
        }
        return fieldCache[klass] = std::move(list);
    }

    const Field &fieldNamed(Class *klass, std::string_view name) {
        for (const Field &f : fieldsOf(klass)) {
            if (f.name == name) {
                return f;
            }
        }
        throw std::runtime_error(std::string("no field ") + std::string(name));
    }
    Object *refField(Object *o, Class *klass, std::string_view name) {
        return *reinterpret_cast<Object **>(reinterpret_cast<char *>(o) +
                                            fieldNamed(klass, name).offset);
    }
    int intField(Object *o, Class *klass, std::string_view name) {
        return *reinterpret_cast<int *>(reinterpret_cast<char *>(o) +
                                        fieldNamed(klass, name).offset);
    }

    // base: the object, or a value type's data, whose field offsets still
    // count the header (shift HEADER)
    void fields(Class *klass, char *base, size_t shift) {
        for (const Field &f : fieldsOf(klass)) {
            std::string_view name = f.name;
            // "<Seed>k__BackingField" is the property Seed
            if (name.starts_with('<') && name.ends_with(">k__BackingField")) {
                name = name.substr(1, name.size() - 17);
            }
            out += ',';
            string(name);
            out += ':';
            value(f.type, base + f.offset - shift);
        }
    }

    // a List's or array's first n elements
    void array(Object *arr, Class *klass, int n) {
        if (arr == nullptr) {
            out += "null";
            return;
        }
        Class *element = class_get_element_class(klass);
        const Type *type = class_get_type(element);
        int size = elementSize(klass);
        char *data = reinterpret_cast<char *>(arr) + ARRAY_DATA;
        out += '[';
        for (int i = 0; i < n; i++) {
            if (i > 0) {
                out += ',';
            }
            value(type, data + static_cast<size_t>(i) * size);
        }
        out += ']';
    }

    // a Dictionary's [key, value] pairs or a HashSet's values, from its
    // entries array: used entries have next >= -1
    void entries(Object *o, Class *klass, bool dictionary) {
        Object *arr = refField(o, klass, "_entries");
        int count = intField(o, klass, "_count");
        out += '[';
        if (arr != nullptr) {
            Class *arrClass = object_get_class(arr);
            Class *entry = class_get_element_class(arrClass);
            int size = elementSize(arrClass);
            const Field &next = fieldNamed(entry, "next");
            const Field &key = fieldNamed(entry, dictionary ? "key" : "value");
            char *data = reinterpret_cast<char *>(arr) + ARRAY_DATA;
            bool first = true;
            for (int i = 0; i < count; i++) {
                char *e = data + static_cast<size_t>(i) * size;
                if (*reinterpret_cast<int *>(e + next.offset - HEADER) < -1) {
                    continue;
                }
                out += first ? "" : ",";
                first = false;
                if (dictionary) {
                    const Field &val = fieldNamed(entry, "value");
                    out += '[';
                    value(key.type, e + key.offset - HEADER);
                    out += ',';
                    value(val.type, e + val.offset - HEADER);
                    out += ']';
                } else {
                    value(key.type, e + key.offset - HEADER);
                }
            }
        }
        out += ']';
    }

    void value(const Type *type, void *at) {
        char *p = static_cast<char *>(at);
        switch (type_get_type(type)) {
        case T_BOOLEAN:
            out += *reinterpret_cast<bool *>(p) ? "true" : "false";
            return;
        case T_CHAR:
        case T_U2:
            number(*reinterpret_cast<uint16_t *>(p));
            return;
        case T_I1:
            number(*reinterpret_cast<int8_t *>(p));
            return;
        case T_U1:
            number(*reinterpret_cast<uint8_t *>(p));
            return;
        case T_I2:
            number(*reinterpret_cast<int16_t *>(p));
            return;
        case T_I4:
            number(*reinterpret_cast<int32_t *>(p));
            return;
        case T_U4:
            number(*reinterpret_cast<uint32_t *>(p));
            return;
        case T_I8:
            number(*reinterpret_cast<int64_t *>(p));
            return;
        case T_U8:
            number(static_cast<long long>(*reinterpret_cast<uint64_t *>(p)));
            return;
        case T_R4:
            real(*reinterpret_cast<float *>(p));
            return;
        case T_R8:
            real(*reinterpret_cast<double *>(p));
            return;
        case T_STRING:
        case T_CLASS:
        case T_OBJECT:
        case T_SZARRAY:
        case T_ARRAY:
            object(*reinterpret_cast<Object **>(p));
            return;
        case T_VALUETYPE:
        case T_GENERICINST: {
            Class *klass = class_from_type(type);
            if (!class_is_valuetype(klass)) {
                object(*reinterpret_cast<Object **>(p));
            } else if (class_is_enum(klass)) {
                enumValue(klass, p);
            } else {
                out += "{\"$type\":";
                string(class_get_name(klass));
                fields(klass, p, HEADER);
                out += '}';
            }
            return;
        }
        default:
            out += "null";
        }
    }

    void enumValue(Class *klass, char *p) {
        uint32_t align = 0;
        int size = class_value_size(klass, &align);
        long long v = readInt(p, size);
        auto [names, fresh] = enumCache.try_emplace(klass);
        if (fresh) {
            void *iter = nullptr;
            while (il2cpp::Field *f = class_get_fields(klass, &iter)) {
                if (!(field_get_flags(f) & FIELD_STATIC)) {
                    continue;
                }
                char buf[8] = {};
                field_static_get_value(f, buf);
                names->second.try_emplace(readInt(buf, size), field_get_name(f));
            }
        }
        auto name = names->second.find(v);
        if (name == names->second.end()) {
            number(v);
        } else {
            string(name->second);
        }
    }

    static long long readInt(const char *p, int size) {
        switch (size) {
        case 1:
            return *reinterpret_cast<const uint8_t *>(p);
        case 2:
            return *reinterpret_cast<const int16_t *>(p);
        case 8:
            return *reinterpret_cast<const int64_t *>(p);
        default:
            return *reinterpret_cast<const int32_t *>(p);
        }
    }

    void number(long long v) { out += std::to_string(v); }
    void real(double v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.9g", v);
        out += buf;
    }
    void string(std::string_view s) {
        out += '"';
        for (char c : s) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
        out += '"';
    }
};

} // namespace

std::string dump(Object *object) {
    Dumper d;
    d.object(object);
    return std::move(d.out);
}

} // namespace oracle
