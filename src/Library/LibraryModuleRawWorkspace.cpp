#include "LibraryModule.h"

#include "Editor/EditorModule.h"
#include "Utils/ImGuiExtras.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

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

bool RenderRawLibraryModeButton(const char* label, bool active, const ImVec2& size) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const bool clicked = ImGui::Button(label, size);
    if (active) {
        ImGui::PopStyleColor(3);
    }
    return clicked;
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

    auto switchToProjects = [&]() {
        m_ShowRawWorkspace = false;
        m_ShowAssets = false;
        m_SelectedAssets.clear();
    };
    auto switchToAssets = [&]() {
        m_ShowRawWorkspace = false;
        m_ShowAssets = true;
        m_SelectedProjects.clear();
    };

    if (RenderRawLibraryModeButton("All Projects", false, ImVec2(116.0f, 28.0f))) {
        switchToProjects();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (RenderRawLibraryModeButton("Assets", false, ImVec2(84.0f, 28.0f))) {
        switchToAssets();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }
    ImGui::SameLine(0.0f, 6.0f);
    RenderRawLibraryModeButton("RAW Workspace", true, ImVec2(134.0f, 28.0f));

    ImGui::SameLine(0.0f, 22.0f);
    if (editor != nullptr && ImGui::Button("Open RAW Folder", ImVec2(150.0f, 28.0f))) {
        editor->OpenRawWorkspaceFolderDialog();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (editor != nullptr && ImGui::Button("Rescan", ImVec2(82.0f, 28.0f))) {
        editor->RescanRawWorkspace();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (editor != nullptr && ImGui::Button("Clear", ImVec2(74.0f, 28.0f))) {
        editor->ClearRawWorkspaceForUser();
    }

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
    auto renderRawWorkspaceBusyOverlay = [&]() {
        if (editor->IsRawWorkspaceScanBusy()) {
            const std::string status = editor->GetRawWorkspaceScanStatusText();
            ImGuiExtras::RenderBusyOverlay(
                status.empty()
                    ? "Loading RAW Workspace..."
                    : status.c_str());
        } else if (editor->IsRawWorkspaceProjectLoadBusy()) {
            const std::string status = editor->GetRawWorkspaceProjectLoadStatusText();
            ImGuiExtras::RenderBusyOverlay(
                status.empty()
                    ? "Loading RAW project..."
                    : status.c_str());
        } else if (editor->IsRawWorkspaceProjectSaveBusy()) {
            const std::string status = editor->GetRawWorkspaceProjectSaveStatusText();
            ImGuiExtras::RenderBusyOverlay(
                status.empty()
                    ? "Saving RAW project..."
                    : status.c_str());
        }
    };

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

    auto openSelectedInRawTab = [&]() {
        if (activeTab != nullptr && rawWorkspaceTabId >= 0) {
            *activeTab = rawWorkspaceTabId;
        }
    };

    if (!state.selectedSourceKey.empty()) {
        ImGui::SameLine(0.0f, 18.0f);
        if (ImGui::Button("Open in RAW", ImVec2(112.0f, 28.0f))) {
            openSelectedInRawTab();
        }
    }

    ImGui::Spacing();
    if (state.workspaceRoot.empty()) {
        ImGui::TextDisabled("No RAW Workspace is open.");
        renderRawWorkspaceBusyOverlay();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }

    const Stack::RawWorkspace::GalleryPresentation& presentation =
        editor->GetRawWorkspaceGalleryPresentation();
    if (presentation.totalSources == 0 && !scanBusy) {
        ImGui::TextDisabled("No RAW files found.");
        renderRawWorkspaceBusyOverlay();
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
            editor->SelectRawWorkspaceSourceForPreview(source.relativePathKey);
        }
        if (doubleClicked) {
            editor->SelectRawWorkspaceSourceForPreview(source.relativePathKey);
            openSelectedInRawTab();
        }
        if (hovered) {
            ImGui::SetTooltip("%s", source.relativePathKey.c_str());
        }

        const bool selected = source.relativePathKey == editor->GetRawWorkspaceState().selectedSourceKey || view.selected;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect tileRect(tileMin, ImVec2(tileMin.x + tileSize.x, tileMin.y + tileSize.y));
        if (hovered || selected) {
            drawList->AddRectFilled(
                tileRect.Min,
                tileRect.Max,
                ImGui::GetColorU32(selected ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBgHovered, selected ? 0.58f : 0.34f),
                6.0f);
        }

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
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
            if (selected) {
                drawList->AddRect(imageRect.Min, imageRect.Max, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f, 0, 2.0f);
            }
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

    ImGui::BeginChild("LibraryRawWorkspaceGrid", ImVec2(0.0f, 0.0f), false);
    for (const Stack::RawWorkspace::GalleryFolderGroup& group : presentation.groups) {
        ImGui::TextDisabled("%s", group.label.c_str());
        const int sourceCount = static_cast<int>(group.sources.size());
        const int rowCount = (sourceCount + columns - 1) / columns;
        for (int row = 0; row < rowCount; ++row) {
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const bool rowVisible = tileVisible(rowMin, ImVec2(availableWidth, tileHeight));
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
        ImGui::Spacing();
    }
    ImGui::EndChild();
    renderRawWorkspaceBusyOverlay();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
