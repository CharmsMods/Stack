#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Stack::Editor {
// Per-editor presentation only. Textures are released on the UI GL context by
// Clear, both on document replacement and before renderer shutdown.
struct RawLayerPanelState {
    struct Thumbnail {
        std::string layerId;
        std::string maskId;
        unsigned int texture = 0;
        int width = 0, height = 0;
        int visibleFrame = -1;
        std::uint64_t revision = 0;
        std::string error;
    };
    bool floating = false;
    bool expanded = true;
    bool focusRename = false;
    std::string renameId;
    char renameBuffer[256] = {};
    std::unordered_map<std::string, Thumbnail> thumbnails;

    static std::string Key(const std::string& layerId, const std::string& maskId = {}) {
        return layerId + "/" + (maskId.empty() ? "output" : maskId);
    }
    void Clear();
    void Prune(int frame);
};
}
