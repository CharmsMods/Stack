#include "RawImageBackdrop.h"
#include "RawImageBackdropShaders.h"
#include "Renderer/GLHelpers.h"
#include "App/UiPerformanceTrace.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace Stack::Renderer {
namespace {
// These units belong to the compositor pass. Clear them before ImGui rather
// than querying and restoring arbitrary shared objects on every UI frame.
struct TextureUnits {
    static bool UsesArray(int unit) {
        return unit == 0 || unit == 1 || unit == 8 || unit == 9 ||
            unit == 10 || unit == 13 || unit == 14;
    }
    TextureUnits() {
        for (int i = 0; i < 15; ++i) {
            glBindSampler(i, 0);
        }
        glActiveTexture(GL_TEXTURE0);
    }
    ~TextureUnits() {
        for (int i = 0; i < 15; ++i) {
            glActiveTexture(GL_TEXTURE0+i);
            glBindTexture(GL_TEXTURE_2D, 0);
            if (UsesArray(i)) glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        }
        glActiveTexture(GL_TEXTURE0);
    }
};
unsigned int CreateImage(int width, int height) {
    return GLHelpers::CreateTextureFromData(nullptr, width, height,
        GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
}

std::size_t CopyIdentity(const RawImageBackdropFrame& frame,
    const ImDrawData& data, const ImVec4& clear) {
    std::size_t hash = frame.presentationFingerprint;
    const auto append = [&](auto value) {
        hash ^= std::hash<decltype(value)>{}(value) + std::size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
    };
    const auto point = [&](ImVec2 p) { append(p.x); append(p.y); };
    point(data.DisplayPos); point(data.DisplaySize); point(data.FramebufferScale);
    append(clear.x); append(clear.y); append(clear.z);
    append(frame.patches.size());
    for (const auto& patch : frame.patches) {
        append(patch.texture); append(patch.previousTexture);
        append(patch.fadeAmount); append(patch.encodedSrgb);
        point(patch.minimum); point(patch.maximum);
        point(patch.uvMinimum); point(patch.uvMaximum);
        point(patch.previousOffset); point(patch.previousScale);
        append(patch.extendEdges.x); append(patch.extendEdges.y);
        append(patch.extendEdges.z); append(patch.extendEdges.w);
    }
    return hash;
}
}

void RawImageBackdrop::ReleaseImages() {
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (texture) glDeleteTextures(1, &texture);
    framebuffer = texture = 0;
    width = height = 0;
    copiedIdentity = 0;
    copiedValid = false;
}

bool RawImageBackdrop::Ensure(int w, int h) {
    if (failed) return false;
    if (!copyProgram) {
        copyProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropShaders::Copy.c_str());
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

bool RawImageBackdrop::Render(const RawImageBackdropFrame& frame, const ImDrawData& data, const ImVec4& clear,
    unsigned int destinationFramebuffer) {
    Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::Backdrop);
    if (frame.patches.empty() || data.DisplaySize.x <= 0 || data.DisplaySize.y <= 0) return false;
    const bool photoCoversWindow = frame.imageMinimum.x <= data.DisplayPos.x &&
        frame.imageMinimum.y <= data.DisplayPos.y &&
        frame.imageMaximum.x >= data.DisplayPos.x + data.DisplaySize.x &&
        frame.imageMaximum.y >= data.DisplayPos.y + data.DisplaySize.y;
    // Even at deep zoom, composition fills margins outside the editing pane's
    // clipped sharp draw. Keep that copy; only its invisible blur work can stop.
    const int w = std::max(1, int(data.DisplaySize.x * data.FramebufferScale.x));
    const int h = std::max(1, int(data.DisplaySize.y * data.FramebufferScale.y));
    // Display-only resources. Never submit RAW work or read image pixels back.
    if (!maximumTextureSize) glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    const float scale = std::min(1.f, float(maximumTextureSize) / std::max(w, h));
    const auto restoreDestination = [&] {
        glBindFramebuffer(GL_FRAMEBUFFER, destinationFramebuffer);
        glViewport(0, 0, w, h);
    };
    TextureUnits bindings;
    if (!Ensure(std::max(1, int(w * scale)), std::max(1, int(h * scale)))) {
        restoreDestination();
        return false;
    }
    const auto rectangle = [&](int location, ImVec2 min, ImVec2 max) {
        glUniform4f(location,
            (min.x - data.DisplayPos.x) / data.DisplaySize.x,
            1.f - (max.y - data.DisplayPos.y) / data.DisplaySize.y,
            (max.x - data.DisplayPos.x) / data.DisplaySize.x,
            1.f - (min.y - data.DisplayPos.y) / data.DisplaySize.y);
    };
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(vao);
    const auto copyIdentity = CopyIdentity(frame, data, clear);
    // Tools and graph nodes can move while the accepted photo stays put. Keep
    // its exact existing screen-space copy until content, fade or placement
    // changes. Anonymous texture owners cannot promise unchanged pixels.
    if (!frame.presentationFingerprint || !copiedValid || copiedIdentity != copyIdentity) {
        copiedValid = false;
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, width, height);
        glClearColor(clear.x, clear.y, clear.z, 1.f); glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(copyProgram);
        glUniform1i(BackdropUniforms::Copy::Image, 0);
        glUniform1i(BackdropUniforms::Copy::Previous, 1);
        glUniform1i(BackdropUniforms::Copy::VisibleColorOnly, 0);
        for (const auto& patch : frame.patches) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, patch.previousTexture ? patch.previousTexture : patch.texture);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, patch.texture);
            rectangle(BackdropUniforms::Copy::Rect, patch.minimum, patch.maximum);
            glUniform4f(BackdropUniforms::Copy::TextureRect,
                patch.uvMinimum.x, patch.uvMaximum.y, patch.uvMaximum.x, patch.uvMinimum.y);
            glUniform4f(BackdropUniforms::Copy::ExtendEdges, patch.extendEdges.x,
                patch.extendEdges.y, patch.extendEdges.z, patch.extendEdges.w);
            glUniform2f(BackdropUniforms::Copy::PreviousOffset, patch.previousOffset.x, patch.previousOffset.y);
            glUniform2f(BackdropUniforms::Copy::PreviousScale, patch.previousScale.x, patch.previousScale.y);
            glUniform1f(BackdropUniforms::Copy::FadeAmount, patch.previousTexture ? patch.fadeAmount : 1.f);
            glUniform1i(BackdropUniforms::Copy::EncodedSrgb, patch.encodedSrgb ? 1 : 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        if (glGetError() == GL_NO_ERROR) {
            copiedIdentity = copyIdentity;
            copiedValid = true;
        }
    }

    const bool controlsOverlapPhoto = frame.controlsAmount > .001f &&
        frame.controlsMinimum.x < frame.imageMaximum.x && frame.controlsMaximum.x > frame.imageMinimum.x &&
        frame.controlsMinimum.y < frame.imageMaximum.y && frame.controlsMaximum.y > frame.imageMinimum.y;
    const bool needsBlur = (frame.extendImage && !photoCoversWindow) || controlsOverlapPhoto;
    if (needsBlur) {
        Stack::UiPerformance::Span blurTiming(Stack::UiPerformance::Stage::BackdropBlur);
        if (!blur.Prepare(frame)) { restoreDestination(); return false; }
    }
    bool noiseReady = false;
    if (needsBlur) {
        Stack::UiPerformance::Span noiseTiming(Stack::UiPerformance::Stage::BackdropNoise);
        noiseReady = noise.Prepare(frame);
    }
    restoreDestination();
    glViewport(0, 0, w, h);
    glUseProgram(presentProgram);
    glBindVertexArray(vao);
    glUniform1i(BackdropUniforms::Present::Image, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (needsBlur) blur.Bind(presentProgram, 1, ImVec2(frame.imageMaximum.x-frame.imageMinimum.x,
        frame.imageMaximum.y-frame.imageMinimum.y));
    // Sampler types must use distinct units even when noise is disabled.
    noise.Bind(presentProgram, 8, 13);
    glUniform1i(BackdropUniforms::Present::NoiseEnabled, noiseReady ? 1 : 0);
    glUniform2f(BackdropUniforms::Present::NoisePhysicalSize, float(w), float(h));
    glUniform2f(BackdropUniforms::Present::NoiseNativeExtent, frame.nativeExtent.x, frame.nativeExtent.y);
    rectangle(BackdropUniforms::Present::Photo, frame.imageMinimum, frame.imageMaximum);
    rectangle(BackdropUniforms::Present::Controls, frame.controlsMinimum, frame.controlsMaximum);
    glUniform2f(BackdropUniforms::Present::ScreenSize, data.DisplaySize.x, data.DisplaySize.y);
    glUniform3f(BackdropUniforms::Present::Background, clear.x, clear.y, clear.z);
    glUniform1i(BackdropUniforms::Present::ExtendImage, frame.extendImage ? 1 : 0);
    glUniform1f(BackdropUniforms::Present::ExtensionOpacity, frame.extensionOpacity);
    glUniform1f(BackdropUniforms::Present::EdgeOverlap, frame.edgeOverlap);
    glUniform1f(BackdropUniforms::Present::ControlsAmount, std::clamp(frame.controlsAmount, 0.f, 1.f));
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
    maximumTextureSize = 0;
}
}
