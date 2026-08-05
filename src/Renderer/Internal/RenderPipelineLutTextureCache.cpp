#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include "Renderer/GLStateGuards.h"

#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

#ifndef GL_RGB32F
#define GL_RGB32F 0x8815
#endif

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif

#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif

using namespace Stack::Renderer::GraphExecution;

namespace {

bool HasFiniteDomain(
    const std::array<float, 3>& domainMin,
    const std::array<float, 3>& domainMax) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (!std::isfinite(domainMin[channel]) ||
            !std::isfinite(domainMax[channel]) ||
            domainMax[channel] <= domainMin[channel]) {
            return false;
        }
    }
    return true;
}

bool HasFiniteValues(const std::vector<float>& values) {
    for (float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

bool IsValidLut1DStage(const ColorLut::Lut1DStage& stage) {
    const std::size_t edge =
        stage.size > 0 ? static_cast<std::size_t>(stage.size) : 0u;
    return edge > 0 &&
        edge <= std::numeric_limits<std::size_t>::max() / 3u &&
        stage.values.size() == edge * 3u &&
        HasFiniteDomain(stage.domainMin, stage.domainMax) &&
        HasFiniteValues(stage.values);
}

bool IsValidLut3DStage(const ColorLut::Lut3DStage& stage) {
    const std::size_t edge =
        stage.size > 0 ? static_cast<std::size_t>(stage.size) : 0u;
    if (edge == 0 ||
        edge > std::numeric_limits<std::size_t>::max() / edge) {
        return false;
    }
    const std::size_t square = edge * edge;
    if (square > std::numeric_limits<std::size_t>::max() / edge) {
        return false;
    }
    const std::size_t cube = square * edge;
    return cube <= std::numeric_limits<std::size_t>::max() / 3u &&
        stage.values.size() == cube * 3u &&
        HasFiniteDomain(stage.domainMin, stage.domainMax) &&
        HasFiniteValues(stage.values);
}

bool SupportsTextureEdge(GLenum limitName, int edge) {
    GLint maximumEdge = 0;
    glGetIntegerv(limitName, &maximumEdge);
    return maximumEdge > 0 && edge <= maximumEdge;
}

std::uint64_t LutTextureBytes(std::size_t valueCount) {
    if (valueCount >
        std::numeric_limits<std::uint64_t>::max() / sizeof(float)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(valueCount) * sizeof(float);
}

} // namespace

void RenderPipeline::DeleteLutTextureEntry(RenderPipeline::CachedGraphTexture& entry) {
    if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
        glDeleteTextures(1, &entry.texture);
    }
    entry.texture = 0;
    entry.owned = false;
    entry.width = 0;
    entry.height = 0;
    entry.fingerprint = 0;
    entry.bytes = 0;
    entry.lastUseSerial = 0;
}

void RenderPipeline::ClearLutTextureKey(const std::string& key) {
    const auto it = m_LutTextureCache.find(key);
    if (it == m_LutTextureCache.end()) {
        return;
    }
    DeleteLutTextureEntry(it->second);
    m_LutTextureCache.erase(it);
}

std::size_t RenderPipeline::HashLut1DStage(const ColorLut::Lut1DStage& stage) {
    if (!IsValidLut1DStage(stage)) {
        return 0;
    }
    std::size_t fingerprint = HashValue(stage.size);
    for (float value : stage.domainMin) {
        HashCombine(fingerprint, HashValue(value));
    }
    for (float value : stage.domainMax) {
        HashCombine(fingerprint, HashValue(value));
    }
    HashCombine(
        fingerprint,
        HashBytes(
            reinterpret_cast<const unsigned char*>(stage.values.data()),
            stage.values.size() * sizeof(float)));
    return fingerprint;
}

std::size_t RenderPipeline::HashLut3DStage(const ColorLut::Lut3DStage& stage) {
    if (!IsValidLut3DStage(stage)) {
        return 0;
    }
    std::size_t fingerprint = HashValue(stage.size);
    for (float value : stage.domainMin) {
        HashCombine(fingerprint, HashValue(value));
    }
    for (float value : stage.domainMax) {
        HashCombine(fingerprint, HashValue(value));
    }
    HashCombine(
        fingerprint,
        HashBytes(
            reinterpret_cast<const unsigned char*>(stage.values.data()),
            stage.values.size() * sizeof(float)));
    return fingerprint;
}

unsigned int RenderPipeline::GetOrCreateLut1DTexture(
    const std::string& key,
    const ColorLut::Lut1DStage& stage,
    std::size_t fingerprint) {
    if (fingerprint == 0 ||
        !IsValidLut1DStage(stage) ||
        !SupportsTextureEdge(GL_MAX_TEXTURE_SIZE, stage.size)) {
        ClearLutTextureKey(key);
        return 0;
    }

    auto cacheIt = m_LutTextureCache.find(key);
    if (cacheIt != m_LutTextureCache.end() &&
        cacheIt->second.texture != 0 &&
        cacheIt->second.fingerprint == fingerprint) {
        TouchGraphCacheEntry(cacheIt->second);
        return cacheIt->second.texture;
    }

    const Stack::Renderer::GLState::TextureBinding savedBinding(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    savedUnpackState.ConfigureTightCpuUpload();
    unsigned int texture = 0;
    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB32F, stage.size, 1);
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, stage.size, 1,
        GL_RGB, GL_FLOAT, stage.values.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const bool uploadSucceeded =
        texture != 0 && glGetError() == GL_NO_ERROR;
    savedUnpackState.Restore();
    savedBinding.Restore();
    if (!uploadSucceeded) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
        return 0;
    }
    Stack::Renderer::ScopedGLTexture textureOwner(texture);

    CachedGraphTexture entry;
    entry.texture = textureOwner.Get();
    entry.fingerprint = fingerprint;
    entry.width = stage.size;
    entry.height = 1;
    entry.owned = texture != 0;
    entry.bytes = LutTextureBytes(stage.values.size());
    TouchGraphCacheEntry(entry);
    if (cacheIt != m_LutTextureCache.end()) {
        DeleteLutTextureEntry(cacheIt->second);
        cacheIt->second = entry;
    } else {
        try {
            m_LutTextureCache.emplace(key, entry);
        } catch (const std::bad_alloc&) {
            return 0;
        } catch (const std::length_error&) {
            return 0;
        }
    }
    return textureOwner.Release();
}

unsigned int RenderPipeline::GetOrCreateLut3DTexture(
    const std::string& key,
    const ColorLut::Lut3DStage& stage,
    std::size_t fingerprint) {
    if (fingerprint == 0 ||
        !IsValidLut3DStage(stage) ||
        !SupportsTextureEdge(GL_MAX_3D_TEXTURE_SIZE, stage.size)) {
        ClearLutTextureKey(key);
        return 0;
    }

    auto cacheIt = m_LutTextureCache.find(key);
    if (cacheIt != m_LutTextureCache.end() &&
        cacheIt->second.texture != 0 &&
        cacheIt->second.fingerprint == fingerprint) {
        TouchGraphCacheEntry(cacheIt->second);
        return cacheIt->second.texture;
    }

    const Stack::Renderer::GLState::TextureBinding savedBinding(
        GL_TEXTURE_3D, GL_TEXTURE_BINDING_3D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    savedUnpackState.ConfigureTightCpuUpload();
    unsigned int texture = 0;
    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_3D, texture);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGB32F, stage.size, stage.size, stage.size);
    glTexSubImage3D(
        GL_TEXTURE_3D,
        0,
        0,
        0,
        0,
        stage.size,
        stage.size,
        stage.size,
        GL_RGB,
        GL_FLOAT,
        stage.values.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    const bool uploadSucceeded =
        texture != 0 && glGetError() == GL_NO_ERROR;
    savedUnpackState.Restore();
    savedBinding.Restore();
    if (!uploadSucceeded) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
        return 0;
    }
    Stack::Renderer::ScopedGLTexture textureOwner(texture);

    CachedGraphTexture entry;
    entry.texture = textureOwner.Get();
    entry.fingerprint = fingerprint;
    entry.width = stage.size;
    entry.height = stage.size;
    entry.owned = texture != 0;
    entry.bytes = LutTextureBytes(stage.values.size());
    TouchGraphCacheEntry(entry);
    if (cacheIt != m_LutTextureCache.end()) {
        DeleteLutTextureEntry(cacheIt->second);
        cacheIt->second = entry;
    } else {
        try {
            m_LutTextureCache.emplace(key, entry);
        } catch (const std::bad_alloc&) {
            return 0;
        } catch (const std::length_error&) {
            return 0;
        }
    }
    return textureOwner.Release();
}

void RenderPipeline::PruneInactiveLutTextureCache(const GraphExecutionContext& executionContext) {
    for (auto it = m_LutTextureCache.begin(); it != m_LutTextureCache.end(); ) {
        const int nodeId = ExtractNodeIdFromCacheKey(it->first);
        if (!executionContext.IsActiveNode(nodeId)) {
            DeleteLutTextureEntry(it->second);
            it = m_LutTextureCache.erase(it);
        } else {
            ++it;
        }
    }
}
