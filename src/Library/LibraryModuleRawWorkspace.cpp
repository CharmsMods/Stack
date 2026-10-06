#include "LibraryModule.h"

#include "Editor/EditorModule.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/RawGallerySelectionVisuals.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

ImVec4 BlendColor(const ImVec4& from, const ImVec4& to, float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        from.x + (to.x - from.x) * clamped,
        from.y + (to.y - from.y) * clamped,
        from.z + (to.z - from.z) * clamped,
        from.w + (to.w - from.w) * clamped);
}

std::string FormatRawLibraryFileSize(std::uintmax_t bytes) {
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;
    char buffer[64] = {};
    if (bytes >= static_cast<std::uintmax_t>(kGiB)) {
        snprintf(buffer, sizeof(buffer), "%.1f GB", static_cast<double>(bytes) / kGiB);
    } else if (bytes >= static_cast<std::uintmax_t>(kMiB)) {
        snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / kMiB);
    } else if (bytes >= static_cast<std::uintmax_t>(kKiB)) {
        snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / kKiB);
    } else {
        snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return std::string(buffer);
}

std::string FitRawLibraryText(const std::string& text, float maxWidth) {
    if (text.empty() || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }

    std::string result = text;
    while (result.size() > 4) {
        result.pop_back();
        std::string candidate = result + "...";
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }
    return "...";
}

ImVec2 FitRawLibraryImage(float sourceWidth, float sourceHeight, const ImVec2& bounds) {
    if (sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
        return bounds;
    }
    const float scale = std::min(bounds.x / sourceWidth, bounds.y / sourceHeight);
    return ImVec2(std::max(1.0f, sourceWidth * scale), std::max(1.0f, sourceHeight * scale));
}

void DrawRawLibraryPlaceholder(ImDrawList* drawList, const ImRect& rect, bool selected) {
    drawList->AddRectFilled(
        rect.Min,
        rect.Max,
        ImGui::GetColorU32(selected ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg),
        4.0f);
    drawList->AddRect(
        rect.Min,
        rect.Max,
        ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGuiCol_Border),
        4.0f,
        0,
        selected ? 2.0f : 1.0f);
    const char* label = "RAW";
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    drawList->AddText(
        ImVec2(
            rect.Min.x + (rect.GetWidth() - labelSize.x) * 0.5f,
            rect.Min.y + (rect.GetHeight() - labelSize.y) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        label);
}

const Stack::RawWorkspace::SourceRecord* FindRawLibrarySource(
    const Stack::RawWorkspace::WorkspaceState& state,
    std::size_t sourceIndex) {
    return sourceIndex < state.sources.size() ? &state.sources[sourceIndex] : nullptr;
}

} // namespace

void LibraryModule::RenderRawWorkspaceView(
    EditorModule* editor,
    int* activeTab,
    int rawWorkspaceTabId) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
    ImGui::BeginChild(
        "LibraryRawWorkspaceView",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const bool optionsPopupOpen = ImGui::IsPopupOpen("RawWorkspaceOptionsMenu");
    const auto renderOptionsTextButton = [&](const char* id, const char* label, bool active) {
        constexpr float hitHeight = 28.0f;
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const float hitWidth = textSize.x + 12.0f;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImRect rect(cursor, ImVec2(cursor.x + hitWidth, cursor.y + hitHeight));
        ImGui::InvisibleButton(id, ImVec2(hitWidth, hitHeight));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const float selectedAmount = active ? 1.0f : 0.0f;

        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const ImVec4 accentColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
        if (held) {
            textColor = accentColor;
        } else {
            const float hoverAmount = hovered ? 0.72f : 0.0f;
            const float accentAmount = 1.0f - (1.0f - selectedAmount) * (1.0f - hoverAmount);
            textColor = BlendColor(textColor, accentColor, accentAmount);
        }
        const ImVec2 textPos(
            cursor.x + (hitWidth - textSize.x) * 0.5f,
            cursor.y + (hitHeight - textSize.y) * 0.5f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float glowAmount = held ? 0.22f : std::max(selectedAmount * 0.22f, hovered ? 0.14f : 0.0f);
        if (glowAmount > 0.001f) {
            ImVec4 glow = accentColor;
            glow.w *= glowAmount;
            const ImU32 glowColor = ImGui::GetColorU32(glow);
            drawList->AddText(ImVec2(textPos.x - 1.0f, textPos.y), glowColor, label);
            drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y), glowColor, label);
            drawList->AddText(ImVec2(textPos.x, textPos.y - 1.0f), glowColor, label);
            drawList->AddText(ImVec2(textPos.x, textPos.y + 1.0f), glowColor, label);
        }
        drawList->AddText(textPos, ImGui::GetColorU32(textColor), label);
        return ImGui::IsItemClicked();
    };

    if (renderOptionsTextButton("##RawWorkspaceOptionsBtn", "Options", optionsPopupOpen)) {
        ImGui::OpenPopup("RawWorkspaceOptionsMenu");
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::BeginPopup("RawWorkspaceOptionsMenu")) {
        const auto drawMenuIcon = [](unsigned int texture, bool enabled = true) {
            if (texture == 0) {
                return;
            }
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const float iconSize = std::min(15.0f, itemMax.y - itemMin.y - 5.0f);
            const ImVec2 iconMin(
                itemMin.x + 6.0f,
                itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f);
            const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
            ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (!enabled) {
                tint = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            }
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)texture,
                iconMin,
                iconMax,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImGui::ColorConvertFloat4ToU32(tint));
        };
        const auto menuItem = [&drawMenuIcon](
            const char* label,
            unsigned int texture,
            bool enabled = true) {
            const std::string paddedLabel = std::string("        ") + label;
            const bool activated = ImGui::MenuItem(
                paddedLabel.c_str(), nullptr, false, enabled);
            drawMenuIcon(texture, enabled);
            return activated;
        };
        const auto menuSeparator = []() {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
        };

        const bool hasEditor = (editor != nullptr);
        const bool scanBusy = hasEditor && editor->IsRawWorkspaceScanBusy();
        const bool hasWorkspace = hasEditor && !editor->GetRawWorkspaceState().workspaceRoot.empty();

        if (menuItem("Open RAW Folder...", m_FileFolderIconTex, hasEditor && !scanBusy)) {
            editor->OpenRawWorkspaceFolderDialog();
        }
        if (menuItem("Rescan", m_ReloadIconTex, hasWorkspace && !scanBusy)) {
            editor->RescanRawWorkspace();
        }
        menuSeparator();
        if (menuItem("Clear", 0, hasWorkspace && !scanBusy)) {
            editor->ClearRawWorkspaceForUser();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);

    if (editor == nullptr) {
        ImGui::Spacing();
        ImGui::TextDisabled("RAW Workspace is unavailable.");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }

    editor->EnsureRawWorkspaceLoaded();
    editor->PumpRawWorkspaceThumbnailTextureUploads();
    const Stack::RawWorkspace::WorkspaceState& state = editor->GetRawWorkspaceState();
    const ImGuiIO& rawWorkspaceIo = ImGui::GetIO();
    const bool rawWorkspaceShortcutAllowed =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !rawWorkspaceIo.WantTextInput &&
        !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (rawWorkspaceShortcutAllowed && rawWorkspaceIo.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        const auto focusedSource = std::find_if(
            state.sources.begin(),
            state.sources.end(),
            [&](const Stack::RawWorkspace::SourceRecord& candidate) {
                return candidate.relativePathKey == state.selectedSourceKey;
            });
        if (focusedSource != state.sources.end()) {
            std::string error;
            if (!editor->CopyRawEditAttributesFromGallerySource(
                    *focusedSource, &error)) {
                editor->ShowUiNotification(
                    UiNotificationSeverity::Error,
                    error.empty()
                        ? "This image has no saved RAW edits."
                        : error,
                    "library-raw-edit-attributes-copy-failed");
            }
        }
    }
    if (rawWorkspaceShortcutAllowed && rawWorkspaceIo.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        editor->RequestPasteRawEditAttributesForGallerySelection(false);
    }
    const std::string rawWorkspaceKey = state.workspaceRoot.lexically_normal().string();
    if (rawWorkspaceKey != m_RawScrollWorkspaceKey) {
        m_RawScrollWorkspaceKey = rawWorkspaceKey;
        m_RawScrollTargetY = -1.0f;
        m_RawScrollCurrentY = -1.0f;
    }

    ImGui::Spacing();
    if (!state.workspaceRoot.empty()) {
        ImGui::TextDisabled("%s", state.workspaceRoot.string().c_str());
    }

    const bool scanBusy = editor->IsRawWorkspaceScanBusy();
    const bool thumbnailBusy = editor->IsRawWorkspaceThumbnailBusy();
    if (scanBusy) {
        const Stack::RawWorkspace::ScanProgress progress = editor->GetRawWorkspaceScanProgress();
        const std::string status = editor->GetRawWorkspaceScanStatusText();
        ImGui::TextDisabled(
            "%s (%d RAW, %d files checked)",
            status.empty() ? "Scanning Workspace..." : status.c_str(),
            progress.discoveredRawCount,
            progress.filesVisited);
    } else {
        const std::string status = editor->GetRawWorkspaceScanStatusText();
        if (!status.empty()) {
            ImGui::TextDisabled("%s", status.c_str());
        }
    }

    if (thumbnailBusy) {
        const Stack::RawWorkspace::ThumbnailProgress progress = editor->GetRawWorkspaceThumbnailProgress();
        const std::string status = editor->GetRawWorkspaceThumbnailStatusText();
        ImGui::TextDisabled(
            "%s (%d completed, %d queued, %d failed)",
            status.empty() ? "Generating RAW thumbnails..." : status.c_str(),
            progress.completed,
            progress.queued,
            progress.failed);
    } else {
        const std::string status = editor->GetRawWorkspaceThumbnailStatusText();
        if (!status.empty() && !state.sources.empty()) {
            ImGui::TextDisabled("%s", status.c_str());
        }
    }

    const std::size_t selectedFrameCount = state.selectedSourceKeys.size();
    ImGui::Spacing();
    if (selectedFrameCount > 0u) {
        ImGui::TextColored(
            ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),
            "%llu %s selected",
            static_cast<unsigned long long>(selectedFrameCount),
            selectedFrameCount == 1u ? "frame" : "frames");
    } else {
        ImGui::TextDisabled("Click selects one frame.");
    }
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextDisabled("Ctrl-click toggles; Shift-click selects a range.");

    auto openSelectedInRawTab = [&]() {
        if (activeTab != nullptr && rawWorkspaceTabId >= 0) {
            *activeTab = rawWorkspaceTabId;
        }
    };

    if (state.selectedSourceKeys.size() >= 2u) {
        const bool canCreateMultiFrame =
            !editor->IsRawWorkspaceProjectLoadBusy();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::BeginDisabled(!canCreateMultiFrame);
        if (ImGui::Button("Create bracket", ImVec2(162.0f, 28.0f))) {
            editor->RequestCreateMultiFrameProjectFromGallerySelection(
                Stack::Project::MultiFrameOperationIntent::RawCaptureSet);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                canCreateMultiFrame
                    ? "Create a bracket from this selection."
                    : "Finish opening the current RAW selection first.");
        }
        const Stack::Project::RawProjectSnapshot* activeProject =
            editor->GetActiveRawProjectSnapshot();
        if (editor->IsMultiFrameRawProjectActive() && activeProject) {
            ImGui::SameLine(0.0f, 8.0f);
            if (ImGui::Button("Add to Current Burst", ImVec2(172.0f, 28.0f))) {
                std::vector<std::filesystem::path> paths;
                paths.reserve(state.selectedSourceKeys.size());
                for (const std::string& key : state.selectedSourceKeys) {
                    const auto source = std::find_if(
                        state.sources.begin(),
                        state.sources.end(),
                        [&](const Stack::RawWorkspace::SourceRecord& candidate) {
                            return candidate.relativePathKey == key;
                        });
                    if (source != state.sources.end()) {
                        paths.push_back(source->absolutePath);
                    }
                }
                std::string error;
                if (!editor->AddFramesToMultiFrameSourceSet(
                        activeProject->activeSourceSetId,
                        paths,
                        &error) &&
                    !error.empty()) {
                    editor->ShowUiNotification(
                        UiNotificationSeverity::Error,
                        error,
                        "library-add-to-current-mfd");
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Embed the selection into the currently open MFD burst.");
            }
        }
    }

    ImGui::Spacing();
    if (state.workspaceRoot.empty()) {
        ImGui::TextDisabled("No RAW Workspace is open.");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }

    const Stack::RawWorkspace::GalleryPresentation& presentation =
        editor->GetRawWorkspaceGalleryPresentation();
    if (presentation.totalSources == 0 &&
        !editor->IsRawWorkspaceScanBlockingGallery()) {
        ImGui::TextDisabled("No RAW files found.");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }

    auto renderTile = [&](const Stack::RawWorkspace::SourceRecord& source,
                          const Stack::RawWorkspace::GallerySourceView& view,
                          const ImVec2& tileSize,
                          const ImVec2& imageBounds) {
        ImGui::PushID(source.relativePathKey.c_str());
        const ImVec2 tileMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##LibraryRawTile", tileSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (clicked) {
            editor->SelectRawWorkspaceSourceForGallery(
                source.relativePathKey,
                ImGui::GetIO().KeyCtrl,
                ImGui::GetIO().KeyShift,
                doubleClicked);
        }
        if (doubleClicked) {
            openSelectedInRawTab();
        }
        const Stack::RawWorkspace::WorkspaceState& currentState =
            editor->GetRawWorkspaceState();
        const auto selection = std::find(
            currentState.selectedSourceKeys.begin(),
            currentState.selectedSourceKeys.end(),
            source.relativePathKey);
        const bool selected = selection != currentState.selectedSourceKeys.end();
        const bool focused = source.relativePathKey == currentState.selectedSourceKey;
        const std::size_t selectionOrdinal = selected
            ? static_cast<std::size_t>(std::distance(
                  currentState.selectedSourceKeys.begin(), selection)) + 1u
            : 0u;
        if (ImGui::BeginPopupContextItem("LibraryRawEditContext")) {
            if (!selected) {
                editor->SelectRawWorkspaceSourceForGallery(
                    source.relativePathKey,
                    false,
                    false,
                    false);
            }
            if (ImGui::MenuItem("Copy Edits", "Ctrl+C")) {
                std::string error;
                if (!editor->CopyRawEditAttributesFromGallerySource(
                        source, &error)) {
                    editor->ShowUiNotification(
                        UiNotificationSeverity::Error,
                        error.empty()
                            ? "This image has no saved RAW edits."
                            : error,
                        "library-raw-edit-attributes-copy-failed");
                }
            }
            if (ImGui::MenuItem("Paste Attributes...", "Ctrl+V")) {
                editor->RequestPasteRawEditAttributesForGallerySelection(false);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open in RAW")) {
                editor->SelectRawWorkspaceSourceForGallery(
                    source.relativePathKey,
                    false,
                    false,
                    true);
                openSelectedInRawTab();
            }
            ImGui::EndPopup();
        }
        if (hovered) {
            ImGui::SetTooltip(
                selected
                    ? "%s\nSelected frame %llu\nCtrl-click toggles; Shift-click selects a range."
                    : "%s\nCtrl-click toggles; Shift-click selects a range.",
                source.relativePathKey.c_str(),
                static_cast<unsigned long long>(selectionOrdinal));
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect tileRect(tileMin, ImVec2(tileMin.x + tileSize.x, tileMin.y + tileSize.y));

        int textureWidth = 0;
        int textureHeight = 0;
        const unsigned int texture = editor->GetRawWorkspaceThumbnailTexture(source, &textureWidth, &textureHeight);
        const ImVec2 imageSize = texture != 0
            ? FitRawLibraryImage(static_cast<float>(textureWidth), static_cast<float>(textureHeight), imageBounds)
            : imageBounds;
        const ImVec2 imageMin(
            tileMin.x + (tileSize.x - imageSize.x) * 0.5f,
            tileMin.y + 2.0f);
        const ImRect imageRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        if (texture != 0) {
            drawList->AddImage(
                (ImTextureID)(intptr_t)texture,
                imageRect.Min,
                imageRect.Max,
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f));
        } else {
            DrawRawLibraryPlaceholder(drawList, imageRect, selected);
        }

        const float textWidth = tileSize.x - 10.0f;
        const std::string title = FitRawLibraryText(source.fileName, textWidth);
        const std::string meta = FitRawLibraryText(
            FormatRawLibraryFileSize(source.fileSizeBytes) + " / Project " +
                Stack::RawWorkspace::ProjectStatusLabel(view.projectStatus),
            textWidth);
        drawList->AddText(ImVec2(tileMin.x + 5.0f, imageRect.Max.y + 6.0f), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
        drawList->AddText(ImVec2(tileMin.x + 5.0f, imageRect.Max.y + 25.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), meta.c_str());
        Stack::RawGallerySelectionVisuals::DrawTileSelection(
            drawList,
            tileRect.Min,
            tileRect.Max,
            selected,
            focused,
            hovered,
            selectionOrdinal);

        if (!m_FlashRawSourceKey.empty() && source.relativePathKey == m_FlashRawSourceKey) {
            const double elapsed = ImGui::GetTime() - m_FlashRawStartTime;
            if (elapsed >= 0.0 && elapsed < 0.95) {
                const float phase = static_cast<float>(elapsed / 0.95);
                const float intensity = std::sin(phase * 3.14159265f);
                ImVec4 flashColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                flashColor.w = 0.38f * intensity;
                drawList->AddRectFilled(tileRect.Min, tileRect.Max, ImGui::GetColorU32(flashColor), 7.0f);
                flashColor.w = 0.90f * intensity;
                drawList->AddRect(tileRect.Min, tileRect.Max, ImGui::GetColorU32(flashColor), 7.0f, 0, 2.5f);
            } else if (elapsed >= 0.95) {
                m_FlashRawSourceKey.clear();
            }
        }

        ImGui::PopID();
    };

    auto tileVisible = [](const ImVec2& tileMin, const ImVec2& tileSize) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window == nullptr) {
            return true;
        }
        constexpr float kCullMargin = 240.0f;
        ImRect clip = window->ClipRect;
        clip.Expand(kCullMargin);
        const ImRect tileRect(tileMin, ImVec2(tileMin.x + tileSize.x, tileMin.y + tileSize.y));
        return clip.Overlaps(tileRect);
    };

    const float tileWidth = 156.0f;
    const float tileHeight = 156.0f;
    const float gap = 16.0f;
    const ImVec2 tileSize(tileWidth, tileHeight);
    const ImVec2 imageBounds(tileWidth - 12.0f, 106.0f);
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const int columns = std::max(1, static_cast<int>((availableWidth + gap) / (tileWidth + gap)));

    ImGui::BeginChild(
        "LibraryRawWorkspaceGrid",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollWithMouse);

    if (m_RawScrollTargetY < 0.0f) {
        m_RawScrollTargetY = ImGui::GetScrollY();
        m_RawScrollCurrentY = m_RawScrollTargetY;
    }

    const float rawScrollY = ImGui::GetScrollY();
    if (std::abs(rawScrollY - m_RawScrollCurrentY) > 2.0f &&
        m_RawScrollCurrentY >= 0.0f) {
        // Preserve direct scrollbar dragging while keeping wheel input eased.
        m_RawScrollTargetY = rawScrollY;
        m_RawScrollCurrentY = rawScrollY;
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl && io.MouseWheel != 0.0f) {
            m_RawScrollTargetY -= io.MouseWheel * 90.0f;
        }
    }

    const ImVec2 gridStartPos = ImGui::GetCursorScreenPos();

    for (const Stack::RawWorkspace::GalleryFolderGroup& group : presentation.groups) {
        const bool targetExpanded = (m_CollapsedRawFolders.find(group.label) == m_CollapsedRawFolders.end());
        RawFolderAnimationState& anim = m_RawFolderAnimations[group.label];
        const float targetVal = targetExpanded ? 1.0f : 0.0f;
        if (!anim.initialized) {
            anim.expansion = targetVal;
            anim.initialized = true;
        } else {
            const float dt = ImGui::GetIO().DeltaTime;
            const float delta = targetVal - anim.expansion;
            if (std::abs(delta) < 0.001f) {
                anim.expansion = targetVal;
            } else {
                anim.expansion += delta * (1.0f - std::exp(-dt * 16.0f));
            }
        }

        ImGui::PushID(group.label.c_str());
        constexpr float headerHeight = 22.0f;
        constexpr float chevronVisualSize = 10.0f;
        constexpr float chevronHitSize = 16.0f;
        const ImVec2 textSize = ImGui::CalcTextSize(group.label.c_str());
        const float headerWidth = chevronHitSize + 6.0f + textSize.x + 8.0f;

        ImGui::InvisibleButton("##FolderHeader", ImVec2(headerWidth, headerHeight));
        const bool headerHovered = ImGui::IsItemHovered();
        const bool headerClicked = ImGui::IsItemClicked();
        if (headerClicked) {
            if (targetExpanded) {
                m_CollapsedRawFolders.insert(group.label);
            } else {
                m_CollapsedRawFolders.erase(group.label);
            }
        }

        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();

        ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        if (headerHovered) {
            tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }

        const ImVec2 chevronMin(
            headerMin.x + (chevronHitSize - chevronVisualSize) * 0.5f,
            headerMin.y + (headerHeight - chevronVisualSize) * 0.5f);
        const ImVec2 chevronMax(
            chevronMin.x + chevronVisualSize,
            chevronMin.y + chevronVisualSize);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (m_ChevronIconTex != 0) {
            // Smoothly rotate between 0 (pointing right) and 90 degrees (pointing down)
            const float angle = anim.expansion * (3.14159265f * 0.5f);
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
                (ImTextureID)(intptr_t)m_ChevronIconTex,
                p1, p2, p3, p4,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImVec2(0.0f, 1.0f),
                ImGui::GetColorU32(tint));
        }

        const ImVec2 textPos(
            headerMin.x + chevronHitSize + 4.0f,
            headerMin.y + (headerHeight - textSize.y) * 0.5f);
        drawList->AddText(textPos, ImGui::GetColorU32(tint), group.label.c_str());

        // In closed state, render tiny thumbnails lined up horizontally to the right of the folder name
        const float t = std::clamp(anim.expansion, 0.0f, 1.0f);
        const float easedT = t * t * (3.0f - 2.0f * t);
        const float miniAlpha = std::clamp(1.0f - easedT, 0.0f, 1.0f);

        if (miniAlpha > 0.01f) {
            constexpr float miniHeight = 18.0f;
            constexpr float miniWidth = 24.0f;
            constexpr float miniGap = 3.0f;
            constexpr float miniRounding = 2.0f;

            float currentX = headerMin.x + chevronHitSize + 4.0f + textSize.x + 14.0f;
            const float rightBound = headerMin.x + availableWidth - 10.0f;

            const int sourceCount = static_cast<int>(group.sources.size());
            for (int i = 0; i < sourceCount; ++i) {
                if (currentX + miniWidth > rightBound) {
                    const int remaining = sourceCount - i;
                    if (remaining > 0) {
                        const std::string moreText = "+" + std::to_string(remaining);
                        const ImVec2 moreTextSize = ImGui::CalcTextSize(moreText.c_str());
                        if (currentX + moreTextSize.x <= rightBound) {
                            ImVec4 moreTint = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                            moreTint.w *= miniAlpha * 0.8f;
                            const ImVec2 morePos(currentX, headerMin.y + (headerHeight - moreTextSize.y) * 0.5f);
                            drawList->AddText(morePos, ImGui::GetColorU32(moreTint), moreText.c_str());
                        }
                    }
                    break;
                }

                const Stack::RawWorkspace::GallerySourceView& view = group.sources[static_cast<std::size_t>(i)];
                const Stack::RawWorkspace::SourceRecord* source = FindRawLibrarySource(state, view.sourceIndex);
                if (source == nullptr) {
                    continue;
                }

                int tw = 0;
                int th = 0;
                const unsigned int texture = editor->GetRawWorkspaceThumbnailTexture(*source, &tw, &th);

                const ImVec2 miniMin(currentX, headerMin.y + (headerHeight - miniHeight) * 0.5f);
                const ImVec2 miniMax(currentX + miniWidth, miniMin.y + miniHeight);

                // Individual interactive button for each mini thumbnail
                ImGui::PushID(source->relativePathKey.c_str());
                ImGui::SetCursorScreenPos(miniMin);
                ImGui::InvisibleButton("##MiniThumb", ImVec2(miniWidth, miniHeight));
                const bool miniHovered = ImGui::IsItemHovered();
                const bool miniClicked = ImGui::IsItemClicked();
                ImGui::PopID();

                if (miniHovered) {
                    ImGui::SetTooltip("%s", source->fileName.c_str());
                }

                if (miniClicked) {
                    // Open the bar
                    m_CollapsedRawFolders.erase(group.label);
                    // Select the image
                    editor->SelectRawWorkspaceSourceForGallery(
                        source->relativePathKey,
                        false,
                        false,
                        false);
                    // Scroll to it
                    m_ScrollToRawSourceKey = source->relativePathKey;
                    // Flash it once
                    m_FlashRawSourceKey = source->relativePathKey;
                    m_FlashRawStartTime = ImGui::GetTime();
                }

                float& hoverAnim = m_MiniThumbHoverAnimations[source->relativePathKey];
                const float targetHover = miniHovered ? 1.0f : 0.0f;
                const float dt = ImGui::GetIO().DeltaTime;
                hoverAnim += (targetHover - hoverAnim) * (1.0f - std::exp(-dt * 18.0f));

                const float scale = 1.0f + 0.15f * hoverAnim;
                const ImVec2 center(
                    (miniMin.x + miniMax.x) * 0.5f,
                    (miniMin.y + miniMax.y) * 0.5f);
                const float halfW = (miniWidth * 0.5f) * scale;
                const float halfH = (miniHeight * 0.5f) * scale;
                const ImVec2 drawMin(center.x - halfW, center.y - halfH);
                const ImVec2 drawMax(center.x + halfW, center.y + halfH);

                if (texture != 0) {
                    ImVec4 imgTint(1.0f, 1.0f, 1.0f, miniAlpha);
                    drawList->AddImageRounded(
                        (ImTextureID)(intptr_t)texture,
                        drawMin,
                        drawMax,
                        ImVec2(0.0f, 1.0f),
                        ImVec2(1.0f, 0.0f),
                        ImGui::GetColorU32(imgTint),
                        miniRounding);
                } else {
                    const ImU32 bgCol = IM_COL32(255, 255, 255, static_cast<int>(24 * miniAlpha));
                    drawList->AddRectFilled(drawMin, drawMax, bgCol, miniRounding);
                }

                currentX += miniWidth + miniGap;
            }
        }

        ImGui::PopID();

        // Smoothly animate expanding/collapsing content height down to 0 without any child window minimum height clamping
        if (anim.expansion > 0.0001f) {
            const int sourceCount = static_cast<int>(group.sources.size());
            const int rowCount = (sourceCount + columns - 1) / columns;
            const float fullContentHeight = rowCount * tileHeight + std::max(0, rowCount - 1) * gap;
            const float currentHeight = fullContentHeight * easedT;

            const ImVec2 contentStartPos = ImGui::GetCursorScreenPos();
            const ImRect clipRect(
                contentStartPos,
                ImVec2(contentStartPos.x + availableWidth, contentStartPos.y + currentHeight));

            ImGui::PushClipRect(clipRect.Min, clipRect.Max, true);

            for (int row = 0; row < rowCount; ++row) {
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const bool rowVisible = tileVisible(rowMin, ImVec2(availableWidth, tileHeight)) &&
                                        (rowMin.y < clipRect.Max.y);
                for (int column = 0; column < columns; ++column) {
                    const int sourceOffset = row * columns + column;
                    if (sourceOffset >= sourceCount) {
                        break;
                    }
                    const Stack::RawWorkspace::GallerySourceView& view =
                        group.sources[static_cast<std::size_t>(sourceOffset)];
                    const Stack::RawWorkspace::SourceRecord* source = FindRawLibrarySource(state, view.sourceIndex);
                    if (source == nullptr) {
                        continue;
                    }
                    if (!m_ScrollToRawSourceKey.empty() && source->relativePathKey == m_ScrollToRawSourceKey) {
                        const float rawMaxScrollY = ImGui::GetScrollMaxY();
                        const float targetScroll = (rowMin.y - gridStartPos.y) + m_RawScrollCurrentY - 60.0f;
                        m_RawScrollTargetY = std::clamp(targetScroll, 0.0f, std::max(rawMaxScrollY, targetScroll));
                        m_ScrollToRawSourceKey.clear();
                    }
                    if (column > 0) {
                        ImGui::SameLine(0.0f, gap);
                    }
                    if (rowVisible) {
                        renderTile(*source, view, tileSize, imageBounds);
                    } else {
                        ImGui::Dummy(tileSize);
                    }
                }
            }

            ImGui::PopClipRect();
            ImGui::SetCursorScreenPos(ImVec2(contentStartPos.x, contentStartPos.y + currentHeight));
        }

        const float bottomSpacing = 4.0f + 12.0f * easedT;
        ImGui::Dummy(ImVec2(0.0f, bottomSpacing));
    }

    const float rawMaxScrollY = ImGui::GetScrollMaxY();
    m_RawScrollTargetY = std::clamp(m_RawScrollTargetY, 0.0f, rawMaxScrollY);
    if (std::abs(m_RawScrollCurrentY - m_RawScrollTargetY) > 0.05f) {
        const float scrollT = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 6.5f);
        m_RawScrollCurrentY +=
            (m_RawScrollTargetY - m_RawScrollCurrentY) * scrollT;
        m_RawScrollCurrentY = std::clamp(
            m_RawScrollCurrentY,
            0.0f,
            rawMaxScrollY);
    } else {
        m_RawScrollCurrentY = m_RawScrollTargetY;
    }
    ImGui::SetScrollY(m_RawScrollCurrentY);

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
