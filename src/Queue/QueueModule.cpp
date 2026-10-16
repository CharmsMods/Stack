#include "Queue/QueueModule.h"
#include "App/settings/AppearanceTheme.h"
#include "App/PlatformHelpers.h"
#include "Library/LibraryControls.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "Utils/FileDialogs.h"
#include "imgui.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace Stack::Queue {

void QueueModule::SetExportRequestHandler(ExportRequest handler) {
    m_ExportRequest = std::move(handler);
}

QueueModule::TextureEntry* QueueModule::EnsureTexture(const Item& item) {
    if (!item.thumbnailBytes || item.thumbnailBytes->empty()) return nullptr;
    const int frame = ImGui::GetFrameCount();
    const auto found = m_Textures.find(item.id);
    if (found != m_Textures.end() && found->second.bytes == item.thumbnailBytes) {
        found->second.usedFrame = frame;
        return found->second.texture ? &found->second : nullptr;
    }
    // First visibility requests only. Bound one-time thumbnail decode/upload
    // demand, including when a long queue is first opened or scrolled rapidly.
    if (m_ThumbnailFrame != frame) { m_ThumbnailFrame = frame; m_ThumbnailLoads = 0; }
    if (m_ThumbnailLoads >= 2) return nullptr;
    ++m_ThumbnailLoads;
    ReleaseTexture(item.id);
    if (m_Textures.size() >= 128) {
        const auto oldest = std::min_element(m_Textures.begin(), m_Textures.end(),
            [](const auto& a, const auto& b) { return a.second.usedFrame < b.second.usedFrame; });
        if (oldest != m_Textures.end()) ReleaseTexture(oldest->first);
    }
    auto& entry = m_Textures[item.id];
    entry.bytes = item.thumbnailBytes;
    entry.usedFrame = frame;
    const auto& bytes = *entry.bytes;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return nullptr;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    auto* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
        &entry.width, &entry.height, &channels, 4);
    if (!decoded || entry.width <= 0 || entry.height <= 0) {
        if (decoded) stbi_image_free(decoded);
        return nullptr;
    }
    entry.texture = GLHelpers::CreateTextureFromPixels(decoded, entry.width, entry.height, 4);
    stbi_image_free(decoded);
    // Failed bytes retain their immutable identity too; don't retry each frame.
    return entry.texture ? &entry : nullptr;
}

void QueueModule::ReleaseTexture(std::uint64_t id) {
    const auto found = m_Textures.find(id);
    if (found == m_Textures.end()) return;
    if (found->second.texture) glDeleteTextures(1, &found->second.texture);
    m_Textures.erase(found);
}

const std::vector<Item>& QueueModule::FrameItems() {
    const auto oldSize = m_FrameItems.size();
    if (m_Model.SnapshotIfChanged(m_FrameItems, m_SnapshotRevision, &m_PresentationRevision)) {
        if (oldSize != m_FrameItems.size() && !m_Textures.empty()) {
            std::unordered_set<std::uint64_t> present;
            for (const auto& item : m_FrameItems) present.insert(item.id);
            for (auto it = m_Textures.begin(); it != m_Textures.end();) {
                if (present.find(it->first) != present.end()) { ++it; continue; }
                if (it->second.texture) glDeleteTextures(1, &it->second.texture);
                it = m_Textures.erase(it);
            }
        }
    }
    m_View.Prepare(m_FrameItems, m_PresentationRevision);
    return m_FrameItems;
}

void QueueModule::DrawThumbnail(const Item& item, ImVec2 position, ImVec2 size) {
    auto* draw = ImGui::GetWindowDrawList();
    if (auto* texture = EnsureTexture(item)) {
        const float scale = std::min(size.x / texture->width, size.y / texture->height);
        const ImVec2 fitted(texture->width * scale, texture->height * scale);
        const ImVec2 at(position.x + (size.x - fitted.x) * .5f, position.y + (size.y - fitted.y) * .5f);
        draw->AddImage((ImTextureID)(intptr_t)texture->texture, at, ImVec2(at.x + fitted.x, at.y + fitted.y));
    } else {
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text); color.w *= .065f;
        draw->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y),
            ImGui::GetColorU32(color), 3);
        color.w = .72f;
        const float extent = std::min(size.x, size.y) * .38f;
        LibraryUi::DrawIcon(item.kind == ItemKind::SourceImage ? LibraryUi::Icon::Gallery : LibraryUi::Icon::SavedEdits,
            ImVec2(position.x + (size.x - extent) * .5f, position.y + (size.y - extent) * .5f),
            extent, ImGui::GetColorU32(color));
    }
}

void QueueModule::Execute(ViewAction action) {
    switch (action.command) {
    case ViewCommand::OpenGallery:
        if (m_OpenGallery) m_OpenGallery();
        break;
    case ViewCommand::ChooseDestination:
        if (!m_ExportState.busy) {
            const auto folder = FileDialogs::OpenFolderDialog("Choose Queue Export Folder");
            if (!folder.empty()) {
                m_Destination = std::filesystem::u8path(folder);
                m_ExportState.destination = folder;
            }
        }
        break;
    case ViewCommand::Export:
        if (!m_ExportRequest || m_ExportState.busy) break;
        if (m_Destination.empty()) Execute({ViewCommand::ChooseDestination});
        if (!m_Destination.empty()) {
            std::vector<Item> selected;
            for (const auto& item : m_Model.Snapshot())
                if (item.selected && (item.state == ItemState::Waiting || item.state == ItemState::Failed)) selected.push_back(item);
            if (!selected.empty()) m_ExportRequest(m_Destination, selected);
        }
        break;
    case ViewCommand::StopAfterCurrent:
        if (m_ExportState.busy && !m_ExportState.stopping && m_Stop) {
            m_Stop();
            m_ExportState.stopping = true;
        }
        break;
    case ViewCommand::SelectAll: m_Model.SetAllSelected(true); break;
    case ViewCommand::SelectNone: m_Model.SetAllSelected(false); break;
    case ViewCommand::ClearCompleted: m_Model.ClearCompleted(); break;
    case ViewCommand::Include: m_Model.SetSelected(action.id, action.included); break;
    case ViewCommand::Remove:
        m_Model.Remove(action.id);
        ReleaseTexture(action.id);
        break;
    case ViewCommand::RevealOutput: {
        const auto items = m_Model.Snapshot();
        const auto found = std::find_if(items.begin(), items.end(), [&](const Item& item) { return item.id == action.id; });
        if (found != items.end() && !found->outputPath.empty()) {
            std::string error;
            if (!PlatformHelpers::RevealPathInExplorer(found->outputPath, &error)) SetStatusText(error);
        }
        break;
    }
    case ViewCommand::RevealDestination:
        if (!m_Destination.empty()) {
            std::string error;
            if (!PlatformHelpers::OpenPath(m_Destination, &error)) SetStatusText(error);
        }
        break;
    default: break;
    }
}

void QueueModule::RenderUI(StackAppearance::AppearanceManager* appearance) {
    const float s = ImGui::GetFontSize() / 16.f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * s, 20 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    if (ImGui::BeginChild("QueueSurface", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto& items = FrameItems();
        m_ExportState.available = static_cast<bool>(m_ExportRequest);
        m_ExportState.primaryColors = appearance ? &appearance->GetResolvedCreamPalette().primaryAction : nullptr;
        Execute(m_View.RenderWorkspace(items, m_ExportState,
            [this](const Item& item, ImVec2 at, ImVec2 size) { DrawThumbnail(item, at, size); }));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void QueueModule::Shutdown() {
    for (auto& entry : m_Textures) if (entry.second.texture) glDeleteTextures(1, &entry.second.texture);
    m_Textures.clear();
    m_FrameItems.clear();
    m_SnapshotRevision = 0;
    m_View.Reset();
    m_ExportRequest = {};
    m_OpenGallery = m_Stop = {};
}
} // namespace Stack::Queue
