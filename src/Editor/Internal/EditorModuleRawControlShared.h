#pragma once

#include "Editor/Internal/EditorModuleDevelopDefaults.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawImageData.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Stack::Editor::RawControls {

inline bool GraphSliderRightClickWasConsumed() {
    return ImGuiExtras::IsGraphNodeControlScopeActive() &&
           ImGuiExtras::GetNodeControlState().lastRightClickConsumed;
}

inline std::string RawDisplayName(const EditorNodeGraph::RawSourcePayload& rawSource) {
    const std::string path = rawSource.sourcePath.empty()
        ? rawSource.metadata.sourcePath
        : rawSource.sourcePath;
    if (path.empty()) {
        return rawSource.label.empty() ? std::string("RAW") : rawSource.label;
    }
    try {
        const std::string filename = std::filesystem::path(path).filename().string();
        return filename.empty() ? path : filename;
    } catch (...) {
        return path;
    }
}

inline Raw::RawDevelopSettings BuildRawDecodeDefaultSettingsFromMetadata(const Raw::RawMetadata& metadata) {
    return Stack::Editor::DevelopDefaults::BuildRawDevelopSettingsFromMetadata(metadata);
}

inline std::array<float, 3> EffectiveWhiteBalance(
    const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings) {
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Manual) {
        return settings.manualWhiteBalance;
    }
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Neutral) {
        return { 1.0f, 1.0f, 1.0f };
    }
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Auto) {
        const float r = std::max(0.001f, metadata.daylightWhiteBalance[0]);
        const float g = std::max(0.001f, metadata.daylightWhiteBalance[1]);
        const float b = std::max(0.001f, metadata.daylightWhiteBalance[2]);
        return { r / g, 1.0f, b / g };
    }

    const float r = std::max(0.001f, metadata.cameraWhiteBalance[0]);
    const float g = std::max(0.001f, metadata.cameraWhiteBalance[1]);
    const float b = std::max(0.001f, metadata.cameraWhiteBalance[2]);
    return { r / g, 1.0f, b / g };
}

inline bool SameRawMosaicDenoiseSettings(
    const Raw::RawMosaicDenoiseSettings& a,
    const Raw::RawMosaicDenoiseSettings& b) {
    return a.enabled == b.enabled &&
        a.hotPixelSuppression == b.hotPixelSuppression &&
        a.hotPixelThreshold == b.hotPixelThreshold &&
        a.lumaStrength == b.lumaStrength &&
        a.chromaStrength == b.chromaStrength &&
        a.radius == b.radius &&
        a.edgeProtection == b.edgeProtection &&
        a.iterations == b.iterations;
}

inline bool SameRawDevelopSettings(
    const Raw::RawDevelopSettings& a,
    const Raw::RawDevelopSettings& b) {
    return a.exposureStops == b.exposureStops &&
        a.whiteBalanceMode == b.whiteBalanceMode &&
        a.manualWhiteBalance == b.manualWhiteBalance &&
        a.overrideBlackLevel == b.overrideBlackLevel &&
        a.blackLevelOverride == b.blackLevelOverride &&
        a.overrideWhiteLevel == b.overrideWhiteLevel &&
        a.whiteLevelOverride == b.whiteLevelOverride &&
        a.highlightMode == b.highlightMode &&
        a.highlightStrength == b.highlightStrength &&
        a.highlightThreshold == b.highlightThreshold &&
        a.demosaicMethod == b.demosaicMethod &&
        a.cameraTransformEnabled == b.cameraTransformEnabled &&
        a.cameraTransformSource == b.cameraTransformSource &&
        a.debugBypassCameraTransform == b.debugBypassCameraTransform &&
        a.debugTransposeCameraMatrix == b.debugTransposeCameraMatrix &&
        a.debugView == b.debugView &&
        a.rotationDegrees == b.rotationDegrees &&
        a.rotateToFitFrame == b.rotateToFitFrame &&
        a.flipHorizontally == b.flipHorizontally &&
        a.flipVertically == b.flipVertically &&
        a.falseColorSuppression == b.falseColorSuppression &&
        a.defringeStrength == b.defringeStrength &&
        a.highlightEdgeCleanup == b.highlightEdgeCleanup &&
        a.chromaRadius == b.chromaRadius &&
        a.preserveRealColor == b.preserveRealColor &&
        a.lateralRedCyan == b.lateralRedCyan &&
        a.lateralBlueYellow == b.lateralBlueYellow &&
        SameRawMosaicDenoiseSettings(a.mosaicDenoise, b.mosaicDenoise);
}

inline const EditorNodeGraph::Node* FindUpstreamRawSource(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const EditorNodeGraph::Link* input =
        graph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
    std::vector<int> visited;
    while (input) {
        if (std::find(visited.begin(), visited.end(), input->fromNodeId) != visited.end()) {
            return nullptr;
        }
        visited.push_back(input->fromNodeId);
        const EditorNodeGraph::Node* upstream = graph.FindNode(input->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        input = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}

} // namespace Stack::Editor::RawControls
