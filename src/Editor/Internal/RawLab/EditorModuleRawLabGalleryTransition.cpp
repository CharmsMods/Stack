#include "Editor/EditorModule.h"
#include "RawLabUiSupport.h"

#include <imgui_internal.h>

void EditorModule::RecordRawWorkspaceGallerySlot(const Stack::RawWorkspace::SourceRecord& source,
    const ImVec2& minimum, const ImVec2& size, const ImRect& clip) {
    if (!m_PermanentGalleryWorkspace) return;
    int width = source.thumbnail.width, height = source.thumbnail.height;
    const auto cached = m_RawWorkspaceThumbnailTextures.find(source.relativePathKey);
    if (cached != m_RawWorkspaceThumbnailTextures.end() && cached->second.width > 0) {
        width = cached->second.width;
        height = cached->second.height;
    }
    if (width <= 0 || height <= 0) { width = 3; height = 2; }
    const ImVec2 fitted = Stack::Editor::RawLabInternal::FitLabImage(
        static_cast<float>(width), static_cast<float>(height), size);
    const ImVec2 imageMinimum(minimum.x + (size.x - fitted.x) * .5f,
        minimum.y + (size.y - fitted.y) * .5f);
    m_RawWorkspaceGalleryLayoutAnimation.ObserveSlot(source.relativePathKey,
        ImRect(imageMinimum, ImVec2(imageMinimum.x + fitted.x, imageMinimum.y + fitted.y)), clip);
}

bool EditorModule::RecordRawWorkspaceGalleryThumbnail(const std::string& key,
    const ImVec2& minimum, const ImVec2& maximum, float opacity) {
    const ImRect image(minimum, maximum);
    const ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImRect clip(draw->GetClipRectMin(), draw->GetClipRectMax());
    const auto entry = m_RawWorkspaceThumbnailTextures.find(key);
    if (image.Overlaps(clip) && entry != m_RawWorkspaceThumbnailTextures.end()) {
        entry->second.lastVisibleFrame = ImGui::GetFrameCount();
        entry->second.lastUseSerial = ++m_RawWorkspaceThumbnailTextureUseSerial;
    }
    return m_PermanentGalleryWorkspace && entry != m_RawWorkspaceThumbnailTextures.end() &&
        entry->second.texture != 0 &&
        m_RawWorkspaceGalleryLayoutAnimation.Observe(key, image, clip, opacity);
}

void EditorModule::RenderRawWorkspaceGalleryLayoutTransition() {
    m_RawWorkspaceGalleryLayoutAnimation.EndFrame();
    if (!m_RawWorkspaceGalleryLayoutAnimation.Active()) return;
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImRect bounds(position, ImVec2(position.x + size.x, position.y + size.y));
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(ImGui::GetWindowViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("RawGalleryLayoutTransition", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
    m_RawWorkspaceGalleryLayoutAnimation.Draw(ImGui::GetWindowDrawList(), bounds,
        [this](const std::string& key) -> Stack::Editor::RawLabInternal::GalleryLayoutAnimation::Texture {
            const auto entry = m_RawWorkspaceThumbnailTextures.find(key);
            if (entry == m_RawWorkspaceThumbnailTextures.end()) return {};
            entry->second.lastUseSerial = ++m_RawWorkspaceThumbnailTextureUseSerial;
            return {entry->second.texture, entry->second.width, entry->second.height};
        });
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleVar(3);
}
