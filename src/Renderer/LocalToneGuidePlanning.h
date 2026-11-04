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

inline bool TonePayloadUsesColorGuide(const nlohmann::json& payload) {
    return payload.is_object() && RawRecipe::IsColorGuidedSceneToneActive(RawRecipe::ReadSceneTone(
        payload.value("luminanceTone", nlohmann::json::object())));
}

inline bool RecipeUsesLocalToneGuide(const RawRecipe::RawDevelopmentRecipe& recipe) {
    if (TonePayloadUsesLocalGuide(recipe.finishTone.layerJson)) return true;
    return std::any_of(recipe.toneGradients.begin(), recipe.toneGradients.end(), [](const auto& gradient) {
        return gradient.mask.enabled && TonePayloadUsesLocalGuide(gradient.curveJson);
    });
}

inline bool RecipeUsesColorToneGuide(const RawRecipe::RawDevelopmentRecipe& recipe) {
    if (TonePayloadUsesColorGuide(recipe.finishTone.layerJson)) return true;
    return std::any_of(recipe.toneGradients.begin(), recipe.toneGradients.end(), [](const auto& gradient) {
        return gradient.mask.enabled && TonePayloadUsesColorGuide(gradient.curveJson);
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

inline bool NodeUsesColorToneGuide(const RenderGraphNode& node) {
    if (!NodeUsesLocalToneGuide(node)) return false;
    switch (node.kind) {
        case RenderGraphNodeKind::RawOperation:
            return RawRecipe::IsColorGuidedSceneToneActive(NodeMath::EffectiveToneCurveSceneTone(node.rawOperation.parameters));
        case RenderGraphNodeKind::Layer: return TonePayloadUsesColorGuide(node.layerJson);
        case RenderGraphNodeKind::RawDevelopment: return RecipeUsesColorToneGuide(node.rawDevelopment.recipe);
        case RenderGraphNodeKind::RawDevelop: return TonePayloadUsesColorGuide(node.rawDevelop.integratedToneLayerJson);
        default: return false;
    }
}

inline std::uint64_t LocalToneGuideWorkingBytes(const RenderGraphNode& node, int width, int height) {
    if (width<=0 || height<=0 || !NodeUsesLocalToneGuide(node)) return 0;
    const bool color=NodeUsesColorToneGuide(node);
    return LocalToneGuide::WorkingBytes(width,height,color)+(color ? kMaximumPreparedSceneColorGuideBytes : 0);
}

inline std::uint64_t LocalToneGuideWorkingBytes(const RawRecipe::RawDevelopmentRecipe& recipe, int width, int height) {
    if (width<=0 || height<=0 || !RecipeUsesLocalToneGuide(recipe)) return 0;
    const bool color=RecipeUsesColorToneGuide(recipe);
    return LocalToneGuide::WorkingBytes(width,height,color)+(color ? kMaximumPreparedSceneColorGuideBytes : 0);
}

inline std::uint64_t LocalToneGuideWorkingBytes(const RenderGraphSnapshot& graph, int width, int height) {
    // A conservative fallback for complete requests before per-endpoint extents
    // are resolved. Multiple nodes share this peak because no guide is retained.
    std::uint64_t result=0;
    for (const auto& node:graph.nodes) result=std::max(result,LocalToneGuideWorkingBytes(node,width,height));
    return result;
}

} // namespace Stack::Renderer
