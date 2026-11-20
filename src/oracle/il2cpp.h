#pragma once

// The il2cpp runtime's exported API, loaded from the game's GameAssembly
// library with dlopen (docs/oracle.md). The handles are opaque: the oracle
// only goes through these calls, never the runtime's own structures.

#include <cstddef>
#include <cstdint>
#include <string>

namespace il2cpp {

struct Domain;
struct Assembly;
struct Image;
struct Class;
struct Method;
struct Field;
struct Type;
struct Object;
struct String;
struct Array;
struct Thread;
// a GC handle: pointer-sized since Unity 2021.2
using GCHandle = void *;

// name, return type, parameters
#define IL2CPP_API(X)                                                          \
    X(init, int, (const char *))                                               \
    X(set_data_dir, void, (const char *))                                      \
    X(domain_get, Domain *, ())                                                \
    X(domain_get_assemblies, const Assembly **, (const Domain *, size_t *))    \
    X(assembly_get_image, const Image *, (const Assembly *))                   \
    X(image_get_name, const char *, (const Image *))                           \
    X(image_get_class_count, size_t, (const Image *))                          \
    X(image_get_class, const Class *, (const Image *, size_t))                 \
    X(class_from_name, Class *, (const Image *, const char *, const char *))   \
    X(class_get_name, const char *, (const Class *))                           \
    X(class_get_namespace, const char *, (const Class *))                      \
    X(class_get_parent, Class *, (Class *))                                    \
    X(class_get_method_from_name, const Method *, (Class *, const char *, int)) \
    X(class_get_methods, const Method *, (Class *, void **))                   \
    X(class_get_fields, Field *, (Class *, void **))                           \
    X(class_get_field_from_name, Field *, (Class *, const char *))             \
    X(class_get_type, const Type *, (Class *))                                 \
    X(class_from_type, Class *, (const Type *))                                \
    X(class_is_enum, bool, (const Class *))                                    \
    X(class_is_valuetype, bool, (const Class *))                               \
    X(class_value_size, int32_t, (Class *, uint32_t *))                        \
    X(class_instance_size, int32_t, (Class *))                                 \
    X(class_get_element_class, Class *, (Class *))                             \
    X(class_array_element_size, int, (const Class *))                          \
    X(field_get_name, const char *, (Field *))                                 \
    X(field_get_offset, size_t, (Field *))                                     \
    X(field_get_type, const Type *, (Field *))                                 \
    X(field_get_flags, int, (Field *))                                         \
    X(field_static_get_value, void, (Field *, void *))                         \
    X(type_get_type, int, (const Type *))                                      \
    X(type_get_name, char *, (const Type *))                                   \
    X(method_get_name, const char *, (const Method *))                         \
    X(method_get_param_count, uint32_t, (const Method *))                      \
    X(method_get_param, const Type *, (const Method *, uint32_t))              \
    X(method_get_return_type, const Type *, (const Method *))                  \
    X(method_get_flags, uint32_t, (const Method *, uint32_t *))                \
    X(object_new, Object *, (const Class *))                                   \
    X(object_get_class, Class *, (Object *))                                   \
    X(object_unbox, void *, (Object *))                                        \
    X(value_box, Object *, (Class *, void *))                                  \
    X(runtime_invoke, Object *, (const Method *, void *, void **, Object **))  \
    X(runtime_object_init, void, (Object *))                                   \
    X(runtime_class_init, void, (Class *))                                     \
    X(string_new, String *, (const char *))                                    \
    X(string_chars, const char16_t *, (String *))                              \
    X(string_length, int32_t, (String *))                                      \
    X(array_length, uint32_t, (Array *))                                       \
    X(array_new, Array *, (Class *, uintptr_t))                                \
    X(thread_attach, Thread *, (Domain *))                                     \
    X(format_exception, void, (const Object *, char *, int))                   \
    X(gc_disable, void, ())                                                    \
    X(gc_enable, void, ())                                                     \
    X(gchandle_new, GCHandle, (Object *, bool))                                \
    X(gchandle_get_target, Object *, (GCHandle))                               \
    X(gchandle_free, void, (GCHandle))

#define IL2CPP_DECLARE(name, ret, params) extern ret(*name) params;
IL2CPP_API(IL2CPP_DECLARE)
#undef IL2CPP_DECLARE

// Loads the library and starts the runtime. dataDir holds
// Metadata/global-metadata.dat. Throws std::runtime_error on failure.
void load(const std::string &library, const std::string &dataDir);

// The image of an assembly by name ("GameLogicAssembly.dll"), or throws.
const Image *image(const std::string &name);
// A class by namespace and name (namespace "" for the global one), or throws.
Class *findClass(const Image *image, const std::string &ns,
                 const std::string &name);
// A method by name and parameter count (-1: any), or throws.
const Method *findMethod(Class *klass, const std::string &name, int params);

// A managed string as UTF-8.
std::string toString(String *s);

// Calls a method; throws std::runtime_error with the managed exception's
// text if it throws. instance is null for a static method; a value type's
// instance is a pointer to its data.
Object *invoke(const Method *method, void *instance, void **args);

} // namespace il2cpp
