#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"

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

void RenderPipeline::DeleteLutTextureEntry(RenderPipeline::CachedGraphTexture& entry) {
    if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
        glDeleteTextures(1, &entry.texture);
    }
    entry.texture = 0;
    entry.owned = false;
    entry.width = 0;
    entry.height = 0;
    entry.fingerprint = 0;
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
    if (stage.size <= 0 || stage.values.empty()) {
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
    if (stage.size <= 0 || stage.values.empty()) {
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
    if (fingerprint == 0 || stage.size <= 0 || stage.values.empty()) {
        return 0;
    }

    auto cacheIt = m_LutTextureCache.find(key);
    if (cacheIt != m_LutTextureCache.end() &&
        cacheIt->second.texture != 0 &&
        cacheIt->second.fingerprint == fingerprint) {
        return cacheIt->second.texture;
    }

    if (cacheIt != m_LutTextureCache.end()) {
        DeleteLutTextureEntry(cacheIt->second);
    }

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB32F,
        stage.size,
        1,
        0,
        GL_RGB,
        GL_FLOAT,
        stage.values.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    CachedGraphTexture entry;
    entry.texture = texture;
    entry.fingerprint = fingerprint;
    entry.width = stage.size;
    entry.height = 1;
    entry.owned = texture != 0;
    m_LutTextureCache[key] = entry;
    return texture;
}

unsigned int RenderPipeline::GetOrCreateLut3DTexture(
    const std::string& key,
    const ColorLut::Lut3DStage& stage,
    std::size_t fingerprint) {
    if (fingerprint == 0 || stage.size <= 0 || stage.values.empty()) {
        return 0;
    }

    auto cacheIt = m_LutTextureCache.find(key);
    if (cacheIt != m_LutTextureCache.end() &&
        cacheIt->second.texture != 0 &&
        cacheIt->second.fingerprint == fingerprint) {
        return cacheIt->second.texture;
    }

    if (cacheIt != m_LutTextureCache.end()) {
        DeleteLutTextureEntry(cacheIt->second);
    }

    unsigned int texture = 0;
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
    glBindTexture(GL_TEXTURE_3D, 0);

    CachedGraphTexture entry;
    entry.texture = texture;
    entry.fingerprint = fingerprint;
    entry.width = stage.size;
    entry.height = stage.size;
    entry.owned = texture != 0;
    m_LutTextureCache[key] = entry;
    return texture;
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
