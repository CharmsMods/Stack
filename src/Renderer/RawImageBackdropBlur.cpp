#include "RawImageBackdropBlur.h"
#include "RawImageBackdrop.h"
#include "RawImageBackdropShaders.h"
#include "RawImageBackdropColorFieldShaders.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace Stack::Renderer {
namespace {
constexpr int ColorSampleLevel = 2;
constexpr int ColorFieldSize = 256;
unsigned int CreateImage(int width, int height) {
    return GLHelpers::CreateTextureFromData(nullptr, width, height,
        GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
}
bool IsWholePhoto(const RawImageBackdropFrame::Patch& patch) {
    const auto& b = patch.imageBounds;
    return b.x == 0.f && b.y == 0.f && b.z == 1.f && b.w == 1.f;
}
std::size_t SourceIdentity(const RawImageBackdropFrame& frame,
    const RawImageBackdropFrame::Patch* wholePhoto) {
    std::size_t hash = frame.presentationFingerprint;
    const auto append = [&](auto value) {
        hash ^= std::hash<decltype(value)>{}(value) + std::size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
    };
    const auto point = [&](ImVec2 p) { append(p.x); append(p.y); };
    point(frame.nativeExtent);
    for (const auto& patch : frame.patches) {
        if (wholePhoto && &patch != wholePhoto) continue;
        append(patch.texture); append(patch.previousTexture); append(patch.fadeAmount); append(patch.encodedSrgb);
        append(patch.imageBounds.x); append(patch.imageBounds.y);
        append(patch.imageBounds.z); append(patch.imageBounds.w);
        point(patch.uvMinimum); point(patch.uvMaximum);
        point(patch.previousOffset); point(patch.previousScale);
        append(patch.extendEdges.x); append(patch.extendEdges.y);
        append(patch.extendEdges.z); append(patch.extendEdges.w);
    }
    return hash;
}
}

void RawImageBackdropBlur::ReleaseImages() {
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (texture) glDeleteTextures(1, &texture);
    framebuffer = texture = 0;
    if (colorFieldFramebuffer) glDeleteFramebuffers(1, &colorFieldFramebuffer);
    if (colorField) glDeleteTextures(1, &colorField);
    colorFieldFramebuffer = colorField = 0;
    for (auto& level : levels) {
        if (level.framebuffer) glDeleteFramebuffers(1, &level.framebuffer);
        if (level.scratchFramebuffer) glDeleteFramebuffers(1, &level.scratchFramebuffer);
        if (level.texture) glDeleteTextures(1, &level.texture);
        if (level.scratch) glDeleteTextures(1, &level.scratch);
        level = {};
    }
    width = height = 0;
    identity = 0; valid = false;
}

bool RawImageBackdropBlur::Ensure(int w, int h) {
    if (failed) return false;
    if (!copyProgram) {
        copyProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropShaders::Copy);
        blurProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropShaders::Blur);
        colorFieldProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex,
            RawImageBackdropColorFieldShaders::Generate.c_str());
        glGenVertexArrays(1, &vao);
        if (!copyProgram || !blurProgram || !colorFieldProgram || !vao) { failed = true; return false; }
    }
    if (texture && framebuffer && width == w && height == h) return true;
    ReleaseImages();
    texture = CreateImage(w, h);
    framebuffer = texture ? GLHelpers::CreateFBO(texture) : 0;
    colorField = CreateImage(ColorFieldSize, ColorFieldSize);
    colorFieldFramebuffer = colorField ? GLHelpers::CreateFBO(colorField) : 0;
    bool complete = framebuffer && colorFieldFramebuffer;
    int levelWidth = w, levelHeight = h;
    for (auto& level : levels) {
        if (!complete) break;
        level.width = levelWidth = std::max(1, (levelWidth+1)/2);
        level.height = levelHeight = std::max(1, (levelHeight+1)/2);
        level.texture = CreateImage(levelWidth, levelHeight);
        level.scratch = CreateImage(levelWidth, levelHeight);
        level.framebuffer = level.texture ? GLHelpers::CreateFBO(level.texture) : 0;
        level.scratchFramebuffer = level.scratch ? GLHelpers::CreateFBO(level.scratch) : 0;
        complete = level.framebuffer && level.scratchFramebuffer;
    }
    if (!complete) { ReleaseImages(); failed = true; return false; }
    width = w; height = h;
    return true;
}

bool RawImageBackdropBlur::Prepare(const RawImageBackdropFrame& frame) {
    if (frame.patches.empty() || !frame.completePhotoCoverage ||
        !std::isfinite(frame.nativeExtent.x) || !std::isfinite(frame.nativeExtent.y) ||
        frame.nativeExtent.x <= 0.f || frame.nativeExtent.y <= 0.f) return false;
    // A native crop must not change the surround's color when navigation asks
    // for different detail. Prefer the accepted, complete developed photo.
    const RawImageBackdropFrame::Patch* wholePhoto = nullptr;
    for (const auto& patch : frame.patches) {
        if (IsWholePhoto(patch)) { wholePhoto = &patch; break; }
    }
    const auto nextIdentity = SourceIdentity(frame, wholePhoto);
    // Anonymous callers cannot prove that borrowed texture pixels stayed the
    // same, matching the noise profile's accepted-content reuse contract.
    if (frame.presentationFingerprint && valid && identity == nextIdentity) return true;
    GLint maximum = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    // Blur the extended color field, rather than extending already-blurred
    // edge texels. One shorter-photo dimension of padding on each side lets
    // a broad 2D Gaussian spread across edges and corners without using any
    // screen-clipped input or exceeding the bounded atlas allocation.
    const float padding = std::min(frame.nativeExtent.x, frame.nativeExtent.y);
    const ImVec2 extent(frame.nativeExtent.x + 2.f*padding, frame.nativeExtent.y + 2.f*padding);
    float scale = std::min(1.f, float(std::min(2048, maximum)) / std::max(extent.x, extent.y));
    if (wholePhoto) {
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, wholePhoto->texture);
        GLint sourceWidth = 0, sourceHeight = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &sourceWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &sourceHeight);
        const float photoWidth = sourceWidth*std::abs(wholePhoto->uvMaximum.x-wholePhoto->uvMinimum.x);
        const float photoHeight = sourceHeight*std::abs(wholePhoto->uvMaximum.y-wholePhoto->uvMinimum.y);
        scale = std::min({scale, photoWidth/frame.nativeExtent.x, photoHeight/frame.nativeExtent.y});
    }
    if (scale <= 0.f || !Ensure(std::max(1, int(std::ceil(extent.x*scale))),
        std::max(1, int(std::ceil(extent.y*scale))))) return false;
    photoBounds = ImVec4(padding/extent.x, padding/extent.y,
        (padding+frame.nativeExtent.x)/extent.x, (padding+frame.nativeExtent.y)/extent.y);
    const auto photoPoint = [&](float x, float y) {
        return ImVec2(photoBounds.x+x*(photoBounds.z-photoBounds.x),
            photoBounds.y+y*(photoBounds.w-photoBounds.y));
    };
    valid = false;
    glBindVertexArray(vao);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer); glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(copyProgram);
    glUniform1i(glGetUniformLocation(copyProgram, "image"), 0);
    glUniform1i(glGetUniformLocation(copyProgram, "previous"), 1);
    glUniform1i(glGetUniformLocation(copyProgram, "visibleColorOnly"), 1);
    for (const auto& patch : frame.patches) {
        if (wholePhoto && &patch != wholePhoto) continue;
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, patch.previousTexture ? patch.previousTexture : patch.texture);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, patch.texture);
        const auto minimum = photoPoint(patch.imageBounds.x, patch.imageBounds.y);
        const auto maximum = photoPoint(patch.imageBounds.z, patch.imageBounds.w);
        glUniform4f(glGetUniformLocation(copyProgram, "rect"), minimum.x,
            minimum.y, maximum.x, maximum.y);
        glUniform4f(glGetUniformLocation(copyProgram, "textureRect"),
            patch.uvMinimum.x, patch.uvMaximum.y, patch.uvMaximum.x, patch.uvMinimum.y);
        glUniform4f(glGetUniformLocation(copyProgram, "extendEdges"), patch.extendEdges.x,
            patch.extendEdges.y, patch.extendEdges.z, patch.extendEdges.w);
        glUniform2f(glGetUniformLocation(copyProgram, "previousOffset"), patch.previousOffset.x, patch.previousOffset.y);
        glUniform2f(glGetUniformLocation(copyProgram, "previousScale"), patch.previousScale.x, patch.previousScale.y);
        glUniform1f(glGetUniformLocation(copyProgram, "fadeAmount"), patch.previousTexture ? patch.fadeAmount : 1.f);
        glUniform1i(glGetUniformLocation(copyProgram, "encodedSrgb"), patch.encodedSrgb ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glUseProgram(blurProgram);
    glUniform1i(glGetUniformLocation(blurProgram, "image"), 0);
    glUniform4f(glGetUniformLocation(blurProgram, "photo"), 0, 0, 1, 1);
    unsigned int source = texture;
    int sourceWidth = width;
    float variance = 0.f;
    for (int i = 0; i < Levels; ++i) {
        const auto& level = levels[i];
        glViewport(0, 0, level.width, level.height);
        glBindFramebuffer(GL_FRAMEBUFFER, level.scratchFramebuffer);
        glBindTexture(GL_TEXTURE_2D, source);
        glUniform2f(glGetUniformLocation(blurProgram, "direction"), 1.f/sourceWidth, 0);
        glUniform1f(glGetUniformLocation(blurProgram, "sigma"), 4.f);
        glUniform1i(glGetUniformLocation(blurProgram, "radius"), 12);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, level.framebuffer);
        glBindTexture(GL_TEXTURE_2D, level.scratch);
        glUniform2f(glGetUniformLocation(blurProgram, "direction"), 0, 1.f/level.height);
        glUniform1f(glGetUniformLocation(blurProgram, "sigma"), 2.f);
        glUniform1i(glGetUniformLocation(blurProgram, "radius"), 6);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        const float sigma = 2.f / level.width;
        variance += sigma*sigma;
        normalizedSigmas[i] = std::sqrt(variance);
        source = level.texture; sourceWidth = level.width;
    }
    // The near join keeps its local Gaussian. Farther out, use contributions
    // from many softened photo locations instead of four repeated edge strips.
    // Generate this small field only when accepted complete-photo pixels change.
    glBindFramebuffer(GL_FRAMEBUFFER, colorFieldFramebuffer);
    glViewport(0, 0, ColorFieldSize, ColorFieldSize);
    glUseProgram(colorFieldProgram);
    glBindTexture(GL_TEXTURE_2D, levels[ColorSampleLevel].texture);
    glUniform1i(glGetUniformLocation(colorFieldProgram, "image"), 0);
    glUniform4f(glGetUniformLocation(colorFieldProgram, "photoBounds"), photoBounds.x,
        photoBounds.y, photoBounds.z, photoBounds.w);
    glUniform2f(glGetUniformLocation(colorFieldProgram, "photoAspect"),
        frame.nativeExtent.x/padding, frame.nativeExtent.y/padding);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_NO_ERROR) return false;
    identity = nextIdentity; valid = true;
    return true;
}

void RawImageBackdropBlur::Bind(unsigned int program, int firstUnit, ImVec2 photoSize) const {
    std::array<float, Levels> sigmas{};
    for (int i = 0; i < Levels; ++i) {
        const std::string name = "blurred[" + std::to_string(i) + "]";
        glUniform1i(glGetUniformLocation(program, name.c_str()), firstUnit+i);
        glActiveTexture(GL_TEXTURE0+firstUnit+i);
        glBindTexture(GL_TEXTURE_2D, levels[i].texture);
        sigmas[i] = normalizedSigmas[i]*photoSize.x/(photoBounds.z-photoBounds.x);
    }
    glUniform1fv(glGetUniformLocation(program, "blurSigma[0]"), Levels, sigmas.data());
    glUniform4f(glGetUniformLocation(program, "blurPhotoBounds"), photoBounds.x,
        photoBounds.y, photoBounds.z, photoBounds.w);
    glUniform1i(glGetUniformLocation(program, "blurColorField"), 12);
    glActiveTexture(GL_TEXTURE0+12);
    glBindTexture(GL_TEXTURE_2D, colorField);
}

void RawImageBackdropBlur::Shutdown() {
    ReleaseImages();
    if (copyProgram) glDeleteProgram(copyProgram);
    if (blurProgram) glDeleteProgram(blurProgram);
    if (colorFieldProgram) glDeleteProgram(colorFieldProgram);
    if (vao) glDeleteVertexArrays(1, &vao);
    copyProgram = blurProgram = colorFieldProgram = vao = 0; failed = false;
}
}
