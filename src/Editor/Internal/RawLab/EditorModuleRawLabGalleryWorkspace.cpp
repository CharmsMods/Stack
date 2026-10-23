#include "Editor/EditorModule.h"
#include "RawLabGalleryDetails.h"
#include "RawLabUiSupport.h"
#include "App/AppHeaderStyle.h"
#include "App/WorkspaceInputScope.h"
#include "Library/LibraryControls.h"
#include "Editor/UI/WindowDrawOpacity.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

void EditorModule::RenderRawWorkspaceLabGalleryWorkspace(const ImVec2& size) {
    const bool grid = m_RawWorkspaceLabUi.galleryWorkspaceGrid;
    const bool previewHoverArmed = m_RawWorkspaceGalleryPreviewHovered;
    m_RawWorkspaceGalleryPreviewHovered = false;
    const float unit = ImGui::GetFontSize() / 13.0f;
    const bool showProperties = m_RawWorkspaceLabUi.galleryPropertiesOpen;
    const float target = showProperties ? 1.f : 0.f;
    if (m_RawGalleryPropertiesAmount < 0.f)
        m_RawGalleryPropertiesAmount = target;
    if (m_RawGalleryPropertiesAmount != target) {
        if (Stack::LibraryUi::ReducedMotion()) m_RawGalleryPropertiesAmount = target;
        else Stack::Header::ApproachMotion(m_RawGalleryPropertiesAmount, target, .24f);
    }
    const float propertiesAmount = m_RawGalleryPropertiesAmount;
    const float fullDetailsWidth = std::min(320.0f * ImGui::GetFontSize() / 16.f,
        std::max(1.0f, size.x * 0.34f));
    const float detailsWidth = fullDetailsWidth * propertiesAmount;
    const float imageWidth = std::max(1.0f, size.x - detailsWidth);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    // This is one docked workspace surface. Inner layout children must not
    // inherit rounded, separately shaded window chrome from the app theme.
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, GetWorkspaceBaseColor());
    ImGui::BeginChild("RawGalleryWorkspace", size, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (grid) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * unit, 12.0f * unit));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 16.0f));
        ImGui::BeginChild("Grid", ImVec2(imageWidth, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollWithMouse);
        RenderRawWorkspaceLabGalleryContent(false);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    // Grid thumbnails already own their cache requests. With properties fully
    // closed there is no preview consumer to resolve, upload or format for.
    if (grid && propertiesAmount == 0.f) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    // Resolve after the grid handles input, since its actions may change selection.
    auto& preview = m_RawWorkspaceGalleryPreview;
    const std::string workspaceKey = m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (preview.workspaceKey != workspaceKey) {
        preview = {};
        preview.workspaceKey = workspaceKey;
    }
    // Hover drives browsing even while another image is selected. Edit actions
    // capture their selection separately and never use this preview identity.
    const auto* requestedSource = m_RawWorkspaceLabFilmstripHoverSourceKey.empty() ? nullptr :
        FindRawWorkspaceSourceByKey(m_RawWorkspaceLabFilmstripHoverSourceKey);
    std::filesystem::path requestedProject = m_RawWorkspaceLabFilmstripHoverProjectPath;
    if (!requestedSource) {
        requestedSource = m_RawWorkspace.selectedSourceKey.empty() ? nullptr :
            FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
        requestedProject = m_RawWorkspaceLabFocusedProjectPath;
    }
    if (!requestedSource && !requestedProject.empty()) {
        const auto& projects = GetRawWorkspaceGalleryPresentation().projects;
        const auto project = std::find_if(projects.begin(), projects.end(), [&](const auto& item) {
            return item.projectPath == requestedProject;
        });
        if (project != projects.end()) requestedSource = FindRawWorkspaceSourceByKey(project->referenceSourceKey);
    }
    if (!requestedSource && requestedProject.empty() && !m_Project->rawSourceKey.empty())
        requestedSource = FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);

    struct Thumbnail {
        unsigned int texture = 0;
        std::shared_ptr<const Raw::RawMetadata> metadata;
        int width = 0;
        int height = 0;
        bool ready() const { return texture != 0 && width > 0 && height > 0; }
    };
    const auto resolve = [&](const Stack::RawWorkspace::SourceRecord* source,
                             const std::filesystem::path& projectPath) {
        Thumbnail result;
        if (!source) return result;
        if (projectPath.empty()) {
            result.texture = GetRawWorkspaceThumbnailTexture(*source, &result.width, &result.height, true);
            result.metadata = GetRawWorkspaceThumbnailMetadata(source->relativePathKey);
            return result;
        }
        Stack::RawWorkspace::SourceRecord thumbnailSource = *source;
        if (!projectPath.empty()) {
            const auto& gallery = GetRawWorkspaceGalleryPresentation();
            const auto project = std::find_if(gallery.projects.begin(), gallery.projects.end(),
                [&](const auto& candidate) {
                    return !candidate.projectPath.empty() &&
                        candidate.projectPath.lexically_normal() == projectPath.lexically_normal();
                });
            if (project != gallery.projects.end() && !project->coverThumbnailCachePath.empty()) {
                thumbnailSource.relativePathKey = "project-overlay:" +
                    (!project->projectId.empty() ? project->projectId : projectPath.generic_string());
                thumbnailSource.thumbnail.absolutePath = project->coverThumbnailCachePath;
                thumbnailSource.thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Ready;
            }
        }
        result.texture = GetRawWorkspaceThumbnailTexture(thumbnailSource, &result.width, &result.height, true);
        result.metadata = GetRawWorkspaceThumbnailMetadata(thumbnailSource.relativePathKey);
        return result;
    };
    const Thumbnail requested = resolve(requestedSource, requestedProject);
    const auto* displayedSource = preview.sourceKey.empty() ? nullptr : FindRawWorkspaceSourceByKey(preview.sourceKey);
    if (!displayedSource) {
        preview.sourceKey.clear();
        preview.previousSourceKey.clear();
        preview.blend = 1.0f;
    }
    const Thumbnail displayed = displayedSource == requestedSource && preview.projectPath == requestedProject
        ? requested : resolve(displayedSource, preview.projectPath);
    // Keep the previous image and its properties while an uncached thumbnail loads.
    if (requested.ready() && (preview.sourceKey != requestedSource->relativePathKey ||
            preview.projectPath != requestedProject)) {
        const double now = ImGui::GetTime();
        const bool rapidSwitch = preview.blend < 1.0f ||
            (preview.lastChangedAt >= 0.0 && now - preview.lastChangedAt < .16);
        const bool sameAspect = displayed.ready() && std::abs(
            static_cast<float>(requested.width) / requested.height -
            static_cast<float>(displayed.width) / displayed.height) < .001f;
        // Rapid sweeps switch at full opacity. Restarting an unfinished fade
        // from an older image makes the preview jump backwards and pulse.
        // Different aspect ratios also switch directly, keeping letterbox
        // changes out of the dissolve.
        if (!rapidSwitch && sameAspect) {
            preview.previousSourceKey = preview.sourceKey;
            preview.previousProjectPath = preview.projectPath;
            preview.blend = 0.0f;
        } else {
            preview.previousSourceKey.clear();
            preview.previousProjectPath.clear();
            preview.blend = 1.0f;
        }
        preview.sourceKey = requestedSource->relativePathKey;
        preview.projectPath = requestedProject;
        preview.lastChangedAt = now;
        displayedSource = requestedSource;
    }
    if (preview.blend < 1.0f) {
        preview.blend = std::min(1.0f, preview.blend +
            std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f) / 0.12f);
        if (preview.blend >= 1.0f) {
            preview.previousSourceKey.clear();
            preview.previousProjectPath.clear();
        }
    }
    const Thumbnail current = displayedSource == requestedSource && preview.projectPath == requestedProject
        ? requested : displayed;
    const Thumbnail previous = preview.previousSourceKey.empty() ? Thumbnail{} :
        resolve(FindRawWorkspaceSourceByKey(preview.previousSourceKey), preview.previousProjectPath);
    const auto drawPreview = [&](const ImVec2& available) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 extent(origin.x + available.x, origin.y + available.y);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec4 background = GetWorkspaceBaseColor();
        draw->AddRectFilled(origin, extent, ImGui::GetColorU32(background));
        const auto imageBounds = [&](const Thumbnail& thumbnail) {
            const ImVec2 imageSize = Stack::Editor::RawLabInternal::FitLabImage(
                static_cast<float>(thumbnail.width), static_cast<float>(thumbnail.height),
                ImVec2(std::max(1.0f, available.x - 32.0f * unit),
                       std::max(1.0f, available.y - 24.0f * unit)));
            const ImVec2 minimum(origin.x + (available.x - imageSize.x) * 0.5f,
                origin.y + (available.y - imageSize.y) * 0.5f);
            return std::make_pair(minimum, ImVec2(minimum.x + imageSize.x, minimum.y + imageSize.y));
        };
        const auto drawImage = [&](const Thumbnail& thumbnail, float alpha) {
            if (!thumbnail.ready() || alpha <= 0.0f) return;
            const auto [minimum, maximum] = imageBounds(thumbnail);
            draw->AddImage(
                static_cast<ImTextureID>(static_cast<intptr_t>(thumbnail.texture)), minimum, maximum,
                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)));
            if (!grid && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(minimum, maximum)) {
                m_RawWorkspaceGalleryPreviewHovered = previewHoverArmed || ImGui::GetIO().MouseDelta.y < -0.5f;
            }
        };
        // Dissolve the new image over an opaque old image to avoid a dark dip.
        drawImage(previous, 1.0f);
        drawImage(current, previous.ready() ? preview.blend : 1.0f);
        ImGui::Dummy(available);
    };
    const auto drawEmpty = [&] {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * unit, 8.0f * unit));
        if (requestedSource) {
            ImGui::TextDisabled("Loading preview...");
        } else if (m_RawWorkspace.workspaceRoot.empty()) {
            ImGui::TextUnformatted("Browse RAW images");
            ImGui::TextWrapped("Open a folder to browse its images and camera properties.");
            if (ImGui::Button("Open RAW Folder", ImVec2(160.0f * unit, 32.0f * unit)))
                OpenRawWorkspaceFolderDialog();
        } else if (IsRawWorkspaceScanBusy()) {
            ImGui::TextDisabled("Scanning RAW folder...");
        } else if (m_RawWorkspace.sources.empty()) {
            ImGui::TextWrapped("No RAW images in this folder.");
            if (ImGui::Button("Choose another folder")) OpenRawWorkspaceFolderDialog();
        } else {
            ImGui::TextDisabled("Hover an image to preview it.");
        }
        ImGui::PopStyleVar();
    };
    if (!grid) {
        ImGui::BeginChild("Image", ImVec2(imageWidth, 0.0f), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (current.ready()) drawPreview(ImGui::GetContentRegionAvail());
        else {
            ImGui::SetCursorPos(ImVec2(20.0f * unit, 20.0f * unit));
            drawEmpty();
        }
        ImGui::EndChild();
    }
    if (detailsWidth > .5f) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginChild("PropertiesClip", ImVec2(detailsWidth, 0), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        Stack::Workspace::InputScope propertiesInput(!showProperties, false);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("Properties", ImVec2(fullDetailsWidth, 0.0f), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (grid && current.ready())
            RenderRawGalleryProperties(displayedSource, current.metadata, preview.projectPath,
                [&](const ImVec2& available) { drawPreview(available); });
        else RenderRawGalleryProperties(displayedSource, current.metadata,
            displayedSource ? preview.projectPath : requestedProject, {});
        Stack::Editor::ApplyWindowDrawOpacity(ImGui::GetCurrentWindow(), propertiesAmount);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
