#pragma once
#include "ThirdParty/json.hpp"
#include <set>
#include <string>

namespace Stack::Project {
// The origin provides the sensor coordinate system even when its exposure
// is disabled. Other captures are required only by enabled recipe groups.
inline std::set<std::string> BracketCaptureIds(const nlohmann::json& recipe) {
    std::set<std::string> ids;
    if (!recipe.is_object() || !recipe.contains("groups") || !recipe["groups"].is_array()) return ids;
    const auto origin=recipe.value("origin",std::string{});
    if (!origin.empty()) ids.insert(origin);
    for (const auto& group:recipe.at("groups")) {
        if (!group.is_object() || !group.value("enabled",true) || !group.contains("frames") || !group["frames"].is_array()) continue;
        for (const auto& frame:group.at("frames"))
            if (frame.is_object() && frame.value("enabled",true) && frame.contains("id") && frame["id"].is_string())
                ids.insert(frame["id"].get<std::string>());
    }
    return ids;
}
}
