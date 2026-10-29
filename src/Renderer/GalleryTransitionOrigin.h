#pragma once
#include <imgui.h>
#include <memory>
#include <string>

namespace Stack::Renderer {
// The thumbnail cache retires the GL name on its context after the final lease.
// Destruction of this value never calls GL or reaches back into an editor.
struct GalleryThumbnailLease {
    unsigned int texture=0;
    int width=0,height=0;
    std::size_t bytes=0;
};
struct GalleryTransitionOrigin {
    std::string subject,thumbnailKey,sourceIdentity;
    ImVec2 minimum{},maximum{},uvMinimum{0,1},uvMaximum{1,0};
    std::shared_ptr<const GalleryThumbnailLease> thumbnail;
    bool Valid() const {return maximum.x>minimum.x && maximum.y>minimum.y;}
};
}
