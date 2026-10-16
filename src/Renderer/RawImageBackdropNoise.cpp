#include "RawImageBackdropNoise.h"
#include "RawImageBackdropUniforms.h"
#include "RawImageBackdrop.h"
#include "RawImageBackdropNoiseShaders.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace Stack::Renderer {
namespace {
constexpr int Bands = 3, BrightnessBins = 32, GrainSize = 512;

// Save only the image units changed here; the backdrop guards texture units.
struct ImageBindings {
    struct Binding { GLint name, level, layered, layer, access, format; };
    std::array<Binding, 2> saved{};
    ImageBindings() {
        for (GLuint i = 0; i < saved.size(); ++i) {
            auto& b = saved[i];
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME, i, &b.name);
            glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, i, &b.level);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, i, &b.layered);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, i, &b.layer);
            glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, i, &b.access);
            glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, i, &b.format);
        }
    }
    ~ImageBindings() {
        for (GLuint i = 0; i < saved.size(); ++i) {
            const auto& b = saved[i];
            glBindImageTexture(i, b.name, b.level, b.layered ? GL_TRUE : GL_FALSE,
                b.layer, b.access, b.format);
        }
    }
};
unsigned int CreateArray(int width, int height, bool mipmaps, bool repeat, GLenum format) {
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    const int levels = mipmaps ? 1 + int(std::floor(std::log2(std::max(width, height)))) : 1;
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, format, width, height, Bands);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    // Immutable storage limits sampling to the allocated mip levels.
    if (glGetError() != GL_NO_ERROR) { glDeleteTextures(1, &texture); return 0; }
    return texture;
}
void Delete(unsigned int& texture) {
    if (texture) glDeleteTextures(1, &texture);
    texture = 0;
}
void BindInputs(unsigned int variance, unsigned int covariance) {
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, variance);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D_ARRAY, covariance);
}
void BindOutputs(unsigned int variance, unsigned int covariance) {
    glBindImageTexture(0, variance, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(1, covariance, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
}
void Dispatch(int width, int height, int bands = Bands) {
    glDispatchCompute((width + 7) / 8, (height + 7) / 8, bands);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
        GL_TEXTURE_UPDATE_BARRIER_BIT);
}
std::size_t Identity(const RawImageBackdropFrame& frame) {
    std::size_t hash = frame.presentationFingerprint;
    const auto append = [&](auto value) {
        hash ^= std::hash<decltype(value)>{}(value) + std::size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
    };
    const auto point = [&](ImVec2 p) { append(p.x); append(p.y); };
    point(frame.nativeExtent); append(frame.SurroundPatches().size());
    for (const auto& patch : frame.SurroundPatches()) {
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
    Delete(profileVariance); Delete(profileCovariance);
    Delete(filteredVariance); Delete(filteredCovariance);
    Delete(brightnessVariance); Delete(brightnessCovariance);
    width = height = 0; profileIdentity = 0; profileValid = false;
}
bool RawImageBackdropNoise::Ensure(int w, int h) {
    if (failed) return false;
    glActiveTexture(GL_TEXTURE0);
    if (!analysisProgram) {
        analysisProgram = GLHelpers::CreateComputeProgram(RawImageBackdropNoiseShaders::Analysis.c_str());
        seedProgram = GLHelpers::CreateComputeProgram(RawImageBackdropNoiseShaders::Seed);
        filterProgram = GLHelpers::CreateComputeProgram(RawImageBackdropNoiseShaders::Filter);
        brightnessProgram = GLHelpers::CreateComputeProgram(RawImageBackdropNoiseShaders::BrightnessProfile);
        if (!analysisProgram || !seedProgram || !filterProgram || !brightnessProgram ||
            !(grain = CreateArray(GrainSize, GrainSize, true, true, GL_RGBA16F))) {
            Shutdown(); failed = true; return false;
        }
        glUseProgram(seedProgram);
        glBindImageTexture(0, grain, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        Dispatch(GrainSize, GrainSize);
        glBindTexture(GL_TEXTURE_2D_ARRAY, grain);
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        if (glGetError() != GL_NO_ERROR) { Shutdown(); failed = true; return false; }
    }
    if (profileVariance && width == w && height == h) return true;
    ReleaseProfiles();
    profileVariance = CreateArray(w, h, false, false, GL_RGBA32F);
    profileCovariance = CreateArray(w, h, false, false, GL_RGBA32F);
    filteredVariance = CreateArray(w, h, true, false, GL_RGBA32F);
    filteredCovariance = CreateArray(w, h, true, false, GL_RGBA32F);
    brightnessVariance = CreateArray(BrightnessBins, 1, false, false, GL_RGBA32F);
    brightnessCovariance = CreateArray(BrightnessBins, 1, false, false, GL_RGBA32F);
    if (!profileVariance || !profileCovariance || !filteredVariance || !filteredCovariance ||
        !brightnessVariance || !brightnessCovariance) { Shutdown(); failed = true; return false; }
    width = w; height = h;
    return true;
}
bool RawImageBackdropNoise::Prepare(const RawImageBackdropFrame& frame) {
    if (frame.patches.empty() || !std::isfinite(frame.nativeExtent.x) || !std::isfinite(frame.nativeExtent.y) ||
        frame.nativeExtent.x <= 0.f || frame.nativeExtent.y <= 0.f) return false;
    const auto identity = Identity(frame);
    // The cached path needs no allocation, dispatch or image-binding queries.
    if (frame.presentationFingerprint && profileValid && profileIdentity == identity) return true;
    const float scale = std::min(1.f, 64.f / std::max(frame.nativeExtent.x, frame.nativeExtent.y));
    const int w = std::clamp(int(std::ceil(frame.nativeExtent.x * scale)), 1, 64);
    const int h = std::clamp(int(std::ceil(frame.nativeExtent.y * scale)), 1, 64);
    ImageBindings savedImages;
    if (!Ensure(w, h)) return false;
    profileValid = false;
    glUseProgram(analysisProgram);
    glUniform1i(glGetUniformLocation(analysisProgram, "image"), 0);
    glUniform1i(glGetUniformLocation(analysisProgram, "previous"), 1);
    glUniform2f(glGetUniformLocation(analysisProgram, "nativeExtent"), frame.nativeExtent.x, frame.nativeExtent.y);
    BindOutputs(profileVariance, profileCovariance);
    glUniform1i(glGetUniformLocation(analysisProgram, "clearOnly"), 1);
    Dispatch(width, height, 1);
    glUniform1i(glGetUniformLocation(analysisProgram, "clearOnly"), 0);
    for (const auto& patch : frame.SurroundPatches()) {
        if (!patch.texture || patch.imageBounds.z <= patch.imageBounds.x || patch.imageBounds.w <= patch.imageBounds.y) continue;
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, patch.previousTexture ? patch.previousTexture : patch.texture);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, patch.texture);
        glUniform4f(glGetUniformLocation(analysisProgram, "rect"), patch.imageBounds.x, patch.imageBounds.y,
            patch.imageBounds.z, patch.imageBounds.w);
        glUniform4f(glGetUniformLocation(analysisProgram, "textureRect"), patch.uvMinimum.x, patch.uvMaximum.y,
            patch.uvMaximum.x, patch.uvMinimum.y);
        glUniform2f(glGetUniformLocation(analysisProgram, "previousOffset"), patch.previousOffset.x, patch.previousOffset.y);
        glUniform2f(glGetUniformLocation(analysisProgram, "previousScale"), patch.previousScale.x, patch.previousScale.y);
        glUniform1f(glGetUniformLocation(analysisProgram, "fadeAmount"), patch.previousTexture ? patch.fadeAmount : 1.f);
        glUniform1i(glGetUniformLocation(analysisProgram, "encodedSrgb"), patch.encodedSrgb ? 1 : 0);
        Dispatch(width, height, 1);
    }
    glUseProgram(filterProgram);
    glUniform1i(glGetUniformLocation(filterProgram, "varianceInput"), 0);
    glUniform1i(glGetUniformLocation(filterProgram, "covarianceInput"), 1);
    const auto filter = [&](unsigned int targetVariance, unsigned int targetCovariance,
        unsigned int sourceVariance, unsigned int sourceCovariance, bool median, int x, int y) {
        BindOutputs(targetVariance, targetCovariance); BindInputs(sourceVariance, sourceCovariance);
        glUniform1i(glGetUniformLocation(filterProgram, "rejectOutliers"), median ? 1 : 0);
        glUniform2i(glGetUniformLocation(filterProgram, "direction"), x, y);
        Dispatch(width, height);
    };
    filter(filteredVariance, filteredCovariance, profileVariance, profileCovariance, true, 0, 0);
    filter(profileVariance, profileCovariance, filteredVariance, filteredCovariance, false, 1, 0);
    filter(filteredVariance, filteredCovariance, profileVariance, profileCovariance, false, 0, 1);
    glUseProgram(brightnessProgram);
    glUniform1i(glGetUniformLocation(brightnessProgram, "varianceInput"), 0);
    glUniform1i(glGetUniformLocation(brightnessProgram, "covarianceInput"), 1);
    BindOutputs(brightnessVariance, brightnessCovariance); BindInputs(filteredVariance, filteredCovariance);
    Dispatch(BrightnessBins, 1);
    for (const auto texture : {filteredVariance, filteredCovariance}) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }
    if (glGetError() != GL_NO_ERROR) { Shutdown(); failed = true; return false; }
    profileIdentity = identity; profileValid = true;
    return true;
}
void RawImageBackdropNoise::Bind(unsigned int /*presentProgram*/, int firstUnit, int brightnessUnit) const {
    const std::array<unsigned int, 5> images{filteredVariance, filteredCovariance, grain,
        brightnessVariance, brightnessCovariance};
    const int units[] = {firstUnit, firstUnit + 1, firstUnit + 2, brightnessUnit, brightnessUnit + 1};
    for (std::size_t i = 0; i < images.size(); ++i) {
        glUniform1i(BackdropUniforms::Present::NoiseVariance+int(i), units[i]);
        glActiveTexture(GL_TEXTURE0 + units[i]); glBindTexture(GL_TEXTURE_2D_ARRAY, images[i]);
    }
}
void RawImageBackdropNoise::Shutdown() {
    ReleaseProfiles(); Delete(grain);
    if (analysisProgram) glDeleteProgram(analysisProgram);
    if (seedProgram) glDeleteProgram(seedProgram);
    if (filterProgram) glDeleteProgram(filterProgram);
    if (brightnessProgram) glDeleteProgram(brightnessProgram);
    analysisProgram = seedProgram = filterProgram = brightnessProgram = 0;
    failed = false;
}
}
