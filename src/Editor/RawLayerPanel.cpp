#include "Editor/RawLayerPanel.h"
#include "Renderer/GLLoader.h"

namespace Stack::Editor {
void RawLayerPanelState::Clear() {
    for (auto& entry : thumbnails)
        if (entry.second.texture) glDeleteTextures(1, &entry.second.texture);
    thumbnails.clear();
    renameId.clear();
    focusRename = false;
}
void RawLayerPanelState::Prune(int frame) {
    for (auto it = thumbnails.begin(); it != thumbnails.end();) {
        if (frame - it->second.visibleFrame > 300) {
            if (it->second.texture) glDeleteTextures(1, &it->second.texture);
            it = thumbnails.erase(it);
        } else ++it;
    }
    while (thumbnails.size() > 64) {
        auto oldest = thumbnails.begin();
        for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it)
            if (it->second.visibleFrame < oldest->second.visibleFrame) oldest = it;
        if (oldest->second.visibleFrame >= frame - 1) break;
        if (oldest->second.texture) glDeleteTextures(1, &oldest->second.texture);
        thumbnails.erase(oldest);
    }
}
}
