#pragma once
#include "imgui.h"
#include <array>
#include <cstddef>

namespace Stack::Renderer {
struct RawImageBackdropFrame;

// A local Gaussian pyramid and a broad color field over the complete photo.
// Pixel identity and allocation are independent of window placement; the
// owning backdrop supplies its current GL context.
class RawImageBackdropBlur {
public:
    static constexpr int Levels = 7;
    bool Prepare(const RawImageBackdropFrame& frame);
    void Bind(unsigned int program, int firstTextureUnit, ImVec2 displayedPhotoSize) const;
    void Shutdown();
private:
    struct Level {
        unsigned int texture = 0, scratch = 0, framebuffer = 0, scratchFramebuffer = 0;
        int width = 0, height = 0;
    };
    bool Ensure(int width, int height);
    void ReleaseImages();
    std::array<Level, Levels> levels{};
    std::array<float, Levels> normalizedSigmas{};
    ImVec4 photoBounds{0, 0, 1, 1};
    unsigned int texture = 0, framebuffer = 0, colorField = 0, colorFieldFramebuffer = 0;
    unsigned int copyProgram = 0, blurProgram = 0, colorFieldProgram = 0, vao = 0;
    int width = 0, height = 0;
    std::size_t identity = 0;
    bool valid = false, failed = false;
};
}
