#include "Editor/EditorModule.h"
#include "Editor/NodeEditors/NodeEditorPanelPresentation.h"
#include "Utils/GlassControls.h"
#include "App/settings/AppearanceTheme.h"
#include "App/UiPerformanceTrace.h"
#include "App/UiPerformanceProbe.h"
#include "App/WorkspaceInputScope.h"
#include "Editor/Internal/RawLab/RawFloatingInputTrace.h"
#include "Editor/UI/WindowDrawOpacity.h"
#include "Library/LibraryControls.h"
#include "imgui_internal.h"
#include "Notifications/NotificationPresenter.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <optional>
#include <string_view>

void EditorModule::RenderRawFloatingControls(RawWorkspaceEditContext& context,
    const ImVec2& suppliedMinimum, const ImVec2& suppliedSize, bool enabled) {
    const bool shellBounds = IsViewMode() && m_ViewToolMaximum.x > m_ViewToolMinimum.x;
    const ImVec2 minimum(shellBounds ? std::max(suppliedMinimum.x,m_ViewToolMinimum.x) : suppliedMinimum.x,
        shellBounds ? std::max(suppliedMinimum.y,m_ViewToolMinimum.y) : suppliedMinimum.y);
    const ImVec2 size(std::max(1.f,suppliedMinimum.x+suppliedSize.x-minimum.x),
        std::max(1.f,suppliedMinimum.y+suppliedSize.y-minimum.y));
    Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::FloatingControls);
    auto& surface = m_RawFloatingSurface;
    surface.forwardsImageWheel = false;
    UpdateNodeEditorPanelVisibility();
    const auto* selectedDefinition=SelectedNodeEditor();
    if (m_NodeEditorPanel.visible || m_RawToolPickerAmount >= 1.f ||
        (!IsFocusedNodeEditor() && selectedDefinition && selectedDefinition->panelPrimary)) {
        surface.input={};surface.frame=-2;m_RawActiveControlFrame=-2;
        m_RawToolGlass->BeginFrame(false,{});
        return;
    }
    if (IsBracketingPresentationActive() ||
        (IsBracketingToolActive() && !HasBracketingContributionTool())) {
        // No invisible tool may retain pointer ownership while the project is
        // processing or when this source has no exposure levels to combine.
        surface.input = {};
        surface.frame = -1;
        m_RawActiveControlFrame = -1;
        m_RawToolGlass->BeginFrame(false,{});
        return;
    }
    UpdateNodeOpenTransition();
    const float openingOpacity = (1.f-m_RawToolPickerAmount) * m_NodeOpenTransition.ControlsOpacity(ImGui::GetTime()) *
        m_NodeEditorPanel.Reveal(ImGui::GetTime(),Stack::LibraryUi::ReducedMotion()) *
        (m_GalleryNavigationMotion ? m_GalleryNavigationMotion->ImageControls(m_GalleryNavigationWorkspace) : 1.f);
    if (openingOpacity <= .001f) {
        surface.input = {};
        surface.frame = -1;
        m_RawActiveControlFrame = -1;
        m_RawToolGlass->BeginFrame(false,{});
        return;
    }
    std::optional<Stack::Workspace::InputScope> openingInput;
    if (openingOpacity < 1.f) openingInput.emplace(true, false);
    surface.imageNavigationMenuOpen = false;
    const auto* definition = SelectedNodeEditor();
    if (definition && !definition->hasFloatingSurface) {
        // An image-handle-only editor must not leave an invisible floating
        // window intercepting input over the photograph.
        surface.input={};
        m_RawActiveControlFrame=-1;
        m_RawToolGlass->BeginFrame(false,{});
        return;
    }
    const auto* viewport = ImGui::GetMainViewport();
    const float scale = std::max(.75f, ImGui::GetFontSize() / 16.f);
    const float margin = std::min({12.f * scale, std::max(0.f, size.x) * .05f,
        std::max(0.f, size.y) * .05f});
    const ImVec2 available(std::max(1.f, size.x - margin * 2.f),
        std::max(1.f, size.y - margin * 2.f));
    const int tool = static_cast<int>(m_RawWorkspaceLabUi.activeTool);
    const auto key = RawNodeEditorStateKey();
    m_RawToolGlass->BeginFrame(m_Appearance && m_Appearance->GetGlassChromeEnabled(),key);
    if (!m_RawToolGlass->material.Error().empty() && !m_RawToolGlass->errorReported) {
        PostNotification(UiNotificationSeverity::Error,m_RawToolGlass->material.Error(),"raw-tool-glass");
        m_RawToolGlass->errorReported=true;
    }
    const auto measured = surface.contentHeights.find(key);
    const auto preferred = definition ? definition->preferredSize : ImVec2(400,560);
    const bool genericOperation = definition && !definition->primary;
    const float contentHeight = measured == surface.contentHeights.end() ? preferred.y * scale : measured->second;
    const ImVec2 extent(std::min(preferred.x * scale, available.x),
        std::min(std::max(80.f * scale, contentHeight), available.y));
    const ImVec2 lower(minimum.x + margin, minimum.y + margin);
    const ImVec2 upper(lower.x + std::max(0.f, available.x - extent.x),
        lower.y + std::max(0.f, available.y - extent.y));
    if (!surface.positioned || !surface.userPlaced) {
        surface.verticalOffset = (viewport->Size.y - extent.y) * .5f;
        surface.positioned = true;
    }
    // Keep horizontal placement relative to the usable area. Rail/panel
    // expansion moves and compresses that area; collapse reverses the move.
    // Do not replace the preferred placement when a narrow layout constrains it.
    surface.horizontalTravel = upper.x - lower.x;
    ImVec2 position(lower.x + surface.horizontalTravel * surface.horizontalPlacement,
        std::clamp(viewport->Pos.y + surface.verticalOffset, lower.y, upper.y));
    char name[80];
    std::snprintf(name, sizeof(name), "##RawFloatingControls_%p", static_cast<void*>(this));
    const auto* controls = ImGui::FindWindowByID(ImHashStr(name));
    const bool imageNavigation = IsRawImageNavigationActive();
    const bool canStart = enabled && controls && ImGui::GetCurrentContext()->HoveredWindow == controls &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !imageNavigation &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
    using namespace Stack::Editor::RawLabInternal;
    RawFloatingTraceContext traceContext;
    bool traceInput = false;
    if (FloatingInputTraceEnabled()) {
        const auto identity = [](std::string_view value) { return std::hash<std::string_view>{}(value); };
        traceContext = {this, identity(GetProjectDocumentId()),
            identity(context.source ? context.source->relativePathKey : context.multiFrameSourceSetId),
            identity(context.rawAdjustmentLayerId), identity(context.rawOperationUuid), tool,
            static_cast<int>(context.panelState.projectStatus), static_cast<int>(context.resolvedMode),
            enabled, context.canEdit, imageNavigation, canStart, genericOperation,
            context.editabilityTrace, context.error, context.panelState.readOnlyMessage};
        traceInput = BeginFloatingInputTrace(surface.trace, traceContext,
            static_cast<int>(surface.input.phase), surface.input.OwnsPointer());
    }
    const auto trace = [&](const char* stage) {
        if (traceInput) TraceFloatingInput(traceContext, static_cast<int>(surface.input.phase), stage);
    };
    trace("before-layout");
    const bool wasMoving = surface.input.phase == ImGuiExtras::FloatingSurfaceInput::Phase::Moving;
    std::optional<ImGuiExtras::FloatingSurfaceInputScope> input;
    input.emplace(surface.input, canStart, controls ? controls->MoveId : 0,!IsFocusedNodeEditor());
    if (surface.input.moving) {
        if (!wasMoving) {
            const auto origin = controls ? controls->Pos : position;
            surface.pointerOffset = ImVec2(surface.input.press.x-origin.x, surface.input.press.y-origin.y);
        }
        const auto pointer = ImGui::GetIO().MousePos;
        position = ImVec2(std::clamp(pointer.x-surface.pointerOffset.x, lower.x, upper.x),
            std::clamp(pointer.y-surface.pointerOffset.y, lower.y, upper.y));
        if (surface.horizontalTravel > 0.f)
            surface.horizontalPlacement = (position.x-lower.x) / surface.horizontalTravel;
        surface.verticalOffset = position.y-viewport->Pos.y;
        surface.userPlaced = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    // Resolve input before Begin(), so layout, clipping, drawing and pointer
    // ownership all use this frame's position. Do not move after layout.
    surface.minimum = position;
    surface.maximum = ImVec2(position.x + extent.x, position.y + extent.y);
    surface.frame = ImGui::GetFrameCount();
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(extent);
    ImGui::SetNextWindowViewport(viewport->ID);
    // A stable theme-colored backing keeps labels and plots legible over both
    // bright and dark photographs without another blur/capture pass.
    auto backing = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    backing.w = .96f;
    auto border = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    border.w = .32f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, backing);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f * scale, 8.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
        ImGui::GetStyle().Alpha * m_RawWorkspaceLabEditingSurfaceReveal);
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        (openingOpacity < 1.f ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None);
    if (ImGui::Begin(name, nullptr, flags)) {
        ImGui::BeginDisabled(!enabled || imageNavigation);
        trace("routed");
        ImGui::PushID(key.c_str());
        {
            ImGuiExtras::GlassControlScope glass(*m_RawToolGlass,openingOpacity,false,ImGuiExtras::GlassElements::FloatingTools);
            bool switched=false;
            if(context.nodeTarget && !IsFocusedNodeEditor()) {
                RefreshNodeEditorPanel();
                for(auto& group:m_NodeEditorPanel.groups) {
                    if(group.instances.size()<2)continue;
                    const auto current=std::find_if(group.instances.begin(),group.instances.end(),
                        [&](const auto& instance){return instance.target==*context.nodeTarget;});
                    if(current==group.instances.end())continue;
                    if(group.selectedKey!=current->key)group.selectedKey=current->key;
                    bool unusedToggle=false;
                    const int activated=Stack::Editor::DrawControlsPanelHeader(group,
                        ImGui::GetContentRegionAvail().x,nullptr,unusedToggle);
                    if(activated>=0 && activated<int(group.instances.size()) &&
                        group.instances[activated].target!=*context.nodeTarget && FinishWorkspaceInteraction()) {
                        const auto target=group.instances[activated].target;
                        switched=ActivateNodeEditorPanelTarget(target);
                        if(switched) {group.selectedKey=group.instances[activated].key;group.measuredHeight=0;}
                    }
                    break;
                }
            }
            // The incoming instance receives a fresh context on the next frame.
            if(!switched) {
                RenderRawWorkspaceActiveControls(context);
                const auto* window=ImGui::GetCurrentWindow();
                surface.contentHeights[key]=std::max(80.f,
                    window->DC.CursorMaxPos.y-window->DC.CursorStartPos.y+16.f);
            }
        }
        ImGui::PopID();
        trace("control");
        ImGui::EndDisabled();
    }
    if (openingOpacity < 1.f) {
        // Fade the completed geometry once, including custom plots with explicit
        // colors. Style alpha alone misses those and double-dims other widgets.
        Stack::Editor::ApplyWindowDrawOpacity(ImGui::GetCurrentWindow(),openingOpacity);
    }
    input.reset();
    if(IsFocusedNodeEditor() && m_NodeEditorPanel.focusEnteredFrame<ImGui::GetFrameCount() &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Middle) &&
        !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))
        ExitFocusedNodeEditor();
    surface.forwardsImageWheel = enabled && openingOpacity == 1.f && ImGui::GetIO().MouseWheel != 0.f &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        !surface.input.OwnsPointer() && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId) &&
        ImGui::TestKeyOwner(ImGuiKey_MouseWheelY,ImGuiKeyOwner_NoOwner);
    surface.imageWheel = surface.forwardsImageWheel ? ImGui::GetIO().MouseWheel : 0.f;
    surface.imageWheelPointer = ImGui::GetIO().MousePos;
    trace("restored");
    if (ImGui::GetCurrentContext()->OpenPopupStack.empty())
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Controls,
        GetRenderRevision() ^ Stack::UiPerformance::Probe::Key(position.x,position.y),
        {position.x,position.y,extent.x,extent.y});
    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(3);
}

bool EditorModule::RawFloatingSurfaceOwnsPointer() const {
    const auto& surface = m_RawFloatingSurface;
    if (surface.frame == ImGui::GetFrameCount() && surface.input.OwnsPointer()) return true;
    auto& gui = *ImGui::GetCurrentContext();
    char name[80];
    std::snprintf(name, sizeof(name), "##RawWorkspaceSection_%p", static_cast<const void*>(this));
    auto* section = ImGui::FindWindowByID(ImHashStr(name));
    const auto belongsToSection = [section](ImGuiWindow* window) {
        return section && section->Active && window && ImGui::IsWindowChildOf(window, section, true, true);
    };
    if ((gui.ActiveId && belongsToSection(gui.ActiveIdWindow)) ||
        belongsToSection(gui.HoveredWindow)) return true;
    if (IsRawImageNavigationActive()) return false;
    if (surface.frame != ImGui::GetFrameCount()) return false;
    std::snprintf(name, sizeof(name), "##RawFloatingControls_%p", static_cast<const void*>(this));
    auto* controls = ImGui::FindWindowByID(ImHashStr(name));
    const auto belongsToControls = [controls](ImGuiWindow* window) {
        return controls && window && ImGui::IsWindowChildOf(window, controls, true, true);
    };
    // Menus can extend beyond the panel. Retain ownership during a drag and
    // over descendant popups so direct canvas polling cannot consume it too.
    if ((gui.ActiveId && belongsToControls(gui.ActiveIdWindow)) ||
        belongsToControls(gui.HoveredWindow)) return true;
    return ImRect(surface.minimum, surface.maximum).Contains(ImGui::GetIO().MousePos);
}
