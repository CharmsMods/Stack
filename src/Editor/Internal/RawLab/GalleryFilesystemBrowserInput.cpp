#include "GalleryFilesystemBrowserState.h"

#include <algorithm>
#include <utility>

namespace Stack::Editor::RawLabInternal {
namespace {
namespace fs = std::filesystem;
using FileAction = BrowserFiles::Action;
}

void GalleryFilesystemBrowser::State::CancelGridInput(bool clearSelection) {
    rightDrag.Cancel();
    rightOrigin.reset();
    leftOrigin.reset();
    movingImages = false;
    draggedImages.clear();
    dropFolder.clear();
    if (clearSelection) { selectedRange.reset(); selection.reset(); }
}

bool GalleryFilesystemBrowser::State::GridSelected(std::size_t index) const {
    return selectedRange && selectedRange->Contains(index);
}

bool GalleryFilesystemBrowser::FolderDropTarget(const fs::path& path, bool hovered) {
    auto& s = *state_;
    if (!hovered || !s.leftOrigin || path.empty()) return false;
    s.dropFolder = path;
    return s.movingImages;
}

void GalleryFilesystemBrowser::FinishGridInput() {
    auto& s = *state_;
    if (s.current.empty() || !s.directory || s.directory->pending || !FileInputAvailable() ||
        (s.searchPending || s.searchScheduled)) {
        s.CancelGridInput(false);
        return;
    }
    const auto& io = ImGui::GetIO();
    if ((s.leftOrigin || s.rightDrag.Active()) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        s.escapeConsumed = true;
        s.CancelGridInput(false);
        return;
    }
    const bool overGrid = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const auto hit = s.hit.node ? std::optional{s.hit.index} : std::nullopt;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && overGrid && !s.leftOrigin) {
        s.rightDrag.Begin(hit, s.hit.bounds);
        s.rightOrigin = s.hit.node;
    }
    if (s.rightDrag.Active()) {
        s.rightDrag.Update(io.MousePos, hit);
        if (s.rightDrag.Range()) s.selectedRange = s.rightDrag.Range();
        if (s.rightDrag.Range() && ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            io.MousePos.x >= s.gridBounds.Min.x && io.MousePos.x <= s.gridBounds.Max.x) {
            const float edge = 22.f * ImGui::GetFontSize() / 14.f;
            const float y = std::clamp(io.MousePos.y, s.gridBounds.Min.y, s.gridBounds.Max.y);
            const float speed = std::max(0.f, (y - s.gridBounds.Max.y + edge) / edge) -
                std::max(0.f, (s.gridBounds.Min.y + edge - y) / edge);
            if (speed != 0.f) ImGui::SetScrollY(ImGui::GetScrollY() + speed * 420.f * io.DeltaTime);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            if (s.rightDrag.ReleaseShowsMenu(overGrid)) {
                s.contextNode = s.rightOrigin;
                s.contextRequested = true;
            }
            s.rightOrigin.reset();
        }
    }

    if (!s.rightDrag.Active() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && s.hit.node &&
        s.hit.node->kind == State::Kind::Image) {
        s.leftOrigin = s.hit.node;
        s.leftIndex = s.hit.index;
    }
    if (s.leftOrigin && !s.movingImages && ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {
        const auto& tiles = s.searchKey.empty() ? s.directory->children : s.searchResults;
        if (s.leftIndex >= tiles.size() || tiles[s.leftIndex] != s.leftOrigin) {
            s.CancelGridInput(false);
            return;
        }
        if (s.GridSelected(s.leftIndex)) {
            const auto first = s.selectedRange->First(), last = s.selectedRange->Last();
            for (std::size_t i = first; i <= last && i < tiles.size(); ++i)
                if (tiles[i]->kind == State::Kind::Image) s.draggedImages.push_back(tiles[i]->path);
        } else {
            s.selectedRange.reset();
            s.selection = s.leftOrigin;
            s.draggedImages.push_back(s.leftOrigin->path);
        }
        s.movingImages = true;
    }
    if (s.movingImages && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::BeginTooltip();
        ImGui::Text("%zu %s", s.draggedImages.size(), s.draggedImages.size() == 1 ? "image" : "images");
        ImGui::EndTooltip();
    }
    if (s.leftOrigin && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (s.movingImages && !s.dropFolder.empty()) {
            SubmitFile({FileAction::MoveImages, s.dropFolder, std::move(s.draggedImages)});
        } else if (s.movingImages && s.draggedImages.size() == 1 && s.hit.node &&
            s.hit.node->kind == State::Kind::Image && s.hit.node != s.leftOrigin) {
            SubmitFile({FileAction::GroupImages, s.hit.node->path, std::move(s.draggedImages)});
        }
        s.leftOrigin.reset();
        s.movingImages = false;
        s.draggedImages.clear();
    }
}

} // namespace Stack::Editor::RawLabInternal
