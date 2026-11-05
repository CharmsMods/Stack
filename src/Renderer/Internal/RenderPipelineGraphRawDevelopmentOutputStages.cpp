#include "Renderer/ColorWarpShader.h"
#include "Renderer/RenderPipeline.h"

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawColorWarpMask.h"
#include "Raw/RawColorWarpSelection.h"
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


template <typename T>
void HashColorWarpMaskValue(std::size_t& seed, const T& value) {
    seed ^= std::hash<T> {}(value) + 0x9e3779b97f4a7c15ull +
        (seed << 6u) + (seed >> 2u);
}

std::size_t BuildColorWarpMaskKey(
    const Stack::RawRecipe::RawColorWarpRecipe& colorWarp,
    std::size_t inputKey,
    int qualityLongEdge,
    int width,
    int height) {
    std::size_t seed = inputKey;
    HashColorWarpMaskValue(seed, qualityLongEdge);
    HashColorWarpMaskValue(seed, width);
    HashColorWarpMaskValue(seed, height);
    HashColorWarpMaskValue(seed, Stack::RawRecipe::ColorWarpSelectionFingerprint(colorWarp));
    return seed == 0u ? 1u : seed;
}

} // namespace

bool RenderPipeline::RenderRawDevelopmentColorWarpStage(
    GraphNodeRenderResult& result,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    std::size_t inputFingerprint, int sourceWidth, int sourceHeight,
    const Raw::ViewportRegion& inputRegion) {
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
            Stack::Renderer::ColorWarp::Fragment);
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
    std::vector<std::array<float,4>> params,adjustments,hueRanges,saturationRanges,exposureRanges;
    std::vector<float> spatialSupport;
    std::vector<const Stack::RawRecipe::RawColorWarpPin*> spatialPins;
    const auto packedRange=[](const Stack::RawRecipe::RawColorWarpRange& r) -> std::array<float,4> {
        return r.enabled?std::array<float,4>{r.low-r.featherLow,r.low,r.high,r.high+r.featherHigh}:
            std::array<float,4>{-1e20f,-1e20f,1e20f,1e20f};
    };
    for(const auto& pin:colorWarp.pins) {
        if(params.size()>=colorWarpPinLimit)break;
        // Keep the selection-field layer order stable when adjustments become
        // zero or a selection is disabled. Their fields remain reusable.
        params.push_back({Stack::RawRecipe::ColorWarpHue(pin.sourceA,pin.sourceB),
            pin.enabled?pin.strength:0.f,pin.protectColor?1.f:0.f,
            Stack::RawRecipe::ColorWarpParticipates(pin)?1.f:0.f});
        const auto region=std::find_if(colorWarp.regions.begin(),colorWarp.regions.end(),[&](const auto& r){return r.id==pin.regionId;});
        const bool spatial=region!=colorWarp.regions.end() && region->spatialMode!=Stack::RawRecipe::RawColorWarpSpatialMode::AllMatches;
        adjustments.push_back({pin.hueShift,pin.saturation,pin.exposureEv,spatial?float(spatialPins.size()+1):0.f});
        hueRanges.push_back(packedRange(pin.hueRange));
        saturationRanges.push_back(packedRange(pin.saturationRange));
        exposureRanges.push_back(packedRange(pin.exposureRange));
        spatialSupport.push_back(Stack::RawRecipe::ColorWarpPinSpatialSupport(colorWarp,pin));
        if(spatial)spatialPins.push_back(&pin);
    }
    const int pinCount=static_cast<int>(params.size());
    const int layerCount=static_cast<int>(spatialPins.size());
    if(pinCount==0)return true;

    const bool hasSpatialRegions = std::any_of(
        spatialPins.begin(),
        spatialPins.end(),
        [&](const Stack::RawRecipe::RawColorWarpPin* pin) {
            if(!Stack::RawRecipe::ColorWarpParticipates(*pin))return false;
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
        std::size_t inputKey = inputFingerprint;
        HashColorWarpMaskValue(inputKey, static_cast<int>(recipe.technical.workingSpace));
        HashColorWarpMaskValue(inputKey, sourceWidth);
        HashColorWarpMaskValue(inputKey, sourceHeight);
        HashColorWarpMaskValue(inputKey, m_Width);
        HashColorWarpMaskValue(inputKey, m_Height);
        HashColorWarpMaskValue(inputKey, inputRegion.Fingerprint());
        const std::size_t maskKey = BuildColorWarpMaskKey(
            colorWarp,
            inputKey,
            qualityLongEdge,
            m_Width,
            m_Height);
        auto& cache = m_RawDevelopmentColorWarpMaskCache;
        const bool cachedLayersMatch =
            cache.layers == layerCount &&
            cache.pinIds.size() == spatialPins.size() &&
            std::equal(
                spatialPins.begin(), spatialPins.end(), cache.pinIds.begin(),
                [](const Stack::RawRecipe::RawColorWarpPin* pin,
                   const std::string& cachedId) {
                    return pin != nullptr && pin->id == cachedId;
                });
        const bool compatibleCachedMask =
            inputFingerprint != 0u && cache.inputKey == inputKey &&
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
            guide.sourceWidth = sourceWidth > 0 ? sourceWidth : m_Width;
            guide.sourceHeight = sourceHeight > 0 ? sourceHeight : m_Height;
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
                gateLayers.reserve(spatialPins.size());
                supportLayers.reserve(spatialPins.size());
                boundaryLayers.reserve(spatialPins.size());
                for (const auto* pin : spatialPins) {
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
            if (proxyReady && gateLayers.size() == spatialPins.size()) {
                gateTexture.Reset(GLHelpers::CreateTextureArray(
                    proxyWidth, proxyHeight, layerCount, GL_R8));
                supportTexture.Reset(GLHelpers::CreateTextureArray(
                    proxyWidth, proxyHeight, layerCount, GL_R8));
                boundaryTexture.Reset(GLHelpers::CreateTextureArray(
                    proxyWidth, proxyHeight, layerCount, GL_R8));
                guideTexture.Reset(GLHelpers::CreateEmptyTexture(proxyWidth, proxyHeight));
                if (gateTexture && supportTexture && boundaryTexture && guideTexture) {
                    for (int layer = 0; layer < layerCount; ++layer) {
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
                cache.inputKey = inputKey;
                cache.width = proxyWidth;
                cache.height = proxyHeight;
                cache.layers = layerCount;
                cache.gateTexture = gateTexture.Release();
                cache.supportTexture = supportTexture.Release();
                cache.boundaryTexture = boundaryTexture.Release();
                cache.guideTexture = guideTexture.Release();
                cache.pinIds.clear();
                cache.pinIds.reserve(spatialPins.size());
                for (const auto* pin : spatialPins) {
                    cache.pinIds.push_back(pin->id);
                }
                cache.retainedBytes = static_cast<std::uint64_t>(proxyWidth) *
                    proxyHeight * (static_cast<std::uint64_t>(layerCount) * 3u + 8u);
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
            // qualifier/EV/region edit rebuilds its interaction mask. Input,
            // working space and sample geometry must still match.
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
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uBlendOverlaps"),
                colorWarp.combine==Stack::RawRecipe::RawColorWarpCombine::Blend ? 1 : 0);
            glUniform1f(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uStrength"),
                colorWarp.strength);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uPinCount"),
                pinCount);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentColorWarpProgram, "uUseSpatialMasks"),
                useSpatialMasks ? 1 : 0);
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
            const auto upload=[&](const char* name,const auto& values) {
                glUniform4fv(glGetUniformLocation(m_RawDevelopmentColorWarpProgram,name),pinCount,values.front().data());
            };
            upload("uPinParams",params);upload("uPinAdjust",adjustments);
            upload("uHueRange",hueRanges);upload("uSaturationRange",saturationRanges);upload("uExposureRange",exposureRanges);
            glUniform1fv(glGetUniformLocation(m_RawDevelopmentColorWarpProgram,"uPinSpatialSupport"),pinCount,spatialSupport.data());
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
        result.denoisePending = cached.denoisePending;
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
                true,
                result.denoisePending)) {
            result.owned = false;
        }
        return;
    }

    m_Width = sourceWidth;
    m_Height = sourceHeight;
    std::cerr << "[RenderPipeline] RAW output crop pass failed; preserving "
                 "uncropped output.\n";
}
