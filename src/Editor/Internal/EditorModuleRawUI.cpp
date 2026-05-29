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
        SameRawMosaicDenoiseSettings(a.mosaicDenoise, b.mosaicDenoise);
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
    const char* demosaicLabels[] = { "Fast / Bilinear", "Quality Placeholder" };
    int demosaic = static_cast<int>(settings.demosaicMethod);
    ImGui::BeginDisabled(!demosaicEnabled);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Method", &demosaic, demosaicLabels, 2)) {
        settings.demosaicMethod = static_cast<Raw::DemosaicMethod>(std::clamp(demosaic, 0, 1));
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(demosaicEnabled ? "Edges: clamp to edge" : "Linear RGB DNG: demosaic skipped.");

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
        "Denoise Difference"
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
