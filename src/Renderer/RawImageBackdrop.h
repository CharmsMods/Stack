#pragma once
#include "imgui.h"
#include "RawImageBackdropNoise.h"
#include "RawImageBackdropBlur.h"
#include <array>
#include <vector>

namespace Stack::Renderer {
// Borrowed display textures, valid only for the current UI/render frame.
// The editor supplies image geometry; the renderer owns all backdrop resources.
struct RawImageBackdropFrame {
    struct Patch {
        unsigned int texture = 0;
        ImVec2 minimum{}, maximum{}, uvMinimum{0, 1}, uvMaximum{1, 0};
        ImVec4 extendEdges{1, 1, 1, 1}; // left, bottom, right, top
        unsigned int previousTexture = 0;
        ImVec2 previousOffset{}, previousScale{1, 1};
        float fadeAmount = 1.f;
        bool encodedSrgb = false;
        // Full-photo normalized bounds, with the same bottom-left origin as
        // texture coordinates. Independent of the patch's screen placement.
        ImVec4 imageBounds{0, 0, 1, 1};
    };
    int frame = -1;
    std::size_t presentationFingerprint = 0;
    bool extendImage = false;
    bool completePhotoCoverage = false;
    float extensionOpacity = 0.f;
    float edgeOverlap = 0.f;
    ImDrawList* sharpDrawList = nullptr;
    int sharpCommandBegin = 0, sharpCommandEnd = 0;
    float controlsAmount = 0.f;
    ImVec2 imageMinimum{}, imageMaximum{};
    ImVec2 nativeExtent{};
    ImVec2 controlsMinimum{}, controlsMaximum{};
    std::vector<Patch> patches;
};

class RawImageBackdrop {
public:
    bool Render(const RawImageBackdropFrame& frame, const ImDrawData& data, const ImVec4& clear);
    void Shutdown();
private:
    bool Ensure(int width, int height);
    void ReleaseImages();
    RawImageBackdropBlur blur;
    RawImageBackdropNoise noise;
    unsigned int texture = 0, framebuffer = 0, copyProgram = 0, presentProgram = 0, vao = 0;
    int width = 0, height = 0;
    bool failed = false;
};

// Temporarily leave the photo's narrow edge to the successful backdrop pass.
// A failed backdrop keeps the full ordinary photo draw as its fallback.
class RawPhotoInteriorClip {
public:
    explicit RawPhotoInteriorClip(const RawImageBackdropFrame* frame);
    ~RawPhotoInteriorClip();
    RawPhotoInteriorClip(const RawPhotoInteriorClip&) = delete;
    RawPhotoInteriorClip& operator=(const RawPhotoInteriorClip&) = delete;
private:
    const RawImageBackdropFrame* frame = nullptr;
    std::vector<ImVec4> clips;
};
}
