#include "Editor/EditorModule.h"
#include "Project/RawNodeEditing.h"
#include "App/WorkspacePresentation.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/Internal/RawLab/RawLabToolPresentation.h"

using namespace Stack::Editor::RawLabInternal;

bool EditorModule::ResetRawWorkspaceActiveTool(RawWorkspaceEditContext& context) {
    if (const auto* editor = SelectedNodeEditor())
        return editor->reset ? editor->reset(*this,context) : false;
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
    if (IsBracketingToolActive()) {
        RenderBracketingSidePanelUI();
        return;
    }
    RenderRawWorkspaceLabCommandStrip();
    const bool activeSource = !m_Project->rawSourceKey.empty() &&
        (IsBracketingToolActive() || IsRawWorkspaceProjectActive());
    const auto* source = FindRawWorkspaceSourceByKey(activeSource
        ? m_Project->rawSourceKey : m_RawWorkspace.selectedSourceKey);
    RawWorkspaceEditContext context;
    BeginRawWorkspaceEditContext(source, context);
    const auto* selected = SelectedEditingNode();
    ImGui::TextUnformatted(selected ? selected->title.c_str() : LabToolName(m_RawWorkspaceLabUi.activeTool));
    if (!context.rawAdjustmentLayerId.empty()) {
        const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(), context.rawAdjustmentLayerId);
        if (layer) ImGui::TextDisabled("%s", layer->name.c_str());
    }
    RenderRawOperationInstancePicker(context);
    context.editorState=&m_NodeEditorState;
    const auto* editor = SelectedNodeEditor();
    const bool bracket = m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame;
    if (!bracket && !context.canEdit) {
        ImGui::TextWrapped("%s", context.error.empty() ? "Choose an image from Gallery to edit." : context.error.c_str());
        return;
    }
    bool changed = false;
    if (!bracket && (editor ? editor->reset != nullptr : m_RawWorkspaceLabUi.activeTool != RawLabTool::Light) &&
        BareTextButton("Reset", false, context.canEdit))
        changed |= ResetRawWorkspaceActiveTool(context);
    ImGui::Spacing();
    ImGui::BeginDisabled(!context.canEdit && !bracket);
    if (editor) {
        if (editor->settings) changed |= editor->settings(*this,context);
    } else if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Transform) {
        ImGui::TextWrapped("Rotate and flip controls remain beside the image.");
    } else changed |= RenderRawWorkspaceLabSecondaryControls(context);
    ImGui::EndDisabled();
    // The floating host ends shared gestures after all controls have drawn.
    // An idle Settings panel must not end an active graph or viewport drag.
    if (changed) CommitRawWorkspaceEditContext(context, true, ImGui::IsAnyItemActive());
}
