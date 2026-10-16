#include "Editor/Timeline/TimelineParameterEdits.h"
#include <cmath>

namespace Stack::Timeline {
namespace {
bool ReadValue(const nlohmann::json& values, const std::string& key, float& value) {
    const nlohmann::json* found=nullptr;
    if (!key.empty() && key.front()=='/') {
        const nlohmann::json::json_pointer path(key);
        if (values.contains(path)) found=&values.at(path);
    } else {
        const auto field=values.find(key);
        if (field!=values.end()) found=&*field;
    }
    if (!found) return false;
    if (found->is_boolean()) value=found->get<bool>() ? 1.f : 0.f;
    else if (found->is_number()) value=found->get<float>();
    else return false;
    return std::isfinite(value);
}
}
ParameterEditAnimation PrepareParameterEditAnimation(const TimelineAnimationState& animation,
    const EditorNodeGraph::Node& node, const std::string& graphId, int frame,
    const nlohmann::json& before, const nlohmann::json& after) {
    ParameterEditAnimation result{animation,{}};
    for (const auto& parameter:CollectAnimatableParametersForNode(node,nullptr,graphId)) {
        if (parameter.storageKey.empty()) continue;
        float previous=parameter.defaultValue, current=parameter.defaultValue;
        const bool knownBefore=ReadValue(before,parameter.storageKey,previous);
        if (!ReadValue(after,parameter.storageKey,current) ||
            (knownBefore && std::abs(previous-current)<=.000001f)) continue;
        if (!UpdateExistingKeyframeValue(result.animation,parameter.target,frame,current))
            result.livePreviewTargets.push_back(parameter.target);
    }
    return result;
}
}
