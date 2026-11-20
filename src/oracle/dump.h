#pragma once

// JSON of any managed object, by reflection over its fields (docs/oracle.md).
// Objects become {"$type": class, field: value, ...}, enums their value's
// name, List, HashSet and arrays JSON arrays, Dictionary a list of
// [key, value] pairs. An object met a second time becomes {"$ref": class}.

#include "il2cpp.h"

#include <string>

namespace oracle {

std::string dump(il2cpp::Object *object);

} // namespace oracle
