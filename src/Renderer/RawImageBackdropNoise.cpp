#include "RawImageBackdropNoise.h"
#include "RawImageBackdrop.h"
#include "RawImageBackdropShaders.h"
#include "RawImageBackdropNoiseShaders.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace Stack::Renderer {
namespace {
constexpr int BrightnessBins = 32;
bool CreatePair(int width, int height, bool repeat, unsigned int& first,
                unsigned int& second, unsigned int& framebuffer) {
    // Profiles contain variances, whose range can exceed that of amplitudes.
    const auto format = repeat ? GL_RGBA16F : GL_RGBA32F;
    first = GLHelpers::CreateTextureFromData(nullptr, width, height, format, GL_RGBA, GL_FLOAT, GL_LINEAR);
    second = GLHelpers::CreateTextureFromData(nullptr, width, height, format, GL_RGBA, GL_FLOAT, GL_LINEAR);
    if (!first || !second) return false;
    for (const auto image : {first, second}) {
        glBindTexture(GL_TEXTURE_2D, image);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    }
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, first, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, second, 0);
    const GLenum outputs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, outputs);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}
void DeletePair(unsigned int& first, unsigned int& second, unsigned int& framebuffer) {
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (first) glDeleteTextures(1, &first);
    if (second) glDeleteTextures(1, &second);
    first = second = framebuffer = 0;
}
std::size_t Identity(const RawImageBackdropFrame& frame) {
    std::size_t hash = frame.presentationFingerprint;
    const auto append = [&](auto value) {
        hash ^= std::hash<decltype(value)>{}(value) + std::size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
    };
    const auto point = [&](ImVec2 p) { append(p.x); append(p.y); };
    point(frame.nativeExtent);
    append(frame.patches.size());
    for (const auto& patch : frame.patches) {
        append(patch.texture); append(patch.previousTexture); append(patch.fadeAmount); append(patch.encodedSrgb);
        append(patch.imageBounds.x); append(patch.imageBounds.y);
        append(patch.imageBounds.z); append(patch.imageBounds.w);
        point(patch.uvMinimum); point(patch.uvMaximum);
        point(patch.previousOffset); point(patch.previousScale);
    }
    return hash;
}
}

void RawImageBackdropNoise::ReleaseProfiles() {
    DeletePair(fineProfile, coarseProfile, profileFramebuffer);
    DeletePair(filteredFineProfile, filteredCoarseProfile, filteredFramebuffer);
    DeletePair(fineBrightness, coarseBrightness, brightnessFramebuffer);
    width = height = 0;
    profileIdentity = 0;
    profileValid = false;
}

bool RawImageBackdropNoise::Ensure(int w, int h) {
    if (failed) return false;
    if (!analysisProgram) {
        analysisProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropNoiseShaders::Analysis);
        seedProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropNoiseShaders::Seed);
        filterProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropNoiseShaders::Filter);
        brightnessProgram = GLHelpers::CreateShaderProgram(RawImageBackdropShaders::Vertex, RawImageBackdropNoiseShaders::BrightnessProfile);
        if (!analysisProgram || !seedProgram || !filterProgram || !brightnessProgram ||
            !CreatePair(512, 512, true, fineGrain, coarseGrain, grainFramebuffer)) {
            Shutdown(); failed = true; return false;
        }
        glViewport(0, 0, 512, 512);
        glUseProgram(seedProgram);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // Average the same deterministic field when grain becomes subpixel.
        // These atlases are generated once; the photo blur remains Gaussian.
        for (const auto image : {fineGrain, coarseGrain}) {
            glBindTexture(GL_TEXTURE_2D, image);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
        if (glGetError() != GL_NO_ERROR) {
            Shutdown(); failed = true; return false;
        }
    }
    if (profileFramebuffer && width == w && height == h) return true;
    ReleaseProfiles();
    if (!CreatePair(w, h, false, fineProfile, coarseProfile, profileFramebuffer) ||
        !CreatePair(w, h, false, filteredFineProfile, filteredCoarseProfile, filteredFramebuffer) ||
        !CreatePair(BrightnessBins, 1, false, fineBrightness, coarseBrightness, brightnessFramebuffer)) {
        Shutdown(); failed = true; return false;
    }
    width = w; height = h;
    return true;
}

bool RawImageBackdropNoise::Prepare(const RawImageBackdropFrame& frame) {
    if (!std::isfinite(frame.nativeExtent.x) || !std::isfinite(frame.nativeExtent.y) ||
        frame.nativeExtent.x <= 0.f || frame.nativeExtent.y <= 0.f) return false;
    // Fixed photo-space measurements avoid changing sample phase or strength
    // while navigating. Limit the entire map to 64 samples on its longer axis.
    const float scale = std::min(1.f, 64.f / std::max(frame.nativeExtent.x, frame.nativeExtent.y));
    const int w = std::clamp(int(std::ceil(frame.nativeExtent.x * scale)), 1, 64);
    const int h = std::clamp(int(std::ceil(frame.nativeExtent.y * scale)), 1, 64);
    if (!Ensure(w, h)) return false;
    const auto identity = Identity(frame);
    // A zero identity owner deliberately disables reuse for anonymous callers.
    if (frame.presentationFingerprint && profileValid && profileIdentity == identity) return true;
    glBindFramebuffer(GL_FRAMEBUFFER, profileFramebuffer);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(analysisProgram);
    glUniform1i(glGetUniformLocation(analysisProgram, "image"), 0);
    glUniform1i(glGetUniformLocation(analysisProgram, "previous"), 1);
    glUniform2f(glGetUniformLocation(analysisProgram, "nativeExtent"), frame.nativeExtent.x, frame.nativeExtent.y);
    for (const auto& patch : frame.patches) {
        if (!patch.texture || patch.imageBounds.z <= patch.imageBounds.x || patch.imageBounds.w <= patch.imageBounds.y) continue;
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, patch.previousTexture ? patch.previousTexture : patch.texture);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, patch.texture);
        glUniform4f(glGetUniformLocation(analysisProgram, "rect"),
            patch.imageBounds.x, patch.imageBounds.y, patch.imageBounds.z, patch.imageBounds.w);
        glUniform4f(glGetUniformLocation(analysisProgram, "textureRect"),
            patch.uvMinimum.x, patch.uvMaximum.y, patch.uvMaximum.x, patch.uvMinimum.y);
        glUniform2f(glGetUniformLocation(analysisProgram, "previousOffset"), patch.previousOffset.x, patch.previousOffset.y);
        glUniform2f(glGetUniformLocation(analysisProgram, "previousScale"), patch.previousScale.x, patch.previousScale.y);
        glUniform1f(glGetUniformLocation(analysisProgram, "fadeAmount"), patch.previousTexture ? patch.fadeAmount : 1.f);
        glUniform1i(glGetUniformLocation(analysisProgram, "encodedSrgb"), patch.encodedSrgb ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // Reject isolated structure estimates before spatial smoothing. All three
    // passes operate on the small cached map, never on the full-screen grain.
    glUseProgram(filterProgram);
    glUniform1i(glGetUniformLocation(filterProgram, "fineInput"), 0);
    glUniform1i(glGetUniformLocation(filterProgram, "coarseInput"), 1);
    const auto filter = [&](unsigned int target, unsigned int fine, unsigned int coarse,
                            bool median, float x, float y) {
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, fine);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, coarse);
        glUniform1i(glGetUniformLocation(filterProgram, "rejectOutliers"), median ? 1 : 0);
        glUniform2f(glGetUniformLocation(filterProgram, "direction"), x, y);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    filter(filteredFramebuffer, fineProfile, coarseProfile, true, 0, 0);
    filter(profileFramebuffer, filteredFineProfile, filteredCoarseProfile, false, 1.f / width, 0);
    filter(filteredFramebuffer, fineProfile, coarseProfile, false, 0, 1.f / height);
    // Learn display-noise variance versus measured display brightness. This
    // small GPU-only curve follows edited pixels and needs no source readback.
    glBindFramebuffer(GL_FRAMEBUFFER, brightnessFramebuffer);
    glViewport(0, 0, BrightnessBins, 1);
    glUseProgram(brightnessProgram);
    glUniform1i(glGetUniformLocation(brightnessProgram, "fineInput"), 0);
    glUniform1i(glGetUniformLocation(brightnessProgram, "coarseInput"), 1);
    glUniform1i(glGetUniformLocation(brightnessProgram, "bins"), BrightnessBins);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, filteredFineProfile);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, filteredCoarseProfile);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // Broadening the variance neighborhood outside the photo prevents one
    // edge measurement from becoming an infinitely long horizontal/vertical band.
    for (const auto image : {filteredFineProfile, filteredCoarseProfile}) {
        glBindTexture(GL_TEXTURE_2D, image);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    if (glGetError() != GL_NO_ERROR) {
        Shutdown(); failed = true; return false;
    }
    profileIdentity = identity;
    profileValid = true;
    return true;
}

void RawImageBackdropNoise::Bind(unsigned int program, int firstUnit, int brightnessUnit) const {
    const unsigned int images[] = {filteredFineProfile, filteredCoarseProfile, fineGrain, coarseGrain};
    const char* names[] = {"noiseFineProfile", "noiseCoarseProfile", "noiseFineGrain", "noiseCoarseGrain"};
    for (int i = 0; i < 4; ++i) {
        glUniform1i(glGetUniformLocation(program, names[i]), firstUnit+i);
        glActiveTexture(GL_TEXTURE0+firstUnit+i);
        glBindTexture(GL_TEXTURE_2D, images[i]);
    }
    glUniform1i(glGetUniformLocation(program, "noiseFineBrightness"), brightnessUnit);
    glActiveTexture(GL_TEXTURE0+brightnessUnit);
    glBindTexture(GL_TEXTURE_2D, fineBrightness);
    glUniform1i(glGetUniformLocation(program, "noiseCoarseBrightness"), brightnessUnit+1);
    glActiveTexture(GL_TEXTURE0+brightnessUnit+1);
    glBindTexture(GL_TEXTURE_2D, coarseBrightness);
}

void RawImageBackdropNoise::Shutdown() {
    ReleaseProfiles();
    DeletePair(fineGrain, coarseGrain, grainFramebuffer);
    if (analysisProgram) glDeleteProgram(analysisProgram);
    if (seedProgram) glDeleteProgram(seedProgram);
    if (filterProgram) glDeleteProgram(filterProgram);
    if (brightnessProgram) glDeleteProgram(brightnessProgram);
    analysisProgram = seedProgram = filterProgram = brightnessProgram = 0;
    failed = false;
}
}
