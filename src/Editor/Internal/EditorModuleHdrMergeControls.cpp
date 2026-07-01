#include "Editor/EditorModule.h"

#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <imgui.h>
#include <string>

namespace {

bool SameHdrMergeSettings(const Raw::HdrMergeSettings& a, const Raw::HdrMergeSettings& b) {
    return a.debugView == b.debugView &&
        a.alignmentMode == b.alignmentMode &&
        a.exposureMode == b.exposureMode &&
        a.referenceMode == b.referenceMode &&
        a.deghostMode == b.deghostMode &&
        a.motionPriority == b.motionPriority &&
        a.manualExposureEv[0] == b.manualExposureEv[0] &&
        a.manualExposureEv[1] == b.manualExposureEv[1] &&
        a.manualExposureEv[2] == b.manualExposureEv[2] &&
        a.exposureOffsetEv[0] == b.exposureOffsetEv[0] &&
        a.exposureOffsetEv[1] == b.exposureOffsetEv[1] &&
        a.exposureOffsetEv[2] == b.exposureOffsetEv[2] &&
        a.autoReliability == b.autoReliability &&
        a.clipThreshold == b.clipThreshold &&
        a.clipFeather == b.clipFeather &&
        a.blackThreshold == b.blackThreshold &&
        a.blackFeather == b.blackFeather &&
        a.readNoise == b.readNoise &&
        a.noiseAware == b.noiseAware;
}

void ClampHdrMergeSettings(Raw::HdrMergeSettings& settings) {
    for (float& ev : settings.manualExposureEv) {
        ev = std::clamp(ev, -12.0f, 12.0f);
    }
    for (float& ev : settings.exposureOffsetEv) {
        ev = std::clamp(ev, -4.0f, 4.0f);
    }
    settings.clipThreshold = std::clamp(settings.clipThreshold, 0.50f, 4.0f);
    settings.clipFeather = std::clamp(settings.clipFeather, 0.001f, 1.0f);
    settings.blackThreshold = std::clamp(settings.blackThreshold, 0.0f, 0.25f);
    settings.blackFeather = std::clamp(settings.blackFeather, 0.001f, 0.50f);
    settings.readNoise = std::clamp(settings.readNoise, 0.0f, 0.10f);
}

const char* HdrMergeDebugViewLabel(Raw::HdrMergeDebugView view) {
    switch (view) {
        case Raw::HdrMergeDebugView::Contribution: return "Contribution";
        case Raw::HdrMergeDebugView::Clipping: return "Clipping";
        case Raw::HdrMergeDebugView::NoiseLimited: return "Noise / Black Limited";
        case Raw::HdrMergeDebugView::AlignmentConfidence: return "Alignment Confidence";
        case Raw::HdrMergeDebugView::MotionMask: return "Motion Mask";
        case Raw::HdrMergeDebugView::RejectedSamples: return "Rejected Samples";
        case Raw::HdrMergeDebugView::FinalImage:
        default:
            return "Final Image";
    }
}

const char* HdrMergeAlignmentModeLabel(Raw::HdrMergeAlignmentMode mode) {
    switch (mode) {
        case Raw::HdrMergeAlignmentMode::Translation: return "Translation";
        case Raw::HdrMergeAlignmentMode::WideTranslation: return "Wide Translation";
        case Raw::HdrMergeAlignmentMode::Off:
        default:
            return "Off";
    }
}

const char* HdrMergeExposureModeLabel(Raw::HdrMergeExposureMode mode) {
    switch (mode) {
        case Raw::HdrMergeExposureMode::Manual: return "Manual";
        case Raw::HdrMergeExposureMode::Metadata:
        default:
            return "Metadata";
    }
}

const char* HdrMergeReferenceModeLabel(Raw::HdrMergeReferenceMode mode) {
    switch (mode) {
        case Raw::HdrMergeReferenceMode::Frame1: return "Frame 1";
        case Raw::HdrMergeReferenceMode::Frame2: return "Frame 2";
        case Raw::HdrMergeReferenceMode::Frame3: return "Frame 3";
        case Raw::HdrMergeReferenceMode::Auto:
        default:
            return "Auto";
    }
}

const char* HdrMergeDeghostModeLabel(Raw::HdrMergeDeghostMode mode) {
    switch (mode) {
        case Raw::HdrMergeDeghostMode::Off: return "Off";
        case Raw::HdrMergeDeghostMode::Low: return "Low";
        case Raw::HdrMergeDeghostMode::High: return "High";
        case Raw::HdrMergeDeghostMode::Medium:
        default:
            return "Medium";
    }
}

const char* HdrMergeMotionPriorityLabel(Raw::HdrMergeMotionPriority mode) {
    switch (mode) {
        case Raw::HdrMergeMotionPriority::AverageCleanAreas: return "Blend Static Consensus";
        case Raw::HdrMergeMotionPriority::PreserveReference:
        default:
            return "Prefer Reference";
    }
}

} // namespace

void EditorModule::RenderHdrMergeControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::HdrMerge) {
        return;
    }

    Raw::HdrMergeSettings& settings = node.hdrMerge.settings;
    const Raw::HdrMergeSettings settingsBefore = settings;
    const HdrMergeNodeStatus status = GetHdrMergeNodeStatus(node.id);
    bool changed = false;
    const bool showImage3Controls = status.inputs[2].connected;

    ImGuiExtras::RichSectionLabel("HDR MERGE", 4.0f);
    ImGui::TextDisabled("Input: scene-linear bracket or burst frames");
    ImGui::TextDisabled("Output: scene-linear HDR radiance image");
    ImGui::TextDisabled("This is scene-linear HDR reconstruction. Alignment and deghosting are first-pass tools here; tone mapping stays downstream.");

    const ImVec4 statusColor =
        status.state == HdrMergeRenderState::Failed || status.state == HdrMergeRenderState::IncompatibleInput
            ? ImVec4(0.95f, 0.55f, 0.42f, 1.0f)
            : (status.state == HdrMergeRenderState::BlockedMissingInput
                ? ImVec4(0.92f, 0.76f, 0.42f, 1.0f)
                : ImVec4(0.78f, 0.82f, 0.86f, 1.0f));
    ImGui::TextColored(statusColor, "Status: %s", status.message.c_str());
    ImGui::TextDisabled("Inspection View: %s", HdrMergeDebugViewLabel(status.debugView));
    ImGui::TextDisabled("Alignment: %s", HdrMergeAlignmentModeLabel(settings.alignmentMode));
    ImGui::TextDisabled("Exposure Normalization: %s", HdrMergeExposureModeLabel(settings.exposureMode));
    ImGui::TextDisabled("Reference: %s", HdrMergeReferenceModeLabel(settings.referenceMode));
    ImGui::TextDisabled("Ghost Reduction: %s", HdrMergeDeghostModeLabel(settings.deghostMode));
    ImGui::TextDisabled("Motion Merge Policy: %s", HdrMergeMotionPriorityLabel(settings.motionPriority));
    ImGui::TextDisabled("Reliability Defaults: %s", settings.autoReliability ? "Automatic" : "Manual Override");
    if (!status.normalizationMessage.empty()) {
        ImGui::TextDisabled("Normalization Status: %s", status.normalizationMessage.c_str());
    }
    if (!status.reliabilityMessage.empty()) {
        ImGui::TextDisabled("Reliability Status: %s", status.reliabilityMessage.c_str());
    }
    if (!status.warningMessage.empty()) {
        ImGui::TextColored(ImVec4(0.92f, 0.76f, 0.42f, 1.0f), "%s", status.warningMessage.c_str());
    }
    if (status.stale) {
        ImGui::TextDisabled("The current output is older than the latest HDR Merge settings or connections.");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("INPUTS", 4.0f);
    for (const HdrMergeInputSummary& input : status.inputs) {
        if (input.label.empty() || (!input.active && input.socketId == EditorNodeGraph::kHdrMergeInput3SocketId)) {
            continue;
        }

        std::string secondary = input.active ? (input.connected ? "Connected" : "Missing") : "Inactive";
        if (input.active && input.connected && !input.compatible) {
            secondary = "Dimension mismatch";
        }
        if (input.width > 0 && input.height > 0) {
            secondary += "  " + std::to_string(input.width) + " x " + std::to_string(input.height);
        }
        ImGui::Text("%s: %s", input.label.c_str(), input.sourceLabel.c_str());
        ImGui::TextDisabled("%s", secondary.c_str());
        if (!input.metadataSummary.empty()) {
            ImGui::TextDisabled("%s", input.metadataSummary.c_str());
        }
        if (!input.normalizationSummary.empty()) {
            ImGui::TextDisabled("%s", input.normalizationSummary.c_str());
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel(advanced ? "ALIGNMENT / MOTION" : "NORMAL", 4.0f);
    int alignmentMode = static_cast<int>(settings.alignmentMode);
    const char* alignmentModes[] = { "Off", "Auto Translation", "Wide Translation" };
    if (ImGuiExtras::NodeCombo("Alignment", "##HdrMergeAlignmentMode", &alignmentMode, alignmentModes, IM_ARRAYSIZE(alignmentModes), controlWidth)) {
        settings.alignmentMode = static_cast<Raw::HdrMergeAlignmentMode>(std::clamp(alignmentMode, 0, 2));
        changed = true;
    }
    int deghostMode = static_cast<int>(settings.deghostMode);
    const char* deghostModes[] = { "Off", "Low", "Medium", "High" };
    if (ImGuiExtras::NodeCombo("Ghost Reduction", "##HdrMergeDeghostMode", &deghostMode, deghostModes, IM_ARRAYSIZE(deghostModes), controlWidth)) {
        settings.deghostMode = static_cast<Raw::HdrMergeDeghostMode>(std::clamp(deghostMode, 0, 3));
        changed = true;
    }
    int exposureMode = static_cast<int>(settings.exposureMode);
    const char* exposureModes[] = { "Metadata (Automatic)", "Manual" };
    if (ImGuiExtras::NodeCombo("Exposure Normalization", "##HdrMergeExposureMode", &exposureMode, exposureModes, IM_ARRAYSIZE(exposureModes), controlWidth)) {
        settings.exposureMode = static_cast<Raw::HdrMergeExposureMode>(std::clamp(exposureMode, 0, 1));
        changed = true;
    }

    if (!advanced) {
        ImGui::TextDisabled("Automatic mode reads shutter, ISO, aperture, and Develop exposure when those sources are available.");
        if (settings.exposureMode == Raw::HdrMergeExposureMode::Metadata && !status.metadataNormalizationReady) {
            ImGui::TextDisabled("Automatic normalization is not fully available for this stack. Open Advanced to inspect the fallback inputs.");
        }
        if (settings.autoReliability && !status.automaticReliabilityReady) {
            ImGui::TextDisabled("Automatic reliability is only partial here. Some inputs are using the manual fallback thresholds.");
        }
        ImGui::TextDisabled("Use the advanced editor for reference-frame override, exposure offsets, and manual reliability tuning.");
    } else {
        int referenceMode = static_cast<int>(settings.referenceMode);
        const char* referenceModes[] = { "Auto", "Frame 1", "Frame 2", "Frame 3" };
        if (ImGuiExtras::NodeCombo("Reference Frame", "##HdrMergeReferenceMode", &referenceMode, referenceModes, IM_ARRAYSIZE(referenceModes), controlWidth)) {
            settings.referenceMode = static_cast<Raw::HdrMergeReferenceMode>(std::clamp(referenceMode, 0, 3));
            changed = true;
        }

        int motionPriority = static_cast<int>(settings.motionPriority);
        const char* motionPriorities[] = { "Prefer Reference", "Blend Static Consensus" };
        if (ImGuiExtras::NodeCombo("Motion Merge Policy", "##HdrMergeMotionPriority", &motionPriority, motionPriorities, IM_ARRAYSIZE(motionPriorities), controlWidth)) {
            settings.motionPriority = static_cast<Raw::HdrMergeMotionPriority>(std::clamp(motionPriority, 0, 1));
            changed = true;
        }

        ImGui::TextDisabled("Wide Translation increases search range but is still translation-only. It is not a full handheld warp model yet.");
        ImGui::TextDisabled("Auto reference prefers the least clipped usable bracket or the sharper middle frame for burst-like inputs.");

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("EXPOSURE NORMALIZATION", 4.0f);
        if (settings.exposureMode == Raw::HdrMergeExposureMode::Metadata) {
            changed |= ImGuiExtras::NodeSliderFloat("Frame 1 Offset", "##HdrMergeEvOffset1Advanced", &settings.exposureOffsetEv[0], -4.0f, 4.0f, "%+.2f EV", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Frame 2 Offset", "##HdrMergeEvOffset2Advanced", &settings.exposureOffsetEv[1], -4.0f, 4.0f, "%+.2f EV", controlWidth);
            if (showImage3Controls) {
                changed |= ImGuiExtras::NodeSliderFloat("Frame 3 Offset", "##HdrMergeEvOffset3Advanced", &settings.exposureOffsetEv[2], -4.0f, 4.0f, "%+.2f EV", controlWidth);
            }
            ImGui::TextDisabled("Automatic normalization uses capture exposure metadata plus Develop exposure. These offsets stay available as a precise fallback.");
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Frame 1 EV", "##HdrMergeManualEv1Advanced", &settings.manualExposureEv[0], -12.0f, 12.0f, "%.2f EV", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Frame 2 EV", "##HdrMergeManualEv2Advanced", &settings.manualExposureEv[1], -12.0f, 12.0f, "%.2f EV", controlWidth);
            if (showImage3Controls) {
                changed |= ImGuiExtras::NodeSliderFloat("Frame 3 EV", "##HdrMergeManualEv3Advanced", &settings.manualExposureEv[2], -12.0f, 12.0f, "%.2f EV", controlWidth);
            }
            ImGui::TextDisabled("Manual mode is the fallback for non-RAW inputs, incomplete metadata, or deliberate expert overrides.");
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("RELIABILITY / NOISE", 4.0f);
        changed |= ImGuiExtras::NodeCheckbox("Automatic Reliability", "##HdrMergeAutoReliabilityAdvanced", &settings.autoReliability, controlWidth);
        changed |= ImGuiExtras::NodeCheckbox("Use Noise Model", "##HdrMergeNoiseAwareAdvanced", &settings.noiseAware, controlWidth);
        if (settings.autoReliability) {
            ImGui::TextDisabled("Highlight, shadow, and read-noise limits are derived from RAW white/black levels, ISO, and Develop exposure when available.");
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Highlight Reliability Threshold", "##HdrMergeClipThresholdAdvanced", &settings.clipThreshold, 0.50f, 4.0f, "%.3f", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Highlight Roll-off", "##HdrMergeClipFeatherAdvanced", &settings.clipFeather, 0.001f, 1.0f, "%.3f", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Shadow Reliability Threshold", "##HdrMergeBlackThresholdAdvanced", &settings.blackThreshold, 0.0f, 0.25f, "%.4f", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Shadow Roll-off", "##HdrMergeBlackFeatherAdvanced", &settings.blackFeather, 0.001f, 0.50f, "%.4f", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Read-noise Override", "##HdrMergeReadNoiseAdvanced", &settings.readNoise, 0.0f, 0.10f, "%.4f", controlWidth);
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("PREVIEW", 4.0f);
        int debugView = static_cast<int>(settings.debugView);
        const char* debugViews[] = {
            "Final Image",
            "Contribution",
            "Clipping",
            "Noise / Black Limited",
            "Alignment Confidence",
            "Motion Mask",
            "Rejected Samples"
        };
        if (ImGuiExtras::NodeCombo("Inspection View", "##HdrMergeDebugAdvanced", &debugView, debugViews, IM_ARRAYSIZE(debugViews), controlWidth)) {
            settings.debugView = static_cast<Raw::HdrMergeDebugView>(std::clamp(debugView, 0, 6));
            changed = true;
        }
    }

    ClampHdrMergeSettings(settings);
    if (changed || !SameHdrMergeSettings(settingsBefore, settings)) {
        MarkRenderDirty(node.id);
    }
}
