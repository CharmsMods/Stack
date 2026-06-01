#include "Editor/EditorModule.h"

#include "NeuralDenoise/NeuralDenoiseManager.h"
#include "Raw/RawImageData.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <vector>

namespace {

std::string RawDisplayName(const EditorNodeGraph::RawSourcePayload& rawSource) {
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

std::array<float, 3> EffectiveWhiteBalance(const Raw::RawMetadata& metadata, const Raw::RawDevelopSettings& settings) {
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

bool SameRawMosaicDenoiseSettings(
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

bool SameRawDevelopSettings(
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

bool SameRawDetailFusionSettings(
    const Raw::RawDetailFusionSettings& a,
    const Raw::RawDetailFusionSettings& b) {
    return a.mode == b.mode &&
        a.debugView == b.debugView &&
        a.autoSafetyEnabled == b.autoSafetyEnabled &&
        a.overrideMinEv == b.overrideMinEv &&
        a.overrideMaxEv == b.overrideMaxEv &&
        a.overrideBaseEv == b.overrideBaseEv &&
        a.overrideNoiseProtection == b.overrideNoiseProtection &&
        a.overrideHighlightProtection == b.overrideHighlightProtection &&
        a.overrideShadowLiftLimit == b.overrideShadowLiftLimit &&
        a.overrideWellExposedTarget == b.overrideWellExposedTarget &&
        a.minEvBias == b.minEvBias &&
        a.maxEvBias == b.maxEvBias &&
        a.baseEvBias == b.baseEvBias &&
        a.noiseProtectionBias == b.noiseProtectionBias &&
        a.highlightProtectionBias == b.highlightProtectionBias &&
        a.shadowLiftLimitBias == b.shadowLiftLimitBias &&
        a.wellExposedTargetBias == b.wellExposedTargetBias &&
        a.minEv == b.minEv &&
        a.maxEv == b.maxEv &&
        a.baseEv == b.baseEv &&
        a.strength == b.strength &&
        a.sampleCount == b.sampleCount &&
        a.highlightProtection == b.highlightProtection &&
        a.shadowLiftLimit == b.shadowLiftLimit &&
        a.noiseProtection == b.noiseProtection &&
        a.detailWeight == b.detailWeight &&
        a.wellExposedTarget == b.wellExposedTarget &&
        a.smoothGradientProtection == b.smoothGradientProtection &&
        a.textureSensitivity == b.textureSensitivity &&
        a.skyBias == b.skyBias &&
        a.invertMask == b.invertMask &&
        a.maskBlackPoint == b.maskBlackPoint &&
        a.maskWhitePoint == b.maskWhitePoint &&
        a.maskGamma == b.maskGamma &&
        a.smoothnessRadius == b.smoothnessRadius &&
        a.smoothAreaRadius == b.smoothAreaRadius &&
        a.edgeAwareness == b.edgeAwareness &&
        a.haloGuard == b.haloGuard &&
        a.maskDebandDither == b.maskDebandDither &&
        a.manualBlend == b.manualBlend;
}

void ClampRawDetailFusionSettings(Raw::RawDetailFusionSettings& settings) {
    settings.minEv = std::clamp(settings.minEv, -8.0f, 8.0f);
    settings.maxEv = std::clamp(settings.maxEv, settings.minEv + 0.01f, 8.0f);
    settings.baseEv = std::clamp(settings.baseEv, -8.0f, 8.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -3.0f, 3.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -3.0f, 3.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -3.0f, 3.0f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -0.5f, 0.5f);
    settings.strength = std::clamp(settings.strength, 0.0f, 4.0f);
    settings.sampleCount = std::clamp(settings.sampleCount, 3, 33);
    settings.highlightProtection = std::clamp(settings.highlightProtection, 0.0f, 1.0f);
    settings.shadowLiftLimit = std::clamp(settings.shadowLiftLimit, 0.0f, 1.0f);
    settings.noiseProtection = std::clamp(settings.noiseProtection, 0.0f, 1.0f);
    settings.detailWeight = std::clamp(settings.detailWeight, 0.0f, 1.0f);
    settings.wellExposedTarget = std::clamp(settings.wellExposedTarget, 0.01f, 1.0f);
    settings.smoothGradientProtection = std::clamp(settings.smoothGradientProtection, 0.0f, 1.0f);
    settings.textureSensitivity = std::clamp(settings.textureSensitivity, 0.0f, 1.0f);
    settings.skyBias = std::clamp(settings.skyBias, 0.0f, 1.0f);
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.smoothnessRadius = std::clamp(settings.smoothnessRadius, 0, 16);
    settings.smoothAreaRadius = std::clamp(settings.smoothAreaRadius, 0, 32);
    settings.edgeAwareness = std::clamp(settings.edgeAwareness, 0.0f, 1.0f);
    settings.haloGuard = std::clamp(settings.haloGuard, 0.0f, 1.0f);
    settings.maskDebandDither = std::clamp(settings.maskDebandDither, 0.0f, 1.0f);
    settings.manualBlend = std::clamp(settings.manualBlend, 0.0f, 1.0f);
}

bool RenderAutoGainExposureControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix,
    bool includeStrength) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    changed |= ImGuiExtras::NodeCheckbox("Auto Safety", ("##" + prefix + "AutoSafety").c_str(), &settings.autoSafetyEnabled, controlWidth);
    if (settings.autoSafetyEnabled) {
        ImGui::TextWrapped("Auto Safety reads the connected image and derives safe EV bounds, midtone placement, highlight protection, and noise limits. Sliders below are offsets unless an override is enabled.");

        changed |= ImGuiExtras::NodeCheckbox("Override Min EV", ("##" + prefix + "OverrideMinEv").c_str(), &settings.overrideMinEv, controlWidth);
        if (settings.overrideMinEv) {
            changed |= ImGuiExtras::NodeSliderFloat("Min EV Override", ("##" + prefix + "MinEv").c_str(), &settings.minEv, -8.0f, 2.0f, "%.2f EV", controlWidth);
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Min EV Bias", ("##" + prefix + "MinEvBias").c_str(), &settings.minEvBias, -3.0f, 3.0f, "%+.2f EV", controlWidth);
        }

        changed |= ImGuiExtras::NodeCheckbox("Override Max EV", ("##" + prefix + "OverrideMaxEv").c_str(), &settings.overrideMaxEv, controlWidth);
        if (settings.overrideMaxEv) {
            changed |= ImGuiExtras::NodeSliderFloat("Max EV Override", ("##" + prefix + "MaxEv").c_str(), &settings.maxEv, -2.0f, 8.0f, "%.2f EV", controlWidth);
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Max EV Bias", ("##" + prefix + "MaxEvBias").c_str(), &settings.maxEvBias, -3.0f, 3.0f, "%+.2f EV", controlWidth);
        }

        changed |= ImGuiExtras::NodeCheckbox("Override Base EV", ("##" + prefix + "OverrideBaseEv").c_str(), &settings.overrideBaseEv, controlWidth);
        if (settings.overrideBaseEv) {
            changed |= ImGuiExtras::NodeSliderFloat("Base EV Override", ("##" + prefix + "BaseEv").c_str(), &settings.baseEv, -8.0f, 8.0f, "%.2f EV", controlWidth);
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Base EV Bias", ("##" + prefix + "BaseEvBias").c_str(), &settings.baseEvBias, -3.0f, 3.0f, "%+.2f EV", controlWidth);
        }
    } else {
        changed |= ImGuiExtras::NodeSliderFloat("Min EV", ("##" + prefix + "MinEv").c_str(), &settings.minEv, -8.0f, 2.0f, "%.2f EV", controlWidth);
        changed |= ImGuiExtras::NodeSliderFloat("Max EV", ("##" + prefix + "MaxEv").c_str(), &settings.maxEv, -2.0f, 8.0f, "%.2f EV", controlWidth);
        changed |= ImGuiExtras::NodeSliderFloat("Base EV", ("##" + prefix + "BaseEv").c_str(), &settings.baseEv, -8.0f, 8.0f, "%.2f EV", controlWidth);
    }
    if (includeStrength) {
        changed |= ImGuiExtras::NodeSliderFloat("Strength", ("##" + prefix + "Strength").c_str(), &settings.strength, 0.0f, 2.0f, "%.2f", controlWidth);
    }
    changed |= ImGuiExtras::NodeSliderInt("EV Samples", ("##" + prefix + "Samples").c_str(), &settings.sampleCount, 3, 33, "%d", controlWidth);
    if (settings.maxEv < settings.minEv + 0.01f) {
        settings.maxEv = settings.minEv + 0.01f;
        changed = true;
    }
    return changed;
}

bool RenderAutoGainAnalysisControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    if (settings.autoSafetyEnabled) {
        changed |= ImGuiExtras::NodeCheckbox("Override Highlight", ("##" + prefix + "OverrideHighlight").c_str(), &settings.overrideHighlightProtection, controlWidth);
        if (settings.overrideHighlightProtection) {
            changed |= ImGuiExtras::NodeSliderFloat("Highlight Protection", ("##" + prefix + "Highlight").c_str(), &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth);
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Highlight Bias", ("##" + prefix + "HighlightBias").c_str(), &settings.highlightProtectionBias, -1.0f, 1.0f, "%+.2f", controlWidth);
        }

        changed |= ImGuiExtras::NodeCheckbox("Override Shadow Limit", ("##" + prefix + "OverrideShadow").c_str(), &settings.overrideShadowLiftLimit, controlWidth);
        if (settings.overrideShadowLiftLimit) {
            changed |= ImGuiExtras::NodeSliderFloat("Shadow Lift Limit", ("##" + prefix + "ShadowLimit").c_str(), &settings.shadowLiftLimit, 0.0f, 1.0f, "%.2f", controlWidth);
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Shadow Lift Bias", ("##" + prefix + "ShadowBias").c_str(), &settings.shadowLiftLimitBias, -1.0f, 1.0f, "%+.2f", controlWidth);
        }

        changed |= ImGuiExtras::NodeCheckbox("Override Noise", ("##" + prefix + "OverrideNoise").c_str(), &settings.overrideNoiseProtection, controlWidth);
        if (settings.overrideNoiseProtection) {
            changed |= ImGuiExtras::NodeSliderFloat("Noise Protection", ("##" + prefix + "Noise").c_str(), &settings.noiseProtection, 0.0f, 1.0f, "%.2f", controlWidth);
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Noise Bias", ("##" + prefix + "NoiseBias").c_str(), &settings.noiseProtectionBias, -1.0f, 1.0f, "%+.2f", controlWidth);
        }

        changed |= ImGuiExtras::NodeCheckbox("Override Target", ("##" + prefix + "OverrideTarget").c_str(), &settings.overrideWellExposedTarget, controlWidth);
        if (settings.overrideWellExposedTarget) {
            changed |= ImGuiExtras::NodeSliderFloat("Well-Exposed Target", ("##" + prefix + "Target").c_str(), &settings.wellExposedTarget, 0.05f, 0.95f, "%.2f", controlWidth);
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Target Bias", ("##" + prefix + "TargetBias").c_str(), &settings.wellExposedTargetBias, -0.5f, 0.5f, "%+.2f", controlWidth);
        }
    } else {
        changed |= ImGuiExtras::NodeSliderFloat("Highlight Protection", ("##" + prefix + "Highlight").c_str(), &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth);
        changed |= ImGuiExtras::NodeSliderFloat("Shadow Lift Limit", ("##" + prefix + "ShadowLimit").c_str(), &settings.shadowLiftLimit, 0.0f, 1.0f, "%.2f", controlWidth);
        changed |= ImGuiExtras::NodeSliderFloat("Noise Protection", ("##" + prefix + "Noise").c_str(), &settings.noiseProtection, 0.0f, 1.0f, "%.2f", controlWidth);
        changed |= ImGuiExtras::NodeSliderFloat("Well-Exposed Target", ("##" + prefix + "Target").c_str(), &settings.wellExposedTarget, 0.05f, 0.95f, "%.2f", controlWidth);
    }
    changed |= ImGuiExtras::NodeSliderFloat("Detail Weight", ("##" + prefix + "DetailWeight").c_str(), &settings.detailWeight, 0.0f, 1.0f, "%.2f", controlWidth);
    return changed;
}

enum class RawDetailFusionWorkflow {
    MissingMask,
    Auto,
    Hybrid,
    ExternalMask
};

RawDetailFusionWorkflow ResolveRawDetailFusionWorkflow(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& fusionNode) {
    const EditorNodeGraph::Link* maskInput = graph.FindInputLink(fusionNode.id, EditorNodeGraph::kMaskInputSocketId);
    if (!maskInput) {
        return RawDetailFusionWorkflow::MissingMask;
    }

    const EditorNodeGraph::Node* source = graph.FindNode(maskInput->fromNodeId);
    if (!source) {
        return RawDetailFusionWorkflow::ExternalMask;
    }
    if (source->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask &&
        maskInput->fromSocketId == EditorNodeGraph::kMaskOutputSocketId) {
        return RawDetailFusionWorkflow::Auto;
    }

    std::vector<int> visited;
    const EditorNodeGraph::Link* current = maskInput;
    while (current) {
        if (std::find(visited.begin(), visited.end(), current->fromNodeId) != visited.end()) {
            break;
        }
        visited.push_back(current->fromNodeId);
        const EditorNodeGraph::Node* currentNode = graph.FindNode(current->fromNodeId);
        if (!currentNode) {
            break;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask &&
            current->fromSocketId == EditorNodeGraph::kMaskOutputSocketId) {
            return RawDetailFusionWorkflow::Hybrid;
        }
        if (currentNode->kind != EditorNodeGraph::NodeKind::MaskUtility ||
            current->fromSocketId != EditorNodeGraph::kMaskOutputSocketId) {
            break;
        }
        current = graph.FindInputLink(currentNode->id, EditorNodeGraph::kMaskInputSocketId);
    }

    return RawDetailFusionWorkflow::ExternalMask;
}

const EditorNodeGraph::Node* FindUpstreamRawSource(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
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

} // namespace

void EditorModule::RenderRawSourceControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawSource) {
        return;
    }

    Raw::RawMetadata& metadata = node.rawSource.metadata;

    ImGuiExtras::RichSectionLabel("FILE / CAMERA", 4.0f);
    const std::string rawPath = node.rawSource.sourcePath.empty()
        ? metadata.sourcePath
        : node.rawSource.sourcePath;
    const std::string displayName = RawDisplayName(node.rawSource);
    if (advanced) {
        ImGui::TextDisabled("%s", rawPath.c_str());
    } else {
        ImGui::TextDisabled("%s", displayName.c_str());
    }
    if (!metadata.cameraMake.empty() || !metadata.cameraModel.empty()) {
        ImGui::TextDisabled("%s %s", metadata.cameraMake.c_str(), metadata.cameraModel.c_str());
    }
    if (!metadata.dngUniqueCameraModel.empty()) {
        ImGui::TextDisabled("DNG camera: %s", metadata.dngUniqueCameraModel.c_str());
    }
    if (metadata.rawWidth > 0 && metadata.rawHeight > 0) {
        ImGui::TextDisabled("Raw: %d x %d", metadata.rawWidth, metadata.rawHeight);
    }
    if (metadata.visibleWidth > 0 && metadata.visibleHeight > 0) {
        ImGui::TextDisabled("Visible: %d x %d", metadata.visibleWidth, metadata.visibleHeight);
    }
    ImGui::TextDisabled("Crop: %d, %d  Orientation: %d", metadata.leftMargin, metadata.topMargin, metadata.orientation);
    ImGui::TextDisabled("RAW type: %s", Raw::RawPixelLayoutName(metadata.pixelLayout));
    ImGui::TextDisabled("Output: RAW sensor data and metadata");
    if (!metadata.dngTypeStatus.empty()) {
        ImGui::TextDisabled("%s", metadata.dngTypeStatus.c_str());
    }
    if (metadata.isDng) {
        ImGui::TextDisabled("DNG compression: %d  Photometric: %d",
            metadata.dngCompression,
            metadata.dngPhotometricInterpretation);
    }
    if (metadata.pixelLayout == Raw::RawPixelLayout::LinearRgb) {
        ImGui::TextDisabled("Demosaic: skipped  Bit depth: %d", metadata.bitDepth);
        ImGui::TextDisabled("Linear channels: %d  Sample: %s",
            metadata.linearChannels,
            Raw::RawSampleFormatName(metadata.linearSampleFormat));
    } else {
        ImGui::TextDisabled("CFA: %s  Bit depth: %d", Raw::CfaPatternName(metadata.cfaPattern), metadata.bitDepth);
        if (metadata.isDng && metadata.dngCfaRepeatPatternDim[0] > 0) {
            ImGui::TextDisabled("DNG CFA: repeat %dx%d  planes [%d %d %d]  pattern [%d %d %d %d]",
                metadata.dngCfaRepeatPatternDim[0],
                metadata.dngCfaRepeatPatternDim[1],
                metadata.dngCfaPlaneColor[0],
                metadata.dngCfaPlaneColor[1],
                metadata.dngCfaPlaneColor[2],
                metadata.dngCfaPattern[0],
                metadata.dngCfaPattern[1],
                metadata.dngCfaPattern[2],
                metadata.dngCfaPattern[3]);
        }
    }
    if (!metadata.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.38f, 0.34f, 1.0f));
        ImGui::TextWrapped("%s", metadata.error.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Status: ready");
    }
    ImGui::TextDisabled("Denoise starts in RAW Develop; RAW Source is not RGB image data.");

    if (advanced) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextWrapped("Connect this RAW source to a RAW Develop node to produce unclamped scene-linear RGB.");
        if (ImGui::Button("Add RAW Develop", ImVec2(controlWidth, 0.0f))) {
            const EditorNodeGraph::Vec2 position{ node.position.x + 250.0f, node.position.y };
            const int sourceNodeId = node.id;
            AddRawDevelopNodeAt(position);
            const int developNodeId = m_NodeGraph.GetSelectedNodeId();
            std::string errorMessage;
            ConnectGraphSockets(
                sourceNodeId,
                EditorNodeGraph::kRawOutputSocketId,
                developNodeId,
                EditorNodeGraph::kRawInputSocketId,
                &errorMessage);
            MarkRenderDirty(sourceNodeId);
        }
    }
}

void EditorModule::RenderRawNeuralDenoiseControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
        return;
    }

    using namespace NeuralDenoise;
    NeuralDenoiseManager& manager = NeuralDenoiseManager::Instance();
    NeuralDenoiseSettings& settings = node.rawNeuralDenoise.settings;
    const nlohmann::json before = SerializeSettings(settings);
    const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSource(m_NodeGraph, node);
    const Raw::RawMetadata* metadata = rawSourceNode ? &rawSourceNode->rawSource.metadata : nullptr;
    const bool hasMosaicedCfa = metadata &&
        metadata->pixelLayout == Raw::RawPixelLayout::MosaicBayer &&
        metadata->mosaiced &&
        metadata->cfaPattern != Raw::CfaPattern::Unknown;

    std::vector<const NeuralDenoiseModelInfo*> models = manager.ModelsOfType(ModelType::RawBayerPacked4Ch);
    if (settings.selectedModelId.empty() && !models.empty()) {
        settings.selectedModelId = models.front()->id;
    }
    const NeuralDenoiseModelInfo* selected = manager.FindModel(settings.selectedModelId);
    const ModelAvailability availability = manager.GetAvailability(selected);

    ImGuiExtras::RichSectionLabel("RAW/CFA NEURAL DENOISE", 4.0f);
    ImGuiExtras::NodeCheckbox("Enable", "##RawNeuralEnabled", &settings.enabled, controlWidth);
    if (!rawSourceNode) {
        ImGui::TextWrapped("RAW neural denoise unavailable: connect a RAW source before this node.");
    } else if (!hasMosaicedCfa) {
        ImGui::TextWrapped("RAW neural denoise unavailable: current input is not mosaiced CFA RAW.");
    } else {
        ImGui::TextDisabled("Input: mosaiced CFA RAW, %s", Raw::CfaPatternName(metadata->cfaPattern));
    }
    ImGui::TextDisabled("Execution: bypass / pass-through until real inference is implemented.");
    ImGui::TextDisabled("Status: %s", availability.status.c_str());

    if (advanced) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("MODEL", 4.0f);
        const std::string currentLabel = selected ? selected->displayName : std::string("No model selected");
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::BeginCombo("Selected Model", currentLabel.c_str())) {
            for (const NeuralDenoiseModelInfo* model : models) {
                const bool isSelected = model && model->id == settings.selectedModelId;
                if (model && ImGui::Selectable(model->displayName.c_str(), isSelected)) {
                    settings.selectedModelId = model->id;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            if (models.empty()) {
                ImGui::TextDisabled("No RAW Bayer neural models in manifest.");
            }
            ImGui::EndCombo();
        }

        const char* runtimeLabels[] = { "Auto", "CUDA", "CPU placeholder", "DirectML future", "TensorRT future" };
        int runtime = static_cast<int>(settings.runtimePreference);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Runtime / Provider", &runtime, runtimeLabels, 5)) {
            settings.runtimePreference = static_cast<RuntimePreference>(std::clamp(runtime, 0, 4));
        }
        const char* qualityLabels[] = { "Quality", "Balanced", "Fast" };
        int quality = static_cast<int>(settings.qualityMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Quality Mode", &quality, qualityLabels, 3)) {
            settings.qualityMode = static_cast<QualityMode>(std::clamp(quality, 0, 2));
        }
        if (selected) {
            ImGui::TextDisabled("Model type: %s", ModelTypeLabel(selected->type));
            ImGui::TextDisabled("Packed layout: Bayer 2x2 -> 4 channels");
            ImGui::TextDisabled("File: %s", selected->relativeFile.c_str());
            if (!selected->license.empty()) {
                ImGui::TextWrapped("License: %s", selected->license.c_str());
            }
        }
        for (const std::string& warning : availability.warnings) {
            ImGui::TextWrapped("%s", warning.c_str());
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("BLEND", 4.0f);
        ImGuiExtras::NodeSliderFloat("Overall Strength", "##RawNeuralStrength", &settings.strength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Detail Preservation", "##RawNeuralDetail", &settings.detailPreservation, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Shadows Strength", "##RawNeuralShadows", &settings.shadowsStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Highlight Protection", "##RawNeuralHighlights", &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Difference Amount", "##RawNeuralDifference", &settings.differenceAmount, 0.0f, 2.0f, "%.2f", controlWidth);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("NOISE TARGETING", 4.0f);
        ImGuiExtras::NodeSliderFloat("Chroma Noise", "##RawNeuralChroma", &settings.chromaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Luma Noise", "##RawNeuralLuma", &settings.lumaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Fine Grain", "##RawNeuralFine", &settings.fineGrainStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Blotch / Splotch", "##RawNeuralBlotch", &settings.blotchStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeCheckbox("Hot / Dead Pixel Cleanup", "##RawNeuralHotDead", &settings.hotDeadPixelCleanup, controlWidth);
        ImGuiExtras::NodeCheckbox("Shadow-Biased Denoise", "##RawNeuralShadowBias", &settings.shadowBiasedDenoise, controlWidth);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("RAW / DNG", 4.0f);
        const char* cfaLabels[] = { "From metadata", "RGGB", "BGGR", "GRBG", "GBRG" };
        int cfa = static_cast<int>(settings.cfaOverride);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("CFA Override", &cfa, cfaLabels, 5)) {
            settings.cfaOverride = static_cast<CfaOverride>(std::clamp(cfa, 0, 4));
        }
        if (metadata) {
            ImGui::TextDisabled("Metadata CFA: %s", Raw::CfaPatternName(metadata->cfaPattern));
            ImGui::TextDisabled("Black / white: %.1f / %.1f", metadata->blackLevel, metadata->whiteLevel);
            ImGui::TextDisabled("ISO metadata: not exposed by current RAW pipeline");
        }
        ImGuiExtras::NodeCheckbox("Override Black", "##RawNeuralOverrideBlack", &settings.overrideBlackLevel, controlWidth);
        if (settings.overrideBlackLevel) {
            ImGuiExtras::NodeInputFloat("Black Level", "##RawNeuralBlack", &settings.blackLevel, 1.0f, 16.0f, "%.1f", controlWidth);
        }
        ImGuiExtras::NodeCheckbox("Override White", "##RawNeuralOverrideWhite", &settings.overrideWhiteLevel, controlWidth);
        if (settings.overrideWhiteLevel) {
            ImGuiExtras::NodeInputFloat("White Level", "##RawNeuralWhite", &settings.whiteLevel, 16.0f, 256.0f, "%.1f", controlWidth);
        }
        const char* noiseLabels[] = { "Auto from metadata", "Manual" };
        int noise = static_cast<int>(settings.noiseEstimateMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Noise Estimate", &noise, noiseLabels, 2)) {
            settings.noiseEstimateMode = static_cast<NoiseEstimateMode>(std::clamp(noise, 0, 1));
        }
        if (settings.noiseEstimateMode == NoiseEstimateMode::Manual) {
            ImGuiExtras::NodeSliderFloat("Manual Noise", "##RawNeuralManualNoise", &settings.manualNoiseEstimate, 0.0f, 1.0f, "%.3f", controlWidth);
        }
        const char* wbLabels[] = { "Before white balance", "After white balance" };
        int wbStage = static_cast<int>(settings.rawWhiteBalanceStage);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("WB Stage", &wbStage, wbLabels, 2)) {
            settings.rawWhiteBalanceStage = static_cast<RawWhiteBalanceStage>(std::clamp(wbStage, 0, 1));
        }
        const char* outputLabels[] = { "Denoised CFA", "Continue to demosaic" };
        int outputMode = static_cast<int>(settings.rawOutputMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Output Mode", &outputMode, outputLabels, 2)) {
            settings.rawOutputMode = static_cast<RawOutputMode>(std::clamp(outputMode, 0, 1));
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("PREVIEW / TILING", 4.0f);
        const char* previewLabels[] = { "Denoised", "Original", "Difference", "Split view", "Chroma-only difference", "Luma-only difference" };
        int preview = static_cast<int>(settings.previewMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Preview", &preview, previewLabels, 6)) {
            settings.previewMode = static_cast<PreviewMode>(std::clamp(preview, 0, 5));
        }
        ImGuiExtras::NodeSliderInt("Tile Size", "##RawNeuralTile", &settings.tilePlan.tileSize, 128, 2048, "%d px", controlWidth);
        ImGuiExtras::NodeSliderInt("Overlap", "##RawNeuralOverlap", &settings.tilePlan.overlap, 0, 512, "%d px", controlWidth);
        ImGuiExtras::NodeCheckbox("Feather Merge", "##RawNeuralFeather", &settings.tilePlan.featherMerge, controlWidth);
        settings.tilePlan.tileSize = std::clamp(settings.tilePlan.tileSize, 64, 4096);
        settings.tilePlan.overlap = std::clamp(settings.tilePlan.overlap, 0, settings.tilePlan.tileSize / 2);
    }

    if (before.dump() != SerializeSettings(settings).dump()) {
        MarkRenderDirty(node.id);
    }
}

void EditorModule::RenderRawDevelopControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return;
    }

    const EditorNodeGraph::Link* rawInput = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
    const EditorNodeGraph::Node* rawSourceNode = rawInput ? m_NodeGraph.FindNode(rawInput->fromNodeId) : nullptr;
    const Raw::RawMetadata emptyMetadata;
    const Raw::RawMetadata& metadata =
        (rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource)
            ? rawSourceNode->rawSource.metadata
            : emptyMetadata;
    Raw::RawDevelopSettings& settings = node.rawDevelop.settings;
    const Raw::RawDevelopSettings settingsBefore = settings;
    bool changed = false;

    ImGuiExtras::RichSectionLabel("RAW DEVELOP", 4.0f);
    if (rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
        ImGui::TextDisabled("%s", RawDisplayName(rawSourceNode->rawSource).c_str());
        ImGui::TextDisabled("Output: unclamped scene-linear float RGB");
    } else {
        ImGui::TextWrapped("Connect a RAW Source node to develop sensor data.");
        ImGui::BeginDisabled();
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW DECODE / SENSOR NORMALIZE", 4.0f);
    changed |= ImGuiExtras::NodeCheckbox("Override Black", "##RawOverrideBlack", &settings.overrideBlackLevel, controlWidth);
    if (settings.overrideBlackLevel) {
        changed |= ImGuiExtras::NodeInputFloat("Black Level", "##RawBlackLevel", &settings.blackLevelOverride, 1.0f, 16.0f, "%.1f", controlWidth);
    }
    changed |= ImGuiExtras::NodeCheckbox("Override White", "##RawOverrideWhite", &settings.overrideWhiteLevel, controlWidth);
    if (settings.overrideWhiteLevel) {
        changed |= ImGuiExtras::NodeInputFloat("White Level", "##RawWhiteLevel", &settings.whiteLevelOverride, 16.0f, 256.0f, "%.1f", controlWidth);
    }
    const float effectiveBlack = settings.overrideBlackLevel ? settings.blackLevelOverride : metadata.blackLevel;
    const float requestedWhite = settings.overrideWhiteLevel ? settings.whiteLevelOverride : metadata.whiteLevel;
    const float effectiveWhite = std::max(effectiveBlack + 1.0f, requestedWhite);
    const std::array<float, 3> effectiveWb = EffectiveWhiteBalance(metadata, settings);
    ImGui::TextDisabled("Effective black/white: %.1f / %.1f", effectiveBlack, effectiveWhite);
    if (!metadata.blackLevelSource.empty() || !metadata.whiteLevelSource.empty()) {
        ImGui::TextDisabled("Sources: black %s, white %s",
            metadata.blackLevelSource.empty() ? "unknown" : metadata.blackLevelSource.c_str(),
            metadata.whiteLevelSource.empty() ? "unknown" : metadata.whiteLevelSource.c_str());
    }
    ImGui::TextDisabled("Channel black RGBG: %.1f %.1f %.1f %.1f",
        metadata.perChannelBlack[0],
        metadata.perChannelBlack[1],
        metadata.perChannelBlack[2],
        metadata.perChannelBlack[3]);
    if (metadata.isDng) {
        ImGui::TextDisabled("DNG black repeat %dx%d: %.2f %.2f %.2f %.2f",
            metadata.dngBlackLevelRepeatDim[0],
            metadata.dngBlackLevelRepeatDim[1],
            metadata.dngBlackLevelPattern[0],
            metadata.dngBlackLevelPattern[1],
            metadata.dngBlackLevelPattern[2],
            metadata.dngBlackLevelPattern[3]);
        ImGui::TextDisabled("DNG GainMaps: %d  Unsupported opcodes: %d",
            metadata.dngGainMapCount,
            metadata.dngUnsupportedOpcodeCount);
    }
    ImGui::TextDisabled("Raw min/max: %.0f / %.0f  Clip: %.4f%%",
        metadata.rawMinimum,
        metadata.rawMaximum,
        metadata.defaultWhiteClipPercent);
    ImGui::TextDisabled("Sensor clipping is measured before display tone mapping.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW WHITE BALANCE", 4.0f);
    const char* wbLabels[] = { "Camera WB", "Auto WB", "Neutral", "Manual" };
    int wbMode = static_cast<int>(settings.whiteBalanceMode);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("White Balance", &wbMode, wbLabels, 4)) {
        settings.whiteBalanceMode = static_cast<Raw::WhiteBalanceMode>(std::clamp(wbMode, 0, 3));
        changed = true;
    }
    changed |= ImGuiExtras::NodeSliderFloat("Red Mult", "##RawWbR", &settings.manualWhiteBalance[0], 0.1f, 8.0f, "%.3f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Green Mult", "##RawWbG", &settings.manualWhiteBalance[1], 0.1f, 8.0f, "%.3f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Blue Mult", "##RawWbB", &settings.manualWhiteBalance[2], 0.1f, 8.0f, "%.3f", controlWidth);
    ImGui::TextDisabled("LibRaw cam_mul RGBG: %.3f %.3f %.3f %.3f",
        metadata.cameraWhiteBalance[0],
        metadata.cameraWhiteBalance[1],
        metadata.cameraWhiteBalance[2],
        metadata.cameraWhiteBalance[3]);
    ImGui::TextDisabled("Effective WB RGB: %.3f %.3f %.3f", effectiveWb[0], effectiveWb[1], effectiveWb[2]);
    if (metadata.hasDngAsShotNeutral) {
        ImGui::TextDisabled("DNG AsShotNeutral RGB: %.4f %.4f %.4f",
            metadata.dngAsShotNeutral[0],
            metadata.dngAsShotNeutral[1],
            metadata.dngAsShotNeutral[2]);
    }
    if (metadata.hasDngAnalogBalance) {
        ImGui::TextDisabled("DNG AnalogBalance RGB: %.4f %.4f %.4f",
            metadata.dngAnalogBalance[0],
            metadata.dngAnalogBalance[1],
            metadata.dngAnalogBalance[2]);
    }
    if (!metadata.whiteBalanceSource.empty()) {
        ImGui::TextDisabled("WB source: %s", metadata.whiteBalanceSource.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW EXPOSURE BASELINE", 4.0f);
    changed |= ImGuiExtras::NodeSliderFloat("RAW Baseline Exposure", "##RawExposure", &settings.exposureStops, -5.0f, 5.0f, "%.2f stops", controlWidth);
    ImGui::TextDisabled("Global midtone placement only. Dynamic range belongs in scene tone nodes and View Transform.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW ORIENTATION / ROTATION", 4.0f);
    const char* rotationLabels[] = { "0 Degrees", "90 Degrees CW", "180 Degrees", "270 Degrees CW" };
    int rotationIdx = settings.rotationDegrees / 90;
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Manual Rotation", &rotationIdx, rotationLabels, 4)) {
        settings.rotationDegrees = std::clamp(rotationIdx, 0, 3) * 90;
        changed = true;
    }
    changed |= ImGuiExtras::NodeCheckbox("Stretch To Fit Frame", "##RawRotateToFitFrame", &settings.rotateToFitFrame, controlWidth);
    changed |= ImGuiExtras::NodeCheckbox("Flip Horizontally", "##RawFlipHorizontally", &settings.flipHorizontally, controlWidth);
    changed |= ImGuiExtras::NodeCheckbox("Flip Vertically", "##RawFlipVertically", &settings.flipVertically, controlWidth);
    ImGui::TextDisabled("EXIF metadata orientation: %d", metadata.orientation);
    ImGui::TextDisabled(settings.rotateToFitFrame
        ? "Keeps the current frame size and stretches the rotated image to fit inside it."
        : "Rotates the full canvas dimensions so width and height follow the final orientation.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("HIGHLIGHT RECONSTRUCTION", 4.0f);
    const bool demosaicEnabled = metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer;
    const char* highlightLabels[] = { "Off", "Clip / Neutral", "Luminance", "Color Reconstruction" };
    int highlightMode = static_cast<int>(settings.highlightMode);
    ImGui::BeginDisabled(!demosaicEnabled);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Mode", &highlightMode, highlightLabels, 4)) {
        settings.highlightMode = static_cast<Raw::HighlightReconstructionMode>(std::clamp(highlightMode, 0, 3));
        changed = true;
    }
    changed |= ImGuiExtras::NodeSliderFloat("Strength", "##RawHighlightStrength", &settings.highlightStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Clip Threshold", "##RawHighlightThreshold", &settings.highlightThreshold, 0.8f, 1.0f, "%.3f", controlWidth);
    ImGui::EndDisabled();
    ImGui::TextDisabled(demosaicEnabled
        ? "Repairs clipped RAW channel artifacts; tone nodes handle normal dynamic range."
        : "Linear RGB DNG: RAW channel reconstruction is skipped.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("DEMOSAIC", 4.0f);
    const char* demosaicLabels[] = { "Fast / Bilinear", "Quality / Edge-Aware" };
    int demosaic = static_cast<int>(settings.demosaicMethod);
    ImGui::BeginDisabled(!demosaicEnabled);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Method", &demosaic, demosaicLabels, 2)) {
        settings.demosaicMethod = static_cast<Raw::DemosaicMethod>(std::clamp(demosaic, 0, 1));
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(demosaicEnabled ? "Quality mode reduces colored zippering around high-contrast RAW edges." : "Linear RGB DNG: demosaic skipped.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("COLOR EDGE CLEANUP", 4.0f);
    ImGui::BeginDisabled(!demosaicEnabled);
    changed |= ImGuiExtras::NodeSliderFloat("False Color Suppression", "##RawFalseColorSuppression", &settings.falseColorSuppression, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Defringe Strength", "##RawDefringeStrength", &settings.defringeStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Highlight Edge Cleanup", "##RawHighlightEdgeCleanup", &settings.highlightEdgeCleanup, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderInt("Chroma Radius", "##RawChromaRadius", &settings.chromaRadius, 1, 3, "%d px", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Preserve Real Color", "##RawPreserveRealColor", &settings.preserveRealColor, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Red / Cyan CA", "##RawLateralRedCyan", &settings.lateralRedCyan, -3.0f, 3.0f, "%.2f px", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Blue / Yellow CA", "##RawLateralBlueYellow", &settings.lateralBlueYellow, -3.0f, 3.0f, "%.2f px", controlWidth);
    settings.falseColorSuppression = std::clamp(settings.falseColorSuppression, 0.0f, 1.0f);
    settings.defringeStrength = std::clamp(settings.defringeStrength, 0.0f, 1.0f);
    settings.highlightEdgeCleanup = std::clamp(settings.highlightEdgeCleanup, 0.0f, 1.0f);
    settings.chromaRadius = std::clamp(settings.chromaRadius, 1, 3);
    settings.preserveRealColor = std::clamp(settings.preserveRealColor, 0.0f, 1.0f);
    settings.lateralRedCyan = std::clamp(settings.lateralRedCyan, -3.0f, 3.0f);
    settings.lateralBlueYellow = std::clamp(settings.lateralBlueYellow, -3.0f, 3.0f);
    ImGui::EndDisabled();
    ImGui::TextDisabled(demosaicEnabled
        ? "Chroma-only cleanup preserves luminance detail while reducing colored edge artifacts."
        : "Linear RGB DNG: RAW color-edge cleanup is skipped.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW MOSAIC DENOISE, BEFORE DEMOSAIC", 4.0f);
    ImGui::BeginDisabled(!demosaicEnabled);
    changed |= ImGuiExtras::NodeCheckbox("Enable", "##RawMosaicDenoiseEnabled", &settings.mosaicDenoise.enabled, controlWidth);
    changed |= ImGuiExtras::NodeCheckbox("Hot Pixel Suppression", "##RawMosaicHotPixels", &settings.mosaicDenoise.hotPixelSuppression, controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Hot Pixel Threshold", "##RawMosaicHotThreshold", &settings.mosaicDenoise.hotPixelThreshold, 0.005f, 0.5f, "%.3f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Luminance Strength", "##RawMosaicLumaStrength", &settings.mosaicDenoise.lumaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Chroma Strength", "##RawMosaicChromaStrength", &settings.mosaicDenoise.chromaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderInt("Radius", "##RawMosaicRadius", &settings.mosaicDenoise.radius, 1, 4, "%d CFA steps", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Edge Protection", "##RawMosaicEdgeProtection", &settings.mosaicDenoise.edgeProtection, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderInt("Iterations", "##RawMosaicIterations", &settings.mosaicDenoise.iterations, 1, 2, "%d", controlWidth);
    settings.mosaicDenoise.hotPixelThreshold = std::clamp(settings.mosaicDenoise.hotPixelThreshold, 0.001f, 1.0f);
    settings.mosaicDenoise.lumaStrength = std::clamp(settings.mosaicDenoise.lumaStrength, 0.0f, 1.0f);
    settings.mosaicDenoise.chromaStrength = std::clamp(settings.mosaicDenoise.chromaStrength, 0.0f, 1.0f);
    settings.mosaicDenoise.radius = std::clamp(settings.mosaicDenoise.radius, 1, 4);
    settings.mosaicDenoise.edgeProtection = std::clamp(settings.mosaicDenoise.edgeProtection, 0.0f, 1.0f);
    settings.mosaicDenoise.iterations = std::clamp(settings.mosaicDenoise.iterations, 1, 2);
    ImGui::EndDisabled();
    ImGui::TextDisabled(demosaicEnabled
        ? "Before demosaic: sensor/CFA cleanup using same-color mosaic samples."
        : "Linear RGB DNG: pre-demosaic sensor cleanup is skipped.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("CAMERA COLOR TRANSFORM", 4.0f);
    changed |= ImGuiExtras::NodeCheckbox("Camera Transform", "##RawCameraTransform", &settings.cameraTransformEnabled, controlWidth);
    const char* transformLabels[] = {
        "LibRaw rgb_cam",
        "DNG Auto",
        "DNG ForwardMatrix 1",
        "DNG ForwardMatrix 2",
        "DNG ColorMatrix inverse"
    };
    int transformSource = static_cast<int>(settings.cameraTransformSource);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Matrix Source", &transformSource, transformLabels, 5)) {
        settings.cameraTransformSource = static_cast<Raw::RawCameraTransformSource>(std::clamp(transformSource, 0, 4));
        changed = true;
    }
    changed |= ImGuiExtras::NodeCheckbox("Bypass Transform", "##RawBypassTransform", &settings.debugBypassCameraTransform, controlWidth);
    changed |= ImGuiExtras::NodeCheckbox("Matrix Transpose Debug", "##RawMatrixTranspose", &settings.debugTransposeCameraMatrix, controlWidth);
    ImGui::TextDisabled(metadata.hasCameraMatrix ? "LibRaw rgb_cam: available" : "LibRaw rgb_cam: unavailable");
    if (metadata.isDng) {
        ImGui::TextDisabled("DNG illuminants: %d / %d", metadata.dngIlluminant1, metadata.dngIlluminant2);
        ImGui::TextDisabled("DNG matrices: CM1 %s  CM2 %s  FM1 %s  FM2 %s",
            metadata.hasDngColorMatrix1 ? "yes" : "no",
            metadata.hasDngColorMatrix2 ? "yes" : "no",
            metadata.hasDngForwardMatrix1 ? "yes" : "no",
            metadata.hasDngForwardMatrix2 ? "yes" : "no");
        ImGui::TextDisabled("DNG calibration: CC1 %s  CC2 %s  Analog %s",
            metadata.hasDngCameraCalibration1 ? "yes" : "no",
            metadata.hasDngCameraCalibration2 ? "yes" : "no",
            metadata.hasDngAnalogBalance ? "yes" : "no");
    }
    ImGui::TextDisabled("Active source: %s", Raw::RawCameraTransformSourceName(settings.cameraTransformSource));
    if (!metadata.cameraMatrixSource.empty()) {
        ImGui::TextDisabled("Metadata preferred: %s", metadata.cameraMatrixSource.c_str());
    }
    ImGui::TextDisabled("Approximate linear sRGB; transpose toggle is diagnostic only.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("DEBUG", 4.0f);
    const char* debugLabels[] = {
        "Final Output",
        "Normalized Mosaic",
        "CFA False Color",
        "Demosaiced Camera RGB",
        "White Balanced RGB",
        "Camera Transformed RGB",
        "Clipped RAW Channels",
        "Pre-Denoise Mosaic",
        "Post-Denoise Mosaic",
        "Hot Pixel Mask",
        "Denoise Difference",
        "False Color Mask",
        "Defringe Mask",
        "Highlight Edge Mask"
    };
    constexpr int debugLabelCount = static_cast<int>(sizeof(debugLabels) / sizeof(debugLabels[0]));
    int debugView = static_cast<int>(settings.debugView);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Debug View", &debugView, debugLabels, debugLabelCount)) {
        debugView = std::clamp(debugView, 0, debugLabelCount - 1);
        settings.debugView = static_cast<Raw::RawDebugView>(debugView);
        changed = true;
    }
    if (!demosaicEnabled &&
        settings.debugView != Raw::RawDebugView::FinalOutput &&
        settings.debugView != Raw::RawDebugView::CameraTransformedRgb) {
        settings.debugView = Raw::RawDebugView::FinalOutput;
        changed = true;
    }
    if (!demosaicEnabled) {
        ImGui::TextDisabled("Linear RGB DNG: mosaic, CFA, and clipped RAW channel debug views are unavailable.");
    }
    if (metadata.isDng) {
        ImGui::TextDisabled("DNG metadata: black=%s, white=%s, WB=%s, matrix=%s",
            metadata.blackLevelSource.empty() ? "unknown" : metadata.blackLevelSource.c_str(),
            metadata.whiteLevelSource.empty() ? "unknown" : metadata.whiteLevelSource.c_str(),
            metadata.whiteBalanceSource.empty() ? "unknown" : metadata.whiteBalanceSource.c_str(),
            metadata.cameraMatrixSource.empty() ? "unknown" : metadata.cameraMatrixSource.c_str());
    }
    if (!metadata.warnings.empty()) {
        for (const std::string& warning : metadata.warnings) {
            ImGui::TextWrapped("%s", warning.c_str());
        }
    }

    if (changed || !SameRawDevelopSettings(settingsBefore, settings)) {
        MarkRenderDirty(node.id);
    }

    if (!(rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource)) {
        ImGui::EndDisabled();
    }
}

void EditorModule::RenderRawDetailAutoMaskControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    (void)advanced;
    if (node.kind != EditorNodeGraph::NodeKind::RawDetailAutoMask) {
        return;
    }

    Raw::RawDetailFusionSettings& settings = node.rawDetailAutoMask.settings;
    const Raw::RawDetailFusionSettings settingsBefore = settings;
    const bool hasImageInput = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId) != nullptr;
    bool changed = false;

    settings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
    settings.strength = 1.0f;
    settings.invertMask = false;
    settings.maskBlackPoint = 0.0f;
    settings.maskWhitePoint = 1.0f;
    settings.maskGamma = 1.0f;
    settings.manualBlend = 0.0f;

    ImGuiExtras::RichSectionLabel("RAW DETAIL AUTO MASK", 4.0f);
    ImGui::TextDisabled("Input: scene-linear image after RAW Develop");
    ImGui::TextDisabled("Output: generated RGBA16F EV/detail mask");
    if (!hasImageInput) {
        ImGui::TextWrapped("Connect the Image input from RAW Develop. The generated mask keeps that developed image's aspect and orientation.");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("AUTO EXPOSURE FIELD", 4.0f);
    changed |= RenderAutoGainExposureControls(settings, controlWidth, "RawAutoMask", false);
    ImGui::TextDisabled("Auto Safety derives the range from image statistics; overrides still allow -8..+8 EV.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("ANALYSIS WEIGHTS", 4.0f);
    changed |= RenderAutoGainAnalysisControls(settings, controlWidth, "RawAutoMask");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("SKY / SMOOTH GRADIENTS", 4.0f);
    changed |= ImGuiExtras::NodeSliderFloat("Smooth Gradient Protection", "##RawAutoMaskSmoothGradientProtect", &settings.smoothGradientProtection, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Texture Sensitivity", "##RawAutoMaskTextureSensitivity", &settings.textureSensitivity, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Sky Bias", "##RawAutoMaskSkyBias", &settings.skyBias, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGui::TextWrapped("Protects smooth ramps from being treated like texture while still preserving tree lines and horizons.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("SMOOTH / HALO GUARD", 4.0f);
    changed |= ImGuiExtras::NodeSliderInt("Smoothness Radius", "##RawAutoMaskSmoothRadius", &settings.smoothnessRadius, 0, 16, "%d px", controlWidth);
    changed |= ImGuiExtras::NodeSliderInt("Smooth Area Radius", "##RawAutoMaskSmoothAreaRadius", &settings.smoothAreaRadius, 0, 32, "%d px", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Edge Awareness", "##RawAutoMaskEdgeAware", &settings.edgeAwareness, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Halo Guard", "##RawAutoMaskHaloGuard", &settings.haloGuard, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Mask Deband Dither", "##RawAutoMaskDither", &settings.maskDebandDither, 0.0f, 1.0f, "%.2f", controlWidth);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("DIAGNOSTICS", 4.0f);
    const char* debugLabels[] = {
        "Effective EV Map",
        "Confidence Map",
        "Highlight Safety",
        "Shadow / Noise Protection",
        "Sample Selection",
        "Smooth Gradient Protection",
        "True Edge Map",
        "Texture Detail Map",
        "Deband / Chroma Risk",
        "Auto Range Map",
        "Noise Floor / SNR",
        "Highlight Headroom",
        "Channel Saturation",
        "Rejected Detail"
    };
    int debugView = static_cast<int>(settings.debugView);
    if (debugView <= static_cast<int>(Raw::RawDetailFusionDebugView::FinalImage)) {
        debugView = static_cast<int>(Raw::RawDetailFusionDebugView::ExposureMap);
    }
    ImGui::SetNextItemWidth(controlWidth);
    int displayDebug = std::clamp(debugView - 1, 0, 13);
    if (ImGui::Combo("Preview Channel", &displayDebug, debugLabels, 14)) {
        settings.debugView = static_cast<Raw::RawDetailFusionDebugView>(std::clamp(displayDebug + 1, 1, 14));
        changed = true;
    }
    ImGui::TextDisabled("The mask output remains reusable by Preview, Levels Mask, scopes, and Fusion.");

    ClampRawDetailFusionSettings(settings);

    if (changed || !SameRawDetailFusionSettings(settingsBefore, settings)) {
        MarkRenderDirty(node.id);
    }
}

void EditorModule::RenderRawDetailFusionControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    (void)advanced;
    if (node.kind != EditorNodeGraph::NodeKind::RawDetailFusion) {
        return;
    }

    Raw::RawDetailFusionSettings& settings = node.rawDetailFusion.settings;
    const Raw::RawDetailFusionSettings settingsBefore = settings;
    const bool hasImageInput = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId) != nullptr;
    const bool hasHybridMask = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kMaskInputSocketId) != nullptr;
    bool changed = false;

    ImGuiExtras::RichSectionLabel("AUTO GAIN", 4.0f);
    ImGui::TextDisabled("Input: scene-linear image after RAW Develop");
    ImGui::TextDisabled("Output: auto-gain scene-linear RGB");
    if (!hasImageInput) {
        ImGui::TextWrapped("Connect the Image input. Auto gain calculates and applies its gain mask automatically when connected.");
    }
    ImGui::Text("Mode: %s", hasHybridMask ? "Hybrid" : "Auto");
    ImGui::TextWrapped("With this window open, click the single-output viewport image to toggle the generated gain mask preview.");

    if (ImGui::Button("Reset Auto Gain", ImVec2(controlWidth, 0.0f))) {
        settings = Raw::RawDetailFusionSettings{};
        changed = true;
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("EXPOSURE FIELD", 4.0f);
    changed |= RenderAutoGainExposureControls(settings, controlWidth, "AutoGain", true);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("AUTO ANALYSIS", 4.0f);
    changed |= RenderAutoGainAnalysisControls(settings, controlWidth, "AutoGain");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("SKY / SMOOTH GRADIENTS", 4.0f);
    changed |= ImGuiExtras::NodeSliderFloat("Smooth Gradient Protection", "##AutoGainSmoothGradientProtect", &settings.smoothGradientProtection, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Texture Sensitivity", "##AutoGainTextureSensitivity", &settings.textureSensitivity, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Sky Bias", "##AutoGainSkyBias", &settings.skyBias, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGui::TextWrapped("Higher protection smooths sky-like ramps so gain does not exaggerate tiny shade steps. Texture Sensitivity controls how easily foliage and fine detail are preserved.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("SMOOTH / HALO GUARD", 4.0f);
    changed |= ImGuiExtras::NodeSliderInt("Smoothness Radius", "##AutoGainSmoothRadius", &settings.smoothnessRadius, 0, 16, "%d px", controlWidth);
    changed |= ImGuiExtras::NodeSliderInt("Smooth Area Radius", "##AutoGainSmoothAreaRadius", &settings.smoothAreaRadius, 0, 32, "%d px", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Edge Awareness", "##AutoGainEdgeAware", &settings.edgeAwareness, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Halo Guard", "##AutoGainHaloGuard", &settings.haloGuard, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Mask Deband Dither", "##AutoGainDither", &settings.maskDebandDither, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGui::TextWrapped("Raise Edge Awareness or Halo Guard when gain bleeds around trees, horizons, or silhouettes. Smooth Area Radius only expands smoothing where smooth-gradient protection is confident.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("DIAGNOSTICS", 4.0f);
    const char* debugLabels[] = {
        "Effective EV Map",
        "Confidence Map",
        "Highlight Safety",
        "Shadow / Noise Protection",
        "Sample Selection",
        "Smooth Gradient Protection",
        "True Edge Map",
        "Texture Detail Map",
        "Deband / Chroma Risk",
        "Auto Range Map",
        "Noise Floor / SNR",
        "Highlight Headroom",
        "Channel Saturation",
        "Rejected Detail"
    };
    int debugView = static_cast<int>(settings.debugView);
    if (debugView <= static_cast<int>(Raw::RawDetailFusionDebugView::FinalImage)) {
        debugView = static_cast<int>(Raw::RawDetailFusionDebugView::ExposureMap);
    }
    int displayDebug = std::clamp(debugView - 1, 0, 13);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Mask Preview Channel", &displayDebug, debugLabels, 14)) {
        settings.debugView = static_cast<Raw::RawDetailFusionDebugView>(std::clamp(displayDebug + 1, 1, 14));
        changed = true;
    }
    ImGui::TextDisabled("Viewport mask preview uses this channel; normal output remains the final image.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("HYBRID MASK CORRECTION", 4.0f);
    ImGui::BeginDisabled(!hasHybridMask);
    if (!hasHybridMask) {
        ImGui::TextDisabled("Connect a mask input to guide/correct the auto gain field.");
    }
    changed |= ImGuiExtras::NodeCheckbox("Invert Mask", "##AutoGainInvertMask", &settings.invertMask, controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Black Point", "##AutoGainMaskBlack", &settings.maskBlackPoint, 0.0f, 1.0f, "%.3f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("White Point", "##AutoGainMaskWhite", &settings.maskWhitePoint, 0.0f, 1.0f, "%.3f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Gamma", "##AutoGainMaskGamma", &settings.maskGamma, 0.1f, 4.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Manual Blend", "##AutoGainManualBlend", &settings.manualBlend, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGui::EndDisabled();

    settings.mode = hasHybridMask ? Raw::RawDetailFusionMode::Hybrid : Raw::RawDetailFusionMode::AutoAnalyze;
    ClampRawDetailFusionSettings(settings);

    if (changed || !SameRawDetailFusionSettings(settingsBefore, settings)) {
        MarkRenderDirty(node.id);
    }
}
