#include "Renderer/RenderPipeline.h"

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawColorWarpMask.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/CoverageSampling.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/ScopedGLObjects.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_TEXTURE4
#define GL_TEXTURE4 (GL_TEXTURE0 + 4)
#endif
#ifndef GL_TEXTURE5
#define GL_TEXTURE5 (GL_TEXTURE0 + 5)
#endif

namespace {

const char* kRawOutputStageVert = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kRawOutputCropFrag = R"(
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uCropOrigin;
uniform vec2 uCropExtent;

void main() {
    FragColor = sampleCovered(uInputTex, uCropOrigin + vUV * uCropExtent);
}
)";

const char* kRawColorWarpProxyFrag = R"(
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;
uniform sampler2D uInputTex;
void main() { FragColor = texture(uInputTex, vUV); }
)";

const char* kRawColorWarpFrag = R"(
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;

uniform sampler2D uInputTex;
uniform int uInputIsRec2020;
uniform int uUseRelativeChroma;
uniform float uStrength;
uniform int uPinCount;
uniform vec4 uPinSourceTarget[8];
uniform vec4 uPinShape[8];
uniform vec4 uPinLight[8];
uniform vec4 uPinQualifier[8];
uniform sampler2D uEvCurves;
uniform int uUseSpatialMasks;
uniform sampler2DArray uGateMask;
uniform sampler2DArray uSupportMask;
uniform sampler2DArray uBoundaryMask;
uniform sampler2D uMaskGuide;
uniform float uPinSpatialSupport[8];

float signedCbrt(float x) {
    return sign(x) * pow(abs(x), 1.0 / 3.0);
}

vec3 workingToXyz(vec3 rgb) {
    if (uInputIsRec2020 != 0) {
        return vec3(
            dot(rgb, vec3(0.6369580, 0.1446169, 0.1688809)),
            dot(rgb, vec3(0.2627002, 0.6779981, 0.0593017)),
            dot(rgb, vec3(0.0, 0.0280727, 1.0609851)));
    }
    return vec3(
        dot(rgb, vec3(0.4124564, 0.3575761, 0.1804375)),
        dot(rgb, vec3(0.2126729, 0.7151522, 0.0721750)),
        dot(rgb, vec3(0.0193339, 0.1191920, 0.9503041)));
}

vec3 xyzToWorking(vec3 xyz) {
    if (uInputIsRec2020 != 0) {
        return vec3(
            dot(xyz, vec3(1.7166512, -0.3556708, -0.2533663)),
            dot(xyz, vec3(-0.6666844, 1.6164812, 0.0157685)),
            dot(xyz, vec3(0.0176399, -0.0427706, 0.9421031)));
    }
    return vec3(
        dot(xyz, vec3(3.2404542, -1.5371385, -0.4985314)),
        dot(xyz, vec3(-0.9692660, 1.8760108, 0.0415560)),
        dot(xyz, vec3(0.0556434, -0.2040259, 1.0572252)));
}

vec3 xyzToOklab(vec3 xyz) {
    vec3 lms = vec3(
        dot(xyz, vec3(0.8190224380, 0.3619062601, -0.1288737815)),
        dot(xyz, vec3(0.0329836539, 0.9292868616, 0.0361446664)),
        dot(xyz, vec3(0.0481771894, 0.2642395318, 0.6335478285)));
    lms = vec3(signedCbrt(lms.x), signedCbrt(lms.y), signedCbrt(lms.z));
    return vec3(
        dot(lms, vec3(0.2104542553, 0.7936177850, -0.0040720468)),
        dot(lms, vec3(1.9779984951, -2.4285922050, 0.4505937099)),
        dot(lms, vec3(0.0259040371, 0.7827717662, -0.8086757660)));
}

vec3 oklabToXyz(vec3 lab) {
    vec3 roots = vec3(
        lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z,
        lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z,
        lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z);
    vec3 lms = roots * roots * roots;
    return vec3(
        dot(lms, vec3(1.2268798734, -0.5578149966, 0.2813910502)),
        dot(lms, vec3(-0.0405757626, 1.1122868294, -0.0717110667)),
        dot(lms, vec3(-0.0763729497, -0.4214933240, 1.5869240244)));
}

float pinLightnessWeight(float ev, int pinIndex) {
    float samplePosition = clamp((ev + 16.0) / 32.0, 0.0, 1.0) * 256.0;
    float curveX = (samplePosition + 0.5) / 257.0;
    float curveY = (float(pinIndex) + 0.5) / max(1.0, float(uPinCount));
    return texture(uEvCurves, vec2(curveX, curveY)).r;
}

float pinShapeDistance(vec2 delta, float radius, vec4 qualifier) {
    float radialDistance = length(delta);
    float circularDistance = radialDistance / max(0.005, radius);
    float directionality = clamp(qualifier.x, 0.0, 1.0);
    if (directionality <= 0.000001 || radialDistance <= 0.000001) {
        return circularDistance;
    }
    vec2 direction = vec2(cos(qualifier.y), sin(qualifier.y));
    float cosine = clamp(dot(delta, direction) / radialDistance, -1.0, 1.0);
    float forward = (cosine + 1.0) * 0.5;
    float aperture = clamp(qualifier.z, 0.10, 1.0);
    float lobePower = 1.0 + 4.0 * (1.0 - aperture);
    float directionalReach = max(
        0.05,
        aperture + (1.0 - aperture) * pow(forward, lobePower));
    float directionalDistance = circularDistance / directionalReach;
    return mix(circularDistance, directionalDistance, directionality);
}

void main() {
    vec4 inputColor = texture(uInputTex, vUV);
    vec3 xyz = workingToXyz(inputColor.rgb);
    vec3 lab = xyzToOklab(xyz);
    float chromaScale = max(0.001, abs(lab.x));
    vec2 chroma = uUseRelativeChroma != 0
        ? lab.yz / chromaScale
        : lab.yz;
    float sceneEv = log2(max(xyz.y, 0.000001) / 0.18);
    vec2 displacement = vec2(0.0);
    float lightnessDelta = 0.0;
    float totalWeight = 0.0;
    for (int index = 0; index < 8; ++index) {
        if (index >= uPinCount) break;
        vec4 sourceTarget = uPinSourceTarget[index];
        vec4 shape = uPinShape[index];
        vec4 light = uPinLight[index];
        vec4 qualifier = uPinQualifier[index];
        if (shape.z <= 0.0001) continue;
        float radius = max(0.005, shape.x);
        float shapeDistance = pinShapeDistance(
            chroma - sourceTarget.xy,
            radius,
            qualifier);
        if (shapeDistance >= 1.0) continue;
        float innerBoundary = max(0.0, 1.0 - clamp(shape.y, 0.0, 1.0));
        float radial = shapeDistance <= innerBoundary
            ? 1.0
            : 1.0 - smoothstep(innerBoundary, 1.0, shapeDistance);
        float direct = radial * pinLightnessWeight(sceneEv, index);
        float gate = 1.0;
        float support = 0.0;
        float boundary = 1.0;
        if (uUseSpatialMasks != 0) {
            float layer = float(index);
            vec3 maskUv = vec3(vUV, layer);
            vec3 guide = texture(uMaskGuide, vUV).rgb;
            float guideDistance = length(lab.yz - guide.xy) * 36.0 +
                abs(sceneEv - guide.z) * 0.28;
            float guided = clamp(exp(-guideDistance), 0.04, 1.0);
            gate = texture(uGateMask, maskUv).r * guided;
            support = texture(uSupportMask, maskUv).r * guided;
            boundary = texture(uBoundaryMask, maskUv).r;
        }
        float directContribution = direct * gate;
        float spatialContribution = support * uPinSpatialSupport[index];
        float weight = max(directContribution, spatialContribution) *
            boundary * shape.z;
        if (weight <= 0.000001) continue;
        totalWeight += weight;
        if (shape.w < 0.5) {
            displacement += (sourceTarget.zw - sourceTarget.xy) * weight;
            lightnessDelta += light.w * weight;
        }
    }
    if (totalWeight > 0.000001) {
        float denominator = max(1.0, totalWeight);
        chroma += uStrength * displacement / denominator;
        lab.yz = uUseRelativeChroma != 0
            ? chroma * chromaScale
            : chroma;
        vec3 outputRgb = xyzToWorking(oklabToXyz(lab));
        outputRgb *= exp2(uStrength * lightnessDelta / denominator);
        FragColor = vec4(outputRgb, inputColor.a);
    } else {
        FragColor = inputColor;
    }
}
)";

template <typename T>
void HashColorWarpMaskValue(std::size_t& seed, const T& value) {
    seed ^= std::hash<T> {}(value) + 0x9e3779b97f4a7c15ull +
        (seed << 6u) + (seed >> 2u);
}

std::size_t BuildColorWarpMaskKey(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const Stack::RawRecipe::RawColorWarpRecipe& colorWarp,
    int qualityLongEdge,
    int width,
    int height) {
    const auto stages = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(
        recipe,
        qualityLongEdge);
    std::size_t seed = stages.postFinishTone;
    HashColorWarpMaskValue(seed, qualityLongEdge);
    HashColorWarpMaskValue(seed, width);
    HashColorWarpMaskValue(seed, height);
    for (const auto& pin : colorWarp.pins) {
        if (!pin.enabled) continue;
        HashColorWarpMaskValue(seed, pin.id);
        HashColorWarpMaskValue(seed, pin.sourceA);
        HashColorWarpMaskValue(seed, pin.sourceB);
        HashColorWarpMaskValue(seed, pin.radius);
        HashColorWarpMaskValue(seed, pin.softness);
        HashColorWarpMaskValue(seed, pin.qualifierDirectionality);
        HashColorWarpMaskValue(seed, pin.qualifierOrientationRadians);
        HashColorWarpMaskValue(seed, pin.qualifierAperture);
        HashColorWarpMaskValue(seed, pin.regionId);
        for (const float sample : pin.evCurve.samples) {
            HashColorWarpMaskValue(seed, sample);
        }
    }
    for (const auto& region : colorWarp.regions) {
        HashColorWarpMaskValue(seed, region.id);
        HashColorWarpMaskValue(seed, static_cast<int>(region.spatialMode));
        HashColorWarpMaskValue(seed, region.reachPixels);
        HashColorWarpMaskValue(seed, region.spatialSupport);
        HashColorWarpMaskValue(seed, region.edgeStop);
        HashColorWarpMaskValue(seed, region.featherPixels);
        HashColorWarpMaskValue(seed, static_cast<int>(region.featherDirection));
        for (const auto& circle : region.circles) {
            HashColorWarpMaskValue(seed, circle.id);
            HashColorWarpMaskValue(seed, circle.centerU);
            HashColorWarpMaskValue(seed, circle.centerV);
            HashColorWarpMaskValue(seed, circle.radiusU);
            HashColorWarpMaskValue(seed, circle.radiusV);
            HashColorWarpMaskValue(seed, static_cast<int>(circle.interpretation));
            HashColorWarpMaskValue(seed, static_cast<int>(circle.polarity));
        }
    }
    return seed == 0u ? 1u : seed;
}

} // namespace

bool RenderPipeline::RenderRawDevelopmentColorWarpStage(
    GraphNodeRenderResult& result,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const Stack::RawRecipe::RawColorWarpRecipe colorWarp =
        Stack::RawRecipe::SanitizeColorWarpRecipe(recipe.colorWarp);
    if (!Stack::RawRecipe::IsColorWarpEnabled(colorWarp)) {
        return true;
    }
    if (result.texture == 0) {
        return false;
    }
    if (m_RawDevelopmentColorWarpProgram == 0) {
        m_RawDevelopmentColorWarpProgram = GLHelpers::CreateShaderProgram(
            kRawOutputStageVert,
            kRawColorWarpFrag);
    }
    if (m_RawDevelopmentColorWarpProgram == 0) {
        std::cerr << "[RenderPipeline] RAW Color Warp shader compilation failed; "
                     "passing input texture through.\n";
        if (m_RawDevelopmentColorWarpRequireSpatialResult &&
            !colorWarp.regions.empty()) {
            if (result.owned && result.texture != 0u) {
                glDeleteTextures(1, &result.texture);
            }
            result.texture = 0u;
            result.owned = false;
            m_LastGraphExecutionStats.lastSpecializedFailure =
                "Required Color Warp shader was unavailable for export.";
        }
        return false;
    }

    // Heap-backed uniform packs avoid consuming the normal Windows thread
    // reserve during a high-resolution RAW render.
    constexpr std::size_t colorWarpPinLimit =
        Stack::RawRecipe::kMaxRawColorWarpPins;
    std::vector<float> sourceTarget(colorWarpPinLimit * 4u, 0.0f);
    std::vector<float> shape(colorWarpPinLimit * 4u, 0.0f);
    std::vector<float> light(colorWarpPinLimit * 4u, 0.0f);
    std::vector<float> qualifier(colorWarpPinLimit * 4u, 0.0f);
    std::vector<float> evCurves(
        colorWarpPinLimit * Stack::RawRecipe::kRawColorWarpEvCurveSampleCount,
        1.0f);
    std::vector<float> spatialSupport(colorWarpPinLimit, 0.0f);
    std::vector<const Stack::RawRecipe::RawColorWarpPin*> activePins;
    activePins.reserve(colorWarpPinLimit);
    int pinCount = 0;
    for (const Stack::RawRecipe::RawColorWarpPin& pin : colorWarp.pins) {
        if (!pin.enabled || pinCount >= static_cast<int>(colorWarpPinLimit)) {
            continue;
        }
        const std::size_t offset = static_cast<std::size_t>(pinCount) * 4u;
        sourceTarget[offset + 0u] = pin.sourceA;
        sourceTarget[offset + 1u] = pin.sourceB;
        sourceTarget[offset + 2u] = pin.targetA;
        sourceTarget[offset + 3u] = pin.targetB;
        shape[offset + 0u] = pin.radius;
        shape[offset + 1u] = pin.softness;
        shape[offset + 2u] = pin.strength;
        shape[offset + 3u] = pin.protectColor ? 1.0f : 0.0f;
        light[offset + 3u] = pin.lightnessDeltaEv;
        for (std::size_t sampleIndex = 0;
             sampleIndex < Stack::RawRecipe::kRawColorWarpEvCurveSampleCount;
             ++sampleIndex) {
            evCurves[
                static_cast<std::size_t>(pinCount) *
                    Stack::RawRecipe::kRawColorWarpEvCurveSampleCount +
                sampleIndex] = pin.evCurve.samples[sampleIndex];
        }
        qualifier[offset + 0u] = pin.qualifierDirectionality;
        qualifier[offset + 1u] = pin.qualifierOrientationRadians;
        qualifier[offset + 2u] = pin.qualifierAperture;
        const auto region = std::find_if(
            colorWarp.regions.begin(),
            colorWarp.regions.end(),
            [&](const Stack::RawRecipe::RawColorWarpRegion& candidate) {
                return candidate.id == pin.regionId;
            });
        if (region != colorWarp.regions.end()) {
            spatialSupport[static_cast<std::size_t>(pinCount)] =
                region->spatialSupport;
        }
        activePins.push_back(&pin);
        ++pinCount;
    }
    if (pinCount == 0) {
        return true;
    }

    Stack::Renderer::ScopedGLTexture evCurveTexture;
    {
        while (glGetError() != GL_NO_ERROR) {
        }
        unsigned int texture = 0u;
        glGenTextures(1, &texture);
        if (texture != 0u) {
            const Stack::Renderer::GLState::TextureBinding savedTexture(
                GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
            const Stack::Renderer::GLState::PixelUnpackState savedUnpack;
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            savedUnpack.ConfigureTightCpuUpload();
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_R32F,
                static_cast<int>(Stack::RawRecipe::kRawColorWarpEvCurveSampleCount),
                pinCount,
                0,
                GL_RED,
                GL_FLOAT,
                evCurves.data());
            savedUnpack.Restore();
            savedTexture.Restore();
            if (glGetError() == GL_NO_ERROR) {
                evCurveTexture.Reset(texture);
            } else {
                glDeleteTextures(1, &texture);
            }
        }
    }
    if (!evCurveTexture) {
        std::cerr << "[RenderPipeline] RAW Color Warp EV curve upload failed; "
                     "passing input texture through.\n";
        return false;
    }

    const bool hasSpatialRegions = std::any_of(
        activePins.begin(),
        activePins.end(),
        [&](const Stack::RawRecipe::RawColorWarpPin* pin) {
            const auto region = std::find_if(
                colorWarp.regions.begin(),
                colorWarp.regions.end(),
                [&](const Stack::RawRecipe::RawColorWarpRegion& candidate) {
                    return candidate.id == pin->regionId;
                });
            return region != colorWarp.regions.end() &&
                region->spatialMode != Stack::RawRecipe::RawColorWarpSpatialMode::AllMatches;
        });
    bool useSpatialMasks = false;
    if (hasSpatialRegions) {
        const int qualityLongEdge =
            m_RawDevelopmentColorWarpMaskQualityLongEdge;
        const float proxyScale = std::min(
            1.0f,
            static_cast<float>(qualityLongEdge) /
                std::max(1, std::max(m_Width, m_Height)));
        const int proxyWidth = std::max(1, static_cast<int>(std::lround(m_Width * proxyScale)));
        const int proxyHeight = std::max(1, static_cast<int>(std::lround(m_Height * proxyScale)));
        const std::size_t maskKey = BuildColorWarpMaskKey(
            recipe,
            colorWarp,
            qualityLongEdge,
            m_Width,
            m_Height);
        auto& cache = m_RawDevelopmentColorWarpMaskCache;
        const bool cachedLayersMatch =
            cache.layers == pinCount &&
            cache.pinIds.size() == activePins.size() &&
            std::equal(
                activePins.begin(), activePins.end(), cache.pinIds.begin(),
                [](const Stack::RawRecipe::RawColorWarpPin* pin,
                   const std::string& cachedId) {
                    return pin != nullptr && pin->id == cachedId;
                });
        const bool compatibleCachedMask =
            cachedLayersMatch &&
            cache.gateTexture != 0 && cache.supportTexture != 0 &&
            cache.boundaryTexture != 0 && cache.guideTexture != 0;
        if (cache.key == maskKey && cache.width == proxyWidth &&
            cache.height == proxyHeight && compatibleCachedMask) {
            useSpatialMasks = true;
            ++m_LastGraphExecutionStats.colorWarpMaskCacheHits;
        } else {
            ++m_LastGraphExecutionStats.colorWarpMaskCacheMisses;
            const auto buildStarted = std::chrono::steady_clock::now();
            if (m_RawDevelopmentColorWarpProxyProgram == 0) {
                m_RawDevelopmentColorWarpProxyProgram = GLHelpers::CreateShaderProgram(
                    kRawOutputStageVert,
                    kRawColorWarpProxyFrag);
            }
            Stack::Renderer::ScopedGLTexture proxyTexture(
                GLHelpers::CreateEmptyTexture(proxyWidth, proxyHeight));
            unsigned int proxyFbo = proxyTexture
                ? GLHelpers::CreateFBO(proxyTexture.Get())
                : 0u;
            std::vector<float> proxyPixels(
                static_cast<std::size_t>(proxyWidth) * proxyHeight * 4u,
                0.0f);
            bool proxyReady = proxyFbo != 0u &&
                m_RawDevelopmentColorWarpProxyProgram != 0u;
            if (proxyReady) {
                const Stack::Renderer::GLState::FramebufferState savedFramebuffer(true);
                const Stack::Renderer::GLState::PixelPackState savedPack;
                glBindFramebuffer(GL_FRAMEBUFFER, proxyFbo);
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                glViewport(0, 0, proxyWidth, proxyHeight);
                glUseProgram(m_RawDevelopmentColorWarpProxyProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, result.texture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProxyProgram, "uInputTex"),
                    0);
                m_Quad.Draw();
                glUseProgram(0);
                savedPack.ConfigureTightCpuReadback();
                glReadPixels(
                    0, 0, proxyWidth, proxyHeight,
                    GL_RGBA, GL_FLOAT, proxyPixels.data());
                const GLenum proxyError = glGetError();
                savedPack.Restore();
                savedFramebuffer.Restore(true);
                proxyReady = proxyError == GL_NO_ERROR;
            }
            if (proxyFbo != 0u) glDeleteFramebuffers(1, &proxyFbo);

            Stack::RawRecipe::RawColorWarpMaskGuide guide;
            guide.width = proxyWidth;
            guide.height = proxyHeight;
            guide.sourceWidth = m_Width;
            guide.sourceHeight = m_Height;
            guide.pixels.resize(static_cast<std::size_t>(proxyWidth) * proxyHeight);
            std::vector<float> packedGuide(guide.pixels.size() * 4u, 0.0f);
            if (proxyReady) {
                for (std::size_t index = 0; index < guide.pixels.size(); ++index) {
                    if ((index & 4095u) == 0u &&
                        m_ShouldCancelRender && m_ShouldCancelRender()) {
                        proxyReady = false;
                        ++m_LastGraphExecutionStats.colorWarpMaskCancellations;
                        break;
                    }
                    const auto coordinate = Stack::RawRecipe::WorkingRgbToColorWarpCoordinate(
                        { proxyPixels[index * 4u + 0u],
                          proxyPixels[index * 4u + 1u],
                          proxyPixels[index * 4u + 2u] },
                        recipe.technical.workingSpace);
                    guide.pixels[index] = coordinate;
                    packedGuide[index * 4u + 0u] = coordinate.a;
                    packedGuide[index * 4u + 1u] = coordinate.b;
                    packedGuide[index * 4u + 2u] = coordinate.sceneEv;
                    packedGuide[index * 4u + 3u] = 1.0f;
                }
            }
            std::vector<std::vector<std::uint8_t>> gateLayers;
            std::vector<std::vector<std::uint8_t>> supportLayers;
            std::vector<std::vector<std::uint8_t>> boundaryLayers;
            bool cancelled = false;
            if (proxyReady) {
                gateLayers.reserve(activePins.size());
                supportLayers.reserve(activePins.size());
                boundaryLayers.reserve(activePins.size());
                for (const auto* pin : activePins) {
                    const auto region = std::find_if(
                        colorWarp.regions.begin(),
                        colorWarp.regions.end(),
                        [&](const Stack::RawRecipe::RawColorWarpRegion& candidate) {
                            return candidate.id == pin->regionId;
                        });
                    Stack::RawRecipe::RawColorWarpRegion rendererRegion;
                    const Stack::RawRecipe::RawColorWarpRegion* regionPtr = nullptr;
                    if (region != colorWarp.regions.end()) {
                        rendererRegion = *region;
                        // Recipe/sample coordinates use a top-left image
                        // origin. glReadPixels and the uploaded proxy mask
                        // textures use OpenGL's bottom-left row order. Keep
                        // the guide in native GL order and convert the seed
                        // geometry once at this boundary.
                        for (auto& circle : rendererRegion.circles) {
                            circle.centerV = 1.0f - circle.centerV;
                        }
                        regionPtr = &rendererRegion;
                    }
                    auto fields = Stack::RawRecipe::BuildRawColorWarpMaskFields(
                        guide,
                        *pin,
                        regionPtr,
                        m_ShouldCancelRender);
                    if (fields.cancelled) {
                        cancelled = true;
                        ++m_LastGraphExecutionStats.colorWarpMaskCancellations;
                        break;
                    }
                    gateLayers.push_back(std::move(fields.gate));
                    supportLayers.push_back(std::move(fields.support));
                    boundaryLayers.push_back(std::move(fields.boundary));
                }
            }
            if (cancelled) proxyReady = false;

            Stack::Renderer::ScopedGLTexture gateTexture;
            Stack::Renderer::ScopedGLTexture supportTexture;
            Stack::Renderer::ScopedGLTexture boundaryTexture;
            Stack::Renderer::ScopedGLTexture guideTexture;
            if (proxyReady && gateLayers.size() == activePins.size()) {
                gateTexture.Reset(GLHelpers::CreateTextureArray(
                    proxyWidth, proxyHeight, pinCount, GL_R8));
                supportTexture.Reset(GLHelpers::CreateTextureArray(
                    proxyWidth, proxyHeight, pinCount, GL_R8));
                boundaryTexture.Reset(GLHelpers::CreateTextureArray(
                    proxyWidth, proxyHeight, pinCount, GL_R8));
                guideTexture.Reset(GLHelpers::CreateEmptyTexture(proxyWidth, proxyHeight));
                if (gateTexture && supportTexture && boundaryTexture && guideTexture) {
                    for (int layer = 0; layer < pinCount; ++layer) {
                        GLHelpers::UploadTextureArrayLayer(
                            gateTexture.Get(), layer, proxyWidth, proxyHeight,
                            GL_RED, GL_UNSIGNED_BYTE,
                            gateLayers[static_cast<std::size_t>(layer)].data());
                        GLHelpers::UploadTextureArrayLayer(
                            supportTexture.Get(), layer, proxyWidth, proxyHeight,
                            GL_RED, GL_UNSIGNED_BYTE,
                            supportLayers[static_cast<std::size_t>(layer)].data());
                        GLHelpers::UploadTextureArrayLayer(
                            boundaryTexture.Get(), layer, proxyWidth, proxyHeight,
                            GL_RED, GL_UNSIGNED_BYTE,
                            boundaryLayers[static_cast<std::size_t>(layer)].data());
                    }
                    const Stack::Renderer::GLState::TextureBinding savedTexture(
                        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
                    const Stack::Renderer::GLState::PixelUnpackState savedUnpack;
                    glBindTexture(GL_TEXTURE_2D, guideTexture.Get());
                    savedUnpack.ConfigureTightCpuUpload();
                    glTexSubImage2D(
                        GL_TEXTURE_2D, 0, 0, 0, proxyWidth, proxyHeight,
                        GL_RGBA, GL_FLOAT, packedGuide.data());
                    savedUnpack.Restore();
                    savedTexture.Restore();
                    useSpatialMasks = glGetError() == GL_NO_ERROR;
                }
            }
            if (useSpatialMasks) {
                auto deleteTexture = [](unsigned int& texture) {
                    if (texture != 0u) glDeleteTextures(1, &texture);
                    texture = 0u;
                };
                deleteTexture(cache.gateTexture);
                deleteTexture(cache.supportTexture);
                deleteTexture(cache.boundaryTexture);
                deleteTexture(cache.guideTexture);
                cache.key = maskKey;
                cache.width = proxyWidth;
                cache.height = proxyHeight;
                cache.layers = pinCount;
                cache.gateTexture = gateTexture.Release();
                cache.supportTexture = supportTexture.Release();
                cache.boundaryTexture = boundaryTexture.Release();
                cache.guideTexture = guideTexture.Release();
                cache.pinIds.clear();
                cache.pinIds.reserve(activePins.size());
                for (const auto* pin : activePins) {
                    cache.pinIds.push_back(pin->id);
                }
                cache.retainedBytes = static_cast<std::uint64_t>(proxyWidth) *
                    proxyHeight * (static_cast<std::uint64_t>(pinCount) * 3u + 8u);
            }
            m_LastGraphExecutionStats.colorWarpMaskBuildMs +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - buildStarted).count();
        }
        m_LastGraphExecutionStats.colorWarpMaskQualityLongEdge = qualityLongEdge;
        m_LastGraphExecutionStats.colorWarpMaskBytesRetained =
            m_RawDevelopmentColorWarpMaskCache.retainedBytes;
        if (!useSpatialMasks && m_RawDevelopmentInteractivePreview &&
            compatibleCachedMask) {
            // Keep the last coherent spatial selection on screen while a
            // source/EV/region edit rebuilds its lower-tier interaction mask.
            // Publishing an un-gated direct qualifier here makes every remote
            // color match flash during the drag and then disappear on release.
            useSpatialMasks = true;
        }
        if (!useSpatialMasks && m_RawDevelopmentInteractivePreview) {
            // There is no safe spatial result to show yet. Leave this render
            // unpublished rather than temporarily presenting All Matches for
            // a mode whose defining constraint is spatial membership.
            if (result.owned && result.texture != 0u) {
                glDeleteTextures(1, &result.texture);
            }
            result.texture = 0u;
            result.owned = false;
            m_LastGraphExecutionStats.lastSpecializedFailure =
                "Interactive Color Warp spatial mask is still refining.";
            return false;
        }
        if (!useSpatialMasks && m_RawDevelopmentColorWarpRequireSpatialResult) {
            std::cerr << "[RenderPipeline] Required settled Color Warp spatial mask "
                         "could not be built.\n";
            if (result.owned && result.texture != 0u) {
                glDeleteTextures(1, &result.texture);
            }
            result.texture = 0u;
            result.owned = false;
            m_LastGraphExecutionStats.lastSpecializedFailure =
                "Required settled Color Warp spatial mask could not be built.";
            return false;
        }
    }

    const unsigned int inputTexture = result.texture;
    Stack::Renderer::ScopedGLTexture processed(CreateGraphRenderTargetTexture());
    const bool rendered = RenderIntoGraphTargetTexture(
        processed.Get(),
        [&](unsigned int) {
            while (glGetError() != GL_NO_ERROR) {
            }
            glUseProgram(m_RawDevelopmentColorWarpProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uInputTex"),
                0);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uInputIsRec2020"),
                recipe.technical.workingSpace == Raw::RawWorkingSpace::LinearRec2020D65 ? 1 : 0);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uUseRelativeChroma"),
                colorWarp.version <= 1 ? 1 : 0);
            glUniform1f(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uStrength"),
                colorWarp.strength);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uPinCount"),
                pinCount);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uUseSpatialMasks"),
                useSpatialMasks ? 1 : 0);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, evCurveTexture.Get());
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uEvCurves"),
                5);
            if (useSpatialMasks) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(
                    GL_TEXTURE_2D_ARRAY,
                    m_RawDevelopmentColorWarpMaskCache.gateTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uGateMask"),
                    1);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(
                    GL_TEXTURE_2D_ARRAY,
                    m_RawDevelopmentColorWarpMaskCache.supportTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uSupportMask"),
                    2);
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(
                    GL_TEXTURE_2D_ARRAY,
                    m_RawDevelopmentColorWarpMaskCache.boundaryTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uBoundaryMask"),
                    3);
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(
                    GL_TEXTURE_2D,
                    m_RawDevelopmentColorWarpMaskCache.guideTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uMaskGuide"),
                    4);
                glActiveTexture(GL_TEXTURE0);
            }
            for (int index = 0; index < static_cast<int>(colorWarpPinLimit); ++index) {
                const std::size_t offset = static_cast<std::size_t>(index) * 4u;
                const std::string suffix = "[" + std::to_string(index) + "]";
                glUniform4f(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, ("uPinSourceTarget" + suffix).c_str()),
                    sourceTarget[offset + 0u], sourceTarget[offset + 1u],
                    sourceTarget[offset + 2u], sourceTarget[offset + 3u]);
                glUniform4f(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, ("uPinShape" + suffix).c_str()),
                    shape[offset + 0u], shape[offset + 1u],
                    shape[offset + 2u], shape[offset + 3u]);
                glUniform4f(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, ("uPinLight" + suffix).c_str()),
                    light[offset + 0u], light[offset + 1u],
                    light[offset + 2u], light[offset + 3u]);
                glUniform4f(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, ("uPinQualifier" + suffix).c_str()),
                    qualifier[offset + 0u], qualifier[offset + 1u],
                    qualifier[offset + 2u], qualifier[offset + 3u]);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentColorWarpProgram, ("uPinSpatialSupport" + suffix).c_str()),
                    spatialSupport[static_cast<std::size_t>(index)]);
            }
            glActiveTexture(GL_TEXTURE0);
            m_Quad.Draw();
            glUseProgram(0);
        });
    const GLenum drawError = glGetError();
    if (!rendered || !processed || drawError != GL_NO_ERROR) {
        std::cerr << "[RenderPipeline] RAW Color Warp pass failed; passing "
                     "input texture through.\n";
        if (m_RawDevelopmentColorWarpRequireSpatialResult &&
            hasSpatialRegions) {
            if (result.owned && result.texture != 0u) {
                glDeleteTextures(1, &result.texture);
            }
            result.texture = 0u;
            result.owned = false;
            m_LastGraphExecutionStats.lastSpecializedFailure =
                "Required Color Warp export pass failed.";
        }
        return false;
    }
    if (result.owned && inputTexture != 0) {
        glDeleteTextures(1, &inputTexture);
    }
    result.texture = processed.Release();
    result.owned = true;
    return true;
}

void RenderPipeline::RenderRawDevelopmentOutputCropStage(
    GraphNodeRenderResult& result,
    const Stack::RawRecipe::RawCropRotationRecipe& crop,
    const std::string& cacheKey,
    std::size_t fingerprint) {
    const bool outputCropEnabled =
        crop.cropEnabled &&
        result.texture != 0 &&
        m_Width > 0 &&
        m_Height > 0;
    if (!outputCropEnabled) {
        return;
    }

    const int sourceWidth = m_Width;
    const int sourceHeight = m_Height;
    const float cropX = std::clamp(crop.cropX, 0.0f, 1.0f);
    const float cropY = std::clamp(crop.cropY, 0.0f, 1.0f);
    const float cropWidth = std::clamp(crop.cropWidth, 0.0f, 1.0f - cropX);
    const float cropHeight = std::clamp(crop.cropHeight, 0.0f, 1.0f - cropY);
    const int left = std::clamp(
        static_cast<int>(std::floor(cropX * static_cast<float>(sourceWidth))),
        0,
        sourceWidth - 1);
    const int top = std::clamp(
        static_cast<int>(std::floor(cropY * static_cast<float>(sourceHeight))),
        0,
        sourceHeight - 1);
    const int right = std::clamp(
        static_cast<int>(std::ceil((cropX + cropWidth) * static_cast<float>(sourceWidth))),
        left + 1,
        sourceWidth);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil((cropY + cropHeight) * static_cast<float>(sourceHeight))),
        top + 1,
        sourceHeight);
    const int croppedWidth = right - left;
    const int croppedHeight = bottom - top;
    const bool changesGeometry =
        left != 0 || top != 0 ||
        croppedWidth != sourceWidth || croppedHeight != sourceHeight;
    if (!changesGeometry) {
        return;
    }

    const CachedGraphTexture cached =
        FindRawDevelopStageCacheEntry(cacheKey, fingerprint);
    if (cached.texture != 0 && cached.width > 0 && cached.height > 0) {
        if (result.owned && result.texture != 0 && result.texture != cached.texture) {
            glDeleteTextures(1, &result.texture);
        }
        result.texture = cached.texture;
        result.owned = false;
        m_Width = cached.width;
        m_Height = cached.height;
        ++m_LastGraphExecutionStats.rawStageCacheHits;
        return;
    }

    if (m_RawDevelopmentCropProgram == 0) {
        std::string fragment=kRawOutputCropFrag;
        fragment.insert(fragment.find("void main()"),Stack::Renderer::CoverageSamplingGlsl);
        m_RawDevelopmentCropProgram = GLHelpers::CreateShaderProgram(
            kRawOutputStageVert,
            fragment.c_str());
    }
    if (m_RawDevelopmentCropProgram == 0) {
        std::cerr << "[RenderPipeline] RAW output crop shader compilation failed; "
                     "preserving uncropped output.\n";
        return;
    }

    ++m_LastGraphExecutionStats.rawStageCacheMisses;
    const unsigned int inputTexture = result.texture;
    m_Width = croppedWidth;
    m_Height = croppedHeight;
    Stack::Renderer::ScopedGLTexture croppedTexture(CreateGraphRenderTargetTexture());
    const float originU = static_cast<float>(left) / static_cast<float>(sourceWidth);
    // OpenGL texture V increases upward while cropY is measured downward from
    // the visible image's top edge.
    const float originV =
        static_cast<float>(sourceHeight - bottom) / static_cast<float>(sourceHeight);
    const float extentU =
        static_cast<float>(croppedWidth) / static_cast<float>(sourceWidth);
    const float extentV =
        static_cast<float>(croppedHeight) / static_cast<float>(sourceHeight);
    const bool cropRendered =
        croppedTexture &&
        RenderIntoGraphTargetTexture(
            croppedTexture.Get(),
            [&](unsigned int) {
                glUseProgram(m_RawDevelopmentCropProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, inputTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentCropProgram, "uInputTex"),
                    0);
                glUniform2f(
                    glGetUniformLocation(m_RawDevelopmentCropProgram, "uCropOrigin"),
                    originU,
                    originV);
                glUniform2f(
                    glGetUniformLocation(m_RawDevelopmentCropProgram, "uCropExtent"),
                    extentU,
                    extentV);
                m_Quad.Draw();
                glUseProgram(0);
            });
    if (cropRendered) {
        if (result.owned && inputTexture != 0) {
            glDeleteTextures(1, &inputTexture);
        }
        result.texture = croppedTexture.Release();
        result.owned = true;
        if (StoreRawDevelopStageCacheEntry(
                cacheKey,
                result.texture,
                fingerprint,
                true)) {
            result.owned = false;
        }
        return;
    }

    m_Width = sourceWidth;
    m_Height = sourceHeight;
    std::cerr << "[RenderPipeline] RAW output crop pass failed; preserving "
                 "uncropped output.\n";
}
