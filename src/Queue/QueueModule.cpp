#include "App/settings/PrimaryAction.h"
#include "App/settings/AppearanceTheme.h"
#include "Queue/QueueModule.h"

#include "ThirdParty/stb_image.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace Stack::Queue {

const char* QueueModule::StateLabel(ItemState state) {
    switch (state) {
    case ItemState::Preparing: return "Preparing";
    case ItemState::Rendering: return "Rendering";
    case ItemState::Writing: return "Writing";
    case ItemState::Complete: return "Complete";
    case ItemState::Failed: return "Failed";
    case ItemState::Waiting:
    default: return "Waiting";
    }
}

namespace {
std::size_t HashBytes(const std::vector<unsigned char>& bytes) {
    std::size_t hash = bytes.size();
    const std::size_t stride = std::max<std::size_t>(1u, bytes.size() / 64u);
    for (std::size_t index = 0; index < bytes.size(); index += stride) {
        hash ^= static_cast<std::size_t>(bytes[index]) +
            0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    }
    return hash;
}

} // namespace

void QueueModule::SetExportRequestHandler(ExportRequest handler) {
    m_ExportRequest = std::move(handler);
}

QueueModule::TextureEntry* QueueModule::EnsureTexture(const Item& item) {
    if (item.thumbnailBytes.empty() ||
        item.thumbnailBytes.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return nullptr;
    }
    const std::size_t bytesHash = HashBytes(item.thumbnailBytes);
    TextureEntry& entry = m_Textures[item.id];
    if (entry.texture != 0 && entry.bytesHash == bytesHash) return &entry;
    if (entry.texture != 0) glDeleteTextures(1, &entry.texture);
    entry = {};

    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* decoded = stbi_load_from_memory(
        item.thumbnailBytes.data(),
        static_cast<int>(item.thumbnailBytes.size()),
        &entry.width,
        &entry.height,
        &channels,
        4);
    if (!decoded || entry.width <= 0 || entry.height <= 0) {
        if (decoded) stbi_image_free(decoded);
        entry = {};
        return nullptr;
    }
    entry.texture = GLHelpers::CreateTextureFromPixels(
        decoded, entry.width, entry.height, 4);
    stbi_image_free(decoded);
    entry.bytesHash = bytesHash;
    return entry.texture != 0 ? &entry : nullptr;
}

void QueueModule::ReleaseTexture(std::uint64_t id) {
    const auto found = m_Textures.find(id);
    if (found == m_Textures.end()) return;
    if (found->second.texture != 0) {
        glDeleteTextures(1, &found->second.texture);
    }
    m_Textures.erase(found);
}

const std::vector<Item>& QueueModule::FrameItems() {
    const int frame = ImGui::GetFrameCount();
    if (m_SnapshotFrame != frame) {
        m_FrameItems = m_Model.Snapshot();
        m_SnapshotFrame = frame;
        if (std::none_of(m_FrameItems.begin(), m_FrameItems.end(),
                [this](const Item& item) { return item.id == m_DetailItem; }))
            m_DetailItem = m_FrameItems.empty() ? 0 : m_FrameItems.front().id;
    }
    return m_FrameItems;
}

void QueueModule::RenderUI(StackAppearance::AppearanceManager* appearance) {

    const std::vector<Item>& items = FrameItems();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::BeginChild("QueueSurface", ImVec2(0.0f, 0.0f), false);

    const auto requestSelectedExport = [&]() {
        const std::string folder = FileDialogs::OpenFolderDialog(
            "Choose Queue Export Folder");
        if (folder.empty()) return;
        std::vector<Item> selected;
        for (const Item& item : m_Model.Snapshot()) {
            if (item.selected &&
                (item.state == ItemState::Waiting ||
                 item.state == ItemState::Failed)) {
                selected.push_back(item);
            }
        }
        if (!selected.empty()) {
            m_StatusText = "Queue export started.";
            m_ExportRequest(std::filesystem::u8path(folder), selected);
        }
    };

    const float topRowY = ImGui::GetCursorPosY();
    ImGui::TextUnformatted("Render Queue");
    ImGui::SameLine();
    ImGui::TextDisabled("%llu item%s",
        static_cast<unsigned long long>(items.size()),
        items.size() == 1u ? "" : "s");
    const float headerBottomY = ImGui::GetCursorPosY();
    constexpr float exportWidth = 180.0f;
    ImGui::SetCursorPos(ImVec2(
        std::max(0.0f, (ImGui::GetWindowWidth() - exportWidth) * 0.5f),
        topRowY));
    const bool canExport = m_ExportRequest && m_Model.HasSelectedWaitingItems();
    ImGui::BeginDisabled(!canExport);
    if (StackAppearance::PrimaryActionButton("Export", ImVec2(exportWidth, 36.0f), appearance ? &appearance->GetResolvedCreamPalette().primaryAction : nullptr)) {
        requestSelectedExport();
    }
    ImGui::EndDisabled();
    ImGui::SetCursorPosY(std::max(headerBottomY, topRowY + 36.0f) + 8.0f);
    if (!items.empty()) {
        if (ImGui::SmallButton("Select All")) m_Model.SetAllSelected(true);
        ImGui::SameLine();
        if (ImGui::SmallButton("Select None")) m_Model.SetAllSelected(false);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Completed")) m_Model.ClearCompleted();
    }
    ImGui::Spacing();

    if (items.empty()) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const char* emptyText =
            "Right-click images or saved projects in the RAW Gallery and choose Add to Queue.";
        const ImVec2 textSize = ImGui::CalcTextSize(emptyText);
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (available.x - textSize.x) * 0.5f),
            std::max(60.0f, available.y * 0.36f)));
        ImGui::TextDisabled("%s", emptyText);
    } else {
        constexpr float tileWidth = 184.0f;
        constexpr float tileHeight = 232.0f;
        constexpr float gap = 16.0f;
        const float availableWidth = std::max(tileWidth, ImGui::GetContentRegionAvail().x);
        const int columns = std::max(1, static_cast<int>(
            std::floor((availableWidth + gap) / (tileWidth + gap))));
        int column = 0;
        for (const Item& item : items) {
            ImGui::PushID(static_cast<int>(item.id));
            ImGui::BeginGroup();
            bool selected = item.selected;
            if (ImGui::Checkbox("##Selected", &selected)) {
                m_Model.SetSelected(item.id, selected);
                m_DetailItem = item.id;
            }
            const ImVec2 imageSize(tileWidth, 128.0f);
            const ImVec2 imageMin = ImGui::GetCursorScreenPos();
            if (TextureEntry* texture = EnsureTexture(item)) {
                const float imageAspect = static_cast<float>(texture->width) /
                    static_cast<float>(std::max(1, texture->height));
                ImVec2 fitted = imageSize;
                if (imageAspect > imageSize.x / imageSize.y) {
                    fitted.y = imageSize.x / imageAspect;
                } else {
                    fitted.x = imageSize.y * imageAspect;
                }
                const ImVec2 offset(
                    (imageSize.x - fitted.x) * 0.5f,
                    (imageSize.y - fitted.y) * 0.5f);
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)texture->texture,
                    ImVec2(imageMin.x + offset.x, imageMin.y + offset.y),
                    ImVec2(imageMin.x + offset.x + fitted.x,
                           imageMin.y + offset.y + fitted.y));
            } else {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    imageMin,
                    ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y),
                    ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.10f)),
                    4.0f);
                const char* label = item.kind == ItemKind::Project
                    ? "PROJECT"
                    : "IMAGE";
                const ImVec2 labelSize = ImGui::CalcTextSize(label);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(imageMin.x + (imageSize.x - labelSize.x) * 0.5f,
                           imageMin.y + (imageSize.y - labelSize.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
            }
            if (ImGui::InvisibleButton("##InspectJob", imageSize)) m_DetailItem = item.id;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", item.displayName.c_str());
            const std::string clipped = item.displayName.size() > 25u
                ? item.displayName.substr(0, 22u) + "..."
                : item.displayName;
            ImGui::TextUnformatted(clipped.c_str());
            ImGui::TextDisabled("%s", StateLabel(item.state));
            if (!item.status.empty()) {
                const std::string status = item.status.size() > 27u
                    ? item.status.substr(0, 24u) + "..."
                    : item.status;
                ImGui::TextDisabled("%s", status.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", item.status.c_str());
                }
            } else {
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
            }
            if (item.state != ItemState::Waiting) {
                ImGui::ProgressBar(item.progress, ImVec2(tileWidth - 28.0f, 4.0f), "");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                ReleaseTexture(item.id);
                m_Model.Remove(item.id);
            }
            ImGui::EndGroup();
            ++column;
            if (column < columns) ImGui::SameLine(0.0f, gap);
            else column = 0;
            ImGui::PopID();
        }
    }

    // Emit an item for bottom spacing. SetCursorPosY() alone can extend a
    // child window beyond its submitted-item bounds and triggers Dear ImGui's
    // recovery assertion when the queue/status are both empty.
    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    if (!m_StatusText.empty()) {
        const ImVec2 statusSize = ImGui::CalcTextSize(m_StatusText.c_str());
        ImGui::SetCursorPosX(std::max(0.0f,
            (ImGui::GetWindowWidth() - statusSize.x) * 0.5f));
        ImGui::TextDisabled("%s", m_StatusText.c_str());
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void QueueModule::Shutdown() {
    for (auto& entry : m_Textures) {
        if (entry.second.texture != 0) {
            glDeleteTextures(1, &entry.second.texture);
        }
    }
    m_Textures.clear();
    m_FrameItems.clear();
    m_SnapshotFrame = -1;
    m_DetailItem = 0;
}

} // namespace Stack::Queue
