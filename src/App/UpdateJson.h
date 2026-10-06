#pragma once

#include "ThirdParty/json.hpp"

#include <type_traits>

namespace AppUpdate {

// Optional release fields can be null, and cached fields can have stale types.
template <typename T>
T ReadUpdateField(const nlohmann::json& object, const char* key, const T& fallback) {
    const auto field = object.find(key);
    if (field == object.end() || field->is_null()) {
        return fallback;
    }
    if constexpr (std::is_unsigned_v<T>) {
        if (!field->is_number_unsigned()) {
            return fallback;
        }
    }
    try {
        return field->template get<T>();
    } catch (const nlohmann::json::exception&) {
        return fallback;
    }
}

} // namespace AppUpdate
