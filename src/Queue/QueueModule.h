#pragma once

#include "Queue/RenderQueueModel.h"

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
    void SetStatusText(std::string status) {
        m_StatusText = std::move(status);
    }
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
        std::size_t bytesHash = 0;
    };

    TextureEntry* EnsureTexture(const Item& item);
    void ReleaseTexture(std::uint64_t id);
    const std::vector<Item>& FrameItems();
    static const char* StateLabel(ItemState state);

    RenderQueueModel m_Model;
    ExportRequest m_ExportRequest;
    std::unordered_map<std::uint64_t, TextureEntry> m_Textures;
    std::string m_StatusText;
    std::vector<Item> m_FrameItems;
    int m_SnapshotFrame = -1;
    std::uint64_t m_DetailItem = 0;
    int m_StateFilter = 0;
};

} // namespace Stack::Queue
