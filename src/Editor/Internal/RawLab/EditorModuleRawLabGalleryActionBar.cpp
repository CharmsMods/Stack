#include "Editor/EditorModule.h"
#include "App/AppHeaderStyle.h"
#include "App/WorkspacePresentation.h"
#include "Library/LibraryControls.h"
#include "RawLabUiSupport.h"

#include <algorithm>
#include <array>
#include <string>

#include <imgui_internal.h>

namespace {
using Action = Stack::RawGalleryActions::Action;
using SortMode = Stack::RawWorkspace::RawGalleryFilmstripSortMode;
using Stack::Editor::RawLabInternal::LabTooltip;
namespace Ui = Stack::LibraryUi;
}

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
    const auto count = m_RawWorkspace.selectedSourceKeys.size() + m_RawWorkspaceLabSelectedProjectPaths.size();
    // Queue eligibility needs only selection and the connected service. Resolve
    // full action targets only on a click; do not rebuild indexes on every draw.
    const bool canQueue = count > 0 && m_RawGalleryQueueRequestHandler && !Stack::Workspace::IsPreview();
    const float unit = ImGui::GetFontSize() / 13.f;
    ImGui::PushID(nativeWindow ? "RawNativeGalleryActions" : "RawGalleryActions");
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID amountId = ImGui::GetID("SelectionAmount");
    float amount = storage->GetFloat(amountId, 0.f);
    const float target = canQueue ? 1.f : 0.f;
    if (Ui::ReducedMotion()) amount = target;
    else Stack::Header::ApproachMotion(amount, target, .20f);
    storage->SetFloat(amountId, amount);
    if (amount == 0.f) { ImGui::PopID(); return false; }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f * unit, 6.f * unit));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f * unit, 5.f * unit));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * amount);
    ImGui::BeginChild("SelectionActions", ImVec2(0, (ImGui::GetFrameHeight() + 12.f * unit) * amount),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (canQueue) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu selected", count);
        const float queueWidth = ImGui::CalcTextSize("Queue").x + ImGui::GetStyle().FramePadding.x * 2.f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - queueWidth));
        if (Ui::TextButton("Queue")) ExecuteRawGalleryAction(Action::AddToQueue, CaptureRawGalleryActionContext());
        LabTooltip("Add the selected images and projects to Queue");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopID();
    return false;
}

void EditorModule::RenderRawGallerySidebarControls(bool nativeWindow) {
    const float unit = ImGui::GetFontSize() / 13.f;
    ImGui::PushID(nativeWindow ? "NativeGallerySidebar" : "GallerySidebar");
    const bool canSort = !nativeWindow && m_RawWorkspaceGalleryContentMode ==
        Stack::RawWorkspace::GalleryContentMode::Gallery &&
        !m_RawWorkspace.workspaceRoot.empty() && CanEditRawWorkspaceFilmstripOrganization();
    const bool navigator = !nativeWindow && m_PermanentGalleryWorkspace;
    using FooterAction = Stack::Editor::RawLabInternal::GalleryNavigatorFooterAction;
    FooterAction requested = FooterAction::None;
    if (navigator) {
        const auto organization = m_RawWorkspaceManualGroupings.find(m_RawGalleryFolderTree.workspaceKey);
        const auto sort = organization != m_RawWorkspaceManualGroupings.end()
            ? organization->second.sortMode : SortMode::TimelineAll;
        const char* label = sort == SortMode::TimelineAll ? "Capture time" :
            sort == SortMode::TimelineByFolder ? "Capture time by folder" :
            Stack::RawWorkspace::RawGalleryFilmstripSortModeLabel(sort);
        requested = m_RawGalleryNavigator.RenderFooter(m_RawWorkspaceLabUi.galleryWorkspaceGrid,
            m_RawWorkspaceLabUi.galleryPropertiesOpen, canSort, label);
        if (requested == FooterAction::Grid) SetRawGalleryGridView(true);
        if (requested == FooterAction::Filmstrip) SetRawGalleryGridView(false);
    }
    // Layout children have zero padding/spacing. Menus retain useful native
    // gutters and row targets rather than inheriting the folder-body layout.
    if (navigator) {
        const float scale = ImGui::GetFontSize() / 14.f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * scale, 6 * scale));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * scale, 12 * scale));
    }
    if (requested == FooterAction::Sort || (!navigator && Ui::IconButton("Sort", Ui::Icon::Sort, false, canSort))) ImGui::OpenPopup("Sort");
    if (!navigator && !canSort) LabTooltip("Open a folder and switch to original images to sort them.",
        ImGuiHoveredFlags_AllowWhenDisabled);
    if (ImGui::BeginPopup("Sort")) {
        const auto workspaceKey = m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
        const auto organization = m_RawWorkspaceManualGroupings.find(workspaceKey);
        const auto current = organization != m_RawWorkspaceManualGroupings.end()
            ? organization->second.sortMode : SortMode::TimelineAll;
        constexpr std::array modes {
            SortMode::TimelineAll, SortMode::TimelineByFolder, SortMode::Manual,
            SortMode::FileNameAscending, SortMode::FileNameDescending,
            SortMode::ModifiedNewestFirst, SortMode::ModifiedOldestFirst,
            SortMode::FolderThenNameAscending, SortMode::FolderThenNameDescending
        };
        for (const auto mode : modes) {
            if (ImGui::MenuItem(Stack::RawWorkspace::RawGalleryFilmstripSortModeLabel(mode),
                    nullptr, current == mode, canSort)) SetRawWorkspaceFilmstripSortMode(mode);
        }
        ImGui::EndPopup();
    }
    if (!navigator) {
        ImGui::SameLine(0, 8.f * unit);
        if (Ui::IconButton("View", Ui::Icon::View)) ImGui::OpenPopup("View");
    }
    if (ImGui::BeginPopup("View")) {
        if (nativeWindow) {
            for (const auto mode : {Stack::RawWorkspace::GalleryDisplayMode::Grid,
                    Stack::RawWorkspace::GalleryDisplayMode::List}) {
                if (ImGui::MenuItem(mode == Stack::RawWorkspace::GalleryDisplayMode::Grid ? "Grid" : "List",
                        nullptr, m_RawWorkspaceGalleryDisplayMode == mode)) {
                    m_RawWorkspaceGalleryDisplayMode = mode;
                    SaveRawWorkspaceAppState();
                }
            }
        } else {
            if (ImGui::MenuItem("Grid", nullptr, m_RawWorkspaceLabUi.galleryWorkspaceGrid)) SetRawGalleryGridView(true);
            if (ImGui::MenuItem("Filmstrip", nullptr, !m_RawWorkspaceLabUi.galleryWorkspaceGrid)) SetRawGalleryGridView(false);
        }
        ImGui::Separator();
        bool& properties = nativeWindow ? m_RawWorkspaceLabGalleryPanelsOpen : m_RawWorkspaceLabUi.galleryPropertiesOpen;
        if (ImGui::MenuItem("Image properties", nullptr, properties)) {
            properties = !properties;
            if (nativeWindow && !properties) CancelRawWorkspaceLabGalleryImagePreview();
        }
        ImGui::EndPopup();
    }
    if (!navigator) ImGui::SameLine(0, 8.f * unit);
    const bool properties = nativeWindow ? m_RawWorkspaceLabGalleryPanelsOpen : m_RawWorkspaceLabUi.galleryPropertiesOpen;
    if (requested == FooterAction::Properties || (!navigator && Ui::IconButton("Image properties", Ui::Icon::Details, properties))) {
        if (nativeWindow) {
            m_RawWorkspaceLabGalleryPanelsOpen = !properties;
            if (properties) CancelRawWorkspaceLabGalleryImagePreview();
        } else m_RawWorkspaceLabUi.galleryPropertiesOpen = !properties;
    }
    if (!navigator) {
        ImGui::SameLine(0, 8.f * unit);
        const float moreX = ImGui::GetWindowContentRegionMax().x - 34.f * unit;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), moreX));
    }
    if (requested == FooterAction::More || (!navigator && Ui::IconButton("More", Ui::Icon::More))) ImGui::OpenPopup("More");
    if (ImGui::BeginPopup("More")) {
        RenderAutoBracketControls(true);
        ImGui::Separator();
        RenderRawWorkspaceGalleryFileMenu(false);
        // Menus resolve their selection only while open. Execution retains the
        // existing catalog, project-protection and owner checks.
        const auto context = CaptureRawGalleryActionContext();
        const auto item = [&](Action action) {
            const auto availability = Stack::RawGalleryActions::GetAvailability(context, action);
            if (!availability.enabled) return;
            if (ImGui::MenuItem(Stack::RawGalleryActions::Label(action))) ExecuteRawGalleryAction(action, context);
        };
        if (!context.items.empty()) {
            ImGui::Separator();
            item(Action::CreateCaptureSet);
            item(Action::NewSavedVersion);
            item(Action::CopyPath);
            item(Action::TrashProjects);
            item(Action::RevertProjects);
        }
        ImGui::EndPopup();
    }
    if (navigator) ImGui::PopStyleVar(2);
    ImGui::PopID();
}

void EditorModule::RenderRawGalleryPropertyActions(const Stack::RawWorkspace::SourceRecord* source,
    const std::filesystem::path& projectPath) {
    const bool selected = !m_RawWorkspace.selectedSourceKeys.empty() || !m_RawWorkspaceLabSelectedProjectPaths.empty();
    auto context = selected ? CaptureRawGalleryActionContext() : Stack::RawGalleryActions::Context{};
    if (!selected) {
        if (!source && projectPath.empty()) return;
        context.workspaceRoot = m_RawWorkspace.workspaceRoot;
        context.catalogGeneration = m_RawWorkspaceScanGeneration;
        context.previewOnly = Stack::Workspace::IsPreview();
        Stack::RawGalleryActions::Item item;
        item.kind = projectPath.empty() ? Stack::RawGalleryActions::ItemKind::Source : Stack::RawGalleryActions::ItemKind::Project;
        item.path = projectPath.empty() ? source->absolutePath : projectPath;
        context.items.push_back(std::move(item));
    }
    if (context.items.empty()) return;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (context.items.size() > 1) ImGui::TextDisabled("%zu selected", context.items.size());
    else if (selected) {
        const auto& target = context.items.front();
        const auto displayedPath = projectPath.empty() && source ? source->absolutePath : projectPath;
        if (target.path != displayedPath) {
            ImGui::TextDisabled("Actions for selected image");
            ImGui::TextWrapped("%s", target.path.filename().u8string().c_str());
        }
    }
    const auto button = [&](const char* label, Action action, bool fullWidth = false) {
        const auto availability = Stack::RawGalleryActions::GetAvailability(context, action);
        if (Ui::TextButton(label, availability.enabled, fullWidth ? ImVec2(-1, 0) : ImVec2()))
            ExecuteRawGalleryAction(action, context);
        if (!availability.enabled) LabTooltip(availability.reason.c_str(), ImGuiHoveredFlags_AllowWhenDisabled);
    };
    if (selected) {
        const bool showCopy = m_RawWorkspaceGalleryContentMode != Stack::RawWorkspace::GalleryContentMode::Gallery &&
            context.focusedIndex < context.items.size() &&
            context.items[context.focusedIndex].kind == Stack::RawGalleryActions::ItemKind::Project;
        if (showCopy) {
            button("Copy edits", Action::CopyEdits);
            Stack::Editor::RawLabInternal::ContinueRawLabControlRow("Paste edits", 6.f);
        }
        button("Paste edits", Action::PasteEdits, !showCopy);
    }
    button("Open location", Action::ShowInExplorer, true);
}
