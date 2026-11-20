#include "il2cpp.h"

#include <dlfcn.h>
#include <stdexcept>

namespace il2cpp {

#define IL2CPP_DEFINE(name, ret, params) ret(*name) params = nullptr;
IL2CPP_API(IL2CPP_DEFINE)
#undef IL2CPP_DEFINE

void load(const std::string &library, const std::string &dataDir) {
    void *handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        throw std::runtime_error(std::string("dlopen: ") + dlerror());
    }
#define IL2CPP_RESOLVE(name, ret, params)                                      \
    name = reinterpret_cast<ret(*) params>(dlsym(handle, "il2cpp_" #name));    \
    if (name == nullptr) {                                                     \
        throw std::runtime_error("missing export il2cpp_" #name);              \
    }
    IL2CPP_API(IL2CPP_RESOLVE)
#undef IL2CPP_RESOLVE
    set_data_dir(dataDir.c_str());
    if (!init("oracle")) {
        throw std::runtime_error("il2cpp_init failed");
    }
    thread_attach(domain_get());
}

const Image *image(const std::string &name) {
    size_t count = 0;
    const Assembly **assemblies = domain_get_assemblies(domain_get(), &count);
    for (size_t i = 0; i < count; i++) {
        const Image *img = assembly_get_image(assemblies[i]);
        if (name == image_get_name(img)) {
            return img;
        }
    }
    throw std::runtime_error("no assembly " + name);
}

Class *findClass(const Image *img, const std::string &ns,
                 const std::string &name) {
    Class *klass = class_from_name(img, ns.c_str(), name.c_str());
    if (klass == nullptr) {
        throw std::runtime_error("no class " + ns + "." + name);
    }
    return klass;
}

const Method *findMethod(Class *klass, const std::string &name, int params) {
    const Method *method =
        class_get_method_from_name(klass, name.c_str(), params);
    if (method == nullptr) {
        throw std::runtime_error(std::string("no method ") +
                                 class_get_name(klass) + "." + name);
    }
    return method;
}

std::string toString(String *s) {
    std::string out;
    if (s == nullptr) {
        return out;
    }
    const char16_t *chars = string_chars(s);
    int32_t n = string_length(s);
    for (int32_t i = 0; i < n; i++) {
        uint32_t c = chars[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {
            c = 0x10000 + ((c - 0xD800) << 10) + (chars[++i] - 0xDC00);
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

Object *invoke(const Method *method, void *instance, void **args) {
    Object *exception = nullptr;
    Object *result = runtime_invoke(method, instance, args, &exception);
    if (exception != nullptr) {
        char text[4096];
        format_exception(exception, text, sizeof text);
        throw std::runtime_error(std::string(method_get_name(method)) + ": " +
                                 text);
    }
    return result;
}

} // namespace il2cpp
