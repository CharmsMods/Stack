#include "Editor/EditorModule.h"

#include "Editor/Internal/EditorModuleRawControlShared.h"
#include "NeuralDenoise/NeuralDenoiseManager.h"
#include "Raw/RawImageData.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <string>
#include <vector>

using Stack::Editor::RawControls::BuildRawDecodeDefaultSettingsFromMetadata;
using Stack::Editor::RawControls::EffectiveWhiteBalance;
using Stack::Editor::RawControls::FindUpstreamRawSource;
using Stack::Editor::RawControls::GraphSliderRightClickWasConsumed;
using Stack::Editor::RawControls::RawDisplayName;
using Stack::Editor::RawControls::SameRawDevelopSettings;

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
    if (metadata.hasExposureTime || metadata.hasApertureFNumber || metadata.hasIsoSpeed) {
        const float shutter = metadata.hasExposureTime ? metadata.exposureTimeSeconds : 0.0f;
        const float aperture = metadata.hasApertureFNumber ? metadata.apertureFNumber : 0.0f;
        const float iso = metadata.hasIsoSpeed ? metadata.isoSpeed : 0.0f;
        if (metadata.hasExposureTime && metadata.hasApertureFNumber && metadata.hasIsoSpeed) {
            ImGui::TextDisabled("Capture: %.4f s  f/%.2f  ISO %.0f", shutter, aperture, iso);
        } else if (metadata.hasExposureTime && metadata.hasIsoSpeed) {
            ImGui::TextDisabled("Capture: %.4f s  ISO %.0f", shutter, iso);
        } else if (metadata.hasExposureTime) {
            ImGui::TextDisabled("Capture: %.4f s", shutter);
        }
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
    ImGui::TextDisabled("Output: RAW sensor data. Baseline path: RAW Decode -> Tone Curve -> View Transform.");

    if (advanced) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::Button("Add Manual RAW Chain", ImVec2(controlWidth, 0.0f))) {
            AddFullRawTreeToSource(node.id);
        }
        if (ImGui::Button("Add Advanced Develop", ImVec2(controlWidth, 0.0f))) {
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

void EditorModule::RenderRawDecodeControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawDecode) {
        return;
    }
    if (node.title.empty()) {
        node.title = "RAW Decode";
    }

    const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSource(m_NodeGraph, node);
    const Raw::RawMetadata emptyMetadata;
    const Raw::RawMetadata& metadata =
        (rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource)
            ? rawSourceNode->rawSource.metadata
            : emptyMetadata;
    const bool hasRawSourceInput = rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource;

    EditorNodeGraph::RawDecodePayload& payload = node.rawDecode;
    Raw::RawDevelopSettings& settings = payload.settings;
    const Raw::RawDevelopSettings settingsBefore = settings;
    const Raw::RawDevelopSettings defaultSettings = BuildRawDecodeDefaultSettingsFromMetadata(metadata);
    const std::array<float, 3> effectiveWb = EffectiveWhiteBalance(metadata, settings);
    const bool demosaicEnabled = metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer;

    auto resettableDecodeSliderFloat = [&](const char* label,
                                           const char* id,
                                           float* value,
                                           float resetValue,
                                           float minValue,
                                           float maxValue,
                                           const char* format) {
        bool localChanged = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, format, controlWidth);
        const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
        if (!GraphSliderRightClickWasConsumed() &&
            state.lastHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            std::abs(*value - resetValue) > 0.0001f) {
            *value = resetValue;
            localChanged = true;
        }
        return localChanged;
    };

    ImGuiExtras::RichSectionLabel("RAW DECODE", 4.0f);
    if (hasRawSourceInput) {
        ImGui::TextDisabled("%s", RawDisplayName(rawSourceNode->rawSource).c_str());
        ImGui::TextDisabled("Output: unclamped scene-linear float RGB");
        if (advanced && metadata.visibleWidth > 0 && metadata.visibleHeight > 0) {
            ImGui::TextDisabled("Visible: %d x %d", metadata.visibleWidth, metadata.visibleHeight);
        }
    } else {
        ImGui::TextWrapped("Connect a RAW Source or RAW Neural Denoise node to decode sensor data into scene-linear RGB.");
    }
    ImGui::TextDisabled("Manual RAW foundation: decode, orient, white-balance, and expose before Tone Curve / View Transform.");

    if (!hasRawSourceInput) {
        ImGui::BeginDisabled();
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW COLOR", 4.0f);
    const char* wbLabels[] = { "Camera WB", "Auto WB", "Neutral", "Manual" };
    int wbMode = static_cast<int>(settings.whiteBalanceMode);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("White Balance", &wbMode, wbLabels, 4)) {
        settings.whiteBalanceMode = static_cast<Raw::WhiteBalanceMode>(std::clamp(wbMode, 0, 3));
    }
    ImGui::BeginDisabled(settings.whiteBalanceMode != Raw::WhiteBalanceMode::Manual);
    resettableDecodeSliderFloat("Red Mult", "##RawDecodeWbR", &settings.manualWhiteBalance[0], defaultSettings.manualWhiteBalance[0], 0.05f, 16.0f, "%.3f");
    resettableDecodeSliderFloat("Green Mult", "##RawDecodeWbG", &settings.manualWhiteBalance[1], defaultSettings.manualWhiteBalance[1], 0.05f, 16.0f, "%.3f");
    resettableDecodeSliderFloat("Blue Mult", "##RawDecodeWbB", &settings.manualWhiteBalance[2], defaultSettings.manualWhiteBalance[2], 0.05f, 16.0f, "%.3f");
    ImGui::EndDisabled();
    ImGui::TextDisabled("Effective WB RGB: %.3f %.3f %.3f", effectiveWb[0], effectiveWb[1], effectiveWb[2]);
    if (!metadata.whiteBalanceSource.empty()) {
        ImGui::TextDisabled("WB source: %s", metadata.whiteBalanceSource.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW EXPOSURE / EV", 4.0f);
    auto& exposureDraft = m_RawDevelopExposureDrafts[node.id];
    if (!exposureDraft.editing) {
        exposureDraft.exposureStops = settings.exposureStops;
    }
    const bool exposureDraftChanged = resettableDecodeSliderFloat(
        "RAW Exposure / EV",
        "##RawDecodeExposure",
        &exposureDraft.exposureStops,
        defaultSettings.exposureStops,
        -8.0f,
        8.0f,
        "%+.2f EV");
    const ImGuiExtras::NodeControlState& exposureState = ImGuiExtras::GetNodeControlState();
    if (exposureState.lastHovered) {
        ImGui::SetTooltip("+1 EV multiplies scene-linear values by 2 before Tone Curve, View Transform, and later image operations.");
    }
    if (exposureState.lastActive) {
        exposureDraft.editing = true;
    } else if (exposureDraft.editing) {
        exposureDraft.editing = false;
        if (std::abs(exposureDraft.exposureStops - settings.exposureStops) > 0.0001f) {
            settings.exposureStops = exposureDraft.exposureStops;
        }
    } else if (exposureDraftChanged &&
               std::abs(exposureDraft.exposureStops - settings.exposureStops) > 0.0001f) {
        settings.exposureStops = exposureDraft.exposureStops;
    }
    if (metadata.hasDngBaselineExposure) {
        ImGui::TextDisabled("DNG baseline exposure metadata: %+.2f EV", metadata.dngBaselineExposure);
    }
    ImGui::TextDisabled("Scene-linear scale: x%.2f", std::exp2(settings.exposureStops));

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW HIGHLIGHT RECONSTRUCTION", 4.0f);
    ImGui::TextDisabled("Decode-stage highlight recovery before Tone Curve or later image processing.");
    const char* highlightLabels[] = { "Off", "Clip / Neutral", "Luminance", "Color Reconstruction" };
    int highlightMode = static_cast<int>(settings.highlightMode);
    ImGui::BeginDisabled(!demosaicEnabled);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Mode", &highlightMode, highlightLabels, 4)) {
        settings.highlightMode = static_cast<Raw::HighlightReconstructionMode>(std::clamp(highlightMode, 0, 3));
    }
    resettableDecodeSliderFloat("Strength", "##RawDecodeHighlightStrength", &settings.highlightStrength, defaultSettings.highlightStrength, 0.0f, 1.0f, "%.2f");
    resettableDecodeSliderFloat("Clip Threshold", "##RawDecodeHighlightThreshold", &settings.highlightThreshold, defaultSettings.highlightThreshold, 0.8f, 1.0f, "%.3f");
    ImGui::EndDisabled();
    ImGui::TextDisabled("Current mode: %s", Raw::HighlightReconstructionModeName(settings.highlightMode));

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW DEMOSAIC", 4.0f);
    if (demosaicEnabled) {
        if (settings.demosaicMethod != Raw::DemosaicMethod::Bilinear) {
            settings.demosaicMethod = Raw::DemosaicMethod::Bilinear;
        }
        ImGui::TextDisabled("Method: %s", Raw::DemosaicMethodName(settings.demosaicMethod));
        ImGui::TextDisabled("Status: preview-safe bilinear decode in this build.");
    } else {
        ImGui::TextDisabled("Method: skipped");
        ImGui::TextDisabled("Status: source is already linear RGB, so demosaic is not used.");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW ORIENTATION", 4.0f);
    const char* rotationLabels[] = { "0 Degrees", "90 Degrees CW", "180 Degrees", "270 Degrees CW" };
    int rotationIdx = settings.rotationDegrees / 90;
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Manual Rotation", &rotationIdx, rotationLabels, 4)) {
        settings.rotationDegrees = std::clamp(rotationIdx, 0, 3) * 90;
    }
    ImGuiExtras::NodeCheckbox("Stretch To Fit Frame", "##RawDecodeRotateToFitFrame", &settings.rotateToFitFrame, controlWidth);
    ImGuiExtras::NodeCheckbox("Flip Horizontally", "##RawDecodeFlipHorizontally", &settings.flipHorizontally, controlWidth);
    ImGuiExtras::NodeCheckbox("Flip Vertically", "##RawDecodeFlipVertically", &settings.flipVertically, controlWidth);
    if (advanced && metadata.orientation > 0) {
        ImGui::TextDisabled("Metadata orientation: %d", metadata.orientation);
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("CAMERA COLOR TRANSFORM", 4.0f);
    ImGuiExtras::NodeCheckbox("Camera Transform", "##RawDecodeCameraTransform", &settings.cameraTransformEnabled, controlWidth);
    const char* transformLabels[] = {
        "LibRaw rgb_cam",
        "DNG Auto (approx)",
        "DNG ForwardMatrix 1",
        "DNG ForwardMatrix 2",
        "DNG ColorMatrix inverse"
    };
    int transformSource = static_cast<int>(settings.cameraTransformSource);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Matrix Source", &transformSource, transformLabels, 5)) {
        settings.cameraTransformSource = static_cast<Raw::RawCameraTransformSource>(std::clamp(transformSource, 0, 4));
    }
    if (!metadata.cameraMatrixSource.empty()) {
        ImGui::TextDisabled("Metadata preferred: %s", metadata.cameraMatrixSource.c_str());
    } else if (advanced) {
        ImGui::TextDisabled("Current source: %s", Raw::RawCameraTransformSourceName(settings.cameraTransformSource));
    }

    if (advanced) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("RAW SOURCE SUMMARY", 4.0f);
        if (!metadata.cameraMake.empty() || !metadata.cameraModel.empty()) {
            ImGui::TextDisabled("%s %s", metadata.cameraMake.c_str(), metadata.cameraModel.c_str());
        }
        if (metadata.rawWidth > 0 && metadata.rawHeight > 0) {
            ImGui::TextDisabled("Raw: %d x %d", metadata.rawWidth, metadata.rawHeight);
        }
        if (metadata.visibleWidth > 0 && metadata.visibleHeight > 0) {
            ImGui::TextDisabled("Visible: %d x %d", metadata.visibleWidth, metadata.visibleHeight);
        }
        if (metadata.pixelLayout == Raw::RawPixelLayout::LinearRgb) {
            ImGui::TextDisabled("Layout: linear RGB");
        } else {
            ImGui::TextDisabled("CFA: %s", Raw::CfaPatternName(metadata.cfaPattern));
        }
    }

    if (!hasRawSourceInput) {
        ImGui::EndDisabled();
    }

    if (!metadata.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.38f, 0.34f, 1.0f));
        ImGui::TextWrapped("%s", metadata.error.c_str());
        ImGui::PopStyleColor();
    }

    if (!SameRawDevelopSettings(settingsBefore, settings)) {
        MarkRenderDirty(node.id);
        MarkDirty();
        ValidateActiveRawWorkspaceManagedGraph(true);
    }
}
