#include "App/WorkspacePresentation.h"
#include "App/AppHeaderStyle.h"
#include "Editor/EditorModule.h"
#include "RawLabFilmstripEdgeFade.h"
#include "RawLabGalleryFilmstripStackWindow.h"
#include "Editor/Bracketing/BracketingGallery.h"
#include "Editor/Internal/RawLab/RawLabGalleryLayout.h"
#include "Editor/Internal/RawLab/RawLabGalleryGridSubmission.h"
#include "Editor/Internal/RawLab/RawLabGalleryHitTesting.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Library/LibraryControls.h"
#include "Persistence/ProjectIndex.h"
#include "Persistence/ClosetMerge.h"
#include "Project/ProjectPath.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/RawLoader.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/HoverTextScope.h"
#include "Utils/RawGallerySelectionVisuals.h"
#include "Utils/Utf8Text.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using Stack::Editor::RawLabInternal::FitLabImage;
using Stack::Editor::RawLabInternal::ComputeGalleryVisibleRange;
using Stack::Editor::RawLabInternal::ComputeFilmstripHorizontalLayout;
using Stack::Editor::RawLabInternal::FramelessTextButton;
using Stack::Editor::RawLabInternal::kRawLabFilmstripMaximumHeight;
using Stack::Editor::RawLabInternal::kRawLabFilmstripMinimumHeight;
using Stack::Editor::RawLabInternal::kRawLabFilmstripTileHeight;
using Stack::Editor::RawLabInternal::LabTooltip;

namespace {

// Gallery keys are literal paths, not ImGui labels. Preserve the entire key
// (including legal ##/### filename bytes) while retaining the parent scope.
void PushGalleryKeyID(const std::string& key) {
    ImGui::PushOverrideID(ImHashData(key.data(), key.size(),
        ImGui::GetCurrentWindow()->IDStack.back()));
}

// Keep native row interaction and identity, but treat file/project names as text.
bool GalleryListSelectable(const std::string& label, bool selected) {
    constexpr ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
    if (label.find("##") == std::string::npos)
        return ImGui::Selectable(label.c_str(), selected, flags);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str(), nullptr, false);
    ImVec2 textPos = window->DC.CursorPos;
    textPos.y += window->DC.CurrLineTextBaseOffset;
    const float available = window->WorkRect.Max.x - textPos.x;
    // SpanAvailWidth uses the native (truncated) label width, so retain the
    // explicit literal width when the full name is wider than the row.
    ImGuiSelectableFlags rowFlags = flags;
    if (textSize.x <= available) rowFlags |= ImGuiSelectableFlags_SpanAvailWidth;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    const bool pressed = ImGui::Selectable(label.c_str(), selected, rowFlags, textSize);
    ImGui::PopStyleColor();
    if (ImGui::IsItemVisible()) {
        const ImRect bounds(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::RenderTextClippedEx(window->DrawList, textPos,
            ImVec2(window->WorkRect.Max.x, textPos.y + textSize.y),
            label.c_str(), label.c_str() + label.size(), &textSize,
            ImGui::GetStyle().SelectableTextAlign, &bounds);
    }
    return pressed;
}

void DrawGalleryStackBadges(ImDrawList* drawList, const ImRect& area,
    std::size_t count, float opacity, bool completedBurst, bool aboveImage = false) {
    if (count > 1u) {
        const std::string countText =
            std::to_string(count);
        const ImVec2 countSize = ImGui::CalcTextSize(countText.c_str());
        constexpr float kBadgeInset = 6.0f;
        ImVec4 countColor =
            ImGui::GetStyleColorVec4(ImGuiCol_Text);
        countColor.w *= std::clamp(
            opacity,
            0.0f,
            1.0f);
        drawList->AddText(
            ImVec2(
                area.Max.x - kBadgeInset - countSize.x,
                area.Min.y + kBadgeInset),
            ImGui::GetColorU32(countColor),
            countText.c_str());
    }
    if (completedBurst) {
        const ImVec2 center(area.GetCenter().x, area.Min.y + (aboveImage ? -9.0f : 12.0f));
        ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.w *= std::clamp(opacity, 0.0f, 1.0f);
        const ImU32 tint = ImGui::GetColorU32(color);
        drawList->AddLine(ImVec2(center.x - 4.0f, center.y),
            ImVec2(center.x - 1.0f, center.y + 3.0f), tint, 1.8f);
        drawList->AddLine(ImVec2(center.x - 1.0f, center.y + 3.0f),
            ImVec2(center.x + 4.5f, center.y - 3.5f), tint, 1.8f);
    }
}

constexpr int kRawLabDetachedOpenGraceFrames = 120;
constexpr float kRawLabFilmstripScrollResponseSeconds = 0.11f;
constexpr float kRawLabGalleryCullPrefetchPixels = 240.0f;

float RawLabGalleryPrefetchPixels(float visibleSpan) {
    return std::clamp(
        visibleSpan * 0.35f,
        kRawLabGalleryCullPrefetchPixels,
        480.0f);
}
constexpr char kRawLabFilmstripSourcePayload[] =
    "STACK_RAW_FILMSTRIP_SOURCE";
} // namespace

bool EditorModule::RenderRawWorkspaceLabGalleryHeader(bool nativeWindow, bool sidebar) {
    ImGuiExtras::HoverTextScope noHoverText;
    LoadResourceTextures();

    auto switchHost = [&](RawGalleryHost host) {
        if (host == RawGalleryHost::Filmstrip && !m_PermanentGalleryWorkspace &&
            m_GalleryNavigationHandler) {
            CloseRawWorkspaceLabNativeGallery();
            m_GalleryNavigationHandler();
            return;
        }
        if (host != RawGalleryHost::Filmstrip) {
            m_RawWorkspaceLabUi.galleryWorkspaceOpen = false;
            m_RawWorkspaceLabFilmstripHoverSourceKey.clear();
            m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
            m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
            m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
            m_RawWorkspaceLabFilmstripHoverFrame = -1;
            m_RawWorkspaceLabFilmstripHoverSuppressed = false;
            m_RawWorkspaceLabFilmstripHoverOpacity = 0.0f;
            m_RawWorkspaceLabFilmstripHoverBlend = 1.0f;
            m_RawWorkspaceLabFilmstripDrawerState = {};
            m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = 0.0f;
            m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight = 0.0f;
            m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
            m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
            m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested = false;
            m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
            m_RawWorkspaceLabFilmstripDragState = {};
        }
        if (host == RawGalleryHost::NativeWindow) {
            m_RawWorkspaceLabAnimatedFilmstripHeight = 0.0f;
            m_RawWorkspaceLabFilmstripAnimationOpenHeight = 0.0f;
            m_RawWorkspaceLabAnimatedLowerShelfHeight = 0.0f;
            OpenRawWorkspaceLabNativeGallery();
            return;
        }
        if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
            CloseRawWorkspaceLabNativeGallery();
        }
        m_RawWorkspaceLabUi.galleryHost = host;
        if (host != RawGalleryHost::Closed) {
            m_RawWorkspaceLabUi.lastGalleryHost = host;
        }
        SaveRawWorkspaceAppState();
    };

    const bool creatingMfd =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
        RawGalleryNavigationMode::MultiFrameCreation;
    const bool browsingProjectRoot =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
        RawGalleryNavigationMode::ProjectRoot;
    const bool projectsContent = browsingProjectRoot &&
        m_RawWorkspaceGalleryContentMode !=
            Stack::RawWorkspace::GalleryContentMode::Gallery;
    const std::size_t selectedCount = projectsContent
        ? 0u
        : m_RawWorkspace.selectedSourceKeys.size();
    const char* createProjectLabel = "Create Capture Set";
    const bool compactFilmstrip = !nativeWindow;
    const float unit = ImGui::GetFontSize() / 13.f;
    const auto advanceHeaderControl = [&](float gap) {
        if (compactFilmstrip) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
        else ImGui::SameLine(0.f, gap);
    };
    namespace Ui = Stack::LibraryUi;
    if (!sidebar && !m_PermanentGalleryWorkspace) {
        if (Ui::IconButton("Gallery options", Ui::Icon::More)) ImGui::OpenPopup("RawGalleryOptionsMenu");
        if (browsingProjectRoot) ImGui::SameLine(0.f, 8.f * unit);
    }
    if (browsingProjectRoot) {
        const float width = 3.f * 34.f * unit + 2.f * 10.f * unit;
        if (sidebar || nativeWindow) ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
            std::max(0.f, (ImGui::GetContentRegionAvail().x - width) * .5f));
        const auto category = [&](const char* label, Ui::Icon icon, Stack::RawWorkspace::GalleryContentMode mode) {
            if (Ui::IconButton(label, icon, m_RawWorkspaceGalleryContentMode == mode) &&
                    m_RawWorkspaceGalleryContentMode != mode) {
                m_RawWorkspace.selectedSourceKeys.clear();
                m_RawWorkspaceLabSelectedProjectPaths.clear();
                InvalidateRawWorkspaceGalleryPresentation(false);
                m_RawWorkspaceGalleryContentMode = mode;
                SaveRawWorkspaceAppState();
            }
        };
        category("Original images", Ui::Icon::Gallery, Stack::RawWorkspace::GalleryContentMode::Gallery);
        ImGui::SameLine(0.f, 10.f * unit);
        category("Saved projects", Ui::Icon::Projects, Stack::RawWorkspace::GalleryContentMode::Projects);
        ImGui::SameLine(0.f, 10.f * unit);
        category("Brackets", Ui::Icon::Bracket, Stack::RawWorkspace::GalleryContentMode::Bracket);
        ImGui::SameLine(0.f, 10.f * unit);
        const bool browsingAll = m_RawWorkspaceGalleryContentMode == Stack::RawWorkspace::GalleryContentMode::AllProjects;
        category("Unlinked", Ui::Icon::Projects, Stack::RawWorkspace::GalleryContentMode::Unlinked);
        if (browsingAll || m_RawWorkspaceGalleryContentMode == Stack::RawWorkspace::GalleryContentMode::Unlinked) {
            if (ImGui::Button(m_GalleryComparisonName.empty() ? "Comparison image..." : m_GalleryComparisonName.c_str())) {
                const auto path=FileDialogs::OpenImageFileDialog("Choose a fixed comparison image");
                if (!path.empty()) ChooseGalleryComparison(std::filesystem::u8path(path));
            }
            if (!m_GalleryComparisonName.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Test pattern")) ChooseGalleryComparison({});
            }
            bool all = browsingAll;
            if (ImGui::Checkbox("All projects", &all)) {
                m_RawWorkspaceGalleryContentMode = all ? Stack::RawWorkspace::GalleryContentMode::AllProjects :
                    Stack::RawWorkspace::GalleryContentMode::Unlinked;
                m_RawWorkspaceLabSelectedProjectPaths.clear();
                InvalidateRawWorkspaceGalleryPresentation(false);
            }
        }
    }

    if (m_GalleryApplyChooseTarget) {
        ImGui::TextWrapped("Select a target original or linked project.");
        if (ImGui::SmallButton("Cancel apply")) {
            m_GalleryApplyChooseTarget=false;
            RefreshRawEditAttributeClipboardFromSystem();
        }
    }
    bool workspaceChanged = false;
    bool newFolder = false;
    if (ImGui::BeginPopup("RawGalleryOptionsMenu")) {
        if (ImGui::MenuItem("New photo folder...", nullptr, false,
                !m_RawWorkspace.workspaceRoot.empty() && !m_RawWorkspace.allSourceFolders)) {
            m_RawGalleryNewFolderName[0] = 0;
            newFolder = true;
        }
        if (!m_RawWorkspace.nestedClosets.empty() && ImGui::BeginMenu("Merge nested Closet")) {
            for (const auto& closet : m_RawWorkspace.nestedClosets) {
                if (!ImGui::MenuItem(closet.parent_path().u8string().c_str())) continue;
                const auto root = m_RawWorkspace.workspaceRoot;
                const auto owner = m_Project;
                ProjectTasks().Submit("Inspecting nested Closet", [this, owner, root, closet] {
                    Stack::Project::ClosetMergePlan plan;
                    std::string error;
                    const bool ok = Stack::Project::InspectClosetMerge(root, closet, plan, error);
                    owner->tasks.PostToMain([this, owner, ok, error, plan = std::move(plan)] {
                        if (!ok) { PostNotification(UiNotificationSeverity::Error, error, "closet-merge"); return; }
                        Stack::Notifications::NoticeSpec notice;
                        notice.message = "Merge " + std::to_string(plan.projects.size()) + " projects and " +
                            std::to_string(plan.authoredAssets.size()) + " shared payloads?";
                        notice.details = plan.nestedCloset.u8string();
                        for (const auto& file : plan.projects) notice.details += "\n" + file.filename().u8string();
                        for (const auto& name : plan.authoredAssets) notice.details += "\n" + name;
                        Stack::Notifications::ActionSpec accept;
                        accept.label = "Merge";
                        accept.invoke = [this, owner, plan] {
                            const bool submitted = ProjectTasks().Submit("Merging nested Closet", [this, owner, plan] {
                                std::string error;
                                const bool merged = Stack::Project::MergeCloset(plan, error);
                                owner->tasks.PostToMain([this, merged, error, root = plan.parentRoot] {
                                    PostNotification(merged ? UiNotificationSeverity::Success : UiNotificationSeverity::Error,
                                        merged ? "Nested Closet merged." : error, "closet-merge");
                                    RefreshRawGalleryAfterFileMove(root);
                                });
                            });
                            return submitted ? Stack::Notifications::ActionResult::Success() :
                                Stack::Notifications::ActionResult::Failure("Could not start the merge.");
                        };
                        notice.actions.push_back(std::move(accept));
                        Stack::Notifications::ActionSpec cancel;
                        cancel.label = "Cancel";
                        cancel.safeCancel = true;
                        cancel.invoke = [] { return Stack::Notifications::ActionResult::Success(); };
                        notice.actions.push_back(std::move(cancel));
                        RequestNotificationDecision(std::move(notice));
                    });
                });
            }
            ImGui::EndMenu();
        }
        RenderAutoBracketControls(true);
        ImGui::Separator();
        if (!nativeWindow) {
            workspaceChanged = RenderRawWorkspaceGalleryFileMenu(true);
            ImGui::Separator();
        }
        if (!m_PermanentGalleryWorkspace) {
            if (ImGui::MenuItem(
                    "Show as Filmstrip", nullptr,
                    m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Filmstrip,
                    !creatingMfd)) {
                switchHost(RawGalleryHost::Filmstrip);
            }
            if (!nativeWindow && ImGui::MenuItem(
                    "Pop Out Gallery", nullptr, nativeWindow, !creatingMfd)) {
                switchHost(RawGalleryHost::NativeWindow);
            }
        }
        ImGui::Separator();
        const bool hasEditorToReturnTo = IsRawWorkspaceProjectActive() ||
            IsMultiFrameRawProjectActive() || HasUnsavedBracketingDraft();
        if (!m_PermanentGalleryWorkspace && ImGui::MenuItem("Close Gallery", nullptr, false,
                !creatingMfd && hasEditorToReturnTo)) {
            switchHost(RawGalleryHost::Closed);
        }
        ImGui::EndPopup();
    }
    if (newFolder) ImGui::OpenPopup("New photo folder");
    if (ImGui::BeginPopupModal("New photo folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Enter a folder name relative to the current photo root.");
        const bool enter = ImGui::InputText("Folder", m_RawGalleryNewFolderName,
            sizeof(m_RawGalleryNewFolderName), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Create") || enter) {
            const auto relative = std::filesystem::u8path(m_RawGalleryNewFolderName).lexically_normal();
            const auto root = m_RawWorkspace.workspaceRoot;
            std::error_code ec;
            const auto target = root / relative;
            const bool safe = !relative.empty() && !relative.has_root_path() &&
                *relative.begin() != ".." && relative != "." &&
                std::none_of(relative.begin(), relative.end(), [](const auto& part) {
                    auto name = part.u8string();
                    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return char(std::tolower(c)); });
                    return name == "closet" || name.rfind(".stack-merged-", 0) == 0;
                }) && std::filesystem::weakly_canonical(target, ec) == target && !ec;
            if (safe && std::filesystem::create_directories(target, ec)) {
                RescanRawWorkspace();
                ImGui::CloseCurrentPopup();
            } else PostNotification(UiNotificationSeverity::Warning,
                "Choose an unused folder name inside the photo root.", "gallery-new-folder");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }


    if (creatingMfd) {
        advanceHeaderControl(8.0f);
        if (FramelessTextButton("Cancel")) {
            const bool returnToMultiFrame =
                m_ReturnToMultiFrameAfterGalleryCreationCancel;
            m_RawWorkspace.selectedSourceKey =
                m_RawWorkspaceGallerySelectionRestoreKey;
            m_RawWorkspace.selectedSourceKeys =
                m_RawWorkspaceGallerySelectionRestoreKeys;
            m_RawWorkspaceLabUi.galleryNavigationMode =
                m_RawWorkspaceGalleryRestoreNavigationMode;
            m_RawWorkspaceLabUi.galleryProjectId =
                m_RawWorkspaceGalleryRestoreProjectId;
            m_RawWorkspaceLabUi.galleryHost =
                m_RawWorkspaceGalleryRestoreHost;
            m_ReturnToMultiFrameAfterGalleryCreationCancel = false;
            InvalidateRawWorkspaceGalleryPresentation();
            if (returnToMultiFrame) {
                RequestOpenMultiFrameTab();
            }
        }
        advanceHeaderControl(8.0f);
        if (FramelessTextButton(
                createProjectLabel,
                false,
                selectedCount >= 1u &&
                    !IsMfdExperimentalProcessingBusy())) {
            m_RequestCreateMultiFrameFromGallerySelection = true;
        }
        LabTooltip(
            selectedCount < 1u
                ? "Select at least one compatible mosaiced RAW capture."
                : "Create a neutral RAW capture set. Processing nodes are chosen in Bracket.",
            ImGuiHoveredFlags_AllowWhenDisabled);
    }
    if (selectedCount > 0u &&
        IsMultiFrameRawProjectActive() &&
        !IsBracketingActive() && !IsBracketingToolActive() &&
        m_Project->snapshot) {
        const Stack::Project::MultiFrameSourceSet* activeSet =
            Stack::Project::FindSourceSet(
                *m_Project->snapshot,
                m_Project->snapshot->activeSourceSetId);
        const Stack::Project::ProjectLifecyclePhase phase =
            m_Project->lifecycle.Phase();
        const bool canAdd = activeSet != nullptr &&
            phase != Stack::Project::ProjectLifecyclePhase::Conflict &&
            phase != Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery &&
            !IsMfdExperimentalProcessingBusy();
        advanceHeaderControl(8.0f);
        if (FramelessTextButton("Add to Current Burst", false, canAdd)) {
            std::vector<std::filesystem::path> paths;
            paths.reserve(m_RawWorkspace.selectedSourceKeys.size());
            for (const std::string& key :
                 m_RawWorkspace.selectedSourceKeys) {
                if (const Stack::RawWorkspace::SourceRecord* source =
                        FindRawWorkspaceSourceByKey(key)) {
                    paths.push_back(source->absolutePath);
                }
            }
            std::string error;
            const bool added = !paths.empty() &&
                AddFramesToMultiFrameSourceSet(
                    activeSet->sourceSetId,
                    paths,
                    &error);
            m_RawWorkspaceLabUi.multiFrameStatusText = added
                ? "Selected Gallery frames embedded and verified."
                : error.empty() ? "No selected RAW frames were available."
                                : error;
        }
        LabTooltip(
            canAdd
                ? "Embed the selected Gallery RAW files into the currently open MFD burst."
                : "Resolve the project state or finish processing before adding frames.",
            ImGuiHoveredFlags_AllowWhenDisabled);
    }
    return workspaceChanged;
}

void EditorModule::RenderRawWorkspaceGalleryRevertPopup() {
    if (!m_RawWorkspaceRevertPopupRequested) return;
    m_RawWorkspaceRevertPopupRequested = false;
    namespace N = Stack::Notifications;
    const auto notifier = GetNotifier();
    const auto document = GetProjectDocumentId();
    const auto root = m_RawWorkspace.workspaceRoot;
    const auto remaining = std::make_shared<std::vector<std::filesystem::path>>(m_RawWorkspacePendingRevertProjectPaths);
    const double started = ImGui::GetTime();
    const auto valid = [this, document, root, remaining] {
        return GetProjectDocumentId() == document && m_RawWorkspace.workspaceRoot == root &&
            m_RawWorkspacePendingRevertProjectPaths == *remaining;
    };
    const auto execute = [this, root, remaining]() -> N::ActionResult {
        std::size_t moved = 0;
        std::vector<std::filesystem::path> failed;
        std::string error;
        for (const auto& path : *remaining) {
            const auto currentStore = Stack::Project::ResolveProjectStoreRoot(GetCurrentProjectFileName());
            if (Stack::Project::SameProjectPath(path, currentStore) ||
                (m_ProjectRemovalGuard && m_ProjectRemovalGuard(path))) {
                failed.push_back(path);
                if (error.empty()) error = "Close the project in all tabs before reverting it.";
                continue;
            }
            const auto result = Stack::RawGalleryFileActions::RevertProjectsToTrash({path}, root);
            moved += result.moved;
            if (!result) {
                failed.push_back(path);
                if (error.empty()) error = result.errors.empty() ? "The project could not be moved to Trash." : result.errors.front();
            }
        }
        *remaining = std::move(failed);
        m_RawWorkspacePendingRevertProjectPaths = *remaining;
        if (moved) {
            Stack::Project::ProjectIndex::Get().RebuildDefaultRoots();
            LibraryManager::Get().RequestRefreshLibraryAsync();
            m_RawWorkspaceLabSelectedProjectPaths.clear();
            m_RawWorkspaceLabFocusedProjectPath.clear();
            m_RawWorkspaceLabFocusedProjectName.clear();
            RescanRawWorkspace();
            InvalidateRawWorkspaceGalleryPresentation();
            PostNotification(UiNotificationSeverity::Success,
                "Reverted " + std::to_string(moved) + (moved == 1 ? " project." : " projects."), "raw-gallery-revert");
        }
        if (!remaining->empty()) return N::ActionResult::Failure(std::move(error));
        return N::ActionResult::Success();
    };
    N::NoticeSpec notice;
    notice.title = remaining->size() == 1 ? "Revert RAW project?" : "Revert RAW projects?";
    notice.message = "Move the saved project bundles to Trash? Original RAW captures remain in Gallery.";
    notice.route = N::Route::Center;
    notice.foreground = m_NotificationForeground;
    notice.operationId = notifier.NewOperation();
    notice.customBody = [remaining, started] {
        for (const auto& path : *remaining) ImGui::TextWrapped("%s", path.filename().string().c_str());
        const int seconds = std::max(0, static_cast<int>(std::ceil(3.0 - (ImGui::GetTime() - started))));
        if (seconds) ImGui::TextDisabled("Revert becomes available in %d...", seconds);
    };
    N::ActionSpec revert;
    revert.label = "Revert";
    revert.destructive = true;
    revert.canInvoke = [valid, remaining, started] {
        return valid() && !remaining->empty() && ImGui::GetTime() - started >= 3.0;
    };
    revert.invoke = [this, notifier, valid, execute] {
        if (RequestAutoBracketForeground("revert these projects", [notifier, valid, execute] {
                if (!notifier.Valid() || !valid()) {
                    return;
                }
                const auto activity = notifier.BeginActivity("Reverting projects", false);
                notifier.UpdateActivity(activity, "Moving project bundles to Trash...");
                const auto result = execute();
                if (result.state == N::ActionState::Failure) notifier.FailActivity(activity, result.message);
                else notifier.CompleteActivity(activity, "Projects reverted. Original RAW captures were retained.", false);
            })) {
            return N::ActionResult::Success();
        }
        return execute();
    };
    N::ActionSpec cancel;
    cancel.label = "Cancel";
    cancel.safeCancel = true;
    cancel.invoke = [this, remaining] {
        if (m_RawWorkspacePendingRevertProjectPaths == *remaining) m_RawWorkspacePendingRevertProjectPaths.clear();
        return N::ActionResult::Success();
    };
    notice.actions = {std::move(revert), std::move(cancel)};
    RequestNotificationDecision(std::move(notice));
}
void EditorModule::RenderRawWorkspaceLabGalleryContent(
    bool compactFilmstrip,
    bool expandedFilmstripDrawer,
    float filmstripDrawerExpansion) {
    ImGuiExtras::HoverTextScope noHoverText;
    auto& layoutMotion = m_RawWorkspaceGalleryLayoutAnimation;
    if (ImGui::GetCurrentWindow()->SkipItems) {
        return;
    }
    auto& gridBrowse = m_RawWorkspaceGalleryGridStacks;
    int gridGroup = -1;
    bool gridImageHovered = false;
    const std::string browsingScope = GetRawWorkspaceGalleryScopeKey() + ":" +
        std::to_string(static_cast<int>(m_RawWorkspaceGalleryContentMode)) + ":" +
        m_RawGalleryFolderFilter.value_or(std::string{});
    if (gridBrowse.scope != browsingScope) {
        gridBrowse.Close(); gridBrowse.scope = browsingScope;
    }
    if (!compactFilmstrip) {
        auto& scale = m_RawWorkspaceLabUi.galleryThumbnailScale;
        if (gridBrowse.scaleTarget < 0.f) gridBrowse.scaleTarget = scale;
        const auto& io = ImGui::GetIO();
        if (io.KeyCtrl && io.MouseWheel != 0.f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            gridBrowse.scaleTarget = std::clamp(gridBrowse.scaleTarget * std::exp(io.MouseWheel * .12f), .55f, 2.5f);
        if (scale != gridBrowse.scaleTarget && m_PermanentGalleryWorkspace) {
            layoutMotion.SetViewport(ImGui::GetCurrentWindow()->ClipRect);
            ImRect selectedBounds;
            const auto& selected=m_RawWorkspace.selectedSourceKey;
            const bool visible=layoutMotion.PresentedBounds(selected,selectedBounds) &&
                selectedBounds.Overlaps(ImGui::GetCurrentWindow()->ClipRect);
            layoutMotion.PrepareSwitch(visible ? selected : layoutMotion.ImageAt(io.MousePos));
        }
        if (Stack::LibraryUi::ReducedMotion()) scale = gridBrowse.scaleTarget;
        else if (scale != gridBrowse.scaleTarget) Stack::Header::Approach(scale, gridBrowse.scaleTarget, 18.f, .0005f);
    }
    if (!compactFilmstrip) {
        auto* window=ImGui::GetCurrentWindow();
        const auto bounds=window->InnerRect;
        const float unit=ImGui::GetFontSize()/13.f;
        const ImRect hit({bounds.Max.x-14.f*unit,bounds.Min.y},bounds.Max);
        const bool hover=hit.Contains(ImGui::GetIO().MousePos);
        const float width=(hover ? 10.f : 4.f)*unit;
        const ImRect rail({bounds.Max.x-width,bounds.Min.y},bounds.Max);
        ImS64 scroll=static_cast<ImS64>(window->Scroll.y);
        const ImS64 visible=static_cast<ImS64>(bounds.GetHeight());
        ImGui::ScrollbarEx(rail,window->GetID("GalleryGridScrollbar"),ImGuiAxis_Y,&scroll,
            visible,visible+static_cast<ImS64>(window->ScrollMax.y),ImDrawFlags_RoundCornersAll);
        if (scroll!=static_cast<ImS64>(window->Scroll.y)) {
            m_RawWorkspaceLabGalleryScrollTargetY=m_RawWorkspaceLabGalleryScrollCurrentY=float(scroll);
            ImGui::SetScrollY(float(scroll));
        }
    }
    const ImRect viewClip = ImGui::GetCurrentWindow()->ClipRect;
    if (m_PermanentGalleryWorkspace) layoutMotion.SetViewport(viewClip);
    const std::string movingImageAtPointer = layoutMotion.ImageAt(ImGui::GetIO().MousePos);
    bool gridAnchorApplied = false;
    const auto applyGridAnchor = [&](const std::string& key, const ImVec2& minimum, float imageHeight) {
        if (compactFilmstrip || !m_PermanentGalleryWorkspace || !layoutMotion.AnchorPending() ||
            layoutMotion.AnchorKey() != key) return;
        const float target = minimum.y + imageHeight * .5f - viewClip.Min.y + ImGui::GetScrollY() -
            viewClip.GetHeight() * layoutMotion.AnchorFraction().y;
        m_RawWorkspaceLabGalleryScrollTargetY = std::max(0.0f, target);
        m_RawWorkspaceLabGalleryScrollCurrentY = m_RawWorkspaceLabGalleryScrollTargetY;
        ImGui::SetScrollY(m_RawWorkspaceLabGalleryScrollTargetY);
        layoutMotion.AnchorApplied();
        gridAnchorApplied = true;
    };
    std::unordered_map<
        std::string, const Stack::RawWorkspace::SourceRecord*>
        timelineSourcesByKey;
    const bool stackFilmstrip = compactFilmstrip &&
        m_RawWorkspaceLabUi.galleryNavigationMode ==
            RawGalleryNavigationMode::ProjectRoot &&
        m_RawWorkspaceGalleryContentMode ==
            Stack::RawWorkspace::GalleryContentMode::Gallery;
    const bool projectFilmstrip = compactFilmstrip &&
        m_RawWorkspaceLabUi.galleryNavigationMode ==
            RawGalleryNavigationMode::ProjectRoot &&
        m_RawWorkspaceGalleryContentMode !=
            Stack::RawWorkspace::GalleryContentMode::Gallery;
    if (compactFilmstrip) {
        if (!stackFilmstrip && !projectFilmstrip) {
            m_RawWorkspaceLabFilmstripTimelineEntries.clear();
        }
        if (!stackFilmstrip) m_RawWorkspaceLabFilmstripStackTimelineActive = false;
        if (!projectFilmstrip) m_RawWorkspaceLabFilmstripProjectTimelineActive = false;
        m_RawWorkspaceLabFilmstripTimelineHoveredIndex =
            std::numeric_limits<std::size_t>::max();
        m_RawWorkspaceLabFilmstripTimelineHoveredSourceKey.clear();
        m_RawWorkspaceLabFilmstripTimelineHoveredStackMember = false;
        m_RawWorkspaceLabFilmstripTimelineHoveredCenterX = 0.0f;
    }
    const auto addTimelineSourceTime = [](
        RawLabFilmstripTimelineEntry& entry,
        const Stack::RawWorkspace::SourceRecord* source) {
        if (source == nullptr) return;
        const std::int64_t timestamp = source->captureTimestamp > 0
            ? source->captureTimestamp : source->modifiedUnixSeconds;
        if (timestamp <= 0) return;
        if (entry.firstTimestamp == 0 || timestamp < entry.firstTimestamp)
            entry.firstTimestamp = timestamp;
        entry.lastTimestamp = std::max(entry.lastTimestamp, timestamp);
    };
    const auto appendFilmstripTimelineEntry = [&]
        (const std::string& label,
         const std::vector<std::string>& sourceKeys,
         std::size_t imageCount) {
        RawLabFilmstripTimelineEntry entry;
        entry.label = label;
        entry.imageCount = std::max<std::size_t>(1u, imageCount);
        for (const std::string& key : sourceKeys) {
            const auto found = timelineSourcesByKey.find(key);
            if (found != timelineSourcesByKey.end())
                addTimelineSourceTime(entry, found->second);
        }
        m_RawWorkspaceLabFilmstripTimelineEntries.push_back(std::move(entry));
    };
    const auto appendSingleFilmstripTimelineEntry = [&]
        (const std::string& label,
         const Stack::RawWorkspace::SourceRecord* source,
         std::size_t imageCount) {
        RawLabFilmstripTimelineEntry entry;
        entry.label = label;
        entry.imageCount = std::max<std::size_t>(1u, imageCount);
        addTimelineSourceTime(entry, source);
        m_RawWorkspaceLabFilmstripTimelineEntries.push_back(std::move(entry));
    };
    std::size_t currentFilmstripTimelineIndex =
        std::numeric_limits<std::size_t>::max();
    LoadResourceTextures();
    RenderRawWorkspaceGalleryRevertPopup();
    const Stack::RawWorkspace::GalleryPresentation& presentation =
        GetRawWorkspacePanelGalleryPresentation();
    const bool projectsOnly =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
            RawGalleryNavigationMode::ProjectRoot &&
        m_RawWorkspaceGalleryContentMode !=
            Stack::RawWorkspace::GalleryContentMode::Gallery;
    const bool canEditOrganization = !projectsOnly && CanEditRawWorkspaceFilmstripOrganization();
    const auto* projectStacks = projectsOnly ? &ResolveRawWorkspaceGalleryProjectStacks() : nullptr;
    if(!compactFilmstrip&&m_RawWorkspaceGalleryContentMode==Stack::RawWorkspace::GalleryContentMode::Bracket)RenderAutoBracketQueueItems();
    if ((projectsOnly && presentation.projects.empty()) ||
        (!projectsOnly && presentation.totalSources <= 0)) {
        if (compactFilmstrip) {
            m_RawWorkspaceLabFilmstripTimelineEntries.clear();
            m_RawWorkspaceLabFilmstripStackTimelineActive = false;
            m_RawWorkspaceLabFilmstripProjectTimelineActive = false;
        }
        ImGui::TextDisabled(
            projectsOnly
                ? "No saved projects in this folder."
                : "No RAW images in this workspace.");
        return;
    }



    const auto sameGalleryProjectPath = [](
        const std::filesystem::path& left,
        const std::filesystem::path& right) {
        return !left.empty() && !right.empty() &&
            left.lexically_normal() == right.lexically_normal();
    };
    const auto isProjectSelected = [&](
        const std::filesystem::path& projectPath) {
        return std::any_of(
            m_RawWorkspaceLabSelectedProjectPaths.begin(),
            m_RawWorkspaceLabSelectedProjectPaths.end(),
            [&](const std::filesystem::path& selectedPath) {
                return sameGalleryProjectPath(
                    selectedPath, projectPath);
            });
    };
    const auto selectProject = [&] (
        const std::filesystem::path& projectPath,
        const std::string& projectName,
        bool toggle,
        bool range) {
        m_RawWorkspaceLabFocusedProjectPath = projectPath;
        m_RawWorkspaceLabFocusedProjectName = projectName;
        if (projectStacks) for (const auto& family : *projectStacks) {
            const auto chosen = std::find_if(family.projectIndices.begin(), family.projectIndices.end(),
                [&](const auto index) { return sameGalleryProjectPath(presentation.projects[index].projectPath, projectPath); });
            if (chosen == family.projectIndices.end()) continue;
            const auto& chosenId = presentation.projects[*chosen].projectCoverKey;
            for (const auto index : family.projectIndices)
                m_RawWorkspaceGalleryProjectCoverIds[presentation.projects[index].projectCoverKey] = chosenId;
            break;
        }
        if (range && !m_RawWorkspaceLabProjectSelectionAnchor.empty()) {
            std::vector<const Stack::RawWorkspace::GalleryProjectView*> selectionOrder;
            // List rows expose every saved version; only grid/filmstrip cards
            // collapse a family to one cover. Match the range to the shown order.
            const bool expandedProjectOrder = (!compactFilmstrip && gridBrowse.projectGroups && gridBrowse.groupCount) ||
                (compactFilmstrip && filmstripDrawerExpansion > 0.001f);
            const bool stackedProjectOrder = projectStacks && !expandedProjectOrder && (compactFilmstrip ||
                m_RawWorkspaceGalleryDisplayMode != Stack::RawWorkspace::GalleryDisplayMode::List);
            if (!compactFilmstrip && gridBrowse.projectGroups && gridBrowse.groupCount) {
                for (const auto& key : gridBrowse.groups[0]) {
                    const auto found = m_RawWorkspaceGalleryProjectIndices.find(key);
                    if (found != m_RawWorkspaceGalleryProjectIndices.end())
                        selectionOrder.push_back(&presentation.projects[found->second]);
                }
            } else if (compactFilmstrip && expandedProjectOrder && projectStacks) {
                for (const auto& family : *projectStacks)
                    for (const auto index : family.projectIndices)
                        selectionOrder.push_back(&presentation.projects[index]);
            } else if (stackedProjectOrder) for (const auto& family : *projectStacks) {
                const auto* member = &presentation.projects[family.projectIndices.front()];
                const auto cover = m_RawWorkspaceGalleryProjectCoverIds.find(member->projectCoverKey);
                for (const auto index : family.projectIndices) {
                    const auto& candidate = presentation.projects[index];
                    if (cover != m_RawWorkspaceGalleryProjectCoverIds.end() && candidate.projectCoverKey == cover->second)
                        member = &candidate;
                    if (sameGalleryProjectPath(candidate.projectPath, projectPath) ||
                        sameGalleryProjectPath(candidate.projectPath, m_RawWorkspaceLabProjectSelectionAnchor)) {
                        member = &candidate;
                        break;
                    }
                }
                selectionOrder.push_back(member);
            }
            else for (const auto& project : presentation.projects) selectionOrder.push_back(&project);
            std::size_t anchorIndex = selectionOrder.size();
            std::size_t clickedIndex = selectionOrder.size();
            for (std::size_t index = 0;
                 index < selectionOrder.size();
                 ++index) {
                if (sameGalleryProjectPath(
                        selectionOrder[index]->projectPath,
                        m_RawWorkspaceLabProjectSelectionAnchor)) {
                    anchorIndex = index;
                }
                if (sameGalleryProjectPath(
                        selectionOrder[index]->projectPath,
                        projectPath)) {
                    clickedIndex = index;
                }
            }
            if (anchorIndex < selectionOrder.size() &&
                clickedIndex < selectionOrder.size()) {
                if (!toggle) {
                    m_RawWorkspace.selectedSourceKeys.clear();
                    m_RawWorkspace.selectedSourceKey.clear();
                    m_RawWorkspaceLabSelectedProjectPaths.clear();
                }
                const std::size_t first = std::min(
                    anchorIndex, clickedIndex);
                const std::size_t last = std::max(
                    anchorIndex, clickedIndex);
                for (std::size_t index = first; index <= last; ++index) {
                    const std::filesystem::path candidate =
                        selectionOrder[index]->projectPath;
                    if (!isProjectSelected(candidate)) {
                        m_RawWorkspaceLabSelectedProjectPaths.push_back(
                            candidate);
                    }
                }
                InvalidateRawWorkspaceGalleryPresentation(false);
                return;
            }
        }

        const auto selected = std::find_if(
            m_RawWorkspaceLabSelectedProjectPaths.begin(),
            m_RawWorkspaceLabSelectedProjectPaths.end(),
            [&](const std::filesystem::path& selectedPath) {
                return sameGalleryProjectPath(
                    selectedPath, projectPath);
            });
        if (toggle) {
            if (selected ==
                m_RawWorkspaceLabSelectedProjectPaths.end()) {
                m_RawWorkspaceLabSelectedProjectPaths.push_back(
                    projectPath);
            } else {
                m_RawWorkspaceLabSelectedProjectPaths.erase(selected);
            }
        } else {
            m_RawWorkspace.selectedSourceKeys.clear();
            m_RawWorkspace.selectedSourceKey.clear();
            m_RawWorkspaceLabSelectedProjectPaths.assign(
                1u, projectPath);
        }
        m_RawWorkspaceLabProjectSelectionAnchor = projectPath;
        InvalidateRawWorkspaceGalleryPresentation(false);
    };
    const auto selectSource = [&] (
        const std::string& sourceKey,
        bool toggle,
        bool range,
        bool openForEditing) {
        m_RawWorkspaceLabFocusedProjectPath.clear();
        m_RawWorkspaceLabFocusedProjectName.clear();
        if (!toggle) {
            m_RawWorkspaceLabSelectedProjectPaths.clear();
            m_RawWorkspaceLabProjectSelectionAnchor.clear();
        }
        std::vector<std::string> visibleSourceOrder;
        if (range) {
            visibleSourceOrder = BuildRawWorkspaceGallerySourceSelectionOrder(
                compactFilmstrip, expandedFilmstripDrawer, filmstripDrawerExpansion);
        }
        SelectRawWorkspaceSourceForGallery(
            sourceKey, toggle, range, openForEditing, range ? &visibleSourceOrder : nullptr);
    };
    HandleRawGalleryActionShortcuts();

    const auto updateScrollToSource = [&](
        const Stack::RawWorkspace::SourceRecord& source) {
        if (m_RawLabGalleryScrollToSourceKey != source.relativePathKey) {
            return;
        }
        const float tileScreenY = ImGui::GetCursorScreenPos().y;
        const float windowMinY = ImGui::GetWindowPos().y;
        const float relativeTileY = tileScreenY - windowMinY + ImGui::GetScrollY();
        m_RawWorkspaceLabGalleryScrollTargetY = std::max(
            0.0f,
            relativeTileY - 60.0f);
        m_RawLabGalleryScrollToSourceKey.clear();
    };

    const auto projectReferenceSource = [&](const Stack::RawWorkspace::GalleryProjectView& project)
        -> const Stack::RawWorkspace::SourceRecord* {
        if (project.referenceSourceIndex < m_RawWorkspace.sources.size()) {
            const auto& source = m_RawWorkspace.sources[project.referenceSourceIndex];
            if (source.relativePathKey == project.referenceSourceKey) return &source;
        }
        if (project.projectIndex < m_RawWorkspace.sourceSetProjects.size()) {
            const auto& entry = m_RawWorkspace.sourceSetProjects[project.projectIndex];
            if (entry.thumbnailOriginal) return entry.thumbnailOriginal.get();
            const auto& linked = entry.linkedOriginal;
            if (linked) return linked.get();
        }
        if (!project.referenceSourceKey.empty()) {
            if (const auto* source = FindRawWorkspaceSourceByKey(project.referenceSourceKey)) {
                return source;
            }
        }
        return nullptr;
    };

    if (compactFilmstrip) m_RawWorkspaceLabFilmstripDragState.deliveryHandledThisFrame = false;
    auto drawThumbnail = [&](const Stack::RawWorkspace::SourceRecord& source,
                             const ImVec2& tileSize,
                             float imageHeight,
                             bool showDetails,
                             const std::string& displayLabel,
                             const std::filesystem::path& projectPath,
                             const void* itemId,
                             bool allowOverlap = false,
                             std::size_t similarityStackCount = 1u,
                             float similarityStackCountOpacity = 1.0f,
                             const std::vector<std::string>*
                                 filmstripTargetMembers = nullptr,
                             bool previewHoverEnabled = true,
                             float previewHoverBottomY =
                                 std::numeric_limits<float>::infinity(),
                             bool completedBurst = false,
                             const std::vector<std::size_t>* projectFamily = nullptr,
                             ImRect* deferredBadgeArea = nullptr,
                             float previewHoverTopY = std::numeric_limits<float>::infinity(),
                             const Stack::RawWorkspace::GalleryProjectView* projectCard = nullptr) {
        if (projectPath.empty() &&
            !source.relativePathKey.empty() &&
            source.relativePathKey.rfind("project-overlay:", 0) != 0) {
            PrioritizeRawWorkspaceThumbnailSource(source.relativePathKey);
        }
        // Handle scroll-to navigation before creating the item. Culling can
        // replace a far-off tile with Dummy, but it must not make navigation
        // lose the target.
        if (projectPath.empty()) {
            updateScrollToSource(source);
        }
        // Published catalog storage can relocate during discovery. Permanent
        // filmstrip originals use source identity; embedded project frames keep
        // their distinct record scope because source keys can repeat there.
        ImGui::PushID("RawGalleryTile");
        if (projectFamily && !projectFamily->empty())
            PushGalleryKeyID(presentation.projects[projectFamily->front()].projectCoverKey);
        else if (!projectPath.empty())
            PushGalleryKeyID(source.relativePathKey);
        else if (compactFilmstrip && m_PermanentGalleryWorkspace && projectPath.empty())
            PushGalleryKeyID(source.relativePathKey);
        else
            ImGui::PushID(itemId);
        const ImVec2 tileMinimum = ImGui::GetCursorScreenPos();
        const Stack::Editor::RawLabInternal::GalleryTileMotion tileMotion(layoutMotion,
            source.relativePathKey, ImRect(tileMinimum,
                ImVec2(tileMinimum.x + tileSize.x, tileMinimum.y + tileSize.y)));
        const bool pointerEnabled = movingImageAtPointer.empty() ||
            movingImageAtPointer == source.relativePathKey;
        ImGuiButtonFlags tileButtonFlags =
            ImGuiButtonFlags_PressedOnClickRelease |
            ImGuiButtonFlags_PressedOnDoubleClick;
        // Filmstrip cards have disjoint exposed hit regions. AllowOverlap
        // would require the previous frame's hovered ID and delay handoffs.
        if (allowOverlap && !compactFilmstrip) {
            tileButtonFlags |= ImGuiButtonFlags_AllowOverlap;
        }
        const float exposedHeight = previewHoverEnabled
            ? std::clamp(previewHoverBottomY - tileMinimum.y, 0.0f, tileSize.y)
            : 0.0f;
        const bool rawTilePressed = Stack::Editor::RawLabInternal::GalleryThumbnailButton(
            "##tile",
            tileSize,
            exposedHeight,
            tileButtonFlags, &tileMotion, pointerEnabled);
        const bool pointerOnVisibleCard = previewHoverEnabled && pointerEnabled &&
            ImGui::GetIO().MousePos.y < tileMotion.Map(ImVec2(tileMinimum.x, previewHoverBottomY)).y;
        const bool queuePlaceholder=projectsOnly&&projectPath.empty();
        const bool tilePressed = rawTilePressed && pointerOnVisibleCard&&!queuePlaceholder;
        const bool tileHovered = pointerOnVisibleCard &&
            ImGui::IsItemHovered() &&
            ImGui::GetIO().MousePos.y >= tileMotion.Map(tileMinimum).y;
        // Only preview extends above a short stack. Native item bounds still
        // own selection, double-click, dragging and context actions.
        const ImVec2 pointer = ImGui::GetIO().MousePos;
        const bool previewHovered = tileHovered ||
            (compactFilmstrip && previewHoverEnabled && pointerEnabled && !tileMotion.Active() &&
             ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() &&
             pointer.x >= tileMinimum.x && pointer.x < tileMinimum.x + tileSize.x &&
             pointer.y >= previewHoverTopY && pointer.y < tileMinimum.y);
        if (compactFilmstrip && tileHovered &&
            currentFilmstripTimelineIndex <
                m_RawWorkspaceLabFilmstripTimelineEntries.size()) {
            m_RawWorkspaceLabFilmstripTimelineHoveredIndex =
                currentFilmstripTimelineIndex;
            m_RawWorkspaceLabFilmstripTimelineHoveredSourceKey =
                source.relativePathKey;
            m_RawWorkspaceLabFilmstripTimelineHoveredCenterX =
                tileMinimum.x + tileSize.x * 0.5f;
            m_RawWorkspaceLabFilmstripTimelineHoveredStackMember =
                filmstripTargetMembers != nullptr &&
                filmstripTargetMembers->size() > 1u &&
                filmstripDrawerExpansion > 0.001f;
        }
        gridImageHovered |= tileHovered;
        const bool gridDraggable = !compactFilmstrip && !projectsOnly && m_PermanentGalleryWorkspace &&
            projectPath.empty() && canEditOrganization && !tileMotion.Active();
        if (gridDraggable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            if (gridBrowse.dragged.empty()) {
                gridBrowse.dragged = gridBrowse.groupCount == 0 && filmstripTargetMembers
                    ? *filmstripTargetMembers : std::vector<std::string>{source.relativePathKey};
                gridBrowse.dragGroup = gridGroup;
                if (gridBrowse.groupCount && gridGroup >= 0 &&
                    std::find(m_RawWorkspace.selectedSourceKeys.begin(),m_RawWorkspace.selectedSourceKeys.end(),
                        source.relativePathKey) != m_RawWorkspace.selectedSourceKeys.end()) {
                    gridBrowse.dragged.clear();
                    for (const auto& member : gridBrowse.groups[gridGroup])
                        if (std::find(m_RawWorkspace.selectedSourceKeys.begin(),m_RawWorkspace.selectedSourceKeys.end(),member)
                            != m_RawWorkspace.selectedSourceKeys.end()) gridBrowse.dragged.push_back(member);
                }
            }
            ImGui::SetDragDropPayload("RawGalleryGridMembers", source.relativePathKey.c_str(), source.relativePathKey.size()+1);
            int dragWidth=0,dragHeight=0;
            const auto dragTexture=GetRawWorkspaceThumbnailTexture(source,&dragWidth,&dragHeight,false);
            if (dragTexture && dragWidth>0 && dragHeight>0) {
                const float factor=std::min(112.f/dragWidth,84.f/dragHeight);
                const ImVec2 minimum(pointer.x+14.f,pointer.y+12.f);
                ImGui::GetForegroundDrawList()->AddImageRounded((ImTextureID)(intptr_t)dragTexture,minimum,
                    {minimum.x+dragWidth*factor,minimum.y+dragHeight*factor},{0,1},{1,0},IM_COL32(255,255,255,210),4.f);
            }
            ImGui::EndDragDropSource();
        }
        if (gridDraggable && ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("RawGalleryGridMembers")) {
                const auto target = gridBrowse.groupCount && gridGroup >= 0 ? gridBrowse.groups[gridGroup] :
                    filmstripTargetMembers ? *filmstripTargetMembers : std::vector<std::string>{source.relativePathKey};
                if (payload->IsDelivery() && !gridBrowse.dragged.empty() &&
                    std::find(target.begin(),target.end(),gridBrowse.dragged.front()) == target.end()) {
                    if (!gridBrowse.groupCount && gridBrowse.dragged.size()>1 && target.size()>1) {
                        gridBrowse.Open(gridBrowse.dragged,target,ImGui::GetFrameCount());
                    } else {
                        gridBrowse.pendingMembers = target;
                        gridBrowse.pendingGroup = gridGroup;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        const bool tileDoubleClicked = tilePressed &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        const bool gridSourceStack = !compactFilmstrip && m_PermanentGalleryWorkspace &&
            m_RawWorkspaceLabUi.galleryWorkspaceOpen && filmstripTargetMembers != nullptr;
        const ImRect tileRect(
            tileMinimum,
            ImVec2(tileMinimum.x + tileSize.x, tileMinimum.y + tileSize.y));
        const ImRect imageArea(
            tileRect.Min,
            ImVec2(
                tileRect.Max.x,
                std::min(tileRect.Max.y, tileRect.Min.y + imageHeight)));
        const bool draggableFilmstripSource = compactFilmstrip && !tileMotion.Active() &&
            canEditOrganization &&
            projectPath.empty() &&
            !source.relativePathKey.empty() &&
            source.relativePathKey.rfind("project-overlay:", 0) != 0;
        auto& filmstripDrag = m_RawWorkspaceLabFilmstripDragState;
        if (draggableFilmstripSource && pointerOnVisibleCard &&
            ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
            filmstripDrag.phase == RawGalleryFilmstripDragPhase::Idle) {
            filmstripDrag.sourceKey = source.relativePathKey;
            filmstripDrag.sourceStackMembers = filmstripTargetMembers != nullptr
                ? *filmstripTargetMembers
                : std::vector<std::string> { source.relativePathKey };
            filmstripDrag.collapsedStackSource =
                filmstripDrag.sourceStackMembers.size() > 1u &&
                filmstripDrawerExpansion <= 0.001f;
            filmstripDrag.selectedSourceKeyBeforeDrag =
                m_RawWorkspace.selectedSourceKey;
            filmstripDrag.selectedSourceKeysBeforeDrag =
                m_RawWorkspace.selectedSourceKeys;
            filmstripDrag.originMinimum = tileMinimum;
            filmstripDrag.tileSize = tileSize;
            filmstripDrag.imageHeight = imageHeight;
            filmstripDrag.lastObservedScrollX = ImGui::GetScrollX();
            filmstripDrag.grabOffset = ImVec2(
                std::clamp(
                    ImGui::GetIO().MousePos.x - tileMinimum.x,
                    0.0f,
                    tileSize.x),
                std::clamp(
                    ImGui::GetIO().MousePos.y - tileMinimum.y,
                    0.0f,
                    tileSize.y));
        }
        if (draggableFilmstripSource &&
            (pointerOnVisibleCard ||
             filmstripDrag.sourceKey == source.relativePathKey) &&
            ImGui::BeginDragDropSource(
                ImGuiDragDropFlags_SourceAllowNullID |
                ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            if (filmstripDrag.sourceKey != source.relativePathKey ||
                filmstripDrag.tileSize.x <= 0.0f ||
                filmstripDrag.tileSize.y <= 0.0f) {
                filmstripDrag = {};
                filmstripDrag.sourceKey = source.relativePathKey;
                filmstripDrag.sourceStackMembers =
                    filmstripTargetMembers != nullptr
                        ? *filmstripTargetMembers
                        : std::vector<std::string> { source.relativePathKey };
                filmstripDrag.collapsedStackSource =
                    filmstripDrag.sourceStackMembers.size() > 1u &&
                    filmstripDrawerExpansion <= 0.001f;
                filmstripDrag.selectedSourceKeyBeforeDrag =
                    m_RawWorkspace.selectedSourceKey;
                filmstripDrag.selectedSourceKeysBeforeDrag =
                    m_RawWorkspace.selectedSourceKeys;
                filmstripDrag.originMinimum = tileMinimum;
                filmstripDrag.tileSize = tileSize;
                filmstripDrag.imageHeight = imageHeight;
                filmstripDrag.lastObservedScrollX = ImGui::GetScrollX();
                filmstripDrag.grabOffset = ImVec2(
                    tileSize.x * 0.5f,
                    tileSize.y * 0.5f);
            }
            if (filmstripDrag.phase == RawGalleryFilmstripDragPhase::Idle) {
                filmstripDrag.phase = RawGalleryFilmstripDragPhase::Dragging;
                filmstripDrag.phaseStartedAt = ImGui::GetTime();
                // Preserve the exact pre-drag selection once the pointer
                // crosses the drag threshold so moving one card cannot
                // replace a batch.
                m_RawWorkspace.selectedSourceKey =
                    filmstripDrag.selectedSourceKeyBeforeDrag;
                m_RawWorkspace.selectedSourceKeys =
                    filmstripDrag.selectedSourceKeysBeforeDrag;
            }
            filmstripDrag.proxyMinimum = ImVec2(
                ImGui::GetIO().MousePos.x - filmstripDrag.grabOffset.x,
                ImGui::GetIO().MousePos.y - filmstripDrag.grabOffset.y);
            ImGui::SetDragDropPayload(
                kRawLabFilmstripSourcePayload,
                source.relativePathKey.c_str(),
                source.relativePathKey.size() + 1u);
            m_RawWorkspaceLabFilmstripDrawerInteractionRetained = true;
            ImGui::EndDragDropSource();
        }
        bool filmstripGroupTargetPreview = false;
        if (draggableFilmstripSource && pointerOnVisibleCard &&
            filmstripTargetMembers != nullptr &&
            ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                    kRawLabFilmstripSourcePayload,
                    ImGuiDragDropFlags_AcceptBeforeDelivery |
                        ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                m_RawWorkspaceLabFilmstripDrawerInteractionRetained = true;
                const std::string payloadSource =
                    payload->Data != nullptr
                        ? static_cast<const char*>(payload->Data)
                        : std::string();
                const bool validTarget = !payloadSource.empty() &&
                    std::find(
                        filmstripTargetMembers->begin(),
                        filmstripTargetMembers->end(),
                        payloadSource) == filmstripTargetMembers->end();
                filmstripDrag.imageTargetPreviewFrame =
                    ImGui::GetFrameCount();
                if (validTarget) {
                    filmstripGroupTargetPreview = true;
                    filmstripDrag.dropKind =
                        RawGalleryFilmstripDropKind::Group;
                    filmstripDrag.targetStackMembers =
                        *filmstripTargetMembers;
                    filmstripDrag.animationEndMinimum = tileMinimum;
                    if (payload->IsDelivery()) {
                        filmstripDrag.deliveryHandledThisFrame = true;
                        // Commit after the strip finishes using its borrowed stack order.
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        // Families are prepared on catalog/grouping changes. Drawing a card
        // never searches source memberships or the complete project catalog.
        const std::size_t projectCount = projectFamily ? projectFamily->size() : 0u;
        const auto projectAt = [&](std::size_t index) {
            return &presentation.projects[(*projectFamily)[index]];
        };
        std::size_t activeProjectIndex = 0;
        const auto cover = projectCount ? m_RawWorkspaceGalleryProjectCoverIds.find(projectAt(0)->projectCoverKey) :
            m_RawWorkspaceGalleryProjectCoverIds.end();
        for (std::size_t index = 0; index < projectCount; ++index) {
            if ((cover != m_RawWorkspaceGalleryProjectCoverIds.end() && projectAt(index)->projectCoverKey == cover->second) ||
                (cover == m_RawWorkspaceGalleryProjectCoverIds.end() &&
                    sameGalleryProjectPath(projectAt(index)->projectPath, m_RawWorkspaceLabFocusedProjectPath))) {
                activeProjectIndex = index;
                break;
            }
        }
        const bool stackedProjects = projectCount > 1u;
        const Stack::RawWorkspace::GalleryProjectView* activeProject =
            projectCount ? projectAt(activeProjectIndex) : projectCard;
        const std::filesystem::path activeProjectPath = activeProject != nullptr
            ? activeProject->projectPath
            : projectPath;
        const std::string activeProjectName = activeProject != nullptr &&
                !activeProject->projectName.empty()
            ? activeProject->projectName
            : displayLabel;
        ImRect badgeArea = imageArea;
        int textureWidth = 0;
        int textureHeight = 0;
        {
            unsigned int texture =
                GetRawWorkspaceThumbnailTexture(
                    source,
                    &textureWidth,
                    &textureHeight,
                    projectPath.empty());
            if (!texture && activeProject) {
                if (const auto* fallback=projectReferenceSource(*activeProject))
                    texture=GetRawWorkspaceThumbnailTexture(*fallback,&textureWidth,&textureHeight,false);
            }
            if (texture != 0 && textureWidth > 0 && textureHeight > 0) {
                const ImVec2 fitted = FitLabImage(
                    static_cast<float>(textureWidth),
                    static_cast<float>(textureHeight),
                    imageArea.GetSize());
                const ImVec2 imageMinimum(
                    imageArea.Min.x + (imageArea.GetWidth() - fitted.x) * 0.5f,
                    imageArea.Min.y + (imageArea.GetHeight() - fitted.y) * 0.5f);
                const ImVec2 imageMaximum(imageMinimum.x + fitted.x, imageMinimum.y + fitted.y);
                badgeArea = ImRect(imageMinimum, imageMaximum);
                if (compactFilmstrip && (!filmstripTargetMembers || filmstripTargetMembers->empty() || source.relativePathKey == filmstripTargetMembers->front()))
                    gridBrowse.filmstripSlots.Report(currentFilmstripTimelineIndex, textureWidth, textureHeight);
                if (!RecordRawWorkspaceGalleryThumbnail(source.relativePathKey,
                        imageMinimum, imageMaximum)) {
                    drawList->AddImage(
                        (ImTextureID)(intptr_t)texture,
                        imageMinimum,
                        imageMaximum,
                        ImVec2(0.0f, 1.0f),
                        ImVec2(1.0f, 0.0f));
                }
            } else {
                RecordRawWorkspaceGalleryThumbnail(source.relativePathKey,
                    imageArea.Min, imageArea.Max);
                drawList->AddRectFilled(
                    imageArea.Min,
                    imageArea.Max,
                    ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.10f)));
                const char* rawLabel = activeProject ? "PROJECT" : "RAW";
                const ImVec2 rawSize = ImGui::CalcTextSize(rawLabel);
                drawList->AddText(
                    ImVec2(
                        imageArea.GetCenter().x - rawSize.x * 0.5f,
                        imageArea.GetCenter().y - rawSize.y * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    rawLabel);
            }
        }
        const auto selection = std::find(
            m_RawWorkspace.selectedSourceKeys.begin(),
            m_RawWorkspace.selectedSourceKeys.end(),
            source.relativePathKey);
        const bool selected = selection != m_RawWorkspace.selectedSourceKeys.end();
        if(IsBracketingToolActive()&&filmstripTargetMembers&&filmstripTargetMembers->size()>1&&(!compactFilmstrip||filmstripDrawerExpansion<=0.001f)) {
            std::size_t selectedCount=0;
            for(const auto& key:*filmstripTargetMembers)if(std::find(m_RawWorkspace.selectedSourceKeys.begin(),m_RawWorkspace.selectedSourceKeys.end(),key)!=m_RawWorkspace.selectedSourceKeys.end())++selectedCount;
            if(selectedCount) {
                const auto label=std::to_string(selectedCount)+" / "+std::to_string(filmstripTargetMembers->size())+" selected";
                drawList->PushClipRect(imageArea.Min,imageArea.Max,true);
                auto labelSurface=ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
                labelSurface.w=0.90f;
                drawList->AddRectFilled(imageArea.Min,ImVec2(imageArea.Max.x,std::min(imageArea.Max.y,imageArea.Min.y+ImGui::GetTextLineHeight()+4)),ImGui::GetColorU32(labelSurface));
                drawList->AddText(ImVec2(imageArea.Min.x+(completedBurst?24.0f:3.0f),imageArea.Min.y+2),ImGui::GetColorU32(ImGuiCol_Text),label.c_str());
                drawList->PopClipRect();
            }
        }
        const bool focused =
            source.relativePathKey == m_RawWorkspace.selectedSourceKey;
        const bool focusedProject =
            !activeProjectPath.empty() &&
            sameGalleryProjectPath(
                activeProjectPath,
                m_RawWorkspaceLabFocusedProjectPath);
        const bool selectedProject =
            !activeProjectPath.empty() && isProjectSelected(activeProjectPath);
        const auto projectSelection = std::find_if(
            m_RawWorkspaceLabSelectedProjectPaths.begin(),
            m_RawWorkspaceLabSelectedProjectPaths.end(),
            [&](const std::filesystem::path& selectedPath) {
                return sameGalleryProjectPath(
                    selectedPath, activeProjectPath);
            });
        const std::size_t selectionOrdinal = selectedProject
            ? static_cast<std::size_t>(std::distance(
                  m_RawWorkspaceLabSelectedProjectPaths.begin(),
                  projectSelection)) + 1u
                : (selected
                ? m_RawWorkspaceLabSelectedProjectPaths.size() +
                    static_cast<std::size_t>(std::distance(
                        m_RawWorkspace.selectedSourceKeys.begin(),
                        selection)) + 1u
                : 0u);
        if (showDetails) {
            // Keep labels compact enough for narrow gallery columns while
            // measuring against the actual font size so long names never
            // spill into the neighboring tile.
            ImFont* galleryFont = ImGui::GetFont();
            const float galleryTextSize = std::clamp(
                ImGui::GetFontSize() * 0.62f,
                8.0f,
                10.0f);
            const float galleryTextWidth = std::max(
                12.0f,
                tileRect.GetWidth() - 6.0f);
            const auto fitGalleryText = [&](const std::string& value) {
                if (value.empty() || galleryFont == nullptr) {
                    return value;
                }
                const auto textWidth = [&](const std::string& candidate) {
                    return galleryFont->CalcTextSizeA(
                        galleryTextSize,
                        std::numeric_limits<float>::max(),
                        0.0f,
                        candidate.c_str()).x;
                };
                if (textWidth(value) <= galleryTextWidth) {
                    return value;
                }
                if (textWidth("...") > galleryTextWidth) {
                    return std::string();
                }
                // Bound the copy/trim work to the visible prefix. CalcTextSizeA
                // stops at >= max_width; nextafter retains the old <= boundary.
                const char* remaining = nullptr;
                galleryFont->CalcTextSizeA(galleryTextSize,
                    std::nextafter(galleryTextWidth, std::numeric_limits<float>::infinity()),
                    0.0f, value.c_str(), nullptr, &remaining);
                std::string fitted(value.c_str(), remaining);
                while (!fitted.empty() &&
                       textWidth(fitted + "...") > galleryTextWidth) {
                    Stack::Utf8Text::RemoveLastCodePoint(fitted);
                }
                return fitted.empty() ? std::string("...") : fitted + "...";
            };
            const std::string ordinalPrefix = selectionOrdinal > 0u
                ? std::to_string(selectionOrdinal) + "  "
                : std::string();
            const std::string filename = fitGalleryText(
                ordinalPrefix + (activeProject ? activeProjectName : displayLabel));
            const float textY = imageArea.Max.y + 3.0f;
            drawList->AddText(
                galleryFont,
                galleryTextSize,
                ImVec2(tileRect.Min.x + 2.0f, textY),
                ImGui::GetColorU32(ImGuiCol_Text),
                filename.c_str());
            const std::string projectStatus = !activeProjectPath.empty()
                ? (stackedProjects
                    ? activeProjectName + "  " +
                        std::to_string(activeProjectIndex + 1u) + "/" +
                        std::to_string(projectCount)
                    : std::string("Edited project"))
                : std::string("Unedited RAW");
            const std::string fittedProjectStatus = fitGalleryText(projectStatus);
            drawList->AddText(
                galleryFont,
                galleryTextSize,
                ImVec2(tileRect.Min.x + 2.0f, textY + galleryTextSize + 1.0f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                fittedProjectStatus.c_str());
        }
        Stack::RawGallerySelectionVisuals::DrawTileSelection(
            drawList,
            tileRect.Min,
            tileRect.Max,
            (activeProjectPath.empty() && selected) || selectedProject,
            (activeProjectPath.empty() && focused) || focusedProject,
            ImGui::IsItemHovered(),
            selectionOrdinal,
            compactFilmstrip,
            compactFilmstrip &&
                Stack::Editor::RawLabInternal::
                    ShouldUseSideFilmstripPerforations(
                        textureWidth > 0
                            ? textureWidth
                            : source.thumbnail.width,
                        textureHeight > 0
                            ? textureHeight
                            : source.thumbnail.height),
            compactFilmstrip ? 0.0f : 4.0f, &badgeArea.Min, &badgeArea.Max);
        const bool dragPlaceholder = compactFilmstrip &&
            filmstripDrag.phase != RawGalleryFilmstripDragPhase::Idle &&
            filmstripDrag.sourceKey == source.relativePathKey;
        if (dragPlaceholder) {
            if (filmstripDrag.phase !=
                    RawGalleryFilmstripDragPhase::Settling) {
                filmstripDrag.originMinimum = tileMinimum;
            }
            drawList->AddRectFilled(
                imageArea.Min,
                imageArea.Max,
                IM_COL32(3, 3, 4, 184),
                5.0f);
        }
        if (filmstripGroupTargetPreview) {
            ImVec4 targetColor =
                ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive);
            targetColor.w = 0.22f;
            drawList->AddRectFilled(
                imageArea.Min,
                imageArea.Max,
                ImGui::GetColorU32(targetColor),
                5.0f);
        }
        if (deferredBadgeArea) *deferredBadgeArea = tileMotion.Map(badgeArea);
        else DrawGalleryStackBadges(drawList, badgeArea, stackedProjects ? projectCount : similarityStackCount,
            similarityStackCountOpacity, completedBurst, compactFilmstrip);
        if (projectPath.empty() && m_RawLabGalleryFlashSourceKey == source.relativePathKey) {
            const double flashElapsed = ImGui::GetTime() - m_RawLabGalleryFlashStartTime;
            if (flashElapsed < 1.0) {
                const float flashT = static_cast<float>(flashElapsed / 1.0);
                const float sineVal = std::sin(flashT * 3.14159265f);
                const float flashAlpha = sineVal * 0.45f;
                const ImU32 flashCol = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(0.35f, 0.65f, 1.0f, flashAlpha));
                drawList->AddRect(
                    ImVec2(tileRect.Min.x - 2.0f, tileRect.Min.y - 2.0f),
                    ImVec2(tileRect.Max.x + 2.0f, tileRect.Max.y + 2.0f),
                    flashCol,
                    4.0f,
                    0,
                    2.5f);
            } else {
                m_RawLabGalleryFlashSourceKey.clear();
            }
        }
        const bool collapsedCaptureStack = activeProjectPath.empty() && filmstripTargetMembers &&
            filmstripTargetMembers->size() > 1u &&
            ((gridSourceStack && gridBrowse.groupCount == 0) ||
             (compactFilmstrip && similarityStackCount > 1u && filmstripDrawerExpansion <= .001f));
        const auto selectCaptureStack = [&] {
            selectSource(source.relativePathKey, false, false, false);
            m_RawWorkspace.selectedSourceKeys = *filmstripTargetMembers;
            InvalidateRawWorkspaceGalleryPresentation(false);
        };
        if (tilePressed && (!compactFilmstrip || filmstripDrag.phase == RawGalleryFilmstripDragPhase::Idle)) {
            const bool doubleClicked = tileDoubleClicked;
            const ImGuiIO& click = ImGui::GetIO();
            const bool selectionModifier = click.KeyCtrl || click.KeyShift;
            if (stackedProjects && !compactFilmstrip && m_PermanentGalleryWorkspace &&
                doubleClicked && !selectionModifier) {
                std::vector<std::string> members;
                members.reserve(projectCount);
                for (std::size_t index = 0; index < projectCount; ++index)
                    members.push_back(projectAt(index)->projectCoverKey);
                selectProject(activeProjectPath, activeProjectName, false, false);
                gridBrowse.Open(members, {}, ImGui::GetFrameCount(), true);
            } else if (stackedProjects && compactFilmstrip && doubleClicked && !selectionModifier) {
                selectProject(activeProjectPath, activeProjectName, false, false);
                m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested = true;
            } else if (gridSourceStack && filmstripTargetMembers->size() > 1u &&
                doubleClicked && !selectionModifier) {
                selectSource(source.relativePathKey, false, false, false);
                gridBrowse.Open(*filmstripTargetMembers, {}, ImGui::GetFrameCount());
            } else if (compactFilmstrip && m_RawWorkspaceLabUi.galleryWorkspaceOpen &&
                filmstripTargetMembers && filmstripTargetMembers->size() > 1u &&
                similarityStackCount > 1u &&
                filmstripDrawerExpansion <= 0.001f &&
                doubleClicked && !selectionModifier) {
                selectSource(source.relativePathKey, false, false, false);
                m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested = true;
            } else if(IsBracketingToolActive() && !m_RawWorkspaceLabUi.galleryWorkspaceOpen &&
                filmstripTargetMembers && (!compactFilmstrip || filmstripDrawerExpansion <= 0.001f) &&
                !doubleClicked) {
                SelectBracketingSources(*filmstripTargetMembers,click.KeyCtrl,click.KeyShift);
            } else if (collapsedCaptureStack && !selectionModifier && !doubleClicked) {
                selectCaptureStack();
            } else if (!activeProjectPath.empty()) {
                selectProject(
                    activeProjectPath,
                    activeProjectName,
                    click.KeyCtrl,
                    click.KeyShift);
                if (doubleClicked && !selectionModifier) {
                    RequestOpenRawWorkspaceProjectFromGallery(
                        activeProjectPath);
                }
            } else {
                const bool creationMode =
                    m_RawWorkspaceLabUi.galleryNavigationMode ==
                    RawGalleryNavigationMode::MultiFrameCreation;
                if(IsBracketingToolActive() && !m_RawWorkspaceLabUi.galleryWorkspaceOpen &&
                    filmstripTargetMembers && (!compactFilmstrip || filmstripDrawerExpansion <= 0.001f))
                    SelectBracketingSources(*filmstripTargetMembers,click.KeyCtrl,click.KeyShift);
                else selectSource(
                    source.relativePathKey,
                    click.KeyCtrl,
                    click.KeyShift,
                    doubleClicked && !creationMode && !selectionModifier);
            }
        }
        if (!queuePlaceholder && tileHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            if (collapsedCaptureStack) selectCaptureStack();
            ImGui::OpenPopup("RawGalleryEditContext");
        }
        if (!queuePlaceholder && ImGui::BeginPopup("RawGalleryEditContext")) {
            if (!activeProjectPath.empty()) {
                if (!isProjectSelected(activeProjectPath)) {
                    selectProject(
                        activeProjectPath,
                        activeProjectName,
                        false,
                        false);
                } else {
                    m_RawWorkspaceLabFocusedProjectPath = activeProjectPath;
                    m_RawWorkspaceLabFocusedProjectName = activeProjectName;
                }
            } else if (std::find(
                    m_RawWorkspace.selectedSourceKeys.begin(),
                    m_RawWorkspace.selectedSourceKeys.end(),
                    source.relativePathKey) ==
                    m_RawWorkspace.selectedSourceKeys.end()) {
                selectSource(source.relativePathKey, false, false, false);
            }

            if (stackedProjects && ImGui::BeginMenu("Open saved version")) {
                for (std::size_t index = 0; index < projectCount; ++index) {
                    const auto* project = projectAt(index);
                    PushGalleryKeyID(project->projectCoverKey);
                    const auto label = project->projectName + " / Version " + std::to_string(index + 1u);
                    const auto details = std::to_string(project->frameCount) + " captures";
                    if (ImGui::MenuItem(label.c_str(), details.c_str(),
                            index == activeProjectIndex, !project->projectPath.empty())) {
                        selectProject(project->projectPath, project->projectName, false, false);
                        RequestOpenRawWorkspaceProjectFromGallery(project->projectPath);
                    }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
                ImGui::Separator();
            }
            if (activeProjectPath.empty()) {
                RenderRawGalleryContextActions(source.absolutePath);
            } else {
                RenderRawGalleryContextActions(activeProjectPath);
            }
            ImGui::EndPopup();
        }
        if ((compactFilmstrip || m_PermanentGalleryWorkspace) && previewHovered) {
            m_RawWorkspaceLabFilmstripHoverFrame = ImGui::GetFrameCount();
            std::string hoverKey = source.relativePathKey;
            if (hoverKey.rfind("project-overlay:", 0) == 0) {
                hoverKey = activeProject ? activeProject->referenceSourceKey : std::string{};
            }
            if ((!hoverKey.empty() || !activeProjectPath.empty()) &&
                m_RawWorkspaceLabFilmstripHoverSuppressed &&
                (m_RawWorkspaceLabFilmstripHoverSourceKey != hoverKey ||
                 m_RawWorkspaceLabFilmstripHoverProjectPath !=
                    activeProjectPath)) {
                m_RawWorkspaceLabFilmstripHoverSuppressed = false;
            }
            if ((!hoverKey.empty() || !activeProjectPath.empty()) &&
                !m_RawWorkspaceLabFilmstripHoverSuppressed) {
                if (m_RawWorkspaceLabFilmstripHoverSourceKey != hoverKey ||
                    m_RawWorkspaceLabFilmstripHoverProjectPath != activeProjectPath) {
                    m_RawWorkspaceLabFilmstripPreviousHoverSourceKey =
                        m_RawWorkspaceLabFilmstripHoverSourceKey;
                    m_RawWorkspaceLabFilmstripPreviousHoverProjectPath =
                        m_RawWorkspaceLabFilmstripHoverProjectPath;
                    m_RawWorkspaceLabFilmstripHoverBlend =
                        (m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.empty() &&
                             m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.empty())
                            ? 1.0f
                            : 0.0f;
                }
                m_RawWorkspaceLabFilmstripHoverSourceKey = hoverKey;
                m_RawWorkspaceLabFilmstripHoverProjectPath = activeProjectPath;
            }
            if (!m_PermanentGalleryWorkspace && tilePressed && !ImGui::GetIO().KeyCtrl &&
                !ImGui::GetIO().KeyShift) {
                m_RawWorkspaceLabFilmstripHoverSuppressed = true;
            }
        }
        if (gridSourceStack) {
            m_RawWorkspaceGalleryGridStacks.UpdateHover(*filmstripTargetMembers,
                tileHovered, ImGui::GetIO().KeyCtrl,
                layoutMotion.Active() || ImGui::IsAnyItemActive() ||
                    ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId),
                ImGui::GetFrameCount(), ImGui::GetTime());
        }
        if (!compactFilmstrip && tileHovered && activeProjectPath.empty()) {
            std::string tooltip = selected
                ? displayLabel + "\nSelected frame " +
                    std::to_string(selectionOrdinal) +
                    "\nCtrl-click toggles; Shift-click selects a range."
                : displayLabel +
                    "\nCtrl-click toggles; Shift-click selects a range.";
            if (gridSourceStack && filmstripTargetMembers->size() > 1u) {
                tooltip += "\nHold Ctrl to cycle images. Double-click to expand the stack.";
            }
            if (completedBurst) tooltip += "\nA finished burst is saved in Brackets. Creating another capture set creates a separate project.";
            LabTooltip(tooltip.c_str());
        } else if (!compactFilmstrip && tileHovered) {
            const std::string tooltip = stackedProjects
                ? activeProjectName + "\nProject " +
                    std::to_string(activeProjectIndex + 1u) + " of " +
                    std::to_string(projectCount) +
                    "\nDouble-click to view saved versions."
                : selectedProject
                ? activeProjectName + "\nSelected project " +
                    std::to_string(selectionOrdinal) +
                    "\nCtrl-click toggles; Shift-click selects a range."
                : activeProjectName +
                    "\nCtrl-click toggles; Shift-click selects a range.";
            LabTooltip(tooltip.c_str());
        }
        ImGui::PopID();
        ImGui::PopID();
    };

    const auto sourceMultiSelected = [&](const Stack::RawWorkspace::SourceRecord& source) {
        return std::find(
            m_RawWorkspace.selectedSourceKeys.begin(),
            m_RawWorkspace.selectedSourceKeys.end(),
            source.relativePathKey) != m_RawWorkspace.selectedSourceKeys.end();
    };
    const auto projectCover = [&](const Stack::RawWorkspace::GalleryProjectStack& family)
        -> const Stack::RawWorkspace::GalleryProjectView& {
        const auto& first = presentation.projects[family.projectIndices.front()];
        const auto chosen = m_RawWorkspaceGalleryProjectCoverIds.find(first.projectCoverKey);
        if (chosen != m_RawWorkspaceGalleryProjectCoverIds.end()) {
            for (const auto index : family.projectIndices)
                if (presentation.projects[index].projectCoverKey == chosen->second) return presentation.projects[index];
        }
        return first;
    };
    const auto galleryTileSource = [](
        const Stack::RawWorkspace::SourceRecord& source,
        const Stack::RawWorkspace::GallerySourceView&)
        -> const Stack::RawWorkspace::SourceRecord& {
        // Gallery cards always represent the original capture. Saved edits
        // live exclusively in the Projects tab.
        return source;
    };

    const bool creationMode =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
        RawGalleryNavigationMode::MultiFrameCreation;
    const bool projectFrames =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
        RawGalleryNavigationMode::ProjectFrames;
    if (projectFrames) {
        if (FramelessTextButton("All Projects")) {
            m_RawWorkspaceLabUi.galleryNavigationMode =
                RawGalleryNavigationMode::ProjectRoot;
            m_RawWorkspaceLabUi.galleryProjectId.clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Project frames");
        ImGui::Spacing();
    }

    if (creationMode || projectFrames) {
        const float thumbnailScale = compactFilmstrip ? std::min(m_RawWorkspaceLabUi.galleryThumbnailScale,
        std::max(1.f,ImGui::GetContentRegionAvail().y-8.f)/kRawLabFilmstripTileHeight) : m_RawWorkspaceLabUi.galleryThumbnailScale;
        const float tileWidth = (compactFilmstrip ? 124.0f : 158.0f) * thumbnailScale;
        const float tileHeight = (compactFilmstrip ? kRawLabFilmstripTileHeight : 148.0f) * thumbnailScale;
        const float imageHeight = (compactFilmstrip ? kRawLabFilmstripTileHeight : 108.0f) * thumbnailScale;
        const float gap = compactFilmstrip ? 10.0f : 16.0f;
        const int columns = compactFilmstrip ? 1 : std::max(
            1,
            static_cast<int>((std::max(tileWidth, ImGui::GetContentRegionAvail().x) + gap) /
                (tileWidth + gap)));
        std::vector<const Stack::RawWorkspace::SourceRecord*> sources;
        sources.reserve(m_RawWorkspace.sources.size());
        for (const auto& source : m_RawWorkspace.sources) {
            if (projectFrames && !std::any_of(
                    source.sourceSetProjectMemberships.begin(),
                    source.sourceSetProjectMemberships.end(),
                    [&](const auto& membership) {
                        return membership.projectId ==
                            m_RawWorkspaceLabUi.galleryProjectId;
                    })) {
                continue;
            }
            sources.push_back(&source);
        }

        const ImRect clip = ImGui::GetCurrentWindow()->ClipRect;
        const ImVec2 contentStart = ImGui::GetCursorScreenPos();
        if (compactFilmstrip) {
            m_RawWorkspaceLabFilmstripTimelineContentOriginX =
                contentStart.x + ImGui::GetScrollX();
            m_RawWorkspaceLabFilmstripTimelineEntries.reserve(sources.size());
            for (const auto* source : sources) {
                appendSingleFilmstripTimelineEntry(
                    source->fileName, source, 1u);
            }
            const auto visible = ComputeGalleryVisibleRange(
                static_cast<int>(sources.size()),
                tileWidth,
                gap,
                contentStart.x,
                clip.Min.x,
                clip.Max.x,
                RawLabGalleryPrefetchPixels(
                    std::max(0.0f, clip.Max.x - clip.Min.x)));
            for (int index = 0; index < static_cast<int>(sources.size()); ++index) {
                if (index > 0) {
                    ImGui::SameLine(0.0f, gap);
                }
                const auto& source = *sources[static_cast<std::size_t>(index)];
                const bool multiSelected = sourceMultiSelected(source);
                RecordRawWorkspaceGallerySlot(source, ImGui::GetCursorScreenPos(),
                    ImVec2(tileWidth, imageHeight), clip);
                if (index < visible.first || index >= visible.lastExclusive) {
                    updateScrollToSource(source);
                }
                if (index >= visible.first && index < visible.lastExclusive) {
                    currentFilmstripTimelineIndex = static_cast<std::size_t>(index);
                    if (creationMode && !multiSelected) {
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.38f);
                    }
                    drawThumbnail(
                        source,
                        ImVec2(tileWidth, tileHeight),
                        imageHeight,
                        false,
                        source.fileName,
                        {},
                        &source);
                    if (creationMode && !multiSelected) {
                        ImGui::PopStyleVar();
                    }
                } else {
                    ImGui::Dummy(ImVec2(tileWidth, tileHeight));
                }
            }
        } else {
            const int rowCount = static_cast<int>(
                (sources.size() + static_cast<std::size_t>(columns) - 1u) /
                static_cast<std::size_t>(columns));
            const auto visibleRows = ComputeGalleryVisibleRange(
                rowCount,
                tileHeight,
                ImGui::GetStyle().ItemSpacing.y,
                contentStart.y,
                clip.Min.y,
                clip.Max.y,
                RawLabGalleryPrefetchPixels(
                    std::max(0.0f, clip.Max.y - clip.Min.y)));
            for (int row = 0; row < rowCount; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const int index = row * columns + column;
                    if (index >= static_cast<int>(sources.size())) {
                        break;
                    }
                    if (column > 0) {
                        ImGui::SameLine(0.0f, gap);
                    }
                    const auto& source = *sources[static_cast<std::size_t>(index)];
                    const bool multiSelected = sourceMultiSelected(source);
                    applyGridAnchor(source.relativePathKey, ImGui::GetCursorScreenPos(), imageHeight);
                    RecordRawWorkspaceGallerySlot(source, ImGui::GetCursorScreenPos(),
                        ImVec2(tileWidth, imageHeight), clip);
                    if (row < visibleRows.first || row >= visibleRows.lastExclusive) {
                        updateScrollToSource(source);
                    }
                    if (row >= visibleRows.first && row < visibleRows.lastExclusive) {
                        if (creationMode && !multiSelected) {
                            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.38f);
                        }
                        drawThumbnail(
                            source,
                            ImVec2(tileWidth, tileHeight),
                            imageHeight,
                            true,
                            source.fileName,
                            {},
                            &source);
                        if (creationMode && !multiSelected) {
                            ImGui::PopStyleVar();
                        }
                    } else {
                        ImGui::Dummy(ImVec2(tileWidth, tileHeight));
                    }
                }
            }
        }
        return;
    }

    if (!compactFilmstrip &&
        m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::List) {
        if (projectsOnly && !presentation.projects.empty()) {
            ImGui::TextDisabled("%s",m_RawWorkspaceGalleryContentMode==Stack::RawWorkspace::GalleryContentMode::Bracket?"Bracket":"Projects");
            for (const auto& project : presentation.projects) {
                PushGalleryKeyID(project.projectCoverKey);
                const std::string row = project.projectName + "    " +
                    (!project.multiFrameProject
                        ? std::string("Single image")
                        : std::to_string(project.frameCount) +
                            (project.frameCount == 1u ? " frame" : " frames"));
                const bool listProjectSelected =
                    isProjectSelected(project.projectPath);
                if (GalleryListSelectable(row, listProjectSelected) && !project.projectPath.empty()) {
                    selectProject(
                        project.projectPath,
                        project.projectName,
                        ImGui::GetIO().KeyCtrl,
                        ImGui::GetIO().KeyShift);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                        !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
                        RequestOpenRawWorkspaceProjectFromGallery(project.projectPath);
                    }
                }
                if (!project.projectPath.empty() && ImGui::BeginPopupContextItem("RawGalleryProjectListContext")) {
                    if (!isProjectSelected(project.projectPath)) {
                        selectProject(
                            project.projectPath,
                            project.projectName,
                            false,
                            false);
                    } else {
                        m_RawWorkspaceLabFocusedProjectPath = project.projectPath;
                        m_RawWorkspaceLabFocusedProjectName = project.projectName;
                    }
                    RenderRawGalleryContextActions(project.projectPath);
                    ImGui::EndPopup();
                }
                const auto selectedProject = std::find_if(
                    m_RawWorkspaceLabSelectedProjectPaths.begin(),
                    m_RawWorkspaceLabSelectedProjectPaths.end(),
                    [&](const std::filesystem::path& selectedPath) {
                        return sameGalleryProjectPath(
                            selectedPath, project.projectPath);
                    });
                const bool selected = selectedProject !=
                    m_RawWorkspaceLabSelectedProjectPaths.end();
                const std::string tooltip = selected
                    ? project.projectName + "\nSelected project " +
                        std::to_string(static_cast<std::size_t>(std::distance(
                            m_RawWorkspaceLabSelectedProjectPaths.begin(),
                            selectedProject)) + 1u) +
                        "\nCtrl-click toggles; Shift-click selects a range."
                    : project.projectName +
                        "\nCtrl-click toggles; Shift-click selects a range.";
                LabTooltip(tooltip.c_str());
                ImGui::PopID();
            }
            ImGui::Spacing();
        }
        if (projectsOnly) {
            return;
        }
        for (const auto& group : presentation.groups) {
            ImGui::TextDisabled("%s", group.label.c_str());
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(group.sources.size()));
            while (clipper.Step()) {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                    const auto& view = group.sources[static_cast<std::size_t>(index)];
                    const Stack::RawWorkspace::SourceRecord* source =
                        FindRawWorkspaceSourceByKey(view.relativePathKey);
                    if (source == nullptr) {
                        continue;
                    }
                    PushGalleryKeyID(source->relativePathKey);
                    const bool selected = view.multiSelected;
                    const std::string row = source->fileName;
                    if (GalleryListSelectable(row, selected)) {
                        const bool doubleClicked =
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                            !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift &&
                            m_RawWorkspaceLabUi.galleryNavigationMode !=
                                RawGalleryNavigationMode::MultiFrameCreation;
                        selectSource(
                            source->relativePathKey,
                            ImGui::GetIO().KeyCtrl,
                            ImGui::GetIO().KeyShift,
                            doubleClicked);
                    }
                    if (ImGui::BeginPopupContextItem("RawGallerySourceListContext")) {
                        if (!selected) {
                            selectSource(
                                source->relativePathKey,
                                false,
                                false,
                                false);
                        }
                        RenderRawGalleryContextActions(source->absolutePath);
                        ImGui::EndPopup();
                    }
                    const std::string tooltip = source->relativePathKey +
                        "\nOriginal RAW. Double-click creates a new project.";
                    LabTooltip(tooltip.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::Spacing();
        }
        return;
    }

    const float thumbnailScale = compactFilmstrip ? std::min(m_RawWorkspaceLabUi.galleryThumbnailScale,
        std::max(1.f,ImGui::GetContentRegionAvail().y-8.f)/kRawLabFilmstripTileHeight) : m_RawWorkspaceLabUi.galleryThumbnailScale;
    auto& slots = gridBrowse.filmstripSlots;
    const auto slotX = [&](std::size_t i) { return slots.enabled ? slots.Position(i) : float(i) * (124.f * thumbnailScale + 10.f); };
    const auto slotWidth = [&](std::size_t i) { return slots.enabled ? slots.ItemWidth(i) : 124.f * thumbnailScale; };
    const bool alignedSlots = compactFilmstrip && slots.enabled;
    const float tileWidth = alignedSlots ? slots.tile : (compactFilmstrip ? 124.0f : 158.0f) * thumbnailScale;
    const float tileHeight = compactFilmstrip
        ? kRawLabFilmstripTileHeight * thumbnailScale
        : 148.0f * thumbnailScale;
    const float imageHeight = compactFilmstrip
        ? kRawLabFilmstripTileHeight * thumbnailScale
        : 108.0f * thumbnailScale;
    const float gap = alignedSlots ? slots.gap : compactFilmstrip ? 10.0f : 16.0f;
    if (compactFilmstrip) {
        const ImGuiIO& io = ImGui::GetIO();
        const float viewportWidth = ImGui::GetCurrentWindow()->ClipRect.GetWidth();
        auto& scrollMotion = gridBrowse.filmstripScroll;
        const float currentScrollX = ImGui::GetScrollX();
        scrollMotion.Observe(currentScrollX,m_RawWorkspaceLabFilmstripScrollTargetX,ImGui::GetFrameCount());
        const float wheelMaximum = std::max(0.f,ImGui::GetScrollMaxX());
        const auto applyWheel = [&](float wheel) {
            if (alignedSlots) {
                slots.Wheel(wheel, m_RawWorkspaceLabFilmstripScrollTargetX);
                return;
            }
            scrollMotion.Wheel(wheel, std::min(viewportWidth*.22f,(tileWidth+gap)*1.5f),
                wheelMaximum,ImGui::GetTime(),m_RawWorkspaceLabFilmstripScrollTargetX);
        };
        m_RawFilmstripHeight.ObserveScroll(currentScrollX, ImGui::GetTime(), io.DeltaTime);
        if (m_RawWorkspaceLabFilmstripDragState.phase !=
                RawGalleryFilmstripDragPhase::Idle &&
            m_RawWorkspaceLabFilmstripDragState.lastObservedScrollX >= 0.0f) {
            m_RawWorkspaceLabFilmstripDragState.originMinimum.x +=
                m_RawWorkspaceLabFilmstripDragState.lastObservedScrollX -
                currentScrollX;
            m_RawWorkspaceLabFilmstripDragState.lastObservedScrollX =
                currentScrollX;
        }
        if (!std::isfinite(m_RawWorkspaceLabFilmstripScrollTargetX) ||
            m_RawWorkspaceLabFilmstripScrollTargetX < 0.0f) {
            m_RawWorkspaceLabFilmstripScrollTargetX = currentScrollX;
        }
        if (!std::isfinite(m_RawWorkspaceLabFilmstripScrollLastAppliedX) ||
            m_RawWorkspaceLabFilmstripScrollLastAppliedX < 0.0f) {
            m_RawWorkspaceLabFilmstripScrollLastAppliedX = currentScrollX;
        }

        if (!m_LibraryWindowHovered &&
            !io.KeyCtrl &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            const float wheel = std::abs(io.MouseWheelH) > 0.0001f
                ? io.MouseWheelH
                : io.MouseWheel;
            if (std::abs(wheel) > 0.0001f) {
                applyWheel(wheel);
            }
        }

        const float scrollMaximumX = alignedSlots ? slots.maximum : std::max(0.f,ImGui::GetScrollMaxX());
        if (alignedSlots) {
            const bool continuous = (slots.dragging && io.MouseDown[0]) ||
                m_RawWorkspaceLabFilmstripDragState.phase == RawGalleryFilmstripDragPhase::Dragging;
            if (!continuous) m_RawWorkspaceLabFilmstripScrollTargetX = slots.Snap(m_RawWorkspaceLabFilmstripScrollTargetX);
            scrollMotion.overshoot = scrollMotion.rebound = 0.f;
        }
        scrollMotion.Step(m_RawWorkspaceLabFilmstripScrollTargetX,scrollMaximumX,io.DeltaTime,
            ImGui::GetTime(),Stack::LibraryUi::ReducedMotion());
        ImGui::SetScrollX(scrollMotion.current);
        m_RawWorkspaceLabFilmstripScrollLastAppliedX = scrollMotion.current;
        const ImRect filmstripClip = ImGui::GetCurrentWindow()->ClipRect;
        ImVec2 filmstripStart = ImGui::GetCursorScreenPos();
        if (alignedSlots) filmstripStart.x = ImGui::GetWindowPos().x + slots.gap * .5f - ImGui::GetScrollX();
        float visualRebound=scrollMotion.rebound;
        filmstripStart.x += visualRebound;
        ImGui::SetCursorScreenPos(filmstripStart);
        const float unscrolledContentOriginX =
            filmstripStart.x - visualRebound + ImGui::GetScrollX();
        m_RawWorkspaceLabFilmstripTimelineContentOriginX =
            unscrolledContentOriginX;
        const float filmstripAvailableWidth = std::max(
            0.0f,
            filmstripClip.Max.x - unscrolledContentOriginX);
        const auto centerFilmstripItems = [&](std::size_t itemCount) {
            if (alignedSlots) return; // The shared slot viewport already centers short collections.
            const auto horizontalLayout = ComputeFilmstripHorizontalLayout(
                itemCount,
                tileWidth,
                gap,
                filmstripAvailableWidth);
            if (!horizontalLayout.centered) {
                return;
            }
            visualRebound=0.f;scrollMotion.overshoot=scrollMotion.rebound=0.f;
            m_RawWorkspaceLabFilmstripScrollTargetX = 0.0f;
            m_RawWorkspaceLabFilmstripScrollLastAppliedX = 0.0f;
            ImGui::SetScrollX(0.0f);
            filmstripStart.x = unscrolledContentOriginX +
                horizontalLayout.leadingOffset;
            m_RawWorkspaceLabFilmstripTimelineContentOriginX =
                filmstripStart.x;
            ImGui::SetCursorScreenPos(filmstripStart);
        };
        const auto applyFilmstripAnchor = [&](std::size_t index, std::size_t count) {
            const bool explicitFocus = stackFilmstrip && !m_RawLabGalleryScrollToSourceKey.empty();
            if (!m_PermanentGalleryWorkspace || (!explicitFocus && !layoutMotion.AnchorPending())) return;
            const float width = static_cast<float>(count) * tileWidth +
                static_cast<float>(count > 0 ? count - 1 : 0) * gap;
            const float desired = filmstripClip.Min.x + filmstripClip.GetWidth() *
                (explicitFocus ? 0.5f : layoutMotion.AnchorFraction().x);
            float target = std::clamp(filmstripStart.x + ImGui::GetScrollX() +
                slotX(index) + slotWidth(index) * .5f - desired,
                0.0f, std::max(0.0f, width - filmstripClip.GetWidth()));
            if (alignedSlots) target = slots.Snap(filmstripStart.x + ImGui::GetScrollX() + slotX(index) + slotWidth(index) * .5f - desired);
            m_RawWorkspaceLabFilmstripScrollTargetX = target;
            m_RawWorkspaceLabFilmstripScrollLastAppliedX = target;
            ImGui::SetScrollX(target);
            layoutMotion.AnchorApplied();
            if (explicitFocus) m_RawLabGalleryScrollToSourceKey.clear();
        };
        if (m_RawWorkspaceLabFilmstripDragState.phase ==
                RawGalleryFilmstripDragPhase::Dragging &&
            io.MousePos.y >= filmstripClip.Min.y &&
            io.MousePos.y <= filmstripClip.Max.y &&
            scrollMaximumX > 0.0f) {
            constexpr float kAutoScrollEdge = 44.0f;
            constexpr float kAutoScrollMaximumPixelsPerSecond = 720.0f;
            const float autoScrollVelocity = Stack::RawWorkspace::
                ComputeRawGalleryFilmstripAutoScrollVelocity(
                    io.MousePos.x,
                    filmstripClip.Min.x,
                    filmstripClip.Max.x,
                    kAutoScrollEdge,
                    kAutoScrollMaximumPixelsPerSecond);
            if (std::abs(autoScrollVelocity) > 0.001f) {
                m_RawWorkspaceLabFilmstripScrollTargetX = std::clamp(
                    m_RawWorkspaceLabFilmstripScrollTargetX +
                        autoScrollVelocity * std::clamp(io.DeltaTime, 0.0f, 0.05f),
                    0.0f,
                    scrollMaximumX);
                m_RawWorkspaceLabFilmstripDrawerInteractionRetained = true;
            }
        }
        if (projectsOnly) {
            if (!m_RawWorkspaceLabFilmstripProjectTimelineActive ||
                m_RawWorkspaceLabFilmstripProjectTimelineRevision !=
                    m_RawWorkspaceGalleryRevision) {
                m_RawWorkspaceLabFilmstripTimelineEntries.clear();
                m_RawWorkspaceLabFilmstripTimelineEntries.reserve(projectStacks->size());
                m_RawWorkspaceLabFilmstripProjectReferenceSources.clear();
                m_RawWorkspaceLabFilmstripProjectReferenceSources.reserve(projectStacks->size());
                for (const auto& stack : *projectStacks) {
                    const auto& project = projectCover(stack);
                    const auto* referenceSource = projectReferenceSource(project);
                    m_RawWorkspaceLabFilmstripProjectReferenceSources.push_back(
                        referenceSource);
                    appendSingleFilmstripTimelineEntry(
                        project.projectName,
                        referenceSource,
                        std::max<std::size_t>(1u, stack.projectIndices.size()));
                }
                m_RawWorkspaceLabFilmstripProjectTimelineRevision =
                    m_RawWorkspaceGalleryRevision;
                m_RawWorkspaceLabFilmstripProjectTimelineActive = true;
            }
            centerFilmstripItems(projectStacks->size());
            const auto visibleItems = alignedSlots ? slots.Range(filmstripStart.x, filmstripClip.Min.x, filmstripClip.Max.x, RawLabGalleryPrefetchPixels(filmstripClip.GetWidth())) : ComputeGalleryVisibleRange(
                static_cast<int>(projectStacks->size()),
                tileWidth,
                gap,
                filmstripStart.x,
                filmstripClip.Min.x,
                filmstripClip.Max.x,
                RawLabGalleryPrefetchPixels(
                    std::max(
                        0.0f,
                        filmstripClip.Max.x - filmstripClip.Min.x)));
            const auto onScreen = alignedSlots ? slots.Range(filmstripStart.x,
                filmstripClip.Min.x, filmstripClip.Max.x, 0.0f) : ComputeGalleryVisibleRange(
                    static_cast<int>(projectStacks->size()), tileWidth, gap,
                    filmstripStart.x, filmstripClip.Min.x, filmstripClip.Max.x, 0.0f);
            auto& height = m_RawFilmstripHeight;
            if (height.first != onScreen.first || height.last != onScreen.lastExclusive ||
                height.revision != m_RawWorkspaceGalleryRevision) {
                height.first = onScreen.first;
                height.last = onScreen.lastExclusive;
                height.revision = m_RawWorkspaceGalleryRevision;
                height.maximum = 1u;
                for (int index = onScreen.first; index < onScreen.lastExclusive; ++index)
                    height.maximum = std::max(height.maximum,
                        (*projectStacks)[static_cast<std::size_t>(index)].projectIndices.size());
            }
            if (m_PermanentGalleryWorkspace) {
                const bool needAllSlots = layoutMotion.Active() || layoutMotion.AnchorPending() ||
                    !m_RawLabGalleryScrollToSourceKey.empty();
                const auto firstSlot = needAllSlots ? 0u : static_cast<std::size_t>(visibleItems.first);
                const auto lastSlot = needAllSlots ? projectStacks->size() : static_cast<std::size_t>(visibleItems.lastExclusive);
                for (std::size_t index = firstSlot; index < lastSlot; ++index) {
                    const float tileWidth = slotWidth(index);
                    const auto& project = projectCover((*projectStacks)[index]);
                    Stack::RawWorkspace::SourceRecord slot;
                    slot.relativePathKey = project.projectCoverKey;
                    if (layoutMotion.AnchorKey() == slot.relativePathKey)
                        applyFilmstripAnchor(index, projectStacks->size());
                    RecordRawWorkspaceGallerySlot(slot, ImVec2(filmstripStart.x +
                        slotX(index), filmstripStart.y),
                        ImVec2(tileWidth, imageHeight), filmstripClip);
                }
            }

            for (int tileIndex = visibleItems.first;
                 tileIndex < visibleItems.lastExclusive;
                 ++tileIndex) {
                const float tileWidth = slotWidth(tileIndex);
                ImGui::SetCursorScreenPos(ImVec2(
                    filmstripStart.x + slotX(tileIndex),
                    filmstripStart.y));
                const auto& family = (*projectStacks)[static_cast<std::size_t>(tileIndex)];
                const auto& project = projectCover(family);
                {
                    currentFilmstripTimelineIndex =
                        static_cast<std::size_t>(tileIndex);
                    const auto projectIndex = static_cast<std::size_t>(&project - presentation.projects.data());
                    const auto& tileSource = m_RawWorkspaceGalleryProjectTileSources[projectIndex];
                    if (filmstripDrawerExpansion <= 0.001f || family.projectIndices.size() <= 1u) drawThumbnail(
                        tileSource,
                        ImVec2(tileWidth, tileHeight),
                        imageHeight,
                        false,
                        project.projectName,
                        project.projectPath,
                        &project, false, 1u, 1.0f, nullptr, true,
                        std::numeric_limits<float>::infinity(), false, &family.projectIndices);
                    else {
                        const ImVec2 base = ImGui::GetCursorScreenPos();
                        ImGui::Dummy(ImVec2(tileWidth, tileHeight));
                        Stack::Editor::RawLabInternal::RenderGalleryFilmstripStackWindow(
                            family.familyKey, base, tileWidth, tileHeight, gap,
                            family.projectIndices.size(), filmstripClip.Min.y + 14.0f,
                            filmstripDrawerExpansion, filmstripClip, [&](const auto& layout) {
                                HandleRawGalleryActionShortcuts();
                                if (!m_LibraryWindowHovered && !io.KeyCtrl && ImGui::IsWindowHovered()) {
                                    const float wheel = std::abs(io.MouseWheelH) > 0.0001f ? io.MouseWheelH : io.MouseWheel;
                                    if (std::abs(wheel) > 0.0001f) applyWheel(wheel);
                                }
                                ImRect badge;
                                for (std::size_t reverse = family.projectIndices.size(); reverse > 0u; --reverse) {
                                    const auto depth = reverse - 1u;
                                    // Retain catalog order for the open drawer;
                                    // selecting a version must not move its hit target.
                                    const auto index = family.projectIndices[depth];
                                    const auto& member = presentation.projects[index];
                                    ImGui::SetCursorScreenPos({base.x, base.y - layout.cardStep * static_cast<float>(depth)});
                                    const float exposedBottom = depth == 0u ? std::numeric_limits<float>::infinity() :
                                        std::min(base.y, base.y - layout.cardStep * static_cast<float>(depth) + layout.cardStep);
                                    drawThumbnail(m_RawWorkspaceGalleryProjectTileSources[index],
                                        {tileWidth, tileHeight}, imageHeight, false,
                                        member.projectName, member.projectPath, &member,
                                        false, 1u, 1.0f, nullptr, true, exposedBottom,
                                        false, nullptr, depth + 1u == family.projectIndices.size() ? &badge : nullptr,
                                        std::numeric_limits<float>::infinity(), &member);
                                }
                                if (badge.GetWidth() > 0.0f)
                                    DrawGalleryStackBadges(ImGui::GetWindowDrawList(), badge,
                                        family.projectIndices.size(), 1.0f, false, true);
                            });
                    }
                }
            }
            if (!projectStacks->empty()) {
                ImGui::SetCursorScreenPos(ImVec2(
                    filmstripStart.x +
                        slotX(projectStacks->size() - 1u),
                    filmstripStart.y));
                ImGui::Dummy(ImVec2(slotWidth(projectStacks->size() - 1u), tileHeight));
            }
        } else {
            const auto& filmstripStacks = ResolveRawWorkspaceVisibleGalleryStacks();
            const auto& viewsBySource = m_RawWorkspaceLabGalleryViewsBySource;
            if (!m_RawWorkspaceLabFilmstripStackTimelineActive) {
                m_RawWorkspaceLabFilmstripTimelineEntries.clear();
                timelineSourcesByKey.reserve(m_RawWorkspace.sources.size());
                for (const auto& source : m_RawWorkspace.sources)
                    timelineSourcesByKey.emplace(source.relativePathKey, &source);
                if (m_PinnedRawWorkspaceSource.has_value()) {
                    timelineSourcesByKey.insert_or_assign(
                        m_PinnedRawWorkspaceSource->relativePathKey,
                        &(*m_PinnedRawWorkspaceSource));
                }
                m_RawWorkspaceLabFilmstripTimelineEntries.reserve(
                    filmstripStacks.size());
                for (const auto& stack : filmstripStacks) {
                    const auto found = timelineSourcesByKey.find(
                        stack.sourceKeys.front());
                    const auto* source = found != timelineSourcesByKey.end()
                        ? found->second : nullptr;
                    appendFilmstripTimelineEntry(
                        !stack.resultProjectName.empty()
                            ? stack.resultProjectName
                            : source != nullptr ? source->fileName : std::string(),
                        stack.sourceKeys,
                        stack.sourceKeys.size());
                }
                m_RawWorkspaceLabFilmstripStackTimelineActive = true;
            }
            centerFilmstripItems(filmstripStacks.size());
            const auto visibleItems = alignedSlots ? slots.Range(filmstripStart.x, filmstripClip.Min.x, filmstripClip.Max.x, RawLabGalleryPrefetchPixels(filmstripClip.GetWidth())) : ComputeGalleryVisibleRange(
                static_cast<int>(filmstripStacks.size()),
                tileWidth,
                gap,
                filmstripStart.x,
                filmstripClip.Min.x,
                filmstripClip.Max.x,
                RawLabGalleryPrefetchPixels(
                    std::max(
                        0.0f,
                        filmstripClip.Max.x - filmstripClip.Min.x)));
            // Record offscreen destinations only when a transition or scroll request needs them.
            if (m_PermanentGalleryWorkspace) {
                const bool needAllSlots = layoutMotion.Active() || layoutMotion.AnchorPending() ||
                    !m_RawLabGalleryScrollToSourceKey.empty();
                const auto firstSlot = needAllSlots ? 0u : static_cast<std::size_t>(visibleItems.first);
                const auto lastSlot = needAllSlots ? filmstripStacks.size() : static_cast<std::size_t>(visibleItems.lastExclusive);
                for (std::size_t index = firstSlot; index < lastSlot; ++index) {
                    const float tileWidth = slotWidth(index);
                    const auto& stack = filmstripStacks[index];
                    const auto& order = stack.sourceKeys;
                    const ImVec2 base(filmstripStart.x + slotX(index),
                        filmstripStart.y);
                    const auto slots = Stack::RawWorkspace::ComputeRawGalleryFilmstripStackLayout(
                        order.size(), tileHeight, gap, base.y, filmstripClip.Min.y,
                        expandedFilmstripDrawer ? filmstripDrawerExpansion : 0.0f);
                    constexpr std::size_t frontDepth = 0;
                    const float frontTop = base.y - slots.cardStep * static_cast<float>(frontDepth);
                    for (std::size_t depth = 0; depth < order.size(); ++depth) {
                        const auto view = viewsBySource.find(order[depth]);
                        if (view == viewsBySource.end()) continue;
                        const auto catalogIndex = view->second->sourceIndex;
                        const auto* source = catalogIndex < m_RawWorkspace.sources.size() &&
                            m_RawWorkspace.sources[catalogIndex].relativePathKey == order[depth]
                            ? &m_RawWorkspace.sources[catalogIndex] : FindRawWorkspaceSourceByKey(order[depth]);
                        if (!source) continue;
                        const bool focusMember = !m_RawLabGalleryScrollToSourceKey.empty()
                            ? m_RawLabGalleryScrollToSourceKey == order[depth]
                            : layoutMotion.AnchorKey() == order[depth];
                        if (focusMember) applyFilmstripAnchor(index, filmstripStacks.size());
                        const ImVec2 minimum(base.x, base.y - slots.cardStep * static_cast<float>(depth));
                        ImRect exposed = filmstripClip;
                        exposed.Max.y = std::min(exposed.Max.y, depth == frontDepth
                            ? minimum.y + tileHeight : std::min(frontTop, minimum.y + slots.cardStep));
                        exposed.Min.y = std::max(exposed.Min.y, minimum.y);
                        if ((!expandedFilmstripDrawer && depth != frontDepth) || exposed.Max.y < exposed.Min.y)
                            exposed.Max.y = exposed.Min.y;
                        RecordRawWorkspaceGallerySlot(*source, minimum,
                            ImVec2(tileWidth, imageHeight), exposed);
                    }
                }
            }

            auto onScreen = alignedSlots ? slots.Range(filmstripStart.x, filmstripClip.Min.x, filmstripClip.Max.x, 0.f) : ComputeGalleryVisibleRange(
                static_cast<int>(filmstripStacks.size()), tileWidth, gap,
                filmstripStart.x, filmstripClip.Min.x, filmstripClip.Max.x, 0.0f);
            // Drawing includes a spare edge slot; height and hover use only
            // cards that actually intersect the visible strip.
            while (onScreen.lastExclusive > onScreen.first && filmstripStart.x +
                slotX(onScreen.lastExclusive-1) >= filmstripClip.Max.x)
                --onScreen.lastExclusive;
            while (onScreen.first < onScreen.lastExclusive && filmstripStart.x +
                slotX(onScreen.first)+slotWidth(onScreen.first) <= filmstripClip.Min.x)
                ++onScreen.first;
            auto& height = m_RawFilmstripHeight;
            if (height.first != onScreen.first || height.last != onScreen.lastExclusive ||
                height.revision != m_RawWorkspaceNavigatorRevision ||
                height.organization != m_RawWorkspaceFilmstripOrganizationRevision) {
                height.first = onScreen.first;
                height.last = onScreen.lastExclusive;
                height.revision = m_RawWorkspaceNavigatorRevision;
                height.organization = m_RawWorkspaceFilmstripOrganizationRevision;
                height.maximum = 1;
                for (int index = onScreen.first; index < onScreen.lastExclusive; ++index)
                    height.maximum = std::max(height.maximum, filmstripStacks[index].sourceKeys.size());
            }
            constexpr float badgeHeadroom = 14.0f;
            const float commonHoverTop = Stack::RawWorkspace::ComputeRawGalleryFilmstripStackLayout(
                height.maximum, tileHeight, gap, filmstripStart.y,
                filmstripClip.Min.y + badgeHeadroom, filmstripDrawerExpansion).top;
            struct VisibleSimilarityStack {
                std::size_t stackIndex = 0;
                ImVec2 baseMinimum;
                std::vector<std::string> orderedMembers;
            };
            std::vector<VisibleSimilarityStack> visibleStacks;
            visibleStacks.reserve(static_cast<std::size_t>(
                std::max(0, visibleItems.lastExclusive - visibleItems.first)));
            for (int visibleIndex = visibleItems.first;
                 visibleIndex < visibleItems.lastExclusive;
                 ++visibleIndex) {
                const std::size_t stackIndex =
                    static_cast<std::size_t>(visibleIndex);
                const float tileWidth = slotWidth(stackIndex);
                const ImVec2 desiredBaseMinimum(
                    filmstripStart.x + slotX(stackIndex),
                    filmstripStart.y);
                const auto& stack = filmstripStacks[stackIndex];
                const std::string stackKey = stack.sourceKeys.empty()
                    ? std::string {}
                    : stack.sourceKeys.front();
                const float desiredSlotX = slotX(stackIndex);
                auto& slotAnimation =
                    m_RawWorkspaceLabFilmstripStackAnimations[stackKey];
                if (!slotAnimation.initialized) {
                    slotAnimation.slotX = desiredSlotX;
                    for (const std::string& sourceKey : stack.sourceKeys) {
                        const auto previous =
                            m_RawWorkspaceLabFilmstripSourceLastSlotX.find(
                                sourceKey);
                        if (previous !=
                            m_RawWorkspaceLabFilmstripSourceLastSlotX.end()) {
                            slotAnimation.slotX = previous->second;
                            break;
                        }
                    }
                    slotAnimation.initialized = true;
                }
                // Permanent Gallery publishes background order directly into
                // settled slots; only deliberate navigation uses the overlay.
                if (m_PermanentGalleryWorkspace || std::abs(desiredSlotX - slotAnimation.slotX) < .1f ||
                    Stack::LibraryUi::ReducedMotion()) {
                    slotAnimation.slotX = desiredSlotX;
                } else {
                    const float slotResponse = 1.0f - std::exp(
                        -std::max(io.DeltaTime, 0.0f) / 0.055f);
                    slotAnimation.slotX +=
                        (desiredSlotX - slotAnimation.slotX) * slotResponse;
                }
                const ImVec2 baseMinimum(
                    filmstripStart.x + slotAnimation.slotX,
                    desiredBaseMinimum.y);
                for (const std::string& sourceKey : stack.sourceKeys) {
                    m_RawWorkspaceLabFilmstripSourceLastSlotX[sourceKey] =
                        slotAnimation.slotX;
                }
                if (!m_RawLabGalleryScrollToSourceKey.empty() &&
                    std::find(stack.sourceKeys.begin(), stack.sourceKeys.end(),
                        m_RawLabGalleryScrollToSourceKey) != stack.sourceKeys.end()) {
                    if (const auto* source = FindRawWorkspaceSourceByKey(
                            m_RawLabGalleryScrollToSourceKey)) {
                        updateScrollToSource(*source);
                    }
                }
                {
                    const auto& orderedMembers = stack.sourceKeys;
                    if (!orderedMembers.empty()) {
                        const std::string& frontSourceKey =
                            orderedMembers.front();
                        const auto viewIt = viewsBySource.find(frontSourceKey);
                        if (viewIt != viewsBySource.end() && viewIt->second->sourceIndex < m_RawWorkspace.sources.size()) {
                            const auto* source = &m_RawWorkspace.sources[viewIt->second->sourceIndex];
                            const auto& view = *viewIt->second;
                            const Stack::RawWorkspace::SourceRecord& tileSource =
                                galleryTileSource(*source, view);
                            ImGui::SetCursorScreenPos(baseMinimum);
                            currentFilmstripTimelineIndex = stackIndex;
                            drawThumbnail(
                                tileSource,
                                ImVec2(tileWidth, tileHeight),
                                imageHeight,
                                false,
                                source->fileName,
                                {},
                                source,
                                false,
                                filmstripDrawerExpansion > 0.001f
                                    ? 1u
                                    : orderedMembers.size(),
                                expandedFilmstripDrawer
                                    ? 1.0f - filmstripDrawerExpansion
                                    : 1.0f,
                                &stack.sourceKeys,
                                !expandedFilmstripDrawer ||
                                    filmstripDrawerExpansion <= 0.001f ||
                                    orderedMembers.size() == 1u,
                                std::numeric_limits<float>::infinity(),
                                stack.hasSavedBracketingResult && (orderedMembers.size() == 1u || filmstripDrawerExpansion <= 0.001f),
                                nullptr, nullptr, commonHoverTop);
                        }
                    }
                    if (expandedFilmstripDrawer &&
                        filmstripDrawerExpansion > 0.001f &&
                        orderedMembers.size() > 1u) {
                        visibleStacks.push_back({
                            stackIndex,
                            baseMinimum,
                            orderedMembers
                        });
                    }
                }
                // Keep scroll extents tied to the destination slot while the
                // thumbnail itself eases to it. This makes regrouping animate
                // without moving the user's horizontal scroll position.
                ImGui::SetCursorScreenPos(desiredBaseMinimum);
                ImGui::Dummy(ImVec2(tileWidth, tileHeight));
            }
            if (!m_RawLabGalleryScrollToSourceKey.empty()) {
                for (std::size_t stackIndex = 0;
                     stackIndex < filmstripStacks.size(); ++stackIndex) {
                    const auto& keys = filmstripStacks[stackIndex].sourceKeys;
                    if (std::find(keys.begin(), keys.end(),
                            m_RawLabGalleryScrollToSourceKey) == keys.end())
                        continue;
                    if (const auto* source = FindRawWorkspaceSourceByKey(
                            m_RawLabGalleryScrollToSourceKey)) {
                        ImGui::SetCursorScreenPos(ImVec2(
                            filmstripStart.x + slotX(stackIndex),
                            filmstripStart.y));
                        updateScrollToSource(*source);
                    }
                    break;
                }
            }
            if (!filmstripStacks.empty()) {
                ImGui::SetCursorScreenPos(ImVec2(
                    filmstripStart.x +
                        slotX(filmstripStacks.size() - 1u),
                    filmstripStart.y));
                ImGui::Dummy(ImVec2(slotWidth(filmstripStacks.size() - 1u), tileHeight));
            }

            ImGuiViewport* mainViewport = ImGui::GetMainViewport();
            const float drawerTop = filmstripClip.Min.y + badgeHeadroom;
            bool stackWheelApplied = false;
            for (const VisibleSimilarityStack& visibleStack : visibleStacks) {
                const float tileWidth = slotWidth(visibleStack.stackIndex);
                currentFilmstripTimelineIndex = visibleStack.stackIndex;
                const std::vector<std::string>& orderedMembers =
                    visibleStack.orderedMembers;
                const auto layout =
                    Stack::RawWorkspace::ComputeRawGalleryFilmstripStackLayout(
                        orderedMembers.size(),
                        tileHeight,
                        gap,
                        visibleStack.baseMinimum.y,
                        drawerTop,
                        filmstripDrawerExpansion);
                const float windowMinimumX = std::max(
                    visibleStack.baseMinimum.x - (alignedSlots ? (slots.tile-tileWidth)*.5f : 0.f),
                    filmstripClip.Min.x);
                const float windowMaximumX = std::min(
                    visibleStack.baseMinimum.x + tileWidth + (alignedSlots ? (slots.tile-tileWidth)*.5f : 0.f),
                    filmstripClip.Max.x);
                if (windowMaximumX <= windowMinimumX ||
                    layout.bottom <= layout.top) {
                    continue;
                }

                const float windowTop = std::min(commonHoverTop, layout.top) - badgeHeadroom;
                ImGui::SetNextWindowViewport(mainViewport->ID);
                ImGui::SetNextWindowPos(
                    ImVec2(windowMinimumX, windowTop),
                    ImGuiCond_Always);
                ImGui::SetNextWindowSize(
                    ImVec2(
                        windowMaximumX - windowMinimumX,
                        layout.bottom - windowTop),
                    ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                const auto& stackIdentity = filmstripStacks[visibleStack.stackIndex].sourceKeys.front();
                const std::string stackWindowName =
                    "RawFilmstripSimilarityStack###RawFilmstripSimilarityStack_" +
                    std::to_string(ImHashData(stackIdentity.data(), stackIdentity.size(),
                        ImGui::GetCurrentWindow()->ID));
                const ImGuiWindowFlags stackWindowFlags =
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoNav |
                    ImGuiWindowFlags_NoBackground |
                    ImGuiWindowFlags_NoDocking;
                if (ImGui::Begin(
                        stackWindowName.c_str(),
                        nullptr,
                        stackWindowFlags)) {
                    HandleRawGalleryActionShortcuts();
                    // Expanded cards use independent transparent windows so
                    // they can overlap during the slide. The drawer promotes
                    // itself above the workspace, which also places its opaque
                    // surface above existing card windows. Bring cards back in
                    // front unless a popup is open. Popups retain the highest
                    // layer while menus and context actions are active.
                    if (!ImGui::IsPopupOpen(
                            nullptr,
                            ImGuiPopupFlags_AnyPopupId)) {
                        ImGui::BringWindowToDisplayFront(
                            ImGui::GetCurrentWindow());
                    }
                    if (!stackWheelApplied &&
                        !m_LibraryWindowHovered &&
                        !io.KeyCtrl &&
                        ImGui::IsWindowHovered()) {
                        const float wheel = std::abs(io.MouseWheelH) > 0.0001f
                            ? io.MouseWheelH
                            : io.MouseWheel;
                        if (std::abs(wheel) > 0.0001f) {
                            applyWheel(wheel);
                            stackWheelApplied = true;
                        }
                    }
                    const std::size_t closingFrontDepth =
                        m_RawWorkspaceLabFilmstripDrawerState.open ? orderedMembers.size() : 0u;
                    ImRect topBadgeArea;
                    const auto drawStackMember = [&](std::size_t depth,
                                                      bool visualFront) {
                        const std::string& sourceKey = orderedMembers[depth];
                        const auto viewIt = viewsBySource.find(sourceKey);
                        if (viewIt == viewsBySource.end() || viewIt->second->sourceIndex >= m_RawWorkspace.sources.size())
                            return;
                        const auto* source = &m_RawWorkspace.sources[viewIt->second->sourceIndex];
                        ImGui::SetCursorScreenPos(ImVec2(
                            visibleStack.baseMinimum.x,
                            visibleStack.baseMinimum.y -
                                layout.cardStep * static_cast<float>(depth)));
                        const auto& view = *viewIt->second;
                        const Stack::RawWorkspace::SourceRecord& tileSource =
                            galleryTileSource(*source, view);
                        const auto& resultStack=filmstripStacks[visibleStack.stackIndex];
                        const bool resultCover=visualFront&&filmstripDrawerExpansion<=0.001f&&!resultStack.resultProjectPath.empty();
                        std::optional<Stack::RawWorkspace::SourceRecord> displayed;
                        const auto* displayedSource = &tileSource;
                        if(resultCover) {
                            displayed = tileSource;
                            displayed->relativePathKey=resultStack.resultProjectCoverKey;
                            displayed->fileName=resultStack.resultProjectName;
                            displayed->thumbnail.absolutePath=resultStack.resultCoverPath;
                            displayed->thumbnail.status=Stack::RawWorkspace::ThumbnailStatus::Ready;
                            displayedSource = &*displayed;
                        }
                        float memberWidth = tileWidth;
                        if (alignedSlots) {
                            const auto cached = m_RawWorkspaceThumbnailTextures.find(displayedSource->relativePathKey);
                            if (cached != m_RawWorkspaceThumbnailTextures.end() && cached->second.height > 0)
                                memberWidth = std::min(slots.tile, imageHeight * cached->second.width / cached->second.height);
                            const auto cursor = ImGui::GetCursorScreenPos();
                            ImGui::SetCursorScreenPos({cursor.x + (tileWidth-memberWidth)*.5f, cursor.y});
                        }
                        currentFilmstripTimelineIndex = visibleStack.stackIndex;
                        const std::size_t frontDepth = closingFrontDepth < orderedMembers.size()
                            ? closingFrontDepth : 0u;
                        const float frontTop = visibleStack.baseMinimum.y -
                            layout.cardStep * static_cast<float>(frontDepth);
                        const float exposedBottom = visualFront
                            ? std::numeric_limits<float>::infinity()
                            : std::min(frontTop, visibleStack.baseMinimum.y -
                                layout.cardStep * static_cast<float>(depth) + layout.cardStep);
                        drawThumbnail(
                            *displayedSource,
                            ImVec2(memberWidth, tileHeight),
                            imageHeight,
                            false,
                            resultCover?resultStack.resultProjectName:source->fileName,
                            resultCover?resultStack.resultProjectPath:std::filesystem::path(),
                            source,
                            depth > 0u,
                            1u,
                            1.0f,
                            &filmstripStacks[visibleStack.stackIndex]
                                 .sourceKeys,
                            true,
                            exposedBottom,
                            false, nullptr,
                            depth + 1u == orderedMembers.size() ? &topBadgeArea : nullptr,
                            depth + 1u == orderedMembers.size() ? commonHoverTop : std::numeric_limits<float>::infinity());
                    };
                    for (std::size_t reverseIndex = orderedMembers.size();
                         reverseIndex > 0u;
                         --reverseIndex) {
                        const std::size_t depth = reverseIndex - 1u;
                        if (depth != closingFrontDepth) {
                            drawStackMember(
                                depth,
                                closingFrontDepth >= orderedMembers.size() &&
                                    depth == 0u);
                        }
                    }
                    if (closingFrontDepth < orderedMembers.size()) {
                        // Keep the first image in front as the stack retracts.
                        drawStackMember(closingFrontDepth, true);
                    }
                    // Draw once above every card so overlap never slices a badge.
                    if (topBadgeArea.GetWidth() > 0.0f) {
                        DrawGalleryStackBadges(ImGui::GetWindowDrawList(), topBadgeArea,
                            orderedMembers.size(), 1.0f,
                            filmstripStacks[visibleStack.stackIndex].hasSavedBracketingResult, true);
                    }
                    if (alignedSlots) {
                        const auto position = ImGui::GetWindowPos();
                        const auto size = ImGui::GetWindowSize();
                        Stack::Editor::RawLabInternal::DrawFilmstripEdgeFade(
                            ImGui::GetWindowDrawList(), {filmstripClip.Min.x, position.y},
                            {filmstripClip.Max.x, position.y + size.y}, 48.f * slots.unit,
                            GetWorkspaceBaseColor());
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar(3);
            }
            bool insertionTargetPreview = false;
            float insertionTargetX = filmstripStart.x;
            if (m_RawWorkspaceLabFilmstripDragState.imageTargetPreviewFrame !=
                    ImGui::GetFrameCount() &&
                ImGui::BeginDragDropTargetCustom(
                    filmstripClip,
                    ImGui::GetID("##RawFilmstripReorderDropTarget"))) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(
                            kRawLabFilmstripSourcePayload,
                            ImGuiDragDropFlags_AcceptBeforeDelivery |
                                ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    auto& drag = m_RawWorkspaceLabFilmstripDragState;
                    m_RawWorkspaceLabFilmstripDrawerInteractionRetained = true;
                    const float slotWidth = tileWidth + gap;
                    const std::size_t insertionIndex = alignedSlots ? static_cast<std::size_t>(std::lower_bound(slots.offsets.begin(), slots.offsets.end(), ImGui::GetIO().MousePos.x - filmstripStart.x) - slots.offsets.begin()) :
                        Stack::RawWorkspace::
                            ResolveRawGalleryFilmstripInsertionIndex(
                                ImGui::GetIO().MousePos.x,
                                filmstripStart.x,
                                slotWidth,
                                filmstripStacks.size());
                    insertionTargetPreview = true;
                    insertionTargetX = filmstripStart.x +
                        (alignedSlots ? (insertionIndex < slots.count ? slots.Position(insertionIndex) : slots.Position(slots.count-1)+slots.ItemWidth(slots.count-1)+gap) : static_cast<float>(insertionIndex) * slotWidth) -
                        gap * 0.5f;
                    drag.dropKind = RawGalleryFilmstripDropKind::Reorder;
                    drag.insertionStackIndex = insertionIndex;
                    drag.dropVisibleStacks = filmstripStacks;
                    drag.animationEndMinimum = ImVec2(
                        filmstripStart.x +
                            (alignedSlots ? (insertionIndex < slots.count ? slots.Position(insertionIndex) : slots.Position(slots.count-1)+slots.ItemWidth(slots.count-1)+gap) : static_cast<float>(insertionIndex) * slotWidth),
                        filmstripStart.y);
                    if (payload->IsDelivery() && payload->Data != nullptr) {
                        drag.deliveryHandledThisFrame = true;
                        drag.animationStartMinimum = drag.proxyMinimum;
                        drag.phaseStartedAt = ImGui::GetTime();
                        if (drag.collapsedStackSource) {
                            drag.phase = RawGalleryFilmstripDragPhase::
                                AwaitingStackChoice;
                            drag.popupRequested = true;
                        } else {
                            const bool changed =
                                ReorderRawWorkspaceFilmstripSources(
                                    { drag.sourceKey },
                                    drag.dropVisibleStacks,
                                    drag.insertionStackIndex,
                                    drag.sourceStackMembers.size() > 1u);
                            drag.phase = changed
                                ? RawGalleryFilmstripDragPhase::Settling
                                : RawGalleryFilmstripDragPhase::Returning;
                            if (!changed) {
                                drag.animationEndMinimum = drag.originMinimum;
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (insertionTargetPreview) {
                ImVec4 markerColor =
                    ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive);
                markerColor.w = 0.90f;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(insertionTargetX - 1.5f, filmstripClip.Min.y + 8.0f),
                    ImVec2(insertionTargetX + 1.5f, filmstripClip.Max.y - 8.0f),
                    ImGui::GetColorU32(markerColor),
                    1.5f);
            }

            auto& drag = m_RawWorkspaceLabFilmstripDragState;
            const auto startMotion = [&](RawGalleryFilmstripDragPhase phase,
                                         const ImVec2& destination) {
                drag.animationStartMinimum = drag.proxyMinimum;
                drag.animationEndMinimum = destination;
                drag.phaseStartedAt = ImGui::GetTime();
                drag.phase = phase;
            };
            const auto commitPendingDrop = [&](bool entireStack) {
                const std::vector<std::string> movingKeys = entireStack
                    ? drag.sourceStackMembers
                    : std::vector<std::string> { drag.sourceKey };
                bool changed = false;
                if (drag.dropKind == RawGalleryFilmstripDropKind::Group) {
                    changed = MergeRawWorkspaceFilmstripStackMembers(
                        movingKeys,
                        drag.targetStackMembers);
                } else if (drag.dropKind ==
                           RawGalleryFilmstripDropKind::Reorder) {
                    changed = ReorderRawWorkspaceFilmstripSources(
                        movingKeys,
                        drag.dropVisibleStacks,
                        drag.insertionStackIndex,
                        !entireStack && drag.sourceStackMembers.size() > 1u);
                }
                startMotion(
                    changed
                        ? RawGalleryFilmstripDragPhase::Settling
                        : RawGalleryFilmstripDragPhase::Returning,
                    changed
                        ? drag.animationEndMinimum
                        : drag.originMinimum);
            };

            if (drag.deliveryHandledThisFrame && drag.phase == RawGalleryFilmstripDragPhase::Dragging &&
                drag.dropKind == RawGalleryFilmstripDropKind::Group)
                commitPendingDrop(drag.collapsedStackSource);

            if (drag.phase == RawGalleryFilmstripDragPhase::Dragging &&
                !ImGui::IsDragDropActive() &&
                !drag.deliveryHandledThisFrame) {
                startMotion(
                    RawGalleryFilmstripDragPhase::Returning,
                    drag.originMinimum);
            }

            constexpr char kStackChoicePopup[] =
                "Raw filmstrip stack move";
            if (drag.phase ==
                RawGalleryFilmstripDragPhase::AwaitingStackChoice) {
                m_RawWorkspaceLabFilmstripDrawerInteractionRetained = true;
                if (drag.popupRequested) {
                    ImGui::SetNextWindowPos(
                        ImVec2(
                            drag.proxyMinimum.x + drag.tileSize.x * 0.5f,
                            drag.proxyMinimum.y),
                        ImGuiCond_Appearing,
                        ImVec2(0.5f, 1.0f));
                    ImGui::OpenPopup(kStackChoicePopup);
                    drag.popupRequested = false;
                }
                bool popupVisible = false;
                if (ImGui::BeginPopup(
                        kStackChoicePopup,
                        ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoSavedSettings)) {
                    popupVisible = true;
                    const bool grouping =
                        drag.dropKind == RawGalleryFilmstripDropKind::Group;
                    if (ImGui::MenuItem(
                            grouping
                                ? "Add this image to stack"
                                : "Move this image")) {
                        commitPendingDrop(false);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem(
                            grouping
                                ? "Add entire stack"
                                : "Move entire stack")) {
                        commitPendingDrop(true);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Cancel") ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        startMotion(
                            RawGalleryFilmstripDragPhase::Returning,
                            drag.originMinimum);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                if (!popupVisible &&
                    drag.phase ==
                        RawGalleryFilmstripDragPhase::AwaitingStackChoice &&
                    !ImGui::IsPopupOpen(kStackChoicePopup)) {
                    startMotion(
                        RawGalleryFilmstripDragPhase::Returning,
                        drag.originMinimum);
                }
            }

            bool resetDragAfterDraw = false;
            float proxyScale = 1.0f;
            float proxyOpacity = 1.0f;
            if (drag.phase == RawGalleryFilmstripDragPhase::Dragging) {
                const float lift = std::clamp(
                    static_cast<float>(
                        (ImGui::GetTime() - drag.phaseStartedAt) / 0.12),
                    0.0f,
                    1.0f);
                proxyScale = 1.0f + 0.02f *
                    (lift * lift * (3.0f - 2.0f * lift));
            } else if (drag.phase ==
                           RawGalleryFilmstripDragPhase::AwaitingStackChoice) {
                proxyScale = 1.02f;
            } else if (drag.phase == RawGalleryFilmstripDragPhase::Returning ||
                       drag.phase == RawGalleryFilmstripDragPhase::Settling) {
                const bool settling =
                    drag.phase == RawGalleryFilmstripDragPhase::Settling;
                const double duration = settling ? 0.16 : 0.18;
                const float progress = std::clamp(
                    static_cast<float>(
                        (ImGui::GetTime() - drag.phaseStartedAt) / duration),
                    0.0f,
                    1.0f);
                const float eased = Stack::RawWorkspace::
                    EaseRawGalleryFilmstripDrag(progress);
                drag.proxyMinimum = ImVec2(
                    drag.animationStartMinimum.x +
                        (drag.animationEndMinimum.x -
                         drag.animationStartMinimum.x) * eased,
                    drag.animationStartMinimum.y +
                        (drag.animationEndMinimum.y -
                         drag.animationStartMinimum.y) * eased);
                proxyScale = 1.02f - 0.02f * eased;
                proxyOpacity = settling ? 1.0f - 0.35f * eased : 1.0f;
                resetDragAfterDraw = progress >= 1.0f;
            }

            const Stack::RawWorkspace::SourceRecord* dragSource =
                drag.phase != RawGalleryFilmstripDragPhase::Idle
                    ? FindRawWorkspaceSourceByKey(drag.sourceKey)
                    : nullptr;
            if (dragSource != nullptr) {
                const auto drawProxyImage = [](
                    ImDrawList* drawList,
                    const ImRect& area,
                    unsigned int texture,
                    int textureWidth,
                    int textureHeight,
                    float opacity) {
                    drawList->AddRectFilled(
                        area.Min,
                        area.Max,
                        ImGui::GetColorU32(ImGuiCol_PopupBg, 0.90f * opacity),
                        5.0f);
                    if (texture != 0 && textureWidth > 0 && textureHeight > 0) {
                        const ImVec2 fitted = FitLabImage(
                            static_cast<float>(textureWidth),
                            static_cast<float>(textureHeight),
                            area.GetSize());
                        const ImVec2 minimum(
                            area.Min.x + (area.GetWidth() - fitted.x) * 0.5f,
                            area.Min.y + (area.GetHeight() - fitted.y) * 0.5f);
                        drawList->AddImage(
                            (ImTextureID)(intptr_t)texture,
                            minimum,
                            ImVec2(minimum.x + fitted.x, minimum.y + fitted.y),
                            ImVec2(0.0f, 1.0f),
                            ImVec2(1.0f, 0.0f),
                            IM_COL32(255, 255, 255,
                                static_cast<int>(255.0f * opacity)));
                    }
                };

                int textureWidth = 0;
                int textureHeight = 0;
                const unsigned int texture = GetRawWorkspaceThumbnailTexture(
                    *dragSource,
                    &textureWidth,
                    &textureHeight,
                    false);
                if (drag.phase == RawGalleryFilmstripDragPhase::Settling) {
                    // The organization has already changed at this point, so
                    // retain the old slot until the proxy reaches its new
                    // destination. During the live drag the in-place card
                    // renderer supplies the single 70% dark treatment.
                    const ImRect placeholderArea(
                        drag.originMinimum,
                        ImVec2(
                            drag.originMinimum.x + drag.tileSize.x,
                            drag.originMinimum.y + drag.imageHeight));
                    ImDrawList* placeholderDrawList =
                        ImGui::GetWindowDrawList();
                    drawProxyImage(
                        placeholderDrawList,
                        placeholderArea,
                        texture,
                        textureWidth,
                        textureHeight,
                        1.0f);
                    placeholderDrawList->AddRectFilled(
                        placeholderArea.Min,
                        placeholderArea.Max,
                        IM_COL32(0, 0, 0, 179),
                        5.0f);
                }

                const ImVec2 scaledSize(
                    drag.tileSize.x * proxyScale,
                    drag.imageHeight * proxyScale);
                const ImVec2 proxyMinimum(
                    drag.proxyMinimum.x -
                        (scaledSize.x - drag.tileSize.x) * 0.5f,
                    drag.proxyMinimum.y -
                        (scaledSize.y - drag.imageHeight) * 0.5f);
                const ImRect proxyArea(
                    proxyMinimum,
                    ImVec2(
                        proxyMinimum.x + scaledSize.x,
                        proxyMinimum.y + scaledSize.y));
                ImDrawList* foreground =
                    ImGui::GetForegroundDrawList(mainViewport);
                foreground->AddRectFilled(
                    ImVec2(proxyArea.Min.x + 4.0f, proxyArea.Min.y + 7.0f),
                    ImVec2(proxyArea.Max.x + 4.0f, proxyArea.Max.y + 7.0f),
                    IM_COL32(0, 0, 0,
                        static_cast<int>(92.0f * proxyOpacity)),
                    6.0f);
                drawProxyImage(
                    foreground,
                    proxyArea,
                    texture,
                    textureWidth,
                    textureHeight,
                    proxyOpacity);
                const bool selected = std::find(
                    m_RawWorkspace.selectedSourceKeys.begin(),
                    m_RawWorkspace.selectedSourceKeys.end(),
                    drag.sourceKey) != m_RawWorkspace.selectedSourceKeys.end();
                Stack::RawGallerySelectionVisuals::DrawTileSelection(
                    foreground,
                    proxyArea.Min,
                    proxyArea.Max,
                    selected,
                    drag.sourceKey == m_RawWorkspace.selectedSourceKey,
                    false,
                    0u,
                    true,
                    Stack::Editor::RawLabInternal::
                        ShouldUseSideFilmstripPerforations(
                            textureWidth,
                            textureHeight));
                if (drag.collapsedStackSource &&
                    drag.sourceStackMembers.size() > 1u) {
                    const std::string count =
                        std::to_string(drag.sourceStackMembers.size());
                    const ImVec2 countSize = ImGui::CalcTextSize(count.c_str());
                    foreground->AddText(
                        ImVec2(
                            proxyArea.Max.x - countSize.x - 6.0f,
                            proxyArea.Min.y + 6.0f),
                        ImGui::GetColorU32(ImGuiCol_Text),
                        count.c_str());
                }
            }
            if (resetDragAfterDraw) {
                m_RawWorkspaceLabFilmstripDragState = {};
            }
        }
        ImGui::GetCurrentWindow()->DC.CursorMaxPos.x -= visualRebound;
        return;
    }

    if(IsBracketingToolActive()&&!projectsOnly&&!m_PermanentGalleryWorkspace) {
        Stack::Editor::DrawBracketingGalleryStacks(ResolveRawWorkspaceFilmstripStacks(),{tileWidth,tileHeight},gap,
            [this](const std::string& key){return FindRawWorkspaceSourceByKey(key);},
            [&](const auto& source,const auto& label,const auto& path,const auto* members,std::size_t count) {
                drawThumbnail(source,{tileWidth,tileHeight},imageHeight,false,label,path,
                    &source,false,count,1.f,members);
            });
        return;
    }
    const float availableWidth = std::max(tileWidth, ImGui::GetContentRegionAvail().x);
    const int columns = std::max(
        1,
        static_cast<int>((availableWidth + gap) / (tileWidth + gap)));

    if (gridBrowse.columns != columns) {
        gridBrowse.columns = columns; ++gridBrowse.navigationRevision;
    }
    const std::string workspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (m_RawWorkspaceLabGalleryScrollWorkspaceKey != workspaceKey) {
        m_RawWorkspaceLabGalleryScrollWorkspaceKey = workspaceKey;
        m_RawWorkspaceLabGalleryScrollTargetY = ImGui::GetScrollY();
        m_RawWorkspaceLabGalleryScrollCurrentY = ImGui::GetScrollY();
    }
    if (m_RawWorkspaceLabGalleryScrollTargetY < 0.0f) {
        m_RawWorkspaceLabGalleryScrollTargetY = ImGui::GetScrollY();
        m_RawWorkspaceLabGalleryScrollCurrentY = ImGui::GetScrollY();
    }
    const float currentScrollY = ImGui::GetScrollY();
    if (m_RawWorkspaceLabGalleryScrollCurrentY >= 0.0f &&
        std::abs(currentScrollY - m_RawWorkspaceLabGalleryScrollCurrentY) > 2.0f) {
        // A scrollbar drag is deliberate and should not ease back to an old
        // wheel target.
        m_RawWorkspaceLabGalleryScrollTargetY = currentScrollY;
        m_RawWorkspaceLabGalleryScrollCurrentY = currentScrollY;
    }
    if (!m_LibraryWindowHovered &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl && std::abs(io.MouseWheel) > 0.0001f) {
            m_RawWorkspaceLabGalleryScrollTargetY -= io.MouseWheel * 90.0f;
        }
    }

    struct AnimatedGalleryItem {
        const Stack::RawWorkspace::SourceRecord* source = nullptr;
        std::string label;
        std::filesystem::path projectPath;
        const void* itemId = nullptr;
        std::string animationKey;
        const std::vector<std::string>* stackMembers = nullptr;
        bool hasSavedBracketingResult = false;
        const std::vector<std::size_t>* projectFamily = nullptr;
        const Stack::RawWorkspace::GalleryProjectView* project = nullptr;
    };

    if (!projectsOnly && m_PermanentGalleryWorkspace) ResolveRawWorkspaceVisibleGalleryStacks();
    const auto gridStackSource = [&](const std::string& key)
        -> const Stack::RawWorkspace::SourceRecord* {
        const auto view = m_RawWorkspaceLabGalleryViewsBySource.find(key);
        if (view == m_RawWorkspaceLabGalleryViewsBySource.end()) return nullptr;
        const auto index = view->second->sourceIndex;
        if (index < m_RawWorkspace.sources.size() &&
            m_RawWorkspace.sources[index].relativePathKey == key)
            return &m_RawWorkspace.sources[index];
        return FindRawWorkspaceSourceByKey(key);
    };
    const auto recordGridStackSlot = [&](const AnimatedGalleryItem& item,
        const ImVec2& minimum, float slotHeight, const ImRect& clip) {
        if (!item.source) return;
        const auto record = [&](const Stack::RawWorkspace::SourceRecord& source, bool cover) {
            applyGridAnchor(source.relativePathKey, minimum, slotHeight);
            ImRect exposed = clip;
            if (!cover) exposed.Max.y = exposed.Min.y;
            RecordRawWorkspaceGallerySlot(source, minimum, ImVec2(tileWidth, slotHeight), exposed);
        };
        if (item.stackMembers) {
            for (const auto& key : *item.stackMembers) {
                if (const auto* source = gridStackSource(key))
                    record(*source, key == item.source->relativePathKey);
            }
        } else record(*item.source, true);
    };

    const auto renderAnimatedGroup = [&](
        const std::string& groupKey,
        const std::string& label,
        const std::vector<AnimatedGalleryItem>& items) {
        if (items.empty()) {
            return;
        }

        const bool targetExpanded = gridBrowse.groupCount ||
            m_RawWorkspaceLabCollapsedGalleryGroups.find(groupKey) ==
            m_RawWorkspaceLabCollapsedGalleryGroups.end();
        RawGalleryFolderAnimationState& animation =
            m_RawWorkspaceLabGalleryGroupAnimations[groupKey];
        const float target = targetExpanded ? 1.0f : 0.0f;
        if (!animation.initialized) {
            animation.expansion = target;
            animation.initialized = true;
        } else {
            const float delta = target - animation.expansion;
            if (std::abs(delta) < 0.001f || Stack::LibraryUi::ReducedMotion()) {
                animation.expansion = target;
            } else {
                Stack::Header::ApproachMotion(animation.expansion, target, .18f);
            }
        }

        PushGalleryKeyID(groupKey);
        constexpr float headerHeight = 22.0f;
        constexpr float chevronHitSize = 16.0f;
        constexpr float chevronVisualSize = 10.0f;
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        const float headerWidth =
            chevronHitSize + 6.0f + labelSize.x + 8.0f;
        ImGui::InvisibleButton(
            "##GalleryFolderHeader",
            ImVec2(headerWidth, headerHeight));
        const bool headerHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked() && !gridBrowse.groupCount) {
            if (targetExpanded) {
                m_RawWorkspaceLabCollapsedGalleryGroups.insert(groupKey);
            } else {
                m_RawWorkspaceLabCollapsedGalleryGroups.erase(groupKey);
            }
        }

        const ImVec2 headerMinimum = ImGui::GetItemRectMin();
        const ImVec2 cursorAfterHeader = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec4 headerTint = ImGui::GetStyleColorVec4(
            headerHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        const ImVec2 center(
            headerMinimum.x + chevronHitSize * 0.5f,
            headerMinimum.y + headerHeight * 0.5f);
        const ImVec2 chevronMin(
            headerMinimum.x + (chevronHitSize - chevronVisualSize) * 0.5f,
            headerMinimum.y + (headerHeight - chevronVisualSize) * 0.5f);
        const ImVec2 chevronMax(
            chevronMin.x + chevronVisualSize,
            chevronMin.y + chevronVisualSize);

        if (m_ChevronIconTexture != 0) {
            const float angle = animation.expansion * (3.14159265f * 0.5f);
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            const float cX = (chevronMin.x + chevronMax.x) * 0.5f;
            const float cY = (chevronMin.y + chevronMax.y) * 0.5f;
            const float hs = chevronVisualSize * 0.5f;

            const ImVec2 p1(cX + (-hs * cosA - -hs * sinA), cY + (-hs * sinA + -hs * cosA));
            const ImVec2 p2(cX + ( hs * cosA - -hs * sinA), cY + ( hs * sinA + -hs * cosA));
            const ImVec2 p3(cX + ( hs * cosA -  hs * sinA), cY + ( hs * sinA +  hs * cosA));
            const ImVec2 p4(cX + (-hs * cosA -  hs * sinA), cY + (-hs * sinA +  hs * cosA));

            drawList->AddImageQuad(
                (ImTextureID)(intptr_t)m_ChevronIconTexture,
                p1, p2, p3, p4,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImVec2(0.0f, 1.0f),
                ImGui::GetColorU32(headerTint));
        } else {
            const float angle = animation.expansion * 3.14159265f * 0.5f;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const auto rotate = [&](float x, float y) {
                return ImVec2(
                    center.x + x * cosine - y * sine,
                    center.y + x * sine + y * cosine);
            };
            drawList->AddTriangleFilled(
                rotate(chevronVisualSize * 0.5f, 0.0f),
                rotate(-chevronVisualSize * 0.4f, -chevronVisualSize * 0.5f),
                rotate(-chevronVisualSize * 0.4f, chevronVisualSize * 0.5f),
                ImGui::GetColorU32(headerTint));
        }
        drawList->AddText(
            ImVec2(
                headerMinimum.x + chevronHitSize + 4.0f,
                headerMinimum.y + (headerHeight - labelSize.y) * 0.5f),
            ImGui::GetColorU32(headerTint),
            label.c_str());

        const float expansion = std::clamp(animation.expansion, 0.0f, 1.0f);
        const float easedExpansion =
            expansion * expansion * (3.0f - 2.0f * expansion);
        const float miniAlpha = 1.0f - easedExpansion;
        if (animation.expansion <= 0.0001f && m_PermanentGalleryWorkspace) {
            for (const auto& item : items) {
                if (!item.source) continue;
                const ImRect hidden(headerMinimum, headerMinimum);
                recordGridStackSlot(item, headerMinimum, headerHeight, hidden);
            }
        }
        if (miniAlpha > 0.01f) {
            constexpr float miniWidth = 24.0f;
            constexpr float miniHeight = 18.0f;
            constexpr float miniGap = 3.0f;
            float x = headerMinimum.x + chevronHitSize + 4.0f +
                labelSize.x + 14.0f;
            const float right = headerMinimum.x + availableWidth - 10.0f;
            for (std::size_t index = 0; index < items.size(); ++index) {
                if (x + miniWidth > right) {
                    const std::string remaining =
                        "+" + std::to_string(items.size() - index);
                    ImVec4 remainingTint =
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                    remainingTint.w *= miniAlpha * 0.8f;
                    drawList->AddText(
                        ImVec2(x, headerMinimum.y + 3.0f),
                        ImGui::GetColorU32(remainingTint),
                        remaining.c_str());
                    break;
                }
                const AnimatedGalleryItem& item = items[index];
                if (item.source == nullptr) {
                    continue;
                }
                const ImVec2 miniMinimum(
                    x,
                    headerMinimum.y + (headerHeight - miniHeight) * 0.5f);
                const ImVec2 miniMaximum(
                    x + miniWidth,
                    miniMinimum.y + miniHeight);
                ImGui::PushID(static_cast<int>(index));
                ImGui::SetCursorScreenPos(miniMinimum);
                ImGui::InvisibleButton(
                    "##GalleryMiniThumbnail",
                    ImVec2(miniWidth, miniHeight));
                const bool hovered = ImGui::IsItemHovered();
                const bool clicked = ImGui::IsItemClicked();
                ImGui::PopID();
                if (hovered) {
                    ImGui::SetTooltip("%s", item.label.c_str());
                    m_RawWorkspaceLabFilmstripHoverSourceKey = item.source->relativePathKey;
                    m_RawWorkspaceLabFilmstripHoverProjectPath = item.projectPath;
                    m_RawWorkspaceLabFilmstripHoverFrame = ImGui::GetFrameCount();
                    m_RawWorkspaceLabFilmstripHoverSuppressed = false;
                }
                if (clicked) {
                    m_RawWorkspaceLabCollapsedGalleryGroups.erase(groupKey);
                    if (item.projectPath.empty()) {
                        selectSource(
                            item.source->relativePathKey,
                            false,
                            false,
                            false);
                        m_RawLabGalleryScrollToSourceKey = item.source->relativePathKey;
                        m_RawLabGalleryFlashSourceKey = item.source->relativePathKey;
                        m_RawLabGalleryFlashStartTime = ImGui::GetTime();
                    } else {
                        selectProject(
                            item.projectPath,
                            item.label,
                            false,
                            false);
                    }
                }

                float& hoverAmount =
                    m_RawWorkspaceLabGalleryMiniThumbnailHover[item.animationKey];
                const float hoverTarget = hovered ? 1.0f : 0.0f;
                if (std::abs(hoverTarget - hoverAmount) < .001f || Stack::LibraryUi::ReducedMotion())
                    hoverAmount = hoverTarget;
                else hoverAmount += (hoverTarget - hoverAmount) *
                    (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 24.0f));
                const float scale = 1.0f + hoverAmount * 0.15f;
                const ImVec2 miniCenter(
                    (miniMinimum.x + miniMaximum.x) * 0.5f,
                    (miniMinimum.y + miniMaximum.y) * 0.5f);
                const ImVec2 drawMinimum(
                    miniCenter.x - miniWidth * scale * 0.5f,
                    miniCenter.y - miniHeight * scale * 0.5f);
                const ImVec2 drawMaximum(
                    miniCenter.x + miniWidth * scale * 0.5f,
                    miniCenter.y + miniHeight * scale * 0.5f);
                int textureWidth = 0;
                int textureHeight = 0;
                PrioritizeRawWorkspaceThumbnailSource(
                    item.source->relativePathKey);
                unsigned int texture = GetRawWorkspaceThumbnailTexture(
                    *item.source,
                    &textureWidth,
                    &textureHeight,
                    true);
                if (!texture && item.projectFamily && !item.projectFamily->empty()) {
                    const auto& project=presentation.projects[item.projectFamily->front()];
                    if (const auto* fallback=projectReferenceSource(project))
                        texture=GetRawWorkspaceThumbnailTexture(*fallback,&textureWidth,&textureHeight,false);
                }
                if (texture != 0) {
                    RecordRawWorkspaceGallerySlot(*item.source, drawMinimum,
                        ImVec2(drawMaximum.x - drawMinimum.x, drawMaximum.y - drawMinimum.y), viewClip);
                    if (!RecordRawWorkspaceGalleryThumbnail(item.source->relativePathKey,
                            drawMinimum, drawMaximum, miniAlpha)) drawList->AddImageRounded(
                        (ImTextureID)(intptr_t)texture,
                        drawMinimum,
                        drawMaximum,
                        ImVec2(0.0f, 1.0f),
                        ImVec2(1.0f, 0.0f),
                        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, miniAlpha)),
                        2.0f);
                } else {
                    drawList->AddRectFilled(
                        drawMinimum,
                        drawMaximum,
                        IM_COL32(255, 255, 255, static_cast<int>(24.0f * miniAlpha)),
                        2.0f);
                }
                x += miniWidth + miniGap;
            }
            ImGui::SetCursorScreenPos(cursorAfterHeader);
        }
        ImGui::PopID();

        if (animation.expansion > 0.0001f) {
            const int rowCount =
                (static_cast<int>(items.size()) + columns - 1) / columns;
            const float rowGap = ImGui::GetStyle().ItemSpacing.y;
            const float fullHeight = rowCount * tileHeight +
                std::max(0, rowCount - 1) * rowGap;
            const float animatedHeight = fullHeight * easedExpansion;
            const ImVec2 contentStart = ImGui::GetCursorScreenPos();
            if (m_PermanentGalleryWorkspace) {
                for (std::size_t index = 0; index < items.size(); ++index) {
                    const auto& item = items[index];
                    if (!item.source) continue;
                    const ImVec2 minimum(contentStart.x + static_cast<float>(index % columns) * (tileWidth + gap),
                        contentStart.y + static_cast<float>(index / columns) * (tileHeight + rowGap));
                    recordGridStackSlot(item, minimum, imageHeight, viewClip);
                }
            }
            const ImVec2 clipMaximum(
                contentStart.x + availableWidth,
                contentStart.y + animatedHeight);
            // Moving tiles can cross folder sections; their whole image and
            // decoration share the viewport until the layout settles.
            ImGui::PushClipRect(layoutMotion.Active() ? viewClip.Min : contentStart,
                layoutMotion.Active() ? viewClip.Max : clipMaximum, true);
            const ImRect galleryClip = ImGui::GetCurrentWindow()->ClipRect;
            const auto visibleRows = ComputeGalleryVisibleRange(
                rowCount,
                tileHeight,
                ImGui::GetStyle().ItemSpacing.y,
                contentStart.y,
                galleryClip.Min.y,
                std::min(galleryClip.Max.y, contentStart.y + animatedHeight),
                RawLabGalleryPrefetchPixels(
                    std::max(
                        0.0f,
                        galleryClip.Max.y - galleryClip.Min.y)));
            if (!m_RawLabGalleryScrollToSourceKey.empty()) {
                for (std::size_t index = 0; index < items.size(); ++index) {
                    const auto* source = items[index].source;
                    if (!source || source->relativePathKey != m_RawLabGalleryScrollToSourceKey) continue;
                    const int row = static_cast<int>(index) / columns;
                    if (row < visibleRows.first || row >= visibleRows.lastExclusive) {
                        ImGui::SetCursorScreenPos(ImVec2(contentStart.x,
                            contentStart.y + row * (tileHeight + ImGui::GetStyle().ItemSpacing.y)));
                        updateScrollToSource(*source);
                        ImGui::SetCursorScreenPos(contentStart);
                    }
                    break;
                }
            }
            Stack::Editor::RawLabInternal::SubmitGalleryGrid(
                static_cast<int>(items.size()), columns, ImVec2(tileWidth, tileHeight), gap,
                visibleRows, [&](int index) {
                const AnimatedGalleryItem& item = items[static_cast<std::size_t>(index)];
                if (item.source != nullptr) {
                    // The widget ID follows the stack, so changing its cover
                    // cannot steal a click or interrupt a double-click.
                    PushGalleryKeyID(item.animationKey);
                    drawThumbnail(
                        *item.source,
                        ImVec2(tileWidth, tileHeight),
                        imageHeight,
                        true,
                        item.label,
                        item.projectPath,
                        item.itemId,
                        false,
                        item.stackMembers ? item.stackMembers->size() : 1u,
                        1.0f,
                        item.stackMembers, true, std::numeric_limits<float>::infinity(),
                        item.hasSavedBracketingResult, item.projectFamily, nullptr,
                        std::numeric_limits<float>::infinity(), item.project);
                    ImGui::PopID();
                }
            });
            ImGui::PopClipRect();
            ImGui::SetCursorScreenPos(
                ImVec2(contentStart.x, contentStart.y + animatedHeight));
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f + 12.0f * easedExpansion));
    };

    if (projectsOnly) {
        std::vector<AnimatedGalleryItem> projectItems;
        if (gridBrowse.projectGroups && gridBrowse.groupCount) {
            projectItems.reserve(gridBrowse.groups[0].size());
            for (const auto& key : gridBrowse.groups[0]) {
                const auto found = m_RawWorkspaceGalleryProjectIndices.find(key);
                if (found == m_RawWorkspaceGalleryProjectIndices.end()) continue;
                const auto index = found->second;
                const auto& project = presentation.projects[index];
                projectItems.push_back({&m_RawWorkspaceGalleryProjectTileSources[index],
                    project.projectName, project.projectPath, &project,
                    "project:" + project.projectCoverKey, nullptr, false, nullptr, &project});
            }
        } else {
            projectItems.reserve(projectStacks->size());
            for (const auto& family : *projectStacks) {
                const auto& project = projectCover(family);
                const auto index = static_cast<std::size_t>(&project - presentation.projects.data());
                projectItems.push_back({&m_RawWorkspaceGalleryProjectTileSources[index],
                    project.projectName, project.projectPath, &project,
                    "project-family:" + family.familyKey, nullptr, false, &family.projectIndices});
            }
        }
        renderAnimatedGroup(gridBrowse.projectGroups ? "opened-project-stack" : "projects",
            gridBrowse.projectGroups ? "Stack" :
                m_RawWorkspaceGalleryContentMode == Stack::RawWorkspace::GalleryContentMode::Bracket ? "Bracket" : "Projects",
            projectItems);
        if (gridBrowse.projectGroups && gridBrowse.openedFrame != ImGui::GetFrameCount() &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             (!gridImageHovered && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::GetDragDropPayload())))
            gridBrowse.Close();
    }

    if (!projectsOnly && m_PermanentGalleryWorkspace && gridBrowse.groupCount) {
        for (int group = 0; group < gridBrowse.groupCount; ++group) {
            gridGroup = group;
            const auto start = ImGui::GetCursorScreenPos();
            std::vector<AnimatedGalleryItem> members;
            members.reserve(gridBrowse.groups[group].size());
            for (const auto& key : gridBrowse.groups[group]) {
                if (const auto* source = gridStackSource(key))
                    members.push_back({source, source->fileName, {}, source, "source:"+key});
            }
            renderAnimatedGroup(group == 0 ? "opened-stack-a" : "opened-stack-b",
                gridBrowse.groupCount == 1 ? "Stack" : group == 0 ? "First stack" : "Second stack", members);
            const auto end = ImGui::GetCursorScreenPos();
            const ImRect area(start, {start.x+availableWidth,std::max(end.y,start.y+tileHeight+32.f)});
            if (ImGui::BeginDragDropTargetCustom(area,ImGui::GetID(group ? "StackDropB" : "StackDropA"))) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("RawGalleryGridMembers")) {
                    if (payload->IsDelivery() && gridBrowse.dragGroup != group) {
                        gridBrowse.pendingMembers = gridBrowse.groups[group];
                        gridBrowse.pendingGroup = group;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SetCursorScreenPos({start.x,area.Max.y});
            ImGui::Dummy({0,0}); // Register the drop area in native layout, including an empty group.
            if (group+1 < gridBrowse.groupCount) {
                ImGui::Separator(); ImGui::Dummy({0,16.f});
            }
        }
        gridGroup = -1;
        if (gridBrowse.openedFrame != ImGui::GetFrameCount() &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
             (!gridImageHovered && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::GetDragDropPayload())))
            gridBrowse.Close();
    } else if (!projectsOnly && m_PermanentGalleryWorkspace) {
        const auto& stacks = ResolveRawWorkspaceVisibleGalleryStacks();
        for (const auto& section : m_RawWorkspaceLabVisibleGallerySections) {
            std::vector<AnimatedGalleryItem> sourceItems;
            sourceItems.reserve(section.stackCount);
            for (std::size_t index = section.firstStack;
                 index < section.firstStack + section.stackCount; ++index) {
                const auto* stack = &stacks[index];
                const auto cover = m_RawWorkspaceGalleryGridStacks.Cover(
                    stack->sourceKeys, {});
                const auto* source = gridStackSource(cover);
                const auto view = m_RawWorkspaceLabGalleryViewsBySource.find(cover);
                if (!source || view == m_RawWorkspaceLabGalleryViewsBySource.end()) continue;
                sourceItems.push_back({
                    source,
                    source->fileName,
                    {},
                    nullptr,
                    "stack:" + stack->sourceKeys.front(),
                    &stack->sourceKeys, stack->hasSavedBracketingResult });
            }
            renderAnimatedGroup(
                section.separateFolder ? "folder:" + section.folderKey : "all-originals",
                section.separateFolder
                    ? (section.folderKey.empty() ? "Workspace root" : section.folderKey)
                    : "All images",
                sourceItems);
        }
    } else if (!projectsOnly) {
        for (const auto& group : presentation.groups) {
            std::vector<AnimatedGalleryItem> sourceItems;
            sourceItems.reserve(group.sources.size());
            for (const auto& view : group.sources) {
                const auto* source = FindRawWorkspaceSourceByKey(view.relativePathKey);
                if (!source) continue;
                sourceItems.push_back({source, source->fileName, {}, source,
                    "source:" + source->relativePathKey});
            }
            renderAnimatedGroup("folder:" + group.folderKey, group.label, sourceItems);
        }
    }

    if (!gridBrowse.pendingMembers.empty() || gridBrowse.pendingGroup >= 0) {
        // Empty destinations use the moving set as their seed, preserving the
        // established grouping save/notification path.
        const bool moved = gridBrowse.pendingMembers.empty() && gridBrowse.dragged.size()==1
            ? DetachRawWorkspaceFilmstripStackMember(gridBrowse.dragged.front())
            : MergeRawWorkspaceFilmstripStackMembers(gridBrowse.dragged,gridBrowse.pendingMembers);
        if (moved) {
            if (gridBrowse.groupCount && gridBrowse.pendingGroup >= 0) {
                for (const auto& key : gridBrowse.dragged) {
                    for (auto& group : gridBrowse.groups) group.erase(std::remove(group.begin(),group.end(),key),group.end());
                    gridBrowse.groups[gridBrowse.pendingGroup].push_back(key);
                }
            }
            ++gridBrowse.navigationRevision;
        }
        gridBrowse.pendingMembers.clear(); gridBrowse.pendingGroup = -1;
    }
    if (!ImGui::GetDragDropPayload()) gridBrowse.dragged.clear();
    const float maximumScrollY = gridAnchorApplied
        ? std::max(0.0f, ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y +
            ImGui::GetScrollY() + ImGui::GetStyle().WindowPadding.y - ImGui::GetWindowSize().y)
        : std::max(0.0f, ImGui::GetScrollMaxY());
    m_RawWorkspaceLabGalleryScrollTargetY = std::clamp(
        m_RawWorkspaceLabGalleryScrollTargetY,
        0.0f,
        maximumScrollY);
    const float response = 1.0f - std::exp(
        -std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f) * 6.5f);
    m_RawWorkspaceLabGalleryScrollCurrentY +=
        (m_RawWorkspaceLabGalleryScrollTargetY -
         m_RawWorkspaceLabGalleryScrollCurrentY) * response;
    if (std::abs(
            m_RawWorkspaceLabGalleryScrollCurrentY -
            m_RawWorkspaceLabGalleryScrollTargetY) < 0.05f) {
        m_RawWorkspaceLabGalleryScrollCurrentY =
            m_RawWorkspaceLabGalleryScrollTargetY;
    }
    m_RawWorkspaceLabGalleryScrollCurrentY = std::clamp(
        m_RawWorkspaceLabGalleryScrollCurrentY,
        0.0f,
        maximumScrollY);
    ImGui::SetScrollY(m_RawWorkspaceLabGalleryScrollCurrentY);
}

void EditorModule::OpenRawWorkspaceLabNativeGallery() {
    if (Stack::Workspace::IsPreview()) return;
    if (m_PermanentGalleryWorkspace) {
        OpenRawWorkspaceGalleryWorkspace();
        return;
    }
    m_RawWorkspaceLabUi.galleryWorkspaceOpen = false;
    m_RawWorkspaceLabAnimatedFilmstripHeight = 0.0f;
    m_RawWorkspaceLabFilmstripAnimationOpenHeight = 0.0f;
    m_RawWorkspaceLabAnimatedLowerShelfHeight = 0.0f;
    m_RawWorkspaceLabGalleryPanelsOpen = true;
    m_RawWorkspaceLabGalleryPanelsAnimation = 1.0f;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }
    m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::NativeWindow;
    m_RawWorkspaceLabUi.lastGalleryHost = RawGalleryHost::NativeWindow;
    m_RawWorkspaceLabNativeGalleryMonitorPos = viewport->WorkPos;
    m_RawWorkspaceLabNativeGalleryMonitorSize = viewport->WorkSize;
    m_RawWorkspaceLabNativeGalleryRequestFocus = true;
    m_RawWorkspaceLabNativeGalleryPlacementInitialized = false;
    m_RawWorkspaceLabNativeGalleryShown = false;
    m_RawWorkspaceLabNativeGalleryFirstPresented = false;
    m_RawWorkspaceLabNativeGalleryHadOpenPopup = false;
    m_RawWorkspaceLabNativeGalleryLayoutDetached = false;
    m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    m_RawWorkspaceLabNativeGalleryFocusAttempts = 0;
    m_RawWorkspaceLabNativeGalleryViewportId = 0;
    m_RawWorkspaceLabNativeGalleryStyledWindow = nullptr;
    m_RawWorkspaceLabNativeGalleryStyledSurfaceColor = 0;
    m_RawWorkspaceLabNativeGalleryStyledTextColor = 0;
    SaveRawWorkspaceAppState();
}

void EditorModule::CloseRawWorkspaceLabNativeGallery() {
    CancelRawWorkspaceLabGalleryImagePreview();
    m_RawWorkspaceLabGalleryPanelsOpen = false;
    m_RawWorkspaceLabGalleryPanelsAnimation = 0.0f;
    if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
        m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
    }
    m_RawWorkspaceLabNativeGalleryRequestFocus = false;
    m_RawWorkspaceLabNativeGalleryPlacementInitialized = false;
    m_RawWorkspaceLabNativeGalleryShown = false;
    m_RawWorkspaceLabNativeGalleryFirstPresented = false;
    m_RawWorkspaceLabNativeGalleryHadOpenPopup = false;
    m_RawWorkspaceLabNativeGalleryLayoutDetached = false;
    m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    m_RawWorkspaceLabNativeGalleryFocusAttempts = 0;
    m_RawWorkspaceLabNativeGalleryViewportId = 0;
    m_RawWorkspaceLabNativeGalleryStyledWindow = nullptr;
    m_RawWorkspaceLabNativeGalleryStyledSurfaceColor = 0;
    m_RawWorkspaceLabNativeGalleryStyledTextColor = 0;
}

void EditorModule::CancelRawWorkspaceLabGalleryImagePreview() {
    if (!m_RawGalleryInspectionLoading) {
        return;
    }
    if (m_RawGalleryInspectionRequestHandler) {
        Stack::RawGalleryInspection::Request cancel;
        cancel.requestId = m_RawGalleryInspectionNextRequestId++;
        cancel.cancel = true;
        m_RawGalleryInspectionRequestHandler(cancel);
    }
    m_RawGalleryInspectionActiveRequestId = 0;
    m_RawGalleryInspectionRequestKey.clear();
    m_RawGalleryInspectionLoading = false;
}

void EditorModule::CompleteRawGalleryInspection(
    Stack::RawGalleryInspection::Result result) {
    if (result.requestId == 0 ||
        result.requestId != m_RawGalleryInspectionActiveRequestId) {
        return;
    }
    m_RawGalleryInspectionLoading = false;
    if (!result) {
        m_RawGalleryInspectionStatus = result.error.empty()
            ? "The inspection image is unavailable."
            : std::move(result.error);
        return;
    }
    if (m_RawGalleryInspectionTexture != 0) {
        glDeleteTextures(1, &m_RawGalleryInspectionTexture);
        m_RawGalleryInspectionTexture = 0;
    }
    m_RawGalleryInspectionTexture = GLHelpers::CreateTextureFromPixels(
        result.pixels.data(), result.width, result.height, 4);
    if (m_RawGalleryInspectionTexture == 0) {
        m_RawGalleryInspectionTextureWidth = 0;
        m_RawGalleryInspectionTextureHeight = 0;
        m_RawGalleryInspectionStatus =
            "The inspection image could not be uploaded for display.";
        return;
    }
    m_RawGalleryInspectionTextureWidth = result.width;
    m_RawGalleryInspectionTextureHeight = result.height;
    m_RawGalleryInspectionStatus = result.status;
}

void EditorModule::RenderRawWorkspaceLabGalleryInfoPanel() {
    RenderRawGalleryPropertyActions();
    const auto compactValue = [](const std::string& value, float maxWidth) {
        const std::string fallback = value.empty() ? std::string("—") : value;
        if (ImGui::CalcTextSize(fallback.c_str()).x <= maxWidth) {
            return fallback;
        }
        std::string result = fallback;
        constexpr const char* ellipsis = "…";
        if (ImGui::CalcTextSize(ellipsis).x > maxWidth) {
            return std::string();
        }
        while (!result.empty() &&
               ImGui::CalcTextSize((result + ellipsis).c_str()).x > maxWidth) {
            Stack::Utf8Text::RemoveLastCodePoint(result);
        }
        return result + ellipsis;
    };
    const auto infoRow = [&](const char* label, const std::string& value) {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(0.0f, 5.0f);
        const std::string compact = compactValue(
            value,
            std::max(1.0f, ImGui::GetContentRegionAvail().x));
        ImGui::TextUnformatted(compact.c_str());
    };
    const auto byteLabel = [](std::uintmax_t bytes) {
        const double value = bytes >= 1024u * 1024u
            ? static_cast<double>(bytes) / (1024.0 * 1024.0)
            : static_cast<double>(bytes) / 1024.0;
        char buffer[64] {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            bytes >= 1024u * 1024u ? "%.2f MB" : "%.1f KB",
            value);
        return std::string(buffer);
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 1.0f));

    if (!m_RawWorkspaceLabFocusedProjectPath.empty()) {
        Stack::Project::ProjectRecord project;
        if (!FindRawWorkspaceGalleryProjectRecord(
                m_RawWorkspaceLabFocusedProjectPath,
                project)) {
            infoRow("PROJECT", m_RawWorkspaceLabFocusedProjectName);
            infoRow("LOCATION", m_RawWorkspaceLabFocusedProjectPath.string());
            ImGui::PopStyleVar(2);
            return;
        }
        infoRow("PROJECT", project.displayName);
        infoRow("KIND", project.projectKind);
        infoRow("ID", project.projectId);
        infoRow("PATH", project.absolutePath.string());
        infoRow("CREATED", project.timestamp);
        if (project.sourceWidth > 0 && project.sourceHeight > 0) {
            infoRow(
                "DIM",
                std::to_string(project.sourceWidth) + " × " +
                    std::to_string(project.sourceHeight));
        }
        infoRow("SOURCES", std::to_string(project.sources.size()));
        infoRow("EDIT", std::to_string(project.editRevision));
        infoRow("STORAGE", std::to_string(project.storageRevision));
        infoRow(
            "RAW",
            project.rawSourceLinked
                ? "Linked original"
                : (project.rawSourceEmbedded ? "Embedded" : "Unavailable"));
        infoRow("RAW MODE", project.rawWorkspaceMode);
        if (!project.errorMessage.empty()) {
            infoRow("STATUS", project.errorMessage);
        }
        ImGui::PopStyleVar(2);
        return;
    }

    const Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (source == nullptr) {
        ImGui::TextDisabled("Select an original image or project.");
        ImGui::PopStyleVar(2);
        return;
    }
    infoRow("FILE", source->fileName);
    infoRow("TYPE", source->extension);
    infoRow("SIZE", byteLabel(source->fileSizeBytes));
    infoRow("PATH", source->absolutePath.string());
    infoRow("FOLDER", source->parentFolderKey);
    infoRow("HASH", source->fingerprint);
    if (source->thumbnail.width > 0 && source->thumbnail.height > 0) {
        infoRow(
            "DIM",
            std::to_string(source->thumbnail.width) + " × " +
                std::to_string(source->thumbnail.height));
    }
    infoRow(
        "STATUS",
        Stack::RawWorkspace::ThumbnailStatusLabel(source->thumbnail.status));
    infoRow(
        "PROJECTS",
        std::to_string(source->sourceSetProjectMemberships.size() +
            (source->project.status ==
                 Stack::RawWorkspace::ProjectStatus::Existing
                ? 1u
                : 0u)));
    ImGui::PopStyleVar(2);
}

void EditorModule::RenderRawWorkspaceLabGalleryImagePreviewPanel() {
    if (!m_RawWorkspaceLabGalleryPanelsOpen) {
        return;
    }

    std::filesystem::path projectPath =
        m_RawWorkspaceLabFocusedProjectPath;
    std::filesystem::path sourcePath;
    std::string displayName = m_RawWorkspaceLabFocusedProjectName;

    const bool bracketBusy = AutoBracketWorkActive();
    // Saved After inspection loads the project directly. Do not probe original
    // files (potentially on offline/network storage) just to redraw that view.
    const bool needsOriginalSource = m_RawGalleryInspectionShowBefore || bracketBusy;
    if (!projectPath.empty() && (needsOriginalSource || displayName.empty())) {
        Stack::Project::ProjectRecord record;
        if (FindRawWorkspaceGalleryProjectRecord(
                projectPath, record)) {
            if (displayName.empty()) displayName = record.displayName;
            if (needsOriginalSource) {
                for (const Stack::Project::IndexedProjectSource& source :
                     record.sources) {
                    std::error_code existsError;
                    if (!source.originalPath.empty() &&
                        std::filesystem::is_regular_file(
                            source.originalPath, existsError) &&
                        !existsError) {
                        sourcePath = source.originalPath;
                        break;
                    }
                }
                if (sourcePath.empty()) {
                    for (const Stack::Project::IndexedProjectSource& source :
                         record.sources) {
                        if (source.fingerprint.empty()) continue;
                        const auto found = std::find_if(
                            m_RawWorkspace.sources.begin(),
                            m_RawWorkspace.sources.end(),
                            [&](const Stack::RawWorkspace::SourceRecord& candidate) {
                                return candidate.fingerprint == source.fingerprint;
                            });
                        if (found != m_RawWorkspace.sources.end()) {
                            sourcePath = found->absolutePath;
                            break;
                        }
                    }
                }
            }
        }
    } else if (projectPath.empty() && !m_RawWorkspace.selectedSourceKey.empty()) {
        if (const Stack::RawWorkspace::SourceRecord* source =
                FindRawWorkspaceSourceByKey(
                    m_RawWorkspace.selectedSourceKey)) {
            sourcePath = source->absolutePath;
            displayName = source->fileName;
        }
    }

    if (projectPath.empty() && sourcePath.empty()) {
        return;
    }
    if (displayName.empty()) {
        displayName = !projectPath.empty()
            ? projectPath.filename().string()
            : sourcePath.filename().string();
    }
    const bool hasSavedProject = !projectPath.empty();
    if (!hasSavedProject) m_RawGalleryInspectionShowBefore = false;
    if(bracketBusy) {
        ImGui::TextUnformatted(displayName.c_str());
        const auto source=std::find_if(m_RawWorkspace.sources.begin(),m_RawWorkspace.sources.end(),
            [&](const auto& source){return source.absolutePath==sourcePath;});
        Stack::RawWorkspace::SourceRecord thumbnail=source!=m_RawWorkspace.sources.end()?*source:Stack::RawWorkspace::SourceRecord{};
        const auto project=std::find_if(m_RawWorkspace.sourceSetProjects.begin(),m_RawWorkspace.sourceSetProjects.end(),
            [&](const auto& project){return project.absolutePath==projectPath;});
        if(project!=m_RawWorkspace.sourceSetProjects.end()&&!project->coverThumbnailCachePath.empty()) {
            thumbnail.thumbnail.absolutePath=project->coverThumbnailCachePath;
            thumbnail.thumbnail.status=Stack::RawWorkspace::ThumbnailStatus::Ready;
        }
        int width=0,height=0;
        const auto texture=GetRawWorkspaceThumbnailTexture(thumbnail,&width,&height,false);
        if(texture&&width>0&&height>0) {
            const auto available=ImGui::GetContentRegionAvail();
            const auto size=FitLabImage(float(width),float(height),{available.x,std::max(1.f,available.y-65.f)});
            ImGui::Image(static_cast<ImTextureID>(texture),size,{0,1},{1,0});
        }
        ImGui::TextWrapped("Full quality preview waits for the current bracket.");
        if(ImGui::Button("Load full quality"))RequestAutoBracketForeground("load the full quality preview",[this] {
            m_RawGalleryInspectionRequestKey.clear();
        });
        return;
    }

    const std::string requestKey =
        projectPath.lexically_normal().generic_string() + "|" +
        sourcePath.lexically_normal().generic_string() + "|" +
        (m_RawGalleryInspectionShowBefore ? "before" : "after");
    if (requestKey != m_RawGalleryInspectionRequestKey) {
        m_RawGalleryInspectionRequestKey = requestKey;
        m_RawGalleryInspectionDisplayName = displayName;
        m_RawGalleryInspectionActiveRequestId =
            m_RawGalleryInspectionNextRequestId++;
        m_RawGalleryInspectionLoading = true;
        m_RawGalleryInspectionStatus = hasSavedProject
            ? (m_RawGalleryInspectionShowBefore
                ? "Rendering original at full quality..."
                : "Rendering latest saved edit at full quality...")
            : "Rendering original at full quality...";
        if (m_RawGalleryInspectionTexture != 0) {
            glDeleteTextures(1, &m_RawGalleryInspectionTexture);
            m_RawGalleryInspectionTexture = 0;
        }
        m_RawGalleryInspectionTextureWidth = 0;
        m_RawGalleryInspectionTextureHeight = 0;

        Stack::RawGalleryInspection::Request request;
        request.requestId = m_RawGalleryInspectionActiveRequestId;
        request.projectPath = projectPath;
        request.sourcePath = sourcePath;
        request.displayName = displayName;
        request.version = m_RawGalleryInspectionShowBefore
            ? Stack::RawGalleryInspection::Version::Before
            : Stack::RawGalleryInspection::Version::After;
        if (m_RawGalleryInspectionRequestHandler) {
            m_RawGalleryInspectionRequestHandler(request);
        } else {
            m_RawGalleryInspectionLoading = false;
            m_RawGalleryInspectionStatus =
                "The background inspection renderer is unavailable.";
        }
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 imageArea(
        std::max(1.0f, available.x),
        std::max(120.0f, available.y));
    if (m_RawGalleryInspectionTexture != 0 &&
        m_RawGalleryInspectionTextureWidth > 0 &&
        m_RawGalleryInspectionTextureHeight > 0) {
        const float scale = std::min(
            imageArea.x /
                static_cast<float>(m_RawGalleryInspectionTextureWidth),
            imageArea.y /
                static_cast<float>(m_RawGalleryInspectionTextureHeight));
        const ImVec2 imageSize(
            std::max(1.0f,
                static_cast<float>(m_RawGalleryInspectionTextureWidth) *
                    scale),
            std::max(1.0f,
                static_cast<float>(m_RawGalleryInspectionTextureHeight) *
                    scale));
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() +
            std::max(0.0f, (imageArea.x - imageSize.x) * 0.5f));
        const ImVec2 imageMinimum = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##RawGalleryPreviewToggle", imageSize);
        ImGui::GetWindowDrawList()->AddImage(
            static_cast<ImTextureID>(
                static_cast<intptr_t>(m_RawGalleryInspectionTexture)),
            imageMinimum,
            ImVec2(
                imageMinimum.x + imageSize.x,
                imageMinimum.y + imageSize.y),
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
        if (hasSavedProject && ImGui::IsItemClicked()) {
            m_RawGalleryInspectionShowBefore =
                !m_RawGalleryInspectionShowBefore;
        }
        LabTooltip(
            hasSavedProject
                ? "Click to switch between the original and saved edit."
                : "Original image preview.");
    } else {
        ImGui::Dummy(imageArea);
    }
}

void EditorModule::RenderRawWorkspaceLabNativeGalleryWindow() {
    if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow) {
        return;
    }
    const auto closeGalleryWindow = [this]() {
        const bool restoreGallery = !IsRawWorkspaceProjectActive() &&
            !IsMultiFrameRawProjectActive() && !HasUnsavedBracketingDraft() &&
            !m_RawWorkspaceLockedByEditorProject;
        CloseRawWorkspaceLabNativeGallery();
        if (restoreGallery) OpenRawWorkspaceGalleryWorkspace();
    };
    if (m_RawWorkspaceLabNativeGalleryMonitorSize.x <= 1.0f ||
        m_RawWorkspaceLabNativeGalleryMonitorSize.y <= 1.0f) {
        closeGalleryWindow();
        return;
    }

    const bool initialPlacement = !m_RawWorkspaceLabNativeGalleryPlacementInitialized;
    if (initialPlacement) {
        const float availableWidth =
            std::max(420.0f, m_RawWorkspaceLabNativeGalleryMonitorSize.x - 96.0f);
        const float availableHeight =
            std::max(320.0f, m_RawWorkspaceLabNativeGalleryMonitorSize.y - 96.0f);
        m_RawWorkspaceLabNativeGalleryWindowSize = ImVec2(
            std::clamp(m_RawWorkspaceLabNativeGalleryMonitorSize.x * 0.58f, std::min(640.0f, availableWidth), availableWidth),
            std::clamp(m_RawWorkspaceLabNativeGalleryMonitorSize.y * 0.70f, std::min(480.0f, availableHeight), availableHeight));
        m_RawWorkspaceLabNativeGalleryWindowPos = ImVec2(
            m_RawWorkspaceLabNativeGalleryMonitorPos.x +
                (m_RawWorkspaceLabNativeGalleryMonitorSize.x -
                 m_RawWorkspaceLabNativeGalleryWindowSize.x) *
                    0.5f,
            m_RawWorkspaceLabNativeGalleryMonitorPos.y +
                (m_RawWorkspaceLabNativeGalleryMonitorSize.y -
                 m_RawWorkspaceLabNativeGalleryWindowSize.y) *
                    0.5f);
    }

    ImGuiWindowClass windowClass;
    windowClass.ClassId = ImHashStr("RawLabNativeGalleryWindow");
    windowClass.DockingAllowUnclassed = false;
    windowClass.ParentViewportId = 0;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    windowClass.ViewportFlagsOverrideClear =
        ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&windowClass);
    if (initialPlacement) {
        ImGui::SetNextWindowPos(m_RawWorkspaceLabNativeGalleryWindowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(m_RawWorkspaceLabNativeGalleryWindowSize, ImGuiCond_Always);
    }

    m_RawWorkspaceLabNativeGallerySurfaceColor = GetWorkspaceBaseColor();
    m_RawWorkspaceLabNativeGallerySurfaceColor.w = 1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    bool keepOpen = true;
    const bool visible = ImGui::Begin(
        "RAW Gallery",
        &keepOpen,
        ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);
    ImGuiViewport* galleryViewport = ImGui::GetWindowViewport();
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const bool ownsDedicatedViewport =
        galleryViewport != nullptr &&
        mainViewport != nullptr &&
        galleryViewport->ID != mainViewport->ID;
    m_RawWorkspaceLabNativeGalleryViewportId =
        ownsDedicatedViewport ? galleryViewport->ID : 0;
    GLFWwindow* platformWindow = ownsDedicatedViewport
        ? static_cast<GLFWwindow*>(galleryViewport->PlatformHandle)
        : nullptr;
    m_RawWorkspaceLabNativeGalleryWindowPos = ImGui::GetWindowPos();
    m_RawWorkspaceLabNativeGalleryWindowSize = ImGui::GetWindowSize();
    const bool escape =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        !m_RawWorkspaceLabNativeGalleryHadOpenPopup &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    bool closeAfterEnd = !keepOpen || escape;
    if (!visible || !ownsDedicatedViewport || platformWindow == nullptr) {
        m_RawWorkspaceLabNativeGalleryHadOpenPopup = ImGui::IsPopupOpen(
            nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        ++m_RawWorkspaceLabNativeGalleryPlatformWaitFrames;
        if (m_RawWorkspaceLabNativeGalleryPlatformWaitFrames > kRawLabDetachedOpenGraceFrames) {
            closeAfterEnd = true;
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        if (closeAfterEnd) {
            closeGalleryWindow();
        }
        return;
    }

    m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    m_RawWorkspaceLabNativeGalleryPlacementInitialized = true;
    HandleRawGalleryActionShortcuts();
    RenderRawGallerySidebarControls(true);
    if (RenderRawWorkspaceLabGalleryHeader(true) || RenderRawWorkspaceGalleryActionBar(true)) {
        m_RawWorkspaceLabNativeGalleryHadOpenPopup = ImGui::IsPopupOpen(
            nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        ImGui::End();
        ImGui::PopStyleVar(3);
        if (closeAfterEnd) {
            closeGalleryWindow();
        }
        return;
    }
    ImGui::Spacing();
    const ImVec2 galleryAvailable = ImGui::GetContentRegionAvail();
    const float panelTarget =
        m_RawWorkspaceLabGalleryPanelsOpen ? 1.0f : 0.0f;
    const float deltaTime = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    const float animationResponse =
        1.0f - std::exp(-deltaTime * 12.0f);
    m_RawWorkspaceLabGalleryPanelsAnimation +=
        (panelTarget - m_RawWorkspaceLabGalleryPanelsAnimation) *
        animationResponse;
    if (std::abs(
            m_RawWorkspaceLabGalleryPanelsAnimation - panelTarget) <
        0.001f) {
        m_RawWorkspaceLabGalleryPanelsAnimation = panelTarget;
    }
    const float panelAmount = std::clamp(
        m_RawWorkspaceLabGalleryPanelsAnimation,
        0.0f,
        1.0f);
    const float columnGap = 10.0f;
    const float baseGalleryWidth = std::max(
        260.0f,
        galleryAvailable.x * 0.40f);
    const float baseInfoWidth = std::clamp(
        galleryAvailable.x * 0.14f,
        150.0f,
        220.0f);
    const float basePreviewWidth = std::max(
        240.0f,
        galleryAvailable.x - baseGalleryWidth - baseInfoWidth -
            columnGap * 2.0f);

    if (panelAmount <= 0.001f) {
        ImGui::BeginChild(
            "RawLabNativeGalleryContent",
            ImVec2(0.0f, 0.0f),
            false,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoBackground);
        RenderRawWorkspaceLabGalleryContent(false);
        ImGui::EndChild();
    } else {
        const float animatedGap = columnGap * panelAmount;
        const float infoWidth = std::max(1.0f, baseInfoWidth * panelAmount);
        const float previewWidth = std::max(
            1.0f,
            basePreviewWidth * panelAmount);
        const float galleryWidth = std::max(
            260.0f,
            galleryAvailable.x - infoWidth - previewWidth -
                animatedGap * 2.0f);
        ImGui::BeginChild(
            "RawLabNativeGalleryContent",
            ImVec2(galleryWidth, 0.0f),
            false,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoBackground);
        RenderRawWorkspaceLabGalleryContent(false);
        ImGui::EndChild();
        ImGui::SameLine(0.0f, animatedGap);
        ImGui::BeginChild(
            "RawLabNativeGalleryInfo",
            ImVec2(infoWidth, 0.0f),
            false,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoBackground);
        if (m_RawWorkspaceLabGalleryPanelsOpen) {
            RenderRawWorkspaceLabGalleryInfoPanel();
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, animatedGap);
        ImGui::BeginChild(
            "RawLabNativeGalleryImagePreview",
            ImVec2(previewWidth, 0.0f),
            false,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoBackground);
        if (m_RawWorkspaceLabGalleryPanelsOpen) {
            RenderRawWorkspaceLabGalleryImagePreviewPanel();
        }
        ImGui::EndChild();
    }
    m_RawWorkspaceLabNativeGalleryHadOpenPopup = ImGui::IsPopupOpen(
        nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    ImGui::End();
    ImGui::PopStyleVar(3);
    if (closeAfterEnd) {
        closeGalleryWindow();
    }
}

void EditorModule::RenderRawWorkspaceDetachedWindows() {
    if (!m_RawWorkspaceRootTabActive || m_RawWorkspaceLockedByEditorProject) {
        if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
            CloseRawWorkspaceLabNativeGallery();
        } else {
            m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
        }
        m_RawWorkspaceGalleryWindowOpen = false;
        return;
    }
    RenderRawWorkspaceLabNativeGalleryWindow();
}

bool EditorModule::QueryRawWorkspaceLabNativeGalleryWindow(
    DetachedNativeWindowRequest& request) const {
    request = DetachedNativeWindowRequest{};
    request.kind = DetachedSurfaceKind::RawGallery;
    if (!m_RawWorkspaceRootTabActive ||
        m_RawWorkspaceLockedByEditorProject ||
        m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow) {
        return false;
    }
    request.viewportId = m_RawWorkspaceLabNativeGalleryViewportId;
    request.surfaceColor = m_RawWorkspaceLabNativeGallerySurfaceColor;
    request.surfaceColor.w = 1.0f;
    request.surfaceColorU32 = ImGui::ColorConvertFloat4ToU32(request.surfaceColor);
    request.textColorU32 =
        ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Text));
    request.nativeShown = m_RawWorkspaceLabNativeGalleryShown;
    request.firstPresented = m_RawWorkspaceLabNativeGalleryFirstPresented;
    request.layoutDetached = m_RawWorkspaceLabNativeGalleryLayoutDetached;
    request.focusAttempt = m_RawWorkspaceLabNativeGalleryFocusAttempts;
    request.waitFrames = m_RawWorkspaceLabNativeGalleryPlatformWaitFrames;
    if (m_RawWorkspaceLabNativeGalleryViewportId == 0) {
        return true;
    }
    const ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    for (int index = 0; index < platformIo.Viewports.Size; ++index) {
        const ImGuiViewport* viewport = platformIo.Viewports[index];
        if (viewport == nullptr || viewport->ID != m_RawWorkspaceLabNativeGalleryViewportId) {
            continue;
        }
        request.window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
        request.hasPlatformWindow = request.window != nullptr;
        if (request.hasPlatformWindow) {
            request.applyTheme =
                request.window != m_RawWorkspaceLabNativeGalleryStyledWindow ||
                request.surfaceColorU32 != m_RawWorkspaceLabNativeGalleryStyledSurfaceColor ||
                request.textColorU32 != m_RawWorkspaceLabNativeGalleryStyledTextColor;
            request.requestFocus =
                m_RawWorkspaceLabNativeGalleryRequestFocus &&
                m_RawWorkspaceLabNativeGalleryFocusAttempts < 4;
        }
        return true;
    }
    return true;
}

void EditorModule::CompleteRawWorkspaceLabNativeGalleryWindowRequest(
    const DetachedNativeWindowRequest& request,
    bool themeApplied,
    bool focused) {
    if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow ||
        !request.hasPlatformWindow ||
        request.window == nullptr) {
        return;
    }
    m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    m_RawWorkspaceLabNativeGalleryPlacementInitialized = true;
    if (themeApplied) {
        m_RawWorkspaceLabNativeGalleryStyledWindow = request.window;
        m_RawWorkspaceLabNativeGalleryStyledSurfaceColor = request.surfaceColorU32;
        m_RawWorkspaceLabNativeGalleryStyledTextColor = request.textColorU32;
    }
    if (request.requestFocus) {
        ++m_RawWorkspaceLabNativeGalleryFocusAttempts;
        if (focused || m_RawWorkspaceLabNativeGalleryFocusAttempts >= 4) {
            m_RawWorkspaceLabNativeGalleryRequestFocus = false;
        }
    }
}

void EditorModule::MarkRawWorkspaceLabNativeGalleryWindowShown(
    const DetachedNativeWindowRequest& request,
    bool focused) {
    if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow ||
        !request.hasPlatformWindow ||
        request.window == nullptr) {
        return;
    }
    m_RawWorkspaceLabNativeGalleryShown = true;
    if (request.requestFocus && focused) {
        m_RawWorkspaceLabNativeGalleryRequestFocus = false;
    }
}

void EditorModule::MarkRawWorkspaceLabNativeGalleryPlatformPresented(GLFWwindow* window) {
    if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow || window == nullptr) {
        return;
    }
    if (m_RawWorkspaceLabNativeGalleryStyledWindow != nullptr &&
        window != m_RawWorkspaceLabNativeGalleryStyledWindow) {
        return;
    }
    m_RawWorkspaceLabNativeGalleryFirstPresented = true;
    m_RawWorkspaceLabNativeGalleryLayoutDetached = true;
}

bool EditorModule::RequestOpenRawWorkspaceProjectFromGallery(
    const std::filesystem::path& projectPath) {
    if (m_PermanentGalleryWorkspace) {
        for (const auto& entry : m_RawWorkspace.sourceSetProjects) {
            if (!entry.unlinked || entry.absolutePath!=projectPath) continue;
            m_RawWorkspaceGalleryContentMode=Stack::RawWorkspace::GalleryContentMode::Unlinked;
            m_RawWorkspaceLabFocusedProjectPath=projectPath;
            m_RawWorkspaceLabSelectedProjectPaths.assign(1,projectPath);
            InvalidateRawWorkspaceGalleryPresentation(false);
            return true;
        }
    }
    if (m_PermanentGalleryWorkspace) {
        if (!m_RawGalleryOpenSelectionHandler) return false;
        RawGalleryOpenSelection selection;
        selection.items.push_back({projectPath, true});
        m_RawGalleryOpenSelectionHandler(std::move(selection), false);
        return true;
    }
    if(RequestAutoBracketForeground("open a project",[this,projectPath]{RequestOpenRawWorkspaceProjectFromGallery(projectPath);}))return true;
    if (projectPath.empty() || IsWorkspaceTransitionPending() ||
        !FinishWorkspaceInteraction()) return false;
    if (IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy() ||
        Async::IsBusy(GetProjectLoadTaskState())) {
        PostNotification(UiNotificationSeverity::Warning,
            "Finish opening the current selection before opening another project.",
            "raw-workspace-project-replacement-busy");
        return false;
    }

    for (const auto& card : m_RawWorkspace.sourceSetProjects) {
        if (card.masterSource && card.absolutePath.lexically_normal() == projectPath.lexically_normal())
            return RequestOpenRawWorkspaceSourceForEditing(card.masterSource->relativePathKey);
    }

    // A saved card names one exact document, including a non-default version.
    // Do not select its original or activate Bracketing before the load:
    // those actions can stage another document or create a draft, invalidating
    // the receiving editor's identity/revision while decoding is in flight.
    // Library owns save-before-replacement, task lifetime, generation checks
    // and apply completion. Navigation uses that same owner after acceptance.
    LibraryManager::Get().RequestLoadProjectFromPath(projectPath, this,
        [this](bool opened) {
            if (!opened) return;
            if (IsMultiFrameRawProjectActive()) {
                OpenBracketingTool();
            } else if (!IsRawWorkspaceProjectActive()) {
                RequestOpenEditorTab();
                return;
            } else {
                m_RawWorkspaceLabUi.galleryNavigationMode =
                    RawGalleryNavigationMode::ProjectRoot;
                m_RawWorkspaceLabUi.galleryProjectId.clear();
                if (m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame)
                    m_RawWorkspaceLabUi.activeTool = RawLabTool::Light;
            }
            RequestOpenRawLabTab();
        });
    return true;
}


bool EditorModule::IsRawWorkspaceGalleryAvailable() const {
    return !m_RawWorkspaceLockedByEditorProject;
}

bool EditorModule::IsRawWorkspaceGalleryOpen() const {
    return m_RawWorkspaceLabUi.galleryWorkspaceOpen ||
        m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow;
}

void EditorModule::CloseRawWorkspaceGalleryWorkspace() {
    if (m_PermanentGalleryWorkspace) return;
    if (!m_RawWorkspaceLabUi.galleryWorkspaceOpen) return;
    m_RawWorkspaceLabUi.galleryWorkspaceOpen = false;
    if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Filmstrip) {
        m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
    }
    m_RawWorkspaceLabFilmstripHoverSourceKey.clear();
    m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
    m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
    m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
    m_RawWorkspaceLabFilmstripHoverOpacity = 0.0f;
    m_RawWorkspaceLabFilmstripHoverFrame = -1;
    m_RawWorkspaceLabFilmstripHoverSuppressed = false;
}

bool EditorModule::OpenRawWorkspaceGalleryWorkspace() {
    if (!m_PermanentGalleryWorkspace && m_GalleryNavigationHandler) {
        m_GalleryNavigationHandler();
        return true;
    }
    if (Stack::Workspace::IsPreview() || !IsRawWorkspaceGalleryAvailable() ||
        !FinishWorkspaceInteraction()) {
        return false;
    }
    if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
        CloseRawWorkspaceLabNativeGallery();
    }
    m_RawWorkspaceLabUi.galleryWorkspaceOpen = true;
    m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Filmstrip;
    m_RawWorkspaceLabUi.lastGalleryHost = RawGalleryHost::Filmstrip;
    m_RawWorkspaceGalleryPreviewHovered = false;
    return true;
}

void EditorModule::ToggleRawWorkspaceGallery() {
    if (m_GalleryNavigationHandler) {
        m_GalleryNavigationHandler();
        return;
    }
    if (!m_RawWorkspaceLabUi.galleryWorkspaceOpen) {
        OpenRawWorkspaceGalleryWorkspace();
        return;
    }
    if (Stack::Workspace::IsPreview() || !FinishWorkspaceInteraction()) return;
    // An empty workspace has no editor to return to. Keep its folder picker
    // and filmstrip available until an image or Bracket mode is opened.
    if (!IsRawWorkspaceProjectActive() && !IsMultiFrameRawProjectActive() &&
        !HasUnsavedBracketingDraft()) return;
    CloseRawWorkspaceGalleryWorkspace();
}

void EditorModule::SetPermanentGalleryWorkspace(bool permanent) {
    if (m_PermanentGalleryWorkspace != permanent) m_RawWorkspaceThumbnailTextureTrimNeeded = true;
    m_PermanentGalleryWorkspace = permanent;
    m_RawWorkspaceLabUi.galleryWorkspaceOpen = permanent;
    m_RawWorkspaceLabUi.galleryHost = permanent
        ? RawGalleryHost::Filmstrip : RawGalleryHost::Closed;
}

bool EditorModule::IsRawWorkspaceInfoAvailable() const {
    return IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive();
}

bool EditorModule::IsRawWorkspaceInfoOpen() const {
    return m_RawWorkspaceLabUi.lowerShelfOpen;
}

void EditorModule::ToggleRawWorkspaceInfo() {
    if (!IsRawWorkspaceInfoAvailable()) {
        return;
    }
    m_RawWorkspaceLabUi.lowerShelfOpen = !m_RawWorkspaceLabUi.lowerShelfOpen;
    if (m_RawWorkspaceLabUi.lowerShelfOpen) {
        ClearRawWorkspaceLabGradingScope();
        MarkRenderRefreshDirty();
    }
    SaveRawWorkspaceAppState();
}

void EditorModule::SetRawWorkspaceToolPanelOnRight(bool onRight) {
    if (m_RawWorkspaceLabUi.toolRailOnRight == onRight) {
        return;
    }
    m_RawWorkspaceLabUi.toolRailOnRight = onRight;
    SaveRawWorkspaceAppState();
}

void EditorModule::BeginMultiFrameCaptureSetGallerySelection(
    bool returnToMultiFrameOnCancel) {
    BeginMultiFrameGallerySelection(
        Stack::Project::MultiFrameOperationIntent::RawCaptureSet,
        returnToMultiFrameOnCancel);
}

void EditorModule::BeginMultiFrameDenoiseGallerySelection(
    bool returnToMultiFrameOnCancel) {
    BeginMultiFrameCaptureSetGallerySelection(returnToMultiFrameOnCancel);
}

void EditorModule::BeginMultiFrameHdrGallerySelection(
    bool returnToMultiFrameOnCancel) {
    BeginMultiFrameCaptureSetGallerySelection(returnToMultiFrameOnCancel);
}

void EditorModule::BeginMultiFrameGallerySelection(
    Stack::Project::MultiFrameOperationIntent intent,
    bool returnToMultiFrameOnCancel) {
    if (m_PermanentGalleryWorkspace) {
        OpenBracketingTool();
        return;
    }
    if(intent==Stack::Project::MultiFrameOperationIntent::RawCaptureSet){BeginBracketingDraft(true);OpenBracketingTool();RequestOpenRawLabTab();return;}
    if (!IsRawWorkspaceGalleryAvailable() ||
        IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy() ||
        IsMfdExperimentalProcessingBusy() ||
        IsHdrProcessingBusy()) {
        return;
    }
    m_PendingMultiFrameCreationIntent = intent;
    m_ReturnToMultiFrameAfterGalleryCreationCancel =
        returnToMultiFrameOnCancel;
    m_RawWorkspaceGallerySelectionRestoreKey =
        m_RawWorkspace.selectedSourceKey;
    m_RawWorkspaceGallerySelectionRestoreKeys =
        m_RawWorkspace.selectedSourceKeys;
    m_RawWorkspaceGalleryRestoreNavigationMode =
        m_RawWorkspaceLabUi.galleryNavigationMode;
    m_RawWorkspaceGalleryRestoreProjectId =
        m_RawWorkspaceLabUi.galleryProjectId;
    m_RawWorkspaceGalleryRestoreHost = m_RawWorkspaceLabUi.galleryHost;
    m_RawWorkspace.selectedSourceKey.clear();
    m_RawWorkspace.selectedSourceKeys.clear();
    m_RawWorkspaceLabUi.galleryNavigationMode =
        RawGalleryNavigationMode::MultiFrameCreation;
    m_RawWorkspaceLabUi.galleryProjectId.clear();
    m_RawWorkspaceGalleryDisplayMode =
        Stack::RawWorkspace::GalleryDisplayMode::Grid;
    InvalidateRawWorkspaceGalleryPresentation();
    OpenRawWorkspaceLabNativeGallery();
    SaveRawWorkspaceAppState();
}

