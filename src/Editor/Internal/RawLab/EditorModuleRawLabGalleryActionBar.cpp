#include "Editor/EditorModule.h"
#include "App/AppHeaderStyle.h"
#include "RawLabUiSupport.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>

#include <imgui_internal.h>

namespace {

using Action = Stack::RawGalleryActions::Action;
using SortMode = Stack::RawWorkspace::RawGalleryFilmstripSortMode;
using Stack::Editor::RawLabInternal::BareTextButton;
using Stack::Editor::RawLabInternal::LabTooltip;

float GalleryButtonWidth(const char* label, bool menu = false) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f +
        (menu ? ImGui::GetFontSize() : 0.0f);
}

bool GalleryMenuButton(const char* label, const char* popup, bool enabled = true) {
    const std::string buttonLabel = std::string(label) + "   ##" + popup;
    const bool pressed = BareTextButton(buttonLabel.c_str(), false, enabled,
        ImVec2(GalleryButtonWidth(label, true), 0.0f));
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const float arrow = ImGui::GetFontSize() * 0.20f;
    const ImVec2 center(maximum.x - ImGui::GetStyle().FramePadding.x - arrow,
        (minimum.y + maximum.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(center.x - arrow, center.y - arrow * 0.5f),
        ImVec2(center.x + arrow, center.y - arrow * 0.5f),
        ImVec2(center.x, center.y + arrow * 0.5f),
        ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    if (pressed) ImGui::OpenPopup(popup);
    return pressed;
}

} // namespace

void EditorModule::SetRawGalleryGridView(bool grid) {
    if (m_RawWorkspaceLabUi.galleryWorkspaceGrid == grid) return;
    std::string preferred = m_RawWorkspaceLabFilmstripHoverSourceKey;
    if (!m_RawWorkspaceLabFilmstripHoverProjectPath.empty()) {
        for (const auto& project : GetRawWorkspaceGalleryPresentation().projects) {
            if (project.projectPath.lexically_normal() ==
                m_RawWorkspaceLabFilmstripHoverProjectPath.lexically_normal()) {
                preferred = "project-overlay:" + (!project.projectId.empty()
                    ? project.projectId : project.projectPath.lexically_normal().generic_string());
                break;
            }
        }
    }
    m_RawWorkspaceGalleryLayoutAnimation.PrepareSwitch(preferred);
    m_RawWorkspaceLabUi.galleryWorkspaceGrid = grid;
    if (grid) m_RawWorkspaceGalleryDisplayMode = Stack::RawWorkspace::GalleryDisplayMode::Grid;
}

bool EditorModule::RenderRawWorkspaceGalleryActionBar(bool nativeWindow) {
    const auto context = CaptureRawGalleryActionContext();
    const float unit = ImGui::GetFontSize() / 13.0f;
    const float rowHeight = ImGui::GetFontSize() + 12.0f * unit;
    const float statusHeight = ImGui::GetFontSize() + 8.0f * unit;
    bool workspaceChanged = false;
    ImGui::PushID(nativeWindow ? "RawNativeGalleryActions" : "RawGalleryActions");
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        Stack::Header::ResolvePanelColor(GetWorkspaceBaseColor()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * unit, 6.0f * unit));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * unit, 4.0f * unit));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * unit, 5.0f * unit));
    ImGui::BeginChild("CommandBar", ImVec2(0.0f, rowHeight + statusHeight + 16.0f * unit),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const auto actionMenuItem = [&](Action action) {
        const auto availability = Stack::RawGalleryActions::GetAvailability(context, action);
        if (ImGui::MenuItem(Stack::RawGalleryActions::Label(action), nullptr, false,
                availability.enabled)) {
            ExecuteRawGalleryAction(action, context);
        }
        if (!availability.enabled)
            LabTooltip(availability.reason.c_str(), ImGuiHoveredFlags_AllowWhenDisabled);
    };
    const auto actionButton = [&](const char* label, Action action) {
        const auto availability = Stack::RawGalleryActions::GetAvailability(context, action);
        if (BareTextButton(label, false, availability.enabled))
            ExecuteRawGalleryAction(action, context);
        LabTooltip(availability.enabled ? Stack::RawGalleryActions::Label(action)
            : availability.reason.c_str(), ImGuiHoveredFlags_AllowWhenDisabled);
    };
    const auto folderMenuItems = [&] {
        if (ImGui::MenuItem("Open RAW Folder...")) {
            OpenRawWorkspaceFolderDialog();
            workspaceChanged = true;
        }
        const bool canRescan = !context.workspaceRoot.empty() && !IsRawWorkspaceScanBusy();
        if (ImGui::MenuItem("Rescan", nullptr, false, canRescan)) RescanRawWorkspace();
        if (!canRescan) LabTooltip(context.workspaceRoot.empty()
            ? "Open a RAW folder first." : "Wait for the current folder scan to finish.",
            ImGuiHoveredFlags_AllowWhenDisabled);
    };
    const auto newMenuItems = [&] {
        actionMenuItem(Action::CreateCaptureSet);
        actionMenuItem(Action::NewSavedVersion);
    };
    const bool originalGallery = m_RawWorkspaceGalleryContentMode ==
        Stack::RawWorkspace::GalleryContentMode::Gallery;
    const bool canSort = !nativeWindow && originalGallery &&
        !context.workspaceRoot.empty() && CanEditRawWorkspaceFilmstripOrganization();
    const char* sortUnavailable = nativeWindow
        ? "Sort original captures in the main Gallery."
        : !originalGallery ? "Switch to Gallery to sort original captures."
        : context.workspaceRoot.empty() ? "Open a RAW folder first."
        : "Wait for the current folder scan to finish.";
    const auto sortMenuItems = [&] {
        const std::string workspaceKey = context.workspaceRoot.lexically_normal().generic_string();
        const auto organization = m_RawWorkspaceManualGroupings.find(workspaceKey);
        const auto currentSort = organization != m_RawWorkspaceManualGroupings.end()
            ? organization->second.sortMode : SortMode::TimelineAll;
        constexpr std::array modes {
            SortMode::TimelineAll, SortMode::TimelineByFolder, SortMode::Manual,
            SortMode::FileNameAscending, SortMode::FileNameDescending,
            SortMode::ModifiedNewestFirst, SortMode::ModifiedOldestFirst,
            SortMode::FolderThenNameAscending, SortMode::FolderThenNameDescending
        };
        for (const auto mode : modes) {
            if (ImGui::MenuItem(Stack::RawWorkspace::RawGalleryFilmstripSortModeLabel(mode),
                    nullptr, currentSort == mode, canSort))
                SetRawWorkspaceFilmstripSortMode(mode);
        }
    };
    const auto detailsSelected = [&] {
        return nativeWindow ? m_RawWorkspaceLabGalleryPanelsOpen
            : m_RawWorkspaceLabUi.galleryPropertiesOpen;
    };
    const auto toggleDetails = [&] {
        if (nativeWindow) {
            m_RawWorkspaceLabGalleryPanelsOpen = !m_RawWorkspaceLabGalleryPanelsOpen;
            if (!m_RawWorkspaceLabGalleryPanelsOpen) CancelRawWorkspaceLabGalleryImagePreview();
        } else {
            m_RawWorkspaceLabUi.galleryPropertiesOpen = !m_RawWorkspaceLabUi.galleryPropertiesOpen;
        }
    };
    const auto viewMenuItems = [&] {
        if (nativeWindow) {
            for (const auto mode : {Stack::RawWorkspace::GalleryDisplayMode::Grid,
                    Stack::RawWorkspace::GalleryDisplayMode::List}) {
                if (ImGui::MenuItem(mode == Stack::RawWorkspace::GalleryDisplayMode::Grid
                        ? "Grid" : "List", nullptr, m_RawWorkspaceGalleryDisplayMode == mode)) {
                    m_RawWorkspaceGalleryDisplayMode = mode;
                    SaveRawWorkspaceAppState();
                }
            }
        } else {
            if (ImGui::MenuItem("Grid", nullptr, m_RawWorkspaceLabUi.galleryWorkspaceGrid))
                SetRawGalleryGridView(true);
            if (ImGui::MenuItem("Filmstrip", nullptr, !m_RawWorkspaceLabUi.galleryWorkspaceGrid))
                SetRawGalleryGridView(false);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Details", nullptr, detailsSelected())) toggleDetails();
    };

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const auto widths = [&](std::initializer_list<const char*> labels, int menus) {
        float width = gap * static_cast<float>(labels.size() - 1u);
        for (const char* label : labels) width += GalleryButtonWidth(label);
        return width + menus * ImGui::GetFontSize();
    };
    const float dividerWidth = unit + gap;
    const float fullWidth = widths({"New", "Open folder", "Open", "Copy edits", "Paste edits",
        "Queue", "Sort", "View", "More", "Details"}, 4) + dividerWidth * 2.0f;
    const float compactWidth = widths({"New", "Open", "Queue", "Sort", "View", "More", "Details"}, 4) +
        dividerWidth * 2.0f;
    const float smallWidth = widths({"Open", "View", "More", "Details"}, 2) + dividerWidth;
    const bool full = availableWidth >= fullWidth;
    const bool compact = !full && availableWidth >= compactWidth;
    const bool small = !full && !compact && availableWidth >= smallWidth;
    bool rowStarted = false;
    const auto next = [&] {
        if (rowStarted) ImGui::SameLine();
        rowStarted = true;
    };
    const auto divider = [&] {
        next();
        ImGui::Dummy(ImVec2(unit, rowHeight - 2.0f * unit));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(minimum.x, minimum.y + 4.0f * unit),
            ImVec2(minimum.x, maximum.y - 4.0f * unit), ImGui::GetColorU32(ImGuiCol_Border));
    };
    if (full || compact) {
        next(); GalleryMenuButton("New", "NewMenu");
        if (ImGui::BeginPopup("NewMenu")) {
            newMenuItems();
            ImGui::EndPopup();
        }
    }
    if (full) {
        next();
        if (BareTextButton("Open folder")) {
            OpenRawWorkspaceFolderDialog();
            workspaceChanged = true;
        }
        LabTooltip("Open a RAW folder");
    }
    if (full || compact) divider();
    if (full || compact || small) {
        next(); actionButton("Open", Action::Open);
    }
    if (full) {
        next(); actionButton("Copy edits", Action::CopyEdits);
        next(); actionButton("Paste edits", Action::PasteEdits);
    }
    if (full || compact) {
        next(); actionButton("Queue", Action::AddToQueue);
        divider();
        next(); GalleryMenuButton("Sort", "SortMenu", canSort);
        if (!canSort) LabTooltip(sortUnavailable, ImGuiHoveredFlags_AllowWhenDisabled);
        if (ImGui::BeginPopup("SortMenu")) {
            sortMenuItems();
            ImGui::EndPopup();
        }
    }
    if (small) divider();
    if (full || compact || small) {
        next(); GalleryMenuButton("View", "ViewMenu");
        if (ImGui::BeginPopup("ViewMenu")) {
            viewMenuItems();
            ImGui::EndPopup();
        }
    }
    next(); GalleryMenuButton(full || compact || small ? "More" : "Actions", "MoreMenu");
    if (ImGui::BeginPopup("MoreMenu")) {
        folderMenuItems();
        ImGui::Separator();
        if (!full && !compact && ImGui::BeginMenu("New")) {
            newMenuItems();
            ImGui::EndMenu();
        }
        if (!full && !compact && !small) actionMenuItem(Action::Open);
        if (!full) {
            actionMenuItem(Action::CopyEdits);
            actionMenuItem(Action::PasteEdits);
        }
        if (!full && !compact) actionMenuItem(Action::AddToQueue);
        actionMenuItem(Action::CopyPath);
        actionMenuItem(Action::ShowInExplorer);
        ImGui::Separator();
        actionMenuItem(Action::TrashProjects);
        actionMenuItem(Action::RevertProjects);
        if (!full && !compact) {
            ImGui::Separator();
            if (ImGui::BeginMenu("Sort", canSort)) {
                sortMenuItems();
                ImGui::EndMenu();
            }
            if (!canSort) LabTooltip(sortUnavailable, ImGuiHoveredFlags_AllowWhenDisabled);
        }
        if (!full && !compact && !small && ImGui::BeginMenu("View")) {
            viewMenuItems();
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    if (full || compact || small) {
        ImGui::SameLine();
        const float detailsWidth = GalleryButtonWidth("Details");
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
            ImGui::GetWindowContentRegionMax().x - detailsWidth));
        if (BareTextButton("Details", detailsSelected())) toggleDetails();
        LabTooltip(nativeWindow ? "Show or hide image details and preview"
            : "Show or hide image properties");
    }

    const std::string rootLabel = context.workspaceRoot.empty()
        ? "No RAW folder open" : context.workspaceRoot.u8string();
    const std::string selectionLabel = std::to_string(context.items.size()) + " selected";
    const ImVec2 statusMinimum = ImGui::GetCursorScreenPos();
    const float statusWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float countWidth = ImGui::CalcTextSize(selectionLabel.c_str()).x;
    const float rootWidth = std::max(0.0f, statusWidth - countWidth - 12.0f * unit);
    ImGui::Dummy(ImVec2(statusWidth, statusHeight));
    auto* draw = ImGui::GetWindowDrawList();
    if (rootWidth > 0.0f) {
        const float rootRight = statusMinimum.x + rootWidth;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::RenderTextEllipsis(draw, statusMinimum,
            ImVec2(rootRight, statusMinimum.y + statusHeight), rootRight,
            rootLabel.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    }
    if (countWidth <= statusWidth) draw->AddText(
        ImVec2(statusMinimum.x + statusWidth - countWidth, statusMinimum.y),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), selectionLabel.c_str());
    LabTooltip(rootLabel.c_str());

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::PopID();
    return workspaceChanged;
}
