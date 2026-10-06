#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Stack::Editor::RawWorkspaceInternal {

// This is deliberately smaller than RawWorkspace::SourceRecord.  The
// thumbnail worker only needs the immutable source identity and path data;
// project memberships and other gallery metadata stay on the main thread.
struct RawWorkspaceThumbnailWorkItem {
    std::size_t sourceIndex = 0;
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    std::string sourceKey;
    std::string fileName;
    std::string stem;
    std::string extension;
    std::string parentFolderKey;
    std::uintmax_t fileSizeBytes = 0;
    std::int64_t modifiedTimeTicks = 0;
    std::string fingerprint;
};

class RawWorkspaceThumbnailScheduler {
public:
    void Reset(std::vector<RawWorkspaceThumbnailWorkItem> items) {
        Clear();
        m_OrderByKey.reserve(items.size());
        std::int64_t order = 0;
        for (auto& item : items) {
            if (item.sourceKey.empty() ||
                !m_OrderByKey.emplace(item.sourceKey, order).second) {
                continue;
            }
            m_Items.emplace_hint(m_Items.end(), order++, std::move(item));
        }
        m_NextBackOrder = order;
    }

    void Clear() {
        m_Items.clear();
        m_OrderByKey.clear();
        m_PriorityOrder.clear();
        m_FrontOrder = 0;
        m_NextBackOrder = 0;
    }

    bool Empty() const {
        return m_Items.empty();
    }

    std::size_t Size() const {
        return m_Items.size();
    }

    void Promote(const std::string& sourceKey) {
        if (sourceKey.empty()) {
            return;
        }
        const auto item = m_OrderByKey.find(sourceKey);
        if (item != m_OrderByKey.end()) {
            m_PriorityOrder.insert(item->second);
        }
    }

    void Promote(const std::vector<std::string>& sourceKeys) {
        for (const std::string& sourceKey : sourceKeys) {
            Promote(sourceKey);
        }
    }

    bool PushFront(RawWorkspaceThumbnailWorkItem item) {
        if (item.sourceKey.empty()) {
            return false;
        }
        const auto existing = m_OrderByKey.find(item.sourceKey);
        if (existing != m_OrderByKey.end()) {
            m_PriorityOrder.insert(existing->second);
            return false;
        }
        const auto order = --m_FrontOrder;
        m_OrderByKey.emplace(item.sourceKey, order);
        m_Items.emplace_hint(m_Items.begin(), order, std::move(item));
        m_PriorityOrder.insert(order);
        return true;
    }

    bool PushBack(RawWorkspaceThumbnailWorkItem item) {
        if (item.sourceKey.empty() ||
            m_OrderByKey.find(item.sourceKey) != m_OrderByKey.end()) {
            return false;
        }
        const std::int64_t order = m_NextBackOrder++;
        m_OrderByKey.emplace(item.sourceKey, order);
        m_Items.emplace_hint(m_Items.end(), order, std::move(item));
        return true;
    }

    bool TryTakeNext(RawWorkspaceThumbnailWorkItem& outItem) {
        if (m_Items.empty()) {
            return false;
        }

        auto item = m_Items.begin();
        if (!m_PriorityOrder.empty()) {
            item = m_Items.find(*m_PriorityOrder.begin());
            m_PriorityOrder.erase(m_PriorityOrder.begin());
        }

        outItem = std::move(item->second);
        m_OrderByKey.erase(outItem.sourceKey);
        m_Items.erase(item);
        return true;
    }

private:
    // Preserve catalog order among promoted items without scanning the pending
    // catalog under the mutex shared by the gallery and thumbnail worker.
    std::map<std::int64_t, RawWorkspaceThumbnailWorkItem> m_Items;
    std::unordered_map<std::string, std::int64_t> m_OrderByKey;
    std::set<std::int64_t> m_PriorityOrder;
    std::int64_t m_FrontOrder = 0;
    std::int64_t m_NextBackOrder = 0;
};

} // namespace Stack::Editor::RawWorkspaceInternal
