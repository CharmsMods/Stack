#include "Editor/EditorModule.h"

#include <algorithm>

#include <imgui.h>

void EditorModule::RenderRawWorkspaceLabFilmstripPreviewOverlay() {
    struct PreviewImage {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
    };
    const auto resolveImage = [&](const std::string& sourceKey,
                                  const std::filesystem::path& projectPath) {
        PreviewImage image;
        const auto* source = FindRawWorkspaceSourceByKey(sourceKey);
        if (source == nullptr) return image;

        if (!projectPath.empty()) {
            const auto& gallery = GetRawWorkspaceGalleryPresentation();
            const auto project = std::find_if(
                gallery.projects.begin(), gallery.projects.end(),
                [&](const auto& candidate) {
                    return !candidate.projectPath.empty() &&
                        candidate.projectPath.lexically_normal() ==
                            projectPath.lexically_normal();
                });
            if (project != gallery.projects.end() &&
                !project->coverThumbnailCachePath.empty()) {
                Stack::RawWorkspace::SourceRecord coverSource;
                coverSource.relativePathKey = "project-overlay:" +
                    (!project->projectId.empty()
                        ? project->projectId
                        : project->projectPath.lexically_normal().generic_string());
                coverSource.thumbnail.absolutePath =
                    project->coverThumbnailCachePath;
                coverSource.thumbnail.status =
                    Stack::RawWorkspace::ThumbnailStatus::Ready;
                image.texture = GetRawWorkspaceThumbnailTexture(
                    coverSource, &image.width, &image.height, true);
                return image;
            }
        }
        image.texture = GetRawWorkspaceThumbnailTexture(
            *source, &image.width, &image.height, true);
        return image;
    };

    const PreviewImage current = resolveImage(
        m_RawWorkspaceLabFilmstripHoverSourceKey,
        m_RawWorkspaceLabFilmstripHoverProjectPath);
    const PreviewImage previous = resolveImage(
        m_RawWorkspaceLabFilmstripPreviousHoverSourceKey,
        m_RawWorkspaceLabFilmstripPreviousHoverProjectPath);
    const bool currentReady = current.texture != 0 &&
        current.width > 0 && current.height > 0;
    const bool previousReady = previous.texture != 0 &&
        previous.width > 0 && previous.height > 0;
    if (!currentReady && previousReady) {
        // Keep the last image visible until the next thumbnail is available.
        m_RawWorkspaceLabFilmstripHoverBlend = 0.0f;
    } else if (!previousReady) {
        m_RawWorkspaceLabFilmstripHoverBlend = 1.0f;
    }
    if (!currentReady && !previousReady) return;

    const float opacity = std::clamp(
        m_RawWorkspaceLabFilmstripHoverOpacity, 0.0f, 1.0f);
    const float progress = std::clamp(
        m_RawWorkspaceLabFilmstripHoverBlend, 0.0f, 1.0f);
    const float blend = progress * progress * (3.0f - 2.0f * progress);
    const ImVec2 minimum = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
    ImDrawList* drawList = ImGui::GetForegroundDrawList(
        ImGui::GetWindowViewport());
    ImVec4 backdrop = GetWorkspaceBaseColor();
    backdrop.w *= opacity;
    drawList->AddRectFilled(
        minimum, maximum,
        ImGui::GetColorU32(backdrop));

    const auto drawImage = [&](const PreviewImage& image, float alpha) {
        if (image.texture == 0 || image.width <= 0 ||
            image.height <= 0 || alpha <= 0.0f) return;
        const float scale = std::min(
            std::max(1.0f, size.x - 48.0f) / image.width,
            std::max(1.0f, size.y - 48.0f) / image.height);
        const ImVec2 imageSize(image.width * scale, image.height * scale);
        const ImVec2 imageMinimum(
            minimum.x + (size.x - imageSize.x) * 0.5f,
            minimum.y + (size.y - imageSize.y) * 0.5f);
        drawList->AddImage(
            (ImTextureID)(intptr_t)image.texture,
            imageMinimum,
            ImVec2(imageMinimum.x + imageSize.x,
                   imageMinimum.y + imageSize.y),
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f),
            ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)));
    };
    if (previousReady && blend < 1.0f) {
        drawImage(previous, opacity);
    }
    if (currentReady) {
        drawImage(current, opacity * blend);
    }
    if (blend >= 1.0f) {
        m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
        m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
    }
}
