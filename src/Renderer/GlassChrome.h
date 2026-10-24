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
    // Bracket only neutral foreground geometry. Does not create a hit target.
    bool BeginInk(ImDrawList* draw, ImVec2 min, ImVec2 max, ImGuiID id);
    void EndInk(ImDrawList* draw);
    const std::string& Error() const { return error; }
    void Shutdown();
private:
    struct Surface { GlassChrome* owner; ImVec2 min, max, displayPos, displaySize, scale; ImVec4 tint, clip; float radius, dpi; };
    struct Ink { GlassChrome* owner; ImVec2 min, max; ImGuiID id; unsigned int slot; ImVec4 clip; bool usable=false; };
    static void InkCallback(const ImDrawList*, const ImDrawCmd*);
    static void EndInkCallback(const ImDrawList*, const ImDrawCmd*);
    void DrawInk(const Ink&);
    void PrepareInk();
    static void Callback(const ImDrawList*, const ImDrawCmd*);
    void Draw(const Surface&);
    bool Ensure(int w, int h);
    void ReleaseTargets();
    bool enabled = false;
    std::string error;
    std::deque<Surface> surfaces;
    std::deque<Ink> inks;
    static constexpr unsigned int MaxInkItems=128;
    bool captureValid = false, inkReady=false;
    ImVec2 captureOrigin{}, captureSize{}, captureDisplayPos{}, captureDisplaySize{}, captureScale{};
    ImVec2 captureSoftSize{};
    unsigned int programs[4]{}, textures[3]{}, framebuffers[3]{}, vao = 0;
    unsigned int inkBuffers[2]{};
    int width = 0, height = 0;
};
}
