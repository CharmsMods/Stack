
#pragma once

#include <imgui_internal.h>
#include <string>
#include <vector>

namespace Stack::Editor::RawLabInternal {

// Finite tile-to-grid transition. The browser keeps its existing thumbnail
// textures resident while the outgoing snapshot can still reference them.
class GalleryBrowserMotion {
public:
    struct Tile {
        ImRect bounds;
        const char* label;
        unsigned texture;
        int width, height;
        bool image;
        float unit;
        void (*drawFallback)(bool image, ImVec2 origin, float size, ImU32 color) = nullptr;
    };

    void Capture(ImRect surface);
    void CaptureTile(const Tile&);
    void Start(bool outward, ImRect anchor);
    void Stop();
    void Anchor(ImRect tile) { anchor_ = tile; }
    bool Active() const { return active_; }
    bool Outward() const { return outward_; }
    bool HideLiveGrid() const { return active_ && (waiting_ || !outward_); }
    void Begin(ImRect surface, bool ready, bool reduced);
    void DrawUnder(ImDrawList&) const;
    void DrawOver(ImDrawList&) const;
    void DrawLiveTile(ImDrawList&, const Tile&) const;

private:
    struct SnapshotTile {
        Tile tile;
        std::string label;
    };
    ImRect ZoomBounds(float amount) const;
    void DrawSnapshot(ImDrawList&, ImRect destination, float alpha) const;

    std::vector<SnapshotTile> snapshot_;
    ImRect savedSurface_{}, surface_{}, anchor_{};
    double started_ = 0;
    float progress_ = 0;
    bool active_ = false, outward_ = false, waiting_ = false, waitedForAnchor_ = false;
};

} // namespace Stack::Editor::RawLabInternal
