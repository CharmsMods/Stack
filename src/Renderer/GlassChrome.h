// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "imgui.h"
#include <deque>
#include <string>

namespace Stack::Renderer {
// Presentation-only material. Main-window draw callbacks capture the pixels
// already behind each bar; never reads or mutates an image-processing texture.
// Owner must keep this object alive through RenderDrawData and shut it down on
// its owning GL context before that context is destroyed.
class GlassChrome {
public:
    void BeginFrame(bool enabled);
    bool AddSurface(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 tint, float radius = 0.f);
    const std::string& Error() const { return error; }
    void Shutdown();
private:
    struct Surface { GlassChrome* owner; ImVec2 min, max, displayPos, displaySize, scale; ImVec4 tint, clip; float radius, dpi; };
    static void Callback(const ImDrawList*, const ImDrawCmd*);
    void Draw(const Surface&);
    bool Ensure(int w, int h);
    void ReleaseTargets();
    bool enabled = false;
    std::string error;
    std::deque<Surface> surfaces;
    unsigned int programs[2]{}, textures[3]{}, framebuffers[3]{}, vao = 0;
    int width = 0, height = 0;
};
}
