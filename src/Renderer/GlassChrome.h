// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "imgui.h"
#include <deque>
#include <string>
#include <array>
#include <cstdint>

namespace Stack::Renderer {
// Presentation-only material. Main-window draw callbacks capture the pixels
// behind the layout column once; independent objects reuse that capture.
// Never reads or mutates an image-processing texture.
// Owner must keep this object alive through RenderDrawData and shut it down on
// its owning GL context before that context is destroyed.
class GlassChrome {
public:
    void BeginFrame(bool enabled);
    // Queue once before the column's objects/foreground. Captures and blurs only: no
    // visible background, edge or shadow. Bounds also limit all object output.
    bool AddBackdrop(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 tint);
    // Uses the latest queued backdrop, without copying or blurring again.
    // Elevation scales only the air gap: 0 is contact, 1 is the resting height.
    // Values are clamped to [0,1]; non-finite inputs use the resting height.
    bool AddObject(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 tint,
        float radius, float elevation = 1.f);
    // Bracket only neutral foreground geometry. Does not create a hit target.
    bool BeginInk(ImDrawList* draw, ImVec2 min, ImVec2 max, ImGuiID id);
    void EndInk(ImDrawList* draw);
    const std::string& Error() const { return error; }
    void Shutdown();
private:
    struct CaptureLayout {
        int framebufferWidth=0, framebufferHeight=0;
        int x=0, y=0, width=0, height=0, reserveWidth=0, reserveHeight=0;
    };
    struct ObjectLayout {
        ImVec2 origin{}, size{}, volume{}; // Framebuffer pixels; volume is half-depth/gap.
        ImVec4 shadow{}; // Offset X/Y, projected scale and penumbra sigma.
        ImVec2 apron{};
        float radius=0.f;
        int left=0, bottom=0, width=0, height=0;
    };
    struct Backdrop { GlassChrome* owner; ImVec2 min, max, displayPos, displaySize, scale; ImVec4 tint, clip; float dpi; CaptureLayout layout{}; };
    struct Object { GlassChrome* owner; const Backdrop* backdrop; ImVec4 tint; ObjectLayout layout; };
    struct Ink { GlassChrome* owner; const Backdrop* backdrop; ImVec2 min, max; ImGuiID id; unsigned int slot; ImVec4 clip; bool usable=false; };
    static void InkCallback(const ImDrawList*, const ImDrawCmd*);
    static void EndInkCallback(const ImDrawList*, const ImDrawCmd*);
    void DrawInk(const Ink&);
    void PrepareInk();
    static void BackdropCallback(const ImDrawList*, const ImDrawCmd*);
    static void ObjectCallback(const ImDrawList*, const ImDrawCmd*);
    void CaptureBackdrop(const Backdrop&);
    void DrawObject(const Object&);
    // One renderer-owned layout contract supplies capture, shader parameters,
    // early culling and draw bounds before any GL callbacks are executed.
    static void BuildLayout(Backdrop&);
    static bool BuildLayout(const Backdrop&, ImVec2 min, ImVec2 max, ImVec4 clip,
        float radius, float elevation, ObjectLayout&);
    bool Ensure(int w, int h);
    void ReleaseTargets();
    bool enabled = false;
    std::string error;
    // deque keeps callback payloads and object-to-backdrop pointers stable
    // until the next BeginFrame, after ImGui has consumed this frame.
    std::deque<Backdrop> backdrops;
    std::deque<Object> objects;
    const Backdrop* activeBackdrop = nullptr;
    std::deque<Ink> inks;
    static constexpr unsigned int MaxInkItems=128;
    std::array<ImGuiID,MaxInkItems> inkIdentity{};
    std::array<std::uint64_t,MaxInkItems> inkLastUsed{};
    std::uint64_t inkEpoch=0;
    bool captureValid = false, inkReady=false;
    ImVec2 captureOrigin{}, captureSize{}, captureDisplayPos{}, captureDisplaySize{}, captureScale{};
    ImVec2 captureSoftSize{};
    unsigned int programs[4]{}, textures[3]{}, framebuffers[3]{}, vao = 0;
    unsigned int inkBuffers[2]{};
    int width = 0, height = 0;
};
}
