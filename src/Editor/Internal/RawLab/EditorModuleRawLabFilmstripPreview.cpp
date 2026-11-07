#include "Editor/EditorModule.h"

#include <algorithm>

#include <imgui.h>

void EditorModule::RenderRawWorkspaceLabFilmstripPreviewOverlay() {
    struct PreviewImage {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        bool available = false;
    };
    const auto resolveImage = [&](const std::string& sourceKey,
                                  const std::filesystem::path& projectPath) {
        PreviewImage image;
        const auto* source = FindRawWorkspaceSourceByKey(sourceKey);
        if (!projectPath.empty()) {
            const auto& gallery = GetRawWorkspaceGalleryPresentation();
            const auto normalizedProjectPath = projectPath.lexically_normal();
            const auto project = std::find_if(
                gallery.projects.begin(), gallery.projects.end(),
                [&](const auto& candidate) {
                    return !candidate.projectPath.empty() &&
                        candidate.projectPath.lexically_normal() ==
                            normalizedProjectPath;
                });
            if (project != gallery.projects.end() && !source &&
                project->projectIndex < m_RawWorkspace.sourceSetProjects.size()) {
                const auto& card = m_RawWorkspace.sourceSetProjects[project->projectIndex];
                source = card.thumbnailOriginal ? card.thumbnailOriginal.get() :
                    card.linkedOriginal ? card.linkedOriginal.get() : card.masterSource.get();
            }
            if (project != gallery.projects.end() &&
                (!project->coverThumbnailCachePath.empty() ||
                 (m_GalleryComparisonEnabled && UsesGalleryPresetPreviews()))) {
                Stack::RawWorkspace::SourceRecord coverSource;
                coverSource.relativePathKey = project->projectCoverKey;
                coverSource.thumbnail.absolutePath =
                    project->coverThumbnailCachePath;
                coverSource.thumbnail.status =
                    Stack::RawWorkspace::ThumbnailStatus::Ready;
                image.available = true;
                image.texture = GetRawWorkspaceThumbnailTexture(
                    coverSource, &image.width, &image.height, true);
                const auto cover = m_RawWorkspaceThumbnailTextures.find(project->projectCoverKey);
                if (!source && cover != m_RawWorkspaceThumbnailTextures.end() && cover->second.transparentCover &&
                    !(m_GalleryComparisonEnabled && UsesGalleryPresetPreviews())) image.available = false;
                return image;
            }
        }
        if (source == nullptr) return image;
        image.available = true;
        image.texture = GetRawWorkspaceThumbnailTexture(
            *source, &image.width, &image.height, true);
        return image;
    };

    const PreviewImage current = resolveImage(
        m_RawWorkspaceLabFilmstripHoverSourceKey,
        m_RawWorkspaceLabFilmstripHoverProjectPath);
    // An unavailable target must not retain an unrelated prior project's cover.
    if (!current.available) {
        m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
        m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
        m_RawWorkspaceLabFilmstripHoverBlend = 1.0f;
        return;
    }
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
    PreviewImage full = current;
    if (currentReady && m_RawWorkspaceLabFilmstripHoverProjectPath.empty()) {
        if (const auto* source = FindRawWorkspaceSourceByKey(m_RawWorkspaceLabFilmstripHoverSourceKey)) {
            const ImVec2 area(std::max(1.f, size.x - 48.f), std::max(1.f, size.y - 48.f));
            if (auto detail = GetGalleryOriginalDetail(*source, area, full.width, full.height))
                full.texture = detail;
        }
    }
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
        drawImage(full, opacity * blend);
    }
    if (blend >= 1.0f) {
        m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
        m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
    }
}
