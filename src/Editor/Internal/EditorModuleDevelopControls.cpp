#include "Editor/EditorModule.h"

#include "Editor/Internal/EditorModuleDevelopDefaults.h"
#include "Editor/Internal/EditorModuleDevelopAutoGuidanceControls.h"
#include "Editor/Internal/EditorModuleDevelopAutoStatusControls.h"
#include "Editor/Internal/EditorModuleDevelopFinishToneControls.h"
#include "Editor/Internal/EditorModuleDevelopManualRawControls.h"
#include "Editor/Internal/EditorModuleDevelopPayloadComparison.h"
#include "Editor/Internal/EditorModuleDevelopScenePrepControls.h"
#include "Editor/Internal/EditorModuleRawControlShared.h"
#include "Raw/RawImageData.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <imgui.h>
#include <string>

using Stack::Editor::RawControls::RawDisplayName;
using Stack::Editor::RawControls::SameRawDevelopSettings;
using Stack::Editor::DevelopDefaults::BuildDefaultIntegratedToneLayerJson;
using Stack::Editor::DevelopPayloadComparison::SameRawDevelopPayload;

void EditorModule::RenderRawDevelopControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return;
    }
    if (node.title.empty() || node.title == "RAW Develop") {
        node.title = "Develop";
    }

    const EditorNodeGraph::Link* rawInput = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
    const EditorNodeGraph::Node* rawSourceNode = rawInput ? m_NodeGraph.FindNode(rawInput->fromNodeId) : nullptr;
    const Raw::RawMetadata emptyMetadata;
    const Raw::RawMetadata& metadata =
        (rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource)
            ? rawSourceNode->rawSource.metadata
            : emptyMetadata;

    EditorNodeGraph::RawDevelopPayload& payload = node.rawDevelop;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    NormalizeDevelopAutoGuidance(payload.autoGuidance);
    NormalizeDevelopSubjectImportance(payload.subjectImportance);
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }
    const EditorNodeGraph::RawDevelopPayload payloadBefore = payload;

    Raw::RawDevelopSettings& settings = payload.settings;
    Raw::RawDetailFusionSettings& scenePrepSettings = payload.scenePrepSettings;
    Raw::RawDevelopSettings defaultSettings;
    if (metadata.hasDngBaselineExposure) {
        defaultSettings.exposureStops = metadata.dngBaselineExposure;
    }
    if (!metadata.isDng) {
        defaultSettings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    }
    const bool hasRawSourceInput = rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource;
    const RenderPipeline::PreLocalExposureSummary* liveScenePrepSummary = m_Pipeline.GetPreLocalExposureSummary(node.id);
    EditorNodeGraph::RawDevelopUiMode& uiMode = payload.uiMode;
    bool changed = false;
    const bool showAdvancedManualControls = advanced;
    if (uiMode != EditorNodeGraph::RawDevelopUiMode::Manual) {
        m_RawDevelopExposureDrafts.erase(node.id);
    }

    ImGuiExtras::RichSectionLabel("DEVELOP", 4.0f);
    if (hasRawSourceInput) {
        ImGui::TextDisabled("%s", RawDisplayName(rawSourceNode->rawSource).c_str());
        ImGui::TextDisabled("Output: unclamped scene-linear float RGB");
    } else {
        ImGui::TextWrapped("Connect a RAW Source node to develop sensor data.");
    }

    const char* modeLabels[] = { "Auto", "Manual" };
    int uiModeIndex = static_cast<int>(uiMode);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Mode", &uiModeIndex, modeLabels, 2)) {
        uiMode = static_cast<EditorNodeGraph::RawDevelopUiMode>(std::clamp(uiModeIndex, 0, 1));
        changed = true;
    }
    bool autoMode = uiMode == EditorNodeGraph::RawDevelopUiMode::Auto;
    bool manualMode = uiMode == EditorNodeGraph::RawDevelopUiMode::Manual;
    bool forceAutoReanalysis = payloadBefore.uiMode != payload.uiMode;
    ImGui::TextDisabled(
        autoMode
            ? "Auto is the default merged Develop workflow. It solves RAW conversion, scene prep, and finish tone together."
            : "Manual freezes the current auto-authored result and reveals the deeper RAW, prep, and finish controls.");

    if (!hasRawSourceInput) {
        ImGui::BeginDisabled();
    }

    if (manualMode) {
        m_DevelopAutoGuidanceDrafts.erase(node.id);
        const float buttonGap = 8.0f;
        const float buttonWidth = std::max(110.0f, (controlWidth - buttonGap) * 0.5f);
        if (ImGuiExtras::RichFullWidthButton("Return To Auto", buttonWidth, 0.0f)) {
            uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
            autoMode = true;
            manualMode = false;
            forceAutoReanalysis = true;
            changed = true;
        }
        ImGui::SameLine(0.0f, buttonGap);
        if (ImGuiExtras::RichFullWidthButton("Recalibrate Auto", buttonWidth, 0.0f)) {
            ApplyDevelopAutoSolve(payload, metadata, true);
            changed = true;
        }
        ImGui::TextDisabled("Manual keeps the current authored state in place while you refine the image directly.");
    }

    if (autoMode) {
        auto& draftState = m_DevelopAutoGuidanceDrafts[node.id];
        const Stack::Editor::DevelopAutoGuidanceControls::AutoGuidanceControlResult autoGuidanceControls =
            Stack::Editor::DevelopAutoGuidanceControls::RenderDevelopAutoGuidanceControls(
                payload.autoGuidance,
                payload.subjectImportance,
                draftState,
                forceAutoReanalysis,
                controlWidth);
        changed |= autoGuidanceControls.changed;
        forceAutoReanalysis = autoGuidanceControls.forceAutoReanalysis;
        const bool forceFullAutoReanalysis = autoGuidanceControls.forceFullAutoReanalysis;
        for (int i = 0; i < autoGuidanceControls.recordInteractionCount; ++i) {
            RecordRawDevelopInteraction(node.id);
        }

        changed |= UpdateDevelopAutoState(
            node.id,
            payload,
            metadata,
            forceAutoReanalysis || changed,
            forceFullAutoReanalysis);

        Stack::Editor::DevelopAutoStatusControls::CandidateFeedbackStatus candidateFeedbackStatus;
        candidateFeedbackStatus.deferred = GetDevelopCandidateFeedbackDeferredStatus(
            node.id,
            ImGui::GetTime(),
            candidateFeedbackStatus.quietRemainingSeconds);
        Stack::Editor::DevelopAutoStatusControls::RenderDevelopAutoStatusReadouts(
            payload,
            candidateFeedbackStatus);
    } else {
        m_DevelopAutoSolveTriggerHashes.erase(node.id);

        auto& exposureDraft = m_RawDevelopExposureDrafts[node.id];
        const Stack::Editor::DevelopManualRawControls::ManualRawBasicControlResult manualRawBasicControls =
            Stack::Editor::DevelopManualRawControls::RenderDevelopManualRawBasicControls(
                settings,
                defaultSettings,
                metadata,
                exposureDraft,
                controlWidth);
        changed |= manualRawBasicControls.changed;
        if (manualRawBasicControls.recordInteraction) {
            RecordRawDevelopInteraction(node.id);
        }

        changed |= Stack::Editor::DevelopScenePrepControls::RenderDevelopScenePrepControls(
            scenePrepSettings,
            liveScenePrepSummary,
            hasRawSourceInput,
            controlWidth,
            showAdvancedManualControls);

        changed |= Stack::Editor::DevelopManualRawControls::RenderDevelopManualRawAdvancedControls(
            settings,
            defaultSettings,
            metadata,
            controlWidth,
            showAdvancedManualControls);

        Stack::Editor::DevelopScenePrepControls::NormalizeDevelopScenePrepSettings(scenePrepSettings);
        const bool developSettingsChangedBeforeFinishTone =
            !SameRawDevelopSettings(payloadBefore.settings, payload.settings) ||
            payloadBefore.scenePrepEnabled != payload.scenePrepEnabled ||
            !Stack::Editor::DevelopScenePrepControls::SameDevelopScenePrepSettings(
                payloadBefore.scenePrepSettings,
                payload.scenePrepSettings);

        changed |= Stack::Editor::DevelopFinishToneControls::RenderDevelopFinishToneControls(
            *this,
            node.id,
            payload.integratedToneLayerJson,
            developSettingsChangedBeforeFinishTone,
            controlWidth,
            showAdvancedManualControls);
    }

    if (!hasRawSourceInput) {
        ImGui::EndDisabled();
    }

    const bool developPayloadChanged = changed || !SameRawDevelopPayload(payloadBefore, payload);
    if (developPayloadChanged) {
        RecordRawDevelopInteraction(node.id);
        MarkRenderDirty(node.id);
    }
}
