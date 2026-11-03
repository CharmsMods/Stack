#pragma once

#include "Renderer/LocalToneGuide.h"
#include "Renderer/MaskRenderTypes.h"
#include "NodeMath/ToneCurveParameters.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace Stack::Renderer {

inline bool TonePayloadUsesLocalGuide(const nlohmann::json& payload) {
    return payload.is_object() && RawRecipe::IsLocalSceneToneActive(RawRecipe::ReadSceneTone(
        payload.value("luminanceTone", nlohmann::json::object())));
}

inline bool RecipeUsesLocalToneGuide(const RawRecipe::RawDevelopmentRecipe& recipe) {
    if (TonePayloadUsesLocalGuide(recipe.finishTone.layerJson)) return true;
    return std::any_of(recipe.toneGradients.begin(), recipe.toneGradients.end(), [](const auto& gradient) {
        return gradient.mask.enabled && TonePayloadUsesLocalGuide(gradient.curveJson);
    });
}

// The GPU owner has no graph dependency. Planning interprets only the authored
// paths that can invoke it; a stashed luminance curve in component mode costs
// nothing. RawDevelopment gradients and legacy integrated tone execute serially.
inline bool NodeUsesLocalToneGuide(const RenderGraphNode& node) {
    switch (node.kind) {
        case RenderGraphNodeKind::RawOperation: {
            if (!node.rawOperation.enabled || node.rawOperation.kind != RawRecipe::GraphOperationKind::ToneCurve) return false;
            return RawRecipe::IsLocalSceneToneActive(NodeMath::EffectiveToneCurveSceneTone(node.rawOperation.parameters));
        }
        case RenderGraphNodeKind::Layer:
            return node.layerJson.value("type", std::string{}) == "ToneCurve" &&
                node.layerJson.value("enabled", true) && node.layerJson.value("visible", true) &&
                TonePayloadUsesLocalGuide(node.layerJson);
        case RenderGraphNodeKind::RawDevelopment:
            return RecipeUsesLocalToneGuide(node.rawDevelopment.recipe);
        case RenderGraphNodeKind::RawDevelop:
            return node.rawDevelop.integratedToneEnabled &&
                node.rawDevelop.settings.debugView == Raw::RawDebugView::FinalOutput &&
                TonePayloadUsesLocalGuide(node.rawDevelop.integratedToneLayerJson);
        default: return false;
    }
}

inline std::uint64_t LocalToneGuideWorkingBytes(const RenderGraphSnapshot& graph, int width, int height) {
    // A conservative fallback for complete requests before per-endpoint extents
    // are resolved. Multiple nodes share this peak because no guide is retained.
    return std::any_of(graph.nodes.begin(), graph.nodes.end(), NodeUsesLocalToneGuide)
        ? LocalToneGuide::WorkingBytes(width, height) : 0;
}

} // namespace Stack::Renderer
