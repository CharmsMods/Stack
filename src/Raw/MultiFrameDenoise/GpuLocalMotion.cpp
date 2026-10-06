#include "Raw/MultiFrameDenoise/GpuLocalMotion.h"

#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "Renderer/ScopedComputeBindings.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <vector>

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_R8UI
#define GL_R8UI 0x8232
#endif
#ifndef GL_MAX_ARRAY_TEXTURE_LAYERS
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#endif
#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif

namespace Raw::Mfd {
namespace {

constexpr std::uint32_t kLayerCount = 8u;
constexpr std::uint32_t kWorkgroupSize = 64u;
constexpr std::uint32_t kMaximumDispatchGroups = 65535u;

struct alignas(16) PackedCandidate {
    std::array<float, 4> centerResidual {};
};

static_assert(sizeof(PackedCandidate) == 16u);

const char* kDiscreteSearchShader = R"GLSL(
#version 430 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct Candidate {
    vec4 centerResidual;
};

layout(std430, binding = 0) readonly buffer CandidateBuffer {
    Candidate candidates[];
};
layout(std430, binding = 1) writeonly buffer ScoreBuffer {
    vec4 scores[];
};

layout(r32f, binding = 0) uniform readonly image2DArray uSignal;
layout(r32f, binding = 1) uniform readonly image2DArray uVariance;
layout(r8ui, binding = 2) uniform readonly uimage2DArray uValid;

uniform int uCandidateOffset;
uniform int uCandidateCount;
uniform int uReferenceBase;
uniform int uSourceBase;
uniform int uPatch;
uniform float uLevelScale;
uniform float uExposureScale;
uniform float uNumericalFloor;
uniform float uHuberDelta;
uniform float uCappedResidualSquared;
uniform float uKeysParameter;
uniform vec4 uLinear;
uniform vec4 uCenterTranslation;

uniform ivec2 uLayerExtent0;
uniform ivec2 uLayerExtent1;
uniform ivec2 uLayerExtent2;
uniform ivec2 uLayerExtent3;
uniform ivec2 uLayerExtent4;
uniform ivec2 uLayerExtent5;
uniform ivec2 uLayerExtent6;
uniform ivec2 uLayerExtent7;
uniform ivec2 uReferenceOffset0;
uniform ivec2 uReferenceOffset1;
uniform ivec2 uReferenceOffset2;
uniform ivec2 uReferenceOffset3;
uniform ivec2 uSourceOffset0;
uniform ivec2 uSourceOffset1;
uniform ivec2 uSourceOffset2;
uniform ivec2 uSourceOffset3;

shared float sRobust[64];
shared float sCapped[64];
shared uint sValid[64];

ivec2 layerExtent(int layer) {
    if (layer == 0) return uLayerExtent0;
    if (layer == 1) return uLayerExtent1;
    if (layer == 2) return uLayerExtent2;
    if (layer == 3) return uLayerExtent3;
    if (layer == 4) return uLayerExtent4;
    if (layer == 5) return uLayerExtent5;
    if (layer == 6) return uLayerExtent6;
    return uLayerExtent7;
}

ivec2 referenceOffset(int site) {
    if (site == 0) return uReferenceOffset0;
    if (site == 1) return uReferenceOffset1;
    if (site == 2) return uReferenceOffset2;
    return uReferenceOffset3;
}

ivec2 sourceOffset(int site) {
    if (site == 0) return uSourceOffset0;
    if (site == 1) return uSourceOffset1;
    if (site == 2) return uSourceOffset2;
    return uSourceOffset3;
}

bool finiteValue(float value) {
    return !isnan(value) && !isinf(value);
}

float keys(float distance) {
    float t = abs(distance);
    if (t <= 1.0) {
        return (uKeysParameter + 2.0) * t * t * t -
            (uKeysParameter + 3.0) * t * t + 1.0;
    }
    if (t < 2.0) {
        return uKeysParameter * t * t * t -
            5.0 * uKeysParameter * t * t +
            8.0 * uKeysParameter * t - 4.0 * uKeysParameter;
    }
    return 0.0;
}

bool sampleSource(
    int layer,
    vec2 coordinate,
    out float value,
    out float variance) {
    ivec2 extent = layerExtent(layer);
    ivec2 base = ivec2(floor(coordinate));
    value = 0.0;
    variance = 0.0;
    for (int offsetY = -1; offsetY <= 2; ++offsetY) {
        int tapY = base.y + offsetY;
        if (tapY < 0 || tapY >= extent.y) return false;
        float wy = keys(coordinate.y - float(tapY));
        for (int offsetX = -1; offsetX <= 2; ++offsetX) {
            int tapX = base.x + offsetX;
            if (tapX < 0 || tapX >= extent.x) return false;
            ivec3 location = ivec3(tapX, tapY, layer);
            if (imageLoad(uValid, location).r == 0u) return false;
            float signal = imageLoad(uSignal, location).r;
            float tapVariance = imageLoad(uVariance, location).r;
            if (!finiteValue(signal) || !finiteValue(tapVariance) ||
                tapVariance < 0.0) {
                return false;
            }
            float coefficient = keys(coordinate.x - float(tapX)) * wy;
            value += coefficient * signal;
            variance += coefficient * coefficient * tapVariance;
        }
    }
    return finiteValue(value) && finiteValue(variance) && variance >= 0.0;
}

float huberLoss(float residual) {
    float magnitude = abs(residual);
    if (magnitude <= uHuberDelta) return 0.5 * residual * residual;
    return uHuberDelta * (magnitude - 0.5 * uHuberDelta);
}

void main() {
    uint localIndex = gl_LocalInvocationID.x;
    uint batchIndex = gl_WorkGroupID.x;
    uint candidateIndex = uint(uCandidateOffset) + batchIndex;
    float robust = 0.0;
    float capped = 0.0;
    uint validCount = 0u;
    uint patchSamples = uint(uPatch * uPatch);
    uint nominalCount = patchSamples * 4u;

    if (batchIndex < uint(uCandidateCount)) {
        Candidate candidate = candidates[candidateIndex];
        vec2 centerRaw = candidate.centerResidual.xy;
        vec2 candidateResidual = candidate.centerResidual.zw;
        for (uint sampleIndex = localIndex;
             sampleIndex < nominalCount;
             sampleIndex += 64u) {
            int site = int(sampleIndex / patchSamples);
            uint patchIndex = sampleIndex - uint(site) * patchSamples;
            int patchX = int(patchIndex % uint(uPatch));
            int patchY = int(patchIndex / uint(uPatch));
            int referenceLayer = uReferenceBase + site;
            int sourceLayer = uSourceBase + site;
            ivec2 refOffset = referenceOffset(site);
            ivec2 srcOffset = sourceOffset(site);
            vec2 centerLevel =
                ((centerRaw - vec2(refOffset)) * 0.5) / uLevelScale;
            ivec2 start = ivec2(floor(
                centerLevel - vec2(0.5 * float(uPatch))));
            ivec2 referenceCoordinate = start + ivec2(patchX, patchY);
            ivec2 referenceExtent = layerExtent(referenceLayer);
            if (referenceCoordinate.x < 0 || referenceCoordinate.y < 0 ||
                referenceCoordinate.x >= referenceExtent.x ||
                referenceCoordinate.y >= referenceExtent.y) {
                continue;
            }
            ivec3 referenceLocation = ivec3(
                referenceCoordinate, referenceLayer);
            if (imageLoad(uValid, referenceLocation).r == 0u) continue;
            float referenceValue = imageLoad(uSignal, referenceLocation).r;
            float referenceVariance = imageLoad(
                uVariance, referenceLocation).r;
            if (!finiteValue(referenceValue) ||
                !finiteValue(referenceVariance) ||
                referenceVariance < 0.0) {
                continue;
            }
            vec2 referenceRaw =
                vec2(referenceCoordinate) * (2.0 * uLevelScale) +
                vec2(refOffset);
            vec2 delta = referenceRaw - uCenterTranslation.xy;
            vec2 sourceRaw = vec2(
                uCenterTranslation.x + uLinear.x * delta.x +
                    uLinear.y * delta.y + uCenterTranslation.z,
                uCenterTranslation.y + uLinear.z * delta.x +
                    uLinear.w * delta.y + uCenterTranslation.w) +
                candidateResidual;
            vec2 sourceLevel =
                ((sourceRaw - vec2(srcOffset)) * 0.5) / uLevelScale;
            float sourceValue = 0.0;
            float sourceVariance = 0.0;
            if (!sampleSource(
                    sourceLayer, sourceLevel,
                    sourceValue, sourceVariance)) {
                continue;
            }
            float variance = referenceVariance +
                uExposureScale * uExposureScale * sourceVariance +
                uNumericalFloor;
            if (!finiteValue(variance) || variance <= 0.0) continue;
            float residual =
                (referenceValue - uExposureScale * sourceValue) /
                sqrt(variance);
            if (!finiteValue(residual)) continue;
            robust += huberLoss(residual);
            capped += min(residual * residual, uCappedResidualSquared);
            ++validCount;
        }
    }
    sRobust[localIndex] = robust;
    sCapped[localIndex] = capped;
    sValid[localIndex] = validCount;
    barrier();
    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (localIndex < stride) {
            sRobust[localIndex] += sRobust[localIndex + stride];
            sCapped[localIndex] += sCapped[localIndex + stride];
            sValid[localIndex] += sValid[localIndex + stride];
        }
        barrier();
    }
    if (localIndex == 0u && batchIndex < uint(uCandidateCount)) {
        uint count = sValid[0];
        if (count == 0u) {
            scores[candidateIndex] = vec4(0.0);
        } else {
            scores[candidateIndex] = vec4(
                sRobust[0] / float(count),
                sCapped[0] / float(count),
                float(count) / float(nominalCount),
                float(count));
        }
    }
}
)GLSL";

void ClearGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

bool CheckGl(const char* operation, std::string& error) {
    const GLenum code = glGetError();
    if (code == GL_NO_ERROR) return true;
    error = std::string(operation) + " failed with OpenGL error " +
        std::to_string(static_cast<unsigned int>(code)) + ".";
    ClearGlErrors();
    return false;
}

bool Finite(double value) {
    return std::isfinite(value);
}

} // namespace

struct GpuLocalMotionDiscreteEvaluator::Impl {
    struct LevelTextureSet {
        std::array<GLuint, 3> textures {};
        std::array<PixelExtent, kLayerCount> layerExtents {};
        bool referenceUploaded=false,alternateUploaded=false;
    };

    GLuint program = 0u;
    std::vector<LevelTextureSet> levelTextures;
    GLuint candidateBuffer = 0u;
    GLuint scoreBuffer = 0u;
    std::size_t bufferCapacity=0;
    const CfaPlanePyramid* firstPyramid = nullptr;
    const CfaPlanePyramid* secondPyramid = nullptr;
    bool initialized = false;
    GpuLocalMotionDiagnostics diagnostics;

    ~Impl() {
        if (!initialized) return;
        if (scoreBuffer != 0u) glDeleteBuffers(1, &scoreBuffer);
        if (candidateBuffer != 0u) glDeleteBuffers(1, &candidateBuffer);
        for (LevelTextureSet& level : levelTextures) {
            glDeleteTextures(
                static_cast<GLsizei>(level.textures.size()),
                level.textures.data());
        }
        if (program != 0u) glDeleteProgram(program);
    }

    bool Initialize(std::string& error) {
        if (initialized) return true;
        error.clear();
        GLint major = 0;
        GLint minor = 0;
        GLint maximumLayers = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maximumLayers);
        if (major < 4 || (major == 4 && minor < 3) ||
            maximumLayers < static_cast<GLint>(kLayerCount)) {
            error =
                "OpenGL 4.3 cannot represent the local-motion score working set.";
            return false;
        }
        initialized = true;
        program = GLHelpers::CreateComputeProgram(kDiscreteSearchShader);
        if (program == 0u) {
            error =
                "The local-motion OpenGL candidate-score shader did not compile.";
            return false;
        }
        glGenBuffers(1, &candidateBuffer);
        glGenBuffers(1, &scoreBuffer);
        if (candidateBuffer == 0u || scoreBuffer == 0u) {
            error =
                "OpenGL could not allocate local-motion score buffers.";
            return false;
        }
        const auto glText = [](GLenum name) {
            const GLubyte* value = glGetString(name);
            return value
                ? std::string(reinterpret_cast<const char*>(value))
                : std::string("unknown");
        };
        diagnostics.deviceIdentity = glText(GL_VENDOR) + " | " +
            glText(GL_RENDERER) + " | " + glText(GL_VERSION);
        return CheckGl("Local-motion GPU initialization", error);
    }

    static void DeleteTextures(LevelTextureSet& level) {
        glDeleteTextures(
            static_cast<GLsizei>(level.textures.size()),
            level.textures.data());
        level.textures = {};
        level.layerExtents = {};
        level.referenceUploaded=level.alternateUploaded=false;
    }

    static void UnbindPyramidImages() {
        for (GLuint unit = 0u; unit < 3u; ++unit) {
            glBindImageTexture(
                unit, 0, 0, GL_FALSE, 0, GL_READ_ONLY,
                unit == 2u ? GL_R8UI : GL_R32F);
        }
    }

    bool AllocateTextures(
        LevelTextureSet& level,
        GLsizei width,
        GLsizei height,
        std::string& error) {
        DeleteTextures(level);
        const std::array<GLenum, 3> formats { GL_R32F, GL_R32F, GL_R8UI };
        for (std::size_t index = 0u; index < level.textures.size(); ++index) {
            glGenTextures(1, &level.textures[index]);
            if (level.textures[index] == 0u) {
                error =
                    "OpenGL could not allocate a local-motion pyramid texture.";
                DeleteTextures(level);
                return false;
            }
            glBindTexture(GL_TEXTURE_2D_ARRAY, level.textures[index]);
            ClearGlErrors();
            glTexStorage3D(
                GL_TEXTURE_2D_ARRAY, 1, formats[index], width, height,
                static_cast<GLsizei>(kLayerCount));
            glTexParameteri(
                GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(
                GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(
                GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(
                GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (!CheckGl("Local-motion pyramid texture allocation", error)) {
                DeleteTextures(level);
                return false;
            }
        }
        return true;
    }

    bool UploadPyramids(
        const LocalMotionDirectionRequest& request,
        std::uint32_t level,
        std::string& error) {
        if (!request.reference || !request.source) {
            error = "The local-motion GPU request has no pyramids.";
            return false;
        }
        const bool samePair =
            (request.reference == firstPyramid &&
             request.source == secondPyramid) ||
            (request.reference == secondPyramid &&
             request.source == firstPyramid);
        if (!samePair) {
            UnbindPyramidImages();
            for (LevelTextureSet& cached : levelTextures) {
                DeleteTextures(cached);
            }
            levelTextures.clear();
            firstPyramid = request.reference;
            secondPyramid = request.source;
            levelTextures.resize(std::max(
                firstPyramid->levelCount,
                secondPyramid->levelCount));
        }
        if (level >= levelTextures.size()) {
            error = "The requested local-motion pyramid level is unavailable.";
            return false;
        }
        LevelTextureSet& target = levelTextures[level];
        if (target.textures[0] != 0u&&target.referenceUploaded&&target.alternateUploaded) return true;

        std::array<const CfaPyramidLevel*, kLayerCount> layers {};
        std::array<PixelExtent, kLayerCount> layerExtents {};
        std::uint64_t maximumWidth = 0u;
        std::uint64_t maximumHeight = 0u;
        for (std::size_t pyramidIndex = 0u;
             pyramidIndex < 2u;
             ++pyramidIndex) {
            const CfaPlanePyramid* pyramid = pyramidIndex == 0u
                ? firstPyramid
                : secondPyramid;
            for (std::size_t site = 0u; site < 4u; ++site) {
                const CfaPyramidLevel* layer = FindCfaPyramidLevel(
                    *pyramid, static_cast<CfaSite>(site), level);
                const std::size_t layerIndex = pyramidIndex * 4u + site;
                if (!layer || layer->extent.width == 0u ||
                    layer->extent.height == 0u ||
                    layer->signal.size() != layer->variance.size() ||
                    layer->signal.size() != layer->validMask.size() ||
                    layer->extent.width >
                        std::numeric_limits<std::uint64_t>::max() /
                            layer->extent.height ||
                    layer->signal.size() != static_cast<std::size_t>(
                        layer->extent.width * layer->extent.height)) {
                    error =
                        "A local-motion pyramid layer is invalid for GPU scoring.";
                    return false;
                }
                layers[layerIndex] = layer;
                layerExtents[layerIndex] = layer->extent;
                maximumWidth = std::max(maximumWidth, layer->extent.width);
                maximumHeight = std::max(maximumHeight, layer->extent.height);
            }
        }
        GLint maximumTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
        if (maximumWidth > static_cast<std::uint64_t>(maximumTextureSize) ||
            maximumHeight > static_cast<std::uint64_t>(maximumTextureSize) ||
            maximumWidth > static_cast<std::uint64_t>(
                std::numeric_limits<GLsizei>::max()) ||
            maximumHeight > static_cast<std::uint64_t>(
                std::numeric_limits<GLsizei>::max())) {
            error =
                "The local-motion pyramid level exceeds the GPU texture limit.";
            return false;
        }
        if (target.textures[0]==0u&&!AllocateTextures(
                target,
                static_cast<GLsizei>(maximumWidth),
                static_cast<GLsizei>(maximumHeight), error)) {
            // Retaining forward-direction levels avoids a second upload during
            // reverse search. Under tighter VRAM, discard those optional
            // caches and retry with only the active level resident.
            UnbindPyramidImages();
            for (LevelTextureSet& cached : levelTextures) {
                if (&cached != &target) DeleteTextures(cached);
            }
            ClearGlErrors();
            error.clear();
            if (!AllocateTextures(
                    target,
                    static_cast<GLsizei>(maximumWidth),
                    static_cast<GLsizei>(maximumHeight), error)) {
                return false;
            }
        }
        // AllocateTextures clears the destination record before allocating,
        // including its metadata. Publish the validated extents only after
        // the final allocation attempt succeeds.
        target.layerExtents = layerExtents;

        std::vector<float> staging;
        try {
            std::size_t maximumSamples = 0u;
            for (const CfaPyramidLevel* layer : layers) {
                maximumSamples = std::max(maximumSamples, layer->signal.size());
            }
            staging.resize(maximumSamples);
        } catch (const std::bad_alloc&) {
            error =
                "Host memory could not stage a local-motion pyramid level.";
            return false;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (std::size_t layerIndex = 0u;
             layerIndex < layers.size();
             ++layerIndex) {
            if(layerIndex<4&&target.referenceUploaded) continue;
            const CfaPyramidLevel& layer = *layers[layerIndex];
            const GLsizei width = static_cast<GLsizei>(layer.extent.width);
            const GLsizei height = static_cast<GLsizei>(layer.extent.height);
            for (std::size_t sample = 0u;
                 sample < layer.signal.size();
                 ++sample) {
                staging[sample] = static_cast<float>(layer.signal[sample]);
            }
            glBindTexture(GL_TEXTURE_2D_ARRAY, target.textures[0]);
            glTexSubImage3D(
                GL_TEXTURE_2D_ARRAY, 0, 0, 0,
                static_cast<GLint>(layerIndex), width, height, 1,
                GL_RED, GL_FLOAT, staging.data());
            for (std::size_t sample = 0u;
                 sample < layer.variance.size();
                 ++sample) {
                staging[sample] = static_cast<float>(layer.variance[sample]);
            }
            glBindTexture(GL_TEXTURE_2D_ARRAY, target.textures[1]);
            glTexSubImage3D(
                GL_TEXTURE_2D_ARRAY, 0, 0, 0,
                static_cast<GLint>(layerIndex), width, height, 1,
                GL_RED, GL_FLOAT, staging.data());
            glBindTexture(GL_TEXTURE_2D_ARRAY, target.textures[2]);
            glTexSubImage3D(
                GL_TEXTURE_2D_ARRAY, 0, 0, 0,
                static_cast<GLint>(layerIndex), width, height, 1,
                GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                layer.validMask.data());
            ++diagnostics.uploadedLayers;
        }
        if(!CheckGl("Local-motion pyramid upload", error)) return false;
        target.referenceUploaded=target.alternateUploaded=true;
        return true;
    }

    void SetDirectionUniforms(
        const LocalMotionDirectionRequest& request,
        std::uint32_t level,
        std::uint32_t patch) {
        const bool direct = request.reference == firstPyramid;
        glUniform1i(
            glGetUniformLocation(program, "uReferenceBase"), direct ? 0 : 4);
        glUniform1i(
            glGetUniformLocation(program, "uSourceBase"), direct ? 4 : 0);
        glUniform1i(
            glGetUniformLocation(program, "uPatch"),
            static_cast<GLint>(patch));
        glUniform1f(
            glGetUniformLocation(program, "uLevelScale"),
            static_cast<float>(std::ldexp(1.0, level)));
        glUniform1f(
            glGetUniformLocation(program, "uExposureScale"),
            static_cast<float>(request.exposureScale));
        glUniform1f(
            glGetUniformLocation(program, "uNumericalFloor"),
            static_cast<float>(request.options.registration.
                localNumericalVarianceFloor));
        glUniform1f(
            glGetUniformLocation(program, "uHuberDelta"),
            static_cast<float>(request.options.registration.
                registrationHuberDelta));
        glUniform1f(
            glGetUniformLocation(program, "uCappedResidualSquared"),
            static_cast<float>(request.options.registration.
                cappedResidualSquared));
        glUniform1f(
            glGetUniformLocation(program, "uKeysParameter"),
            static_cast<float>(request.options.registration.
                keysBicubicParameter));
        glUniform4f(
            glGetUniformLocation(program, "uLinear"),
            static_cast<float>(request.globalWarp.linear[0]),
            static_cast<float>(request.globalWarp.linear[1]),
            static_cast<float>(request.globalWarp.linear[2]),
            static_cast<float>(request.globalWarp.linear[3]));
        glUniform4f(
            glGetUniformLocation(program, "uCenterTranslation"),
            static_cast<float>(request.globalWarp.centerRaw.x),
            static_cast<float>(request.globalWarp.centerRaw.y),
            static_cast<float>(request.globalWarp.translationRaw.x),
            static_cast<float>(request.globalWarp.translationRaw.y));
        const std::array<PixelExtent, kLayerCount>& layerExtents =
            levelTextures[level].layerExtents;
        for (std::size_t layer = 0u; layer < layerExtents.size(); ++layer) {
            const std::string name = "uLayerExtent" + std::to_string(layer);
            glUniform2i(
                glGetUniformLocation(program, name.c_str()),
                static_cast<GLint>(layerExtents[layer].width),
                static_cast<GLint>(layerExtents[layer].height));
        }
        for (std::size_t site = 0u; site < 4u; ++site) {
            const CfaOffset referenceOffset =
                request.reference->layout.OffsetFor(
                    static_cast<CfaSite>(site));
            const CfaOffset sourceOffset = request.source->layout.OffsetFor(
                static_cast<CfaSite>(site));
            const std::string referenceName =
                "uReferenceOffset" + std::to_string(site);
            const std::string sourceName =
                "uSourceOffset" + std::to_string(site);
            glUniform2i(
                glGetUniformLocation(program, referenceName.c_str()),
                referenceOffset.x, referenceOffset.y);
            glUniform2i(
                glGetUniformLocation(program, sourceName.c_str()),
                sourceOffset.x, sourceOffset.y);
        }
    }

    bool Evaluate(
        const LocalMotionDirectionRequest& request,
        const std::vector<LocalMotionDiscreteCandidate>& candidates,
        std::vector<LocalMotionDiscreteScore>& scores,
        std::string& error) {
        scores.clear();
        if (candidates.empty()) return true;
        if (!Initialize(error)) return false;
        if (request.shouldCancel && request.shouldCancel()) {
            error = "Local-motion GPU scoring was canceled.";
            return false;
        }
        const std::uint32_t level = candidates.front().level;
        const std::uint32_t patch = candidates.front().patchLevelPixels;
        if (!request.reference || !request.source || patch == 0u ||
            !Finite(request.exposureScale) || request.exposureScale <= 0.0 ||
            std::any_of(
                candidates.begin(), candidates.end(),
                [level, patch](const LocalMotionDiscreteCandidate& candidate) {
                    return candidate.level != level ||
                        candidate.patchLevelPixels != patch ||
                        !Finite(candidate.centerRaw.x) ||
                        !Finite(candidate.centerRaw.y) ||
                        !Finite(candidate.residualRaw.x) ||
                        !Finite(candidate.residualRaw.y);
                })) {
            error = "The local-motion GPU score batch is invalid.";
            return false;
        }
        if (!UploadPyramids(request, level, error)) return false;

        std::vector<PackedCandidate> packed;
        std::vector<std::array<float, 4>> packedScores;
        try {
            packed.resize(candidates.size());
            packedScores.resize(candidates.size());
            scores.resize(candidates.size());
        } catch (const std::bad_alloc&) {
            scores.clear();
            error =
                "Host memory could not hold a local-motion GPU score batch.";
            return false;
        }
        for (std::size_t index = 0u; index < candidates.size(); ++index) {
            packed[index].centerResidual = {
                static_cast<float>(candidates[index].centerRaw.x),
                static_cast<float>(candidates[index].centerRaw.y),
                static_cast<float>(candidates[index].residualRaw.x),
                static_cast<float>(candidates[index].residualRaw.y)
            };
        }
        if(packed.size()>bufferCapacity) {
            bufferCapacity=std::max(packed.size(),bufferCapacity*2);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,candidateBuffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER,bufferCapacity*sizeof(PackedCandidate),nullptr,GL_DYNAMIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,scoreBuffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER,bufferCapacity*sizeof(packedScores.front()),nullptr,GL_DYNAMIC_DRAW);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,candidateBuffer);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,packed.size()*sizeof(PackedCandidate),packed.data());
        if (!CheckGl("Local-motion GPU score-buffer upload", error)) {
            return false;
        }

        glUseProgram(program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, candidateBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, scoreBuffer);
        const LevelTextureSet& activeLevel = levelTextures[level];
        glBindImageTexture(
            0, activeLevel.textures[0], 0, GL_TRUE, 0,
            GL_READ_ONLY, GL_R32F);
        glBindImageTexture(
            1, activeLevel.textures[1], 0, GL_TRUE, 0,
            GL_READ_ONLY, GL_R32F);
        glBindImageTexture(
            2, activeLevel.textures[2], 0, GL_TRUE, 0,
            GL_READ_ONLY, GL_R8UI);
        SetDirectionUniforms(request, level, patch);

        std::uint32_t offset = 0u;
        const std::uint32_t total = static_cast<std::uint32_t>(
            std::min<std::size_t>(
                candidates.size(),
                std::numeric_limits<std::uint32_t>::max()));
        if (total != candidates.size()) {
            error = "The local-motion GPU score batch is too large.";
            return false;
        }
        while (offset < total) {
            const std::uint32_t count = std::min(
                kMaximumDispatchGroups, total - offset);
            glUniform1i(
                glGetUniformLocation(program, "uCandidateOffset"),
                static_cast<GLint>(offset));
            glUniform1i(
                glGetUniformLocation(program, "uCandidateCount"),
                static_cast<GLint>(count));
            glDispatchCompute(count, 1u, 1u);
            glMemoryBarrier(
                GL_SHADER_STORAGE_BARRIER_BIT |
                GL_BUFFER_UPDATE_BARRIER_BIT);
            ++diagnostics.dispatchCount;
            diagnostics.scoredCandidateCount += count;
            offset += count;
        }
        if (!CheckGl("Local-motion GPU candidate scoring", error)) {
            return false;
        }
        if (request.shouldCancel && request.shouldCancel()) {
            error = "Local-motion GPU scoring was canceled.";
            return false;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, scoreBuffer);
        glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GLsizeiptr>(
                packedScores.size() * sizeof(packedScores.front())),
            packedScores.data());
        if (!CheckGl("Local-motion GPU score readback", error)) {
            return false;
        }
        for (std::size_t index = 0u; index < scores.size(); ++index) {
            const auto& packedScore = packedScores[index];
            const double validCount = packedScore[3];
            LocalMotionDiscreteScore score;
            score.valid = Finite(validCount) && validCount >= 1.0 &&
                Finite(packedScore[0]) && Finite(packedScore[1]) &&
                Finite(packedScore[2]);
            if (score.valid) {
                score.validCount = static_cast<std::uint64_t>(
                    std::llround(validCount));
                score.robustCost = packedScore[0];
                score.cappedChiSquared = packedScore[1];
                score.validFraction = packedScore[2];
            }
            scores[index] = score;
        }
        error.clear();
        return true;
    }
};

GpuLocalMotionDiscreteEvaluator::GpuLocalMotionDiscreteEvaluator()
    : m_Impl(std::make_unique<Impl>()) {
}

GpuLocalMotionDiscreteEvaluator::~GpuLocalMotionDiscreteEvaluator() = default;

bool GpuLocalMotionDiscreteEvaluator::Evaluate(
    const LocalMotionDirectionRequest& request,
    const std::vector<LocalMotionDiscreteCandidate>& candidates,
    std::vector<LocalMotionDiscreteScore>& scores,
    std::string& error) {
    Stack::Rendering::ScopedComputeBindings bindings;
    const auto start=std::chrono::steady_clock::now();
    const bool result=m_Impl&&m_Impl->Evaluate(request,candidates,scores,error);
    if(m_Impl) m_Impl->diagnostics.evaluationSeconds+=
        std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    return result;
}

void GpuLocalMotionDiscreteEvaluator::BeginPair(const CfaPlanePyramid& reference,
    const CfaPlanePyramid& alternate) {
    if(!m_Impl) return;
    if(m_Impl->firstPyramid!=&reference) {
        for(auto& level:m_Impl->levelTextures) Impl::DeleteTextures(level);
        m_Impl->levelTextures.clear();
    }
    m_Impl->firstPyramid=&reference;m_Impl->secondPyramid=&alternate;
    m_Impl->levelTextures.resize(std::max(reference.levelCount,alternate.levelCount));
    // The alternate object can reuse the same stack address for a new capture.
    for(auto& level:m_Impl->levelTextures) level.alternateUploaded=false;
}

void GpuLocalMotionDiscreteEvaluator::AbandonContextResources() {
    if(m_Impl) m_Impl->initialized=false;
}

const GpuLocalMotionDiagnostics&
GpuLocalMotionDiscreteEvaluator::Diagnostics() const {
    static const GpuLocalMotionDiagnostics empty;
    return m_Impl ? m_Impl->diagnostics : empty;
}

} // namespace Raw::Mfd
