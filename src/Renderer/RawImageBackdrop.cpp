#include "RawImageBackdrop.h"
#include "RawImageBackdropShaders.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <algorithm>
#include <cmath>

namespace Stack::Renderer {
namespace {
// The compositor saves units 0 and 1. Gaussian levels, the color field and
// noise profiles use additional units. Restore all bindings before ImGui.
struct TextureUnits {
    std::array<GLint, 15> textures{}, samplers{};
    GLint active = 0;
    TextureUnits() {
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        for (int i = 0; i < 15; ++i) {
            glActiveTexture(GL_TEXTURE0+i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
            glGetIntegerv(GL_SAMPLER_BINDING, &samplers[i]);
            glBindSampler(i, 0);
        }
        glActiveTexture(GL_TEXTURE0);
    }
    ~TextureUnits() {
        for (int i = 0; i < 15; ++i) {
            glActiveTexture(GL_TEXTURE0+i);
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glBindSampler(i, samplers[i]);
        }
        glActiveTexture(active);
    }
};
unsigned int CreateImage(int width, int height) {
    return GLHelpers::CreateTextureFromData(nullptr, width, height,
        GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
}
}

void RawImageBackdrop::ReleaseImages() {
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (texture) glDeleteTextures(1, &texture);
    framebuffer = texture = 0;
    width = height = 0;
}

bool RawImageBackdrop::Ensure(int w, int h) {
    if (failed) return false;
    if (!copyProgram) {
        copyProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropShaders::Copy);
        presentProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropShaders::Present.c_str());
        glGenVertexArrays(1, &vao);
        if (!copyProgram || !presentProgram || !vao) { failed = true; return false; }
    }
    if (texture && framebuffer && width == w && height == h) return true;
    ReleaseImages();
    texture = CreateImage(w, h);
    framebuffer = texture ? GLHelpers::CreateFBO(texture) : 0;
    bool complete = framebuffer != 0;
    if (!complete) {
        ReleaseImages();
        failed = true; // Keep the ordinary viewport fallback on allocation failure.
        return false;
    }
    width = w; height = h;
    return true;
}

bool RawImageBackdrop::Render(const RawImageBackdropFrame& frame, const ImDrawData& data, const ImVec4& clear) {
    if (frame.patches.empty() || data.DisplaySize.x <= 0 || data.DisplaySize.y <= 0) return false;
    const int w = std::max(1, int(data.DisplaySize.x * data.FramebufferScale.x));
    const int h = std::max(1, int(data.DisplaySize.y * data.FramebufferScale.y));
    // Display-only resources. Never submit RAW work or read image pixels back.
    GLint maximumTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    const float scale = std::min(1.f, float(maximumTextureSize) / std::max(w, h));
    GLState::FramebufferState destination{true};
    TextureUnits bindings;
    if (!Ensure(std::max(1, int(w * scale)), std::max(1, int(h * scale)))) {
        destination.Restore(true);
        return false;
    }
    const auto rectangle = [&](unsigned int program, const char* name, ImVec2 min, ImVec2 max) {
        glUniform4f(glGetUniformLocation(program, name),
            (min.x - data.DisplayPos.x) / data.DisplaySize.x,
            1.f - (max.y - data.DisplayPos.y) / data.DisplaySize.y,
            (max.x - data.DisplayPos.x) / data.DisplaySize.x,
            1.f - (min.y - data.DisplayPos.y) / data.DisplaySize.y);
    };
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glClearColor(clear.x, clear.y, clear.z, 1.f); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(copyProgram); glBindVertexArray(vao);
    glUniform1i(glGetUniformLocation(copyProgram, "image"), 0);
    glUniform1i(glGetUniformLocation(copyProgram, "previous"), 1);
    glUniform1i(glGetUniformLocation(copyProgram, "visibleColorOnly"), 0);
    for (const auto& patch : frame.patches) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, patch.previousTexture ? patch.previousTexture : patch.texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, patch.texture);
        rectangle(copyProgram, "rect", patch.minimum, patch.maximum);
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

    const bool controlsOverlapPhoto = frame.controlsAmount > .001f &&
        frame.controlsMinimum.x < frame.imageMaximum.x && frame.controlsMaximum.x > frame.imageMinimum.x &&
        frame.controlsMinimum.y < frame.imageMaximum.y && frame.controlsMaximum.y > frame.imageMinimum.y;
    const bool needsBlur = frame.extendImage || controlsOverlapPhoto;
    if (needsBlur && !blur.Prepare(frame)) { destination.Restore(true); return false; }
    const bool noiseReady = needsBlur && noise.Prepare(frame);
    destination.Restore(true);
    glViewport(0, 0, w, h);
    glUseProgram(presentProgram);
    glBindVertexArray(vao);
    glUniform1i(glGetUniformLocation(presentProgram, "image"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (needsBlur) blur.Bind(presentProgram, 1, ImVec2(frame.imageMaximum.x-frame.imageMinimum.x,
        frame.imageMaximum.y-frame.imageMinimum.y));
    if (noiseReady) noise.Bind(presentProgram, 8, 13);
    glUniform1i(glGetUniformLocation(presentProgram, "noiseEnabled"), noiseReady ? 1 : 0);
    glUniform2f(glGetUniformLocation(presentProgram, "noisePhysicalSize"), float(w), float(h));
    glUniform2f(glGetUniformLocation(presentProgram, "noiseNativeExtent"), frame.nativeExtent.x, frame.nativeExtent.y);
    rectangle(presentProgram, "photo", frame.imageMinimum, frame.imageMaximum);
    rectangle(presentProgram, "controls", frame.controlsMinimum, frame.controlsMaximum);
    glUniform2f(glGetUniformLocation(presentProgram, "screenSize"), data.DisplaySize.x, data.DisplaySize.y);
    glUniform3f(glGetUniformLocation(presentProgram, "background"), clear.x, clear.y, clear.z);
    glUniform1i(glGetUniformLocation(presentProgram, "extendImage"), frame.extendImage ? 1 : 0);
    glUniform1f(glGetUniformLocation(presentProgram, "extensionOpacity"), frame.extensionOpacity);
    glUniform1f(glGetUniformLocation(presentProgram, "edgeOverlap"), frame.edgeOverlap);
    glUniform1f(glGetUniformLocation(presentProgram, "controlsAmount"), std::clamp(frame.controlsAmount, 0.f, 1.f));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return true;
}

RawPhotoInteriorClip::RawPhotoInteriorClip(const RawImageBackdropFrame* value) {
    if (!value || !value->sharpDrawList || value->extensionOpacity <= 0.f || value->edgeOverlap <= 0.f) return;
    auto& commands = value->sharpDrawList->CmdBuffer;
    if (value->sharpCommandBegin < 0 || value->sharpCommandEnd > commands.Size ||
        value->sharpCommandEnd <= value->sharpCommandBegin) return;
    clips.reserve(value->sharpCommandEnd-value->sharpCommandBegin);
    for (int i = value->sharpCommandBegin; i < value->sharpCommandEnd; ++i)
        clips.push_back(commands[i].ClipRect);
    frame = value;
    for (int i = frame->sharpCommandBegin; i < frame->sharpCommandEnd; ++i) {
        auto& clip = commands[i].ClipRect;
        clip.x = std::max(clip.x, frame->imageMinimum.x+frame->edgeOverlap);
        clip.y = std::max(clip.y, frame->imageMinimum.y+frame->edgeOverlap);
        clip.z = std::min(clip.z, frame->imageMaximum.x-frame->edgeOverlap);
        clip.w = std::min(clip.w, frame->imageMaximum.y-frame->edgeOverlap);
    }
}

RawPhotoInteriorClip::~RawPhotoInteriorClip() {
    if (!frame) return;
    for (int i = frame->sharpCommandBegin; i < frame->sharpCommandEnd; ++i)
        frame->sharpDrawList->CmdBuffer[i].ClipRect = clips[i-frame->sharpCommandBegin];
}

void RawImageBackdrop::Shutdown() {
    blur.Shutdown();
    noise.Shutdown();
    ReleaseImages();
    if (copyProgram) glDeleteProgram(copyProgram);
    if (presentProgram) glDeleteProgram(presentProgram);
    if (vao) glDeleteVertexArrays(1, &vao);
    copyProgram = presentProgram = vao = 0;
    failed = false;
}
}
