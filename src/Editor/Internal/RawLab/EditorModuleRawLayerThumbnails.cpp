#include "Editor/EditorModule.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"

#include <algorithm>

void EditorModule::AppendRawLayerThumbnailRequests(std::vector<EditorRenderWorker::PreviewRequest>& requests) {
    if (!m_RawWorkspaceRootTabActive || !IsRawWorkspaceProjectActive()) return;
    const auto revision = std::max<std::uint64_t>(1, m_RenderRevision);
    int count = 0;
    for (const auto& entry : m_RawLayerPanel.thumbnails) {
        const auto& thumbnail = entry.second;
        if (ImGui::GetFrameCount() - thumbnail.visibleFrame > 1 || thumbnail.revision == revision) continue;
        EditorRenderWorker::PreviewRequest request;
        request.rawLayerId = thumbnail.layerId;
        request.rawMaskId = thumbnail.maskId;
        request.dirtyGeneration = revision;
        // RAW and imported graph inputs are held by the snapshot. A small
        // transparent reference canvas avoids copying the viewport's pixels.
        request.width = request.height = 256;
        requests.push_back(std::move(request));
        if (++count == 2) break; // Keep auxiliary batches interruptible by edits.
    }
}

void EditorModule::AdoptRawLayerThumbnail(const EditorRenderWorker::PreviewResult& result) {
    if (result.dirtyGeneration != std::max<std::uint64_t>(1, m_RenderRevision)) return;
    auto found = m_RawLayerPanel.thumbnails.find(
        Stack::Editor::RawLayerPanelState::Key(result.rawLayerId, result.rawMaskId));
    if (found == m_RawLayerPanel.thumbnails.end()) return;
    auto& thumbnail = found->second;
    thumbnail.revision = result.dirtyGeneration;
    thumbnail.error = result.error;
    unsigned int texture = 0;
    if (result.success && result.width > 0 && result.height > 0 &&
        result.pixels.size() == static_cast<std::size_t>(result.width) * result.height * 4) {
        texture = GLHelpers::CreateTextureFromPixels(result.pixels.data(), result.width, result.height, 4);
    }
    if (thumbnail.texture) glDeleteTextures(1, &thumbnail.texture);
    thumbnail.texture = texture;
    thumbnail.width = result.width;
    thumbnail.height = result.height;
    if (!texture && thumbnail.error.empty()) thumbnail.error = "Thumbnail unavailable.";
}

bool EditorModule::RenderRawLayerThumbnail(const std::string& layerId, const std::string& maskId,
    const ImVec2& size, bool selected) {
    const std::string key = Stack::Editor::RawLayerPanelState::Key(layerId, maskId);
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
    const bool released = ImGui::InvisibleButton(key.c_str(), size);
    const bool clicked = released || (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
    if (!ImGui::IsItemVisible()) return clicked;
    auto& thumbnail = m_RawLayerPanel.thumbnails[key];
    thumbnail.layerId = layerId;
    thumbnail.maskId = maskId;
    thumbnail.visibleFrame = ImGui::GetFrameCount();
    const bool current = thumbnail.revision == std::max<std::uint64_t>(1, m_RenderRevision);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
    if (thumbnail.texture && thumbnail.width > 0 && thumbnail.height > 0) {
        const float scale = std::min((size.x - 4) / thumbnail.width, (size.y - 4) / thumbnail.height);
        const ImVec2 extent(thumbnail.width * scale, thumbnail.height * scale);
        const ImVec2 origin(minimum.x + (size.x - extent.x) * .5f, minimum.y + (size.y - extent.y) * .5f);
        draw->AddImage((ImTextureID)(intptr_t)thumbnail.texture, origin,
            ImVec2(origin.x + extent.x, origin.y + extent.y), ImVec2(0,1), ImVec2(1,0),
            ImGui::GetColorU32(ImVec4(1,1,1,current ? 1.0f : .45f)));
    } else {
        const char* status = thumbnail.error.empty() ? "..." : "!";
        const auto text = ImGui::CalcTextSize(status);
        draw->AddText(ImVec2(minimum.x + (size.x-text.x)*.5f, minimum.y+(size.y-text.y)*.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), status);
    }
    draw->AddRect(minimum, maximum, ImGui::GetColorU32(selected ? ImGuiCol_SliderGrabActive : ImGuiCol_Border),
        3.0f, 0, selected ? 2.0f : 1.0f);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(maskId.empty() ? "Photo after this layer. Click to edit its RAW controls." :
            "Mask coverage. Click to edit its shape. Double-click to open Graph.");
        if (!thumbnail.error.empty()) ImGui::TextUnformatted(thumbnail.error.c_str());
        else if (!current) ImGui::TextDisabled("Updating thumbnail...");
        if (thumbnail.texture) {
            const float scale = 144.0f / std::max(thumbnail.width, thumbnail.height);
            ImGui::Image((ImTextureID)(intptr_t)thumbnail.texture,
                ImVec2(thumbnail.width*scale, thumbnail.height*scale), ImVec2(0,1), ImVec2(1,0));
        }
        ImGui::EndTooltip();
    }
    return clicked;
}
