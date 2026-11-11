#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/NodeEditors/ColorWarpFeedback.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Renderer/GLHelpers.h"
#include "Restormer/RestormerClient.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/FloatingSlider.h"
#include "Utils/RawGallerySelectionVisuals.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Stack::Editor::RawLabInternal;
using RawCurveGraphUiState = Stack::EditorModuleTypes::RawCurveGraphUiState;

bool EditorModule::RenderRawWorkspaceLabColorSurface(
    RawWorkspaceEditContext& context, RawLabControlSection section) {
    const auto owner = ColorWarpAreaOwner(context);
    const auto& previewIdentity = owner.sourceKey;
    std::size_t cloudInputFingerprint = 0;
    const auto& scope = ColorWarpInput(context, cloudInputFingerprint);
    RawLabColorSurfaceArgs args {
        m_RawWorkspaceLabUi,
        context.recipe.colorWarp,
        context.recipe.technical.workingSpace,
        scope,
        cloudInputFingerprint,
        previewIdentity,
        [this]() { SaveRawWorkspaceAppState(); }
    };
    if(!context.nodeTarget || !context.editorState)return false;
    ImGuiExtras::FloatingSliderScope sliders(context.State<ImGuiExtras::FloatingSliderState>());
    args.state=&context.State<Stack::Editor::ColorWarpEditorState>();
    args.nodeRevision=context.nodeRevision;
    args.panel=context.presentation==Stack::Editor::NodeEditorPresentation::Panel;
    if(args.panel) {
        args.panelControls=&context.State<Stack::Editor::PanelControlState>();
        const int family=args.panelControls->selectedFamily;
        if(family>=0)for(std::size_t i=0;i<context.recipe.colorWarp.pins.size();++i)
            if(context.recipe.colorWarp.pins[i].family==family)args.ui.selectedColorWarpPin=static_cast<int>(i);
    }
    args.strengthDriven = context.IsParameterDriven("strength");
    args.areaOwner = owner;
    args.areaEditingAllowed = context.canEdit;
    if (context.nodeTarget && context.editorState) {
        args.feedback = &context.State<Stack::Editor::ColorWarpFeedback>();
        const bool needsFields=std::any_of(context.recipe.colorWarp.pins.begin(),context.recipe.colorWarp.pins.end(),[](const auto& pin){return pin.smoothSelection>0;}) || args.ui.colorWarpDiagnosticLocked || ImGui::IsKeyDown(ImGuiKey_H) ||
            std::any_of(context.recipe.colorWarp.regions.begin(),context.recipe.colorWarp.regions.end(),
                [](const auto& region){return region.spatialMode!=Stack::RawRecipe::RawColorWarpSpatialMode::AllMatches;});
        if(needsFields && (!args.panel || args.ui.colorWarpDiagnosticLocked || ImGui::IsKeyDown(ImGuiKey_H)))args.feedback->Update(scope, previewIdentity + context.nodeTarget->Key(),
            cloudInputFingerprint, context.recipe.colorWarp,
            context.recipe.technical.workingSpace,
            [this](std::function<void()> job) {
                return ProjectTasks().SubmitHighPriority("Analyzing colors", std::move(job));
            }, context.nodeRevision);
    }
    bool changed = ShowsRawLabGraph(section) && Stack::Editor::RawLabInternal::RenderRawLabColorSurface(args);
    if (ShowsRawLabSettings(section)) changed |= Stack::Editor::RawLabInternal::RenderRawLabColorSettings(args);
    if(!args.panel && args.ui.selectedColorWarpPin>=0 && args.ui.selectedColorWarpPin<int(context.recipe.colorWarp.pins.size()))
        context.State<Stack::Editor::PanelControlState>().selectedFamily=context.recipe.colorWarp.pins[args.ui.selectedColorWarpPin].family;
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabColorSecondaryControls(RawWorkspaceEditContext& context) {
    return RenderRawWorkspaceLabColorSurface(context,RawLabControlSection::Settings);
}
