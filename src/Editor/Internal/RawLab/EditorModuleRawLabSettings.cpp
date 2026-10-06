#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/Internal/RawLab/RawLabToolPresentation.h"

using namespace Stack::Editor::RawLabInternal;

bool EditorModule::ResetRawWorkspaceActiveTool(RawWorkspaceEditContext& context) {
    switch (m_RawWorkspaceLabUi.activeTool) {
        case RawLabTool::Transform:
            context.recipe.cropRotation = {};
            break;
        case RawLabTool::Denoise:
            context.recipe.technical.mosaicDenoise =
                Raw::RawMosaicDenoiseSettings {};
            break;
        case RawLabTool::RgbDenoise:
            context.recipe.rgbDenoise =
                Stack::RawRecipe::RawRgbDenoiseRecipe {};
            break;
        case RawLabTool::Zones:
            if (m_RawWorkspaceLabUi.zonesTargetedView && !m_RawWorkspaceLabUi.zoneAreas.legacy) {
                for (auto& area : context.recipe.localRange.areas) if (area.id == m_RawWorkspaceLabUi.zoneAreas.selectedId) {
                    area.offsetEv = 0;
                    for (auto& point : area.points) { point.deltaEv = 0; point.incoming = {}; point.outgoing = {}; }
                }
            } else if (m_RawWorkspaceLabUi.zonesTargetedView) {
                const auto zoneIt = std::find_if(
                    context.recipe.localRange.targetZones.begin(),
                    context.recipe.localRange.targetZones.end(),
                    [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                        return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
                    });
                if (zoneIt != context.recipe.localRange.targetZones.end()) {
                    zoneIt->deltaEv = 0.0f;
                }
            } else {
                context.recipe.localRange =
                    Stack::RawRecipe::ApplyLocalRangePreset(
                        context.recipe.localRange,
                        Stack::RawRecipe::RawLocalRangePreset::Reset);
            }
            break;
        case RawLabTool::Exposure:
            context.recipe.preToneExposureEv = 0;
            break;
        case RawLabTool::Detail:
            context.recipe.detailContrast = {};
            break;
        case RawLabTool::Tone: {
            if (m_RawWorkspaceLabUi.sceneToneView < 2) {
                context.recipe.finishTone.layerJson["luminanceTone"] = Stack::RawRecipe::SerializeSceneTone({});
                break;
            }
            Stack::RawRecipe::RawPointCurveSet resetSet;
            for (auto& component : resetSet.curves) {
                component.points = {
                    { 0.0f, 0.0f, 1 },
                    { 1.0f, 1.0f, 1 }
                };
            }
            Stack::RawRecipe::StorePointCurveSetInFinishToneJson(
                context.recipe.finishTone.layerJson,
                resetSet);
            m_RawWorkspaceLabUi.selectedTonePoint = -1;
            break;
        }
        case RawLabTool::Color:
            context.recipe.colorWarp =
                Stack::RawRecipe::RawColorWarpRecipe {};
            m_RawWorkspaceLabUi.selectedColorWarpPin = -1;
            m_RawWorkspaceLabUi.colorWarpProvisionalPinValid = false;
            m_RawWorkspaceLabUi.colorWarpProvisionalSceneEvValid = false;
            m_RawWorkspaceLabUi.colorWarpDraggingProvisional = false;
            break;
        case RawLabTool::View:
        {
            const bool enabled =
                Stack::RawRecipe::IsViewTransformEnabled(context.recipe);
            context.recipe.viewTransform.layerJson =
                Stack::RawRecipe::DefaultViewTransformJson();
            context.recipe.viewTransform.layerJson["enabled"] = enabled;
            break;
        }
        default:
            return false;
    }
    return true;
}

void EditorModule::RenderRawWorkspaceToolSettings() {
    const bool activeSource = !m_Project->rawSourceKey.empty() &&
        (IsBracketingToolActive() || IsRawWorkspaceProjectActive());
    const auto* source = FindRawWorkspaceSourceByKey(activeSource
        ? m_Project->rawSourceKey : m_RawWorkspace.selectedSourceKey);
    RawWorkspaceEditContext context;
    BeginRawWorkspaceEditContext(source, context);
    ImGui::TextUnformatted(LabToolName(m_RawWorkspaceLabUi.activeTool));
    if (!context.rawAdjustmentLayerId.empty()) {
        const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(), context.rawAdjustmentLayerId);
        if (layer) ImGui::TextDisabled("%s", layer->name.c_str());
    }
    RenderRawOperationInstancePicker(context);
    const bool bracket = m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame;
    if (!bracket && !context.canEdit) {
        ImGui::TextWrapped("%s", context.error.empty() ? "Choose an image from Gallery to edit." : context.error.c_str());
        return;
    }
    bool changed = false;
    if (!bracket && m_RawWorkspaceLabUi.activeTool != RawLabTool::Light &&
        m_RawWorkspaceLabUi.activeTool != RawLabTool::Calibration && BareTextButton("Reset", false, context.canEdit))
        changed |= ResetRawWorkspaceActiveTool(context);
    ImGui::Spacing();
    ImGui::BeginDisabled(!context.canEdit && !bracket);
    switch (m_RawWorkspaceLabUi.activeTool) {
    case RawLabTool::Zones:
        changed |= RenderRawWorkspaceLabZonesSurface(context, RawLabControlSection::Settings);
        changed |= RenderRawWorkspaceLabZonesSecondaryControls(context);
        break;
    case RawLabTool::Tone:
        changed |= RenderRawWorkspaceLabToneSurface(context, RawLabControlSection::Settings);
        break;
    case RawLabTool::Detail:
        changed |= DrawDetailContrastEditor(context.recipe.detailContrast, m_RawWorkspaceLabUi.detailContrastEditor,
            [this](const auto& id) { return IsRawParameterDriven(id); }, RawLabControlSection::Settings);
        break;
    case RawLabTool::Color:
        changed |= RenderRawWorkspaceLabColorSurface(context, RawLabControlSection::Settings);
        changed |= RenderRawWorkspaceLabColorSecondaryControls(context);
        break;
    case RawLabTool::Exposure:
        ImGui::TextWrapped("Adjust exposure with the floating Exposure control or drag empty space in the Local EV graph.");
        break;
    case RawLabTool::Calibration:
        ImGui::TextWrapped("Calibration controls remain beside the image.");
        break;
    case RawLabTool::Transform:
        ImGui::TextWrapped("Rotate and flip controls remain beside the image.");
        break;
    default:
        changed |= RenderRawWorkspaceLabSecondaryControls(context);
        break;
    }
    ImGui::EndDisabled();
    // The floating host ends shared gestures after all controls have drawn.
    // An idle Settings panel must not end an active graph or viewport drag.
    if (changed) CommitRawWorkspaceEditContext(context, true, ImGui::IsAnyItemActive());
    ImGui::BeginDisabled(!context.canEdit);
    switch (m_RawWorkspaceLabUi.activeTool) {
    case RawLabTool::Zones: RenderRawLayerMaskAttachment("Local EV mask"); break;
    case RawLabTool::Tone: RenderRawLayerMaskAttachment("Tone Curve mask"); break;
    case RawLabTool::Detail: RenderRawLayerMaskAttachment("Detail mask"); break;
    case RawLabTool::Color: RenderRawLayerMaskAttachment("Color Warp mask"); break;
    case RawLabTool::Exposure: RenderRawLayerMaskAttachment("Exposure mask"); break;
    case RawLabTool::Calibration: RenderRawLayerMaskAttachment("Calibration mask"); break;
    default: break;
    }
    ImGui::EndDisabled();
}
