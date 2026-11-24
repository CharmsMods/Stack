
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <imgui_internal.h>

namespace Stack::Editor::RawLabInternal {

struct BrowserSelectionRange {
    std::size_t anchor, end;
    std::size_t First() const { return std::min(anchor, end); }
    std::size_t Last() const { return std::max(anchor, end); }
    bool Contains(std::size_t index) const { return index >= First() && index <= Last(); }
};

class BrowserRightDrag {
public:
    void Begin(std::optional<std::size_t> index, ImRect bounds) {
        active_ = true;
        bounds_ = bounds;
        origin_ = index;
        range_.reset();
    }

    void Update(ImVec2 pointer, std::optional<std::size_t> index) {
        if (!active_) return;
        if (!origin_ && index) {
            origin_ = index;
            range_ = BrowserSelectionRange{*index, *index};
        } else if (origin_ && !range_ && !bounds_.Contains(pointer)) {
            range_ = BrowserSelectionRange{*origin_, *origin_};
        }
        if (range_ && index) range_->end = *index;
    }

    bool ReleaseShowsMenu(bool overGrid) {
        const bool show = active_ && !range_ && overGrid;
        Cancel();
        return show;
    }

    void Cancel() { active_ = false; origin_.reset(); range_.reset(); }
    bool Active() const { return active_; }
    const std::optional<BrowserSelectionRange>& Range() const { return range_; }

private:
    bool active_ = false;
    ImRect bounds_{};
    std::optional<std::size_t> origin_;
    std::optional<BrowserSelectionRange> range_;
};

} // namespace Stack::Editor::RawLabInternal
