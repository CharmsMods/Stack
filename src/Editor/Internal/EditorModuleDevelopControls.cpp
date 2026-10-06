#include "Editor/EditorModule.h"

#include "Editor/Internal/EditorModuleDevelopDefaults.h"
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

    const EditorNodeGraph::Link* rawInput = m_Project->graph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
    const EditorNodeGraph::Node* rawSourceNode = rawInput ? m_Project->graph.FindNode(rawInput->fromNodeId) : nullptr;
    const Raw::RawMetadata emptyMetadata;
    const Raw::RawMetadata& metadata =
        (rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource)
            ? rawSourceNode->rawSource.metadata
            : emptyMetadata;

    EditorNodeGraph::RawDevelopPayload& payload = node.rawDevelop;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }
    const bool archivedAutoMode =
        payload.uiMode != EditorNodeGraph::RawDevelopUiMode::Manual;
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Manual;
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
    bool changed = archivedAutoMode;
    const bool showAdvancedManualControls = advanced;
    m_DevelopAutoGuidanceDrafts.erase(node.id);
    m_DevelopAutoSolveTriggerHashes.erase(node.id);
    m_DevelopAutoRawSolveTriggerHashes.erase(node.id);
    m_DevelopAutoRawCalibrationHashes.erase(node.id);

    ImGuiExtras::RichSectionLabel("DEVELOP", 4.0f);
    if (hasRawSourceInput) {
        ImGui::TextDisabled("%s", RawDisplayName(rawSourceNode->rawSource).c_str());
        ImGui::TextDisabled("Output: unclamped scene-linear float RGB");
    } else {
        ImGui::TextWrapped("Connect a RAW Source node to develop sensor data.");
    }

    ImGui::TextDisabled("Manual RAW development. Every creative adjustment remains directly editable.");

    if (!hasRawSourceInput) {
        ImGui::BeginDisabled();
    }

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

    if (!hasRawSourceInput) {
        ImGui::EndDisabled();
    }

    const bool developPayloadChanged = changed || !SameRawDevelopPayload(payloadBefore, payload);
    if (developPayloadChanged) {
        RecordRawDevelopInteraction(node.id);
        MarkRenderDirty(node.id);
    }
}
