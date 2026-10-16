#pragma once

#include "Queue/RenderQueueModel.h"
#include "Queue/QueueView.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <utility>

namespace StackAppearance { class AppearanceManager; }

namespace Stack::Queue {

class QueueModule {
public:
    using ExportRequest = std::function<void(
        const std::filesystem::path&,
        const std::vector<Item>&)>;

    void SetExportRequestHandler(ExportRequest handler);
    void SetStatusText(const std::string& status) {
        if (m_StatusText != status) { m_StatusText = status; m_ExportState.status = status; }
    }
    void SetGalleryRequestHandler(std::function<void()> handler) { m_OpenGallery = std::move(handler); }
    void SetStopRequestHandler(std::function<void()> handler) { m_Stop = std::move(handler); }
    void SetExecutionState(bool busy) { m_ExportState.busy = busy; if (!busy) m_ExportState.stopping = false; }
    void RenderUI(StackAppearance::AppearanceManager* appearance);
    void RenderSectionPanel();
    void Shutdown();

    RenderQueueModel& Model() { return m_Model; }
    const RenderQueueModel& Model() const { return m_Model; }

private:
    struct TextureEntry {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        std::shared_ptr<const std::vector<unsigned char>> bytes;
        int usedFrame = 0;
    };

    TextureEntry* EnsureTexture(const Item& item);
    void ReleaseTexture(std::uint64_t id);
    const std::vector<Item>& FrameItems();
    void Execute(ViewAction action);
    void DrawThumbnail(const Item& item, ImVec2 position, ImVec2 size);

    RenderQueueModel m_Model;
    ExportRequest m_ExportRequest;
    std::function<void()> m_OpenGallery, m_Stop;
    std::unordered_map<std::uint64_t, TextureEntry> m_Textures;
    std::string m_StatusText;
    std::vector<Item> m_FrameItems;
    std::uint64_t m_SnapshotRevision = 0;
    std::uint64_t m_PresentationRevision = 0;
    int m_ThumbnailFrame = -1, m_ThumbnailLoads = 0;
    QueueView m_View;
    ExportPresentation m_ExportState;
    std::filesystem::path m_Destination;
};

} // namespace Stack::Queue
