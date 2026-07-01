#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr std::size_t kFnvPrime = 1099511628211ull;

std::size_t HashBytes(const unsigned char* data, std::size_t size) {
    std::size_t hash = kFnvOffsetBasis;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::size_t>(data[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

std::size_t HashBytes(const std::vector<unsigned char>& data) {
    return HashBytes(data.data(), data.size());
}

} // namespace

RenderPipeline::RenderPipeline()
    : m_Width(0), m_Height(0),
      m_SourceChannels(4),
      m_SourceTexture(0), m_PingTexture(0), m_PongTexture(0),
      m_PingFBO(0), m_PongFBO(0), m_OutputTexture(0), m_ExternalOutputTexture(0), m_GraphSourceTexture(0),
      m_MaskProgram(0), m_MaskCombineProgram(0), m_MaskBlendProgram(0), m_MixProgram(0),
      m_MaskUtilityProgram(0), m_ImageToMaskProgram(0), m_ImageGeneratorProgram(0),
      m_DataMathProgram(0), m_ChannelSplitProgram(0), m_ChannelCombineProgram(0), m_LutProgram(0),
      m_HdrMergeProgram(0),
      m_RawDetailFusionAnalysisProgram(0), m_RawDetailFusionMetricsProgram(0), m_RawDetailFusionSmoothProgram(0), m_RawDetailFusionApplyProgram(0),
      m_AutoGainStatsProgram(0), m_RawDevelopmentToneCurveProgram(0), m_RawDevelopmentLocalRangeProgram(0),
      m_RawDevelopmentLocalRangeOverlayProgram(0)
{}

RenderPipeline::~RenderPipeline() {
    CleanupFBOs();
    InvalidateGraphCaches();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    if (m_ExternalOutputTexture) glDeleteTextures(1, &m_ExternalOutputTexture);
    if (m_MaskProgram) glDeleteProgram(m_MaskProgram);
    if (m_MaskCombineProgram) glDeleteProgram(m_MaskCombineProgram);
    if (m_MaskBlendProgram) glDeleteProgram(m_MaskBlendProgram);
    if (m_MixProgram) glDeleteProgram(m_MixProgram);
    if (m_MaskUtilityProgram) glDeleteProgram(m_MaskUtilityProgram);
    if (m_ImageToMaskProgram) glDeleteProgram(m_ImageToMaskProgram);
    if (m_ImageGeneratorProgram) glDeleteProgram(m_ImageGeneratorProgram);
    if (m_DataMathProgram) glDeleteProgram(m_DataMathProgram);
    if (m_ChannelSplitProgram) glDeleteProgram(m_ChannelSplitProgram);
    if (m_ChannelCombineProgram) glDeleteProgram(m_ChannelCombineProgram);
    if (m_LutProgram) glDeleteProgram(m_LutProgram);
    if (m_HdrMergeProgram) glDeleteProgram(m_HdrMergeProgram);
    if (m_RawDetailFusionAnalysisProgram) glDeleteProgram(m_RawDetailFusionAnalysisProgram);
    if (m_RawDetailFusionMetricsProgram) glDeleteProgram(m_RawDetailFusionMetricsProgram);
    if (m_RawDetailFusionSmoothProgram) glDeleteProgram(m_RawDetailFusionSmoothProgram);
    if (m_RawDetailFusionApplyProgram) glDeleteProgram(m_RawDetailFusionApplyProgram);
    if (m_AutoGainStatsProgram) glDeleteProgram(m_AutoGainStatsProgram);
    if (m_RawDevelopmentToneCurveProgram) glDeleteProgram(m_RawDevelopmentToneCurveProgram);
    if (m_RawDevelopmentLocalRangeProgram) glDeleteProgram(m_RawDevelopmentLocalRangeProgram);
    if (m_RawDevelopmentLocalRangeOverlayProgram) glDeleteProgram(m_RawDevelopmentLocalRangeOverlayProgram);
    ClearRawDevelopmentLocalRangeOverlay();
}

void RenderPipeline::Initialize() {
    m_Quad.Initialize();
}

void RenderPipeline::CleanupFBOs() {
    if (m_PingFBO)     { glDeleteFramebuffers(1, &m_PingFBO);  m_PingFBO = 0; }
    if (m_PongFBO)     { glDeleteFramebuffers(1, &m_PongFBO);  m_PongFBO = 0; }
    if (m_PingTexture) { glDeleteTextures(1, &m_PingTexture); m_PingTexture = 0; }
    if (m_PongTexture) { glDeleteTextures(1, &m_PongTexture); m_PongTexture = 0; }
}

void RenderPipeline::InvalidateGraphCaches() {
    DestroyGraphCache(m_GraphImageCache);
    DestroyGraphCache(m_GraphMaskCache);
    DestroyGraphCache(m_LutTextureCache);
    DestroyRawDevelopStageCache();
    m_LastGraphImageCacheHits.clear();
    m_AutoGainSceneStatsCache.clear();
}

void RenderPipeline::Resize(int width, int height) {
    if (width == m_Width && height == m_Height) return;
    m_Width = width;
    m_Height = height;

    CleanupFBOs();

    m_PingTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    m_PongTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    m_PingFBO = GLHelpers::CreateFBO(m_PingTexture);
    m_PongFBO = GLHelpers::CreateFBO(m_PongTexture);
}

bool RenderPipeline::LoadSourceImage(const std::string& filepath) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(filepath.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::cerr << "[RenderPipeline] Failed to load image: " << filepath
                  << " (" << stbi_failure_reason() << ")\n";
        return false;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = GLHelpers::CreateTextureFromPixels(data, w, h, 4);
    m_SourceChannels = 4;
    m_SourcePixelsShared.reset();
    m_SourcePixels.assign(data, data + (w * h * 4));
    m_SourceFingerprint = HashBytes(m_SourcePixels);
    stbi_image_free(data);

    Resize(w, h);

    std::cout << "[RenderPipeline] Loaded image " << w << "x" << h << " from: " << filepath << "\n";
    return true;
}

void RenderPipeline::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    const int clampedChannels = std::max(1, ch);
    int targetWidth = w;
    int targetHeight = h;
    if (!data && m_PreviewMaxDimension > 0 && targetWidth > 0 && targetHeight > 0) {
        const int longestSide = std::max(targetWidth, targetHeight);
        if (longestSide > m_PreviewMaxDimension) {
            targetWidth = std::max(1, static_cast<int>(
                (static_cast<long long>(targetWidth) * m_PreviewMaxDimension + longestSide / 2) / longestSide));
            targetHeight = std::max(1, static_cast<int>(
                (static_cast<long long>(targetHeight) * m_PreviewMaxDimension + longestSide / 2) / longestSide));
        }
    }
    const std::size_t pixelCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const std::size_t incomingSize = data ? (pixelCount * static_cast<std::size_t>(clampedChannels)) : 0;
    const std::size_t incomingFingerprint = (!data || incomingSize == 0) ? 0 : HashBytes(data, incomingSize);
    if (m_SourceTexture != 0 &&
        m_Width == targetWidth &&
        m_Height == targetHeight &&
        m_SourceChannels == ch &&
        m_SourceFingerprint == incomingFingerprint &&
        m_SourcePixels.size() == incomingSize) {
        return;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = data ? GLHelpers::CreateTextureFromPixels(data, w, h, ch) : 0;
    m_SourceChannels = ch;
    m_SourcePixelsShared.reset();
    if (data && incomingSize > 0) {
        m_SourcePixels.assign(data, data + incomingSize);
    } else {
        m_SourcePixels.clear();
    }
    m_SourceFingerprint = incomingFingerprint;
    Resize(targetWidth, targetHeight);
}

void RenderPipeline::LoadSourceFromSharedPixels(const SharedPixelBuffer& data, int w, int h, int ch) {
    const int clampedChannels = std::max(1, ch);
    int targetWidth = w;
    int targetHeight = h;
    if (data.empty() && m_PreviewMaxDimension > 0 && targetWidth > 0 && targetHeight > 0) {
        const int longestSide = std::max(targetWidth, targetHeight);
        if (longestSide > m_PreviewMaxDimension) {
            targetWidth = std::max(1, static_cast<int>(
                (static_cast<long long>(targetWidth) * m_PreviewMaxDimension + longestSide / 2) / longestSide));
            targetHeight = std::max(1, static_cast<int>(
                (static_cast<long long>(targetHeight) * m_PreviewMaxDimension + longestSide / 2) / longestSide));
        }
    }

    const std::size_t incomingFingerprint =
        data.fingerprint != 0
            ? data.fingerprint
            : (data.empty() ? 0 : StackHash::HashBytes(*data.bytes));
    if (m_SourceTexture != 0 &&
        m_Width == targetWidth &&
        m_Height == targetHeight &&
        m_SourceChannels == ch &&
        m_SourceFingerprint == incomingFingerprint &&
        m_SourcePixelsShared == data.bytes &&
        m_SourcePixels.empty()) {
        return;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) {
        glDeleteTextures(1, &m_SourceTexture);
    }
    m_SourceTexture = !data.empty()
        ? GLHelpers::CreateTextureFromPixels(data.data(), w, h, ch)
        : 0;
    m_SourceChannels = ch;
    m_SourcePixels.clear();
    m_SourcePixelsShared = data.bytes;
    m_SourceFingerprint = incomingFingerprint;
    Resize(targetWidth, targetHeight);
}

void RenderPipeline::Clear() {
    if (m_SourceTexture) {
        glDeleteTextures(1, &m_SourceTexture);
        m_SourceTexture = 0;
    }
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_OutputTexture = 0;
    m_GraphSourceTexture = 0;
    m_SourcePixelsShared.reset();
    m_SourcePixels.clear();
    m_SourceFingerprint = 0;
    m_SourceChannels = 4;
    m_Width = 0;
    m_Height = 0;
    CleanupFBOs();
    InvalidateGraphCaches();
    m_RawPipelines.clear();
    m_RawDataCache.clear();
    m_RawDataCachePaths.clear();
    m_RawPreviewDataCache.clear();
    m_RawPreviewDataCacheKeys.clear();
    ClearRawDevelopmentLocalRangeOverlay();
    ClearRawDevelopmentLocalRangeTargetSample();
    m_RawDevelopmentViewTransformInputStats = {};
    m_RawDevelopmentLocalSuggestionImage = {};
}

void RenderPipeline::ClearRawDevelopmentLocalRangeOverlay() {
    if (m_RawDevelopmentLocalRangeOverlayTexture != 0) {
        glDeleteTextures(1, &m_RawDevelopmentLocalRangeOverlayTexture);
        m_RawDevelopmentLocalRangeOverlayTexture = 0;
    }
    m_RawDevelopmentLocalRangeOverlayWidth = 0;
    m_RawDevelopmentLocalRangeOverlayHeight = 0;
    m_RawDevelopmentLocalRangeOverlayMode.clear();
}

void RenderPipeline::ClearRawDevelopmentLocalRangeTargetSample() {
    m_RawDevelopmentLocalRangeTargetSampleRequested = false;
    m_RawDevelopmentLocalRangeTargetSampleRequestU = 0.0f;
    m_RawDevelopmentLocalRangeTargetSampleRequestV = 0.0f;
    m_RawDevelopmentLocalRangeTargetSampleValid = false;
    m_RawDevelopmentLocalRangeTargetSampleSceneEv = 0.0f;
    m_RawDevelopmentLocalRangeTargetSampleSceneLuma = 0.0f;
    m_RawDevelopmentLocalRangeTargetSampleSceneR = 0.0f;
    m_RawDevelopmentLocalRangeTargetSampleSceneG = 0.0f;
    m_RawDevelopmentLocalRangeTargetSampleSceneB = 0.0f;
    m_RawDevelopmentLocalRangeTargetSampleU = 0.0f;
    m_RawDevelopmentLocalRangeTargetSampleV = 0.0f;
}

void RenderPipeline::ClearOutput() {
    m_RawDevelopmentViewTransformInputStats = {};
    m_RawDevelopmentLocalSuggestionImage = {};
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_OutputTexture = 0;
    m_GraphSourceTexture = 0;
}

unsigned int RenderPipeline::TakeExternalOutputTexture(int& outW, int& outH) {
    outW = 0;
    outH = 0;
    const unsigned int texture = m_ExternalOutputTexture;
    if (texture == 0) {
        m_OutputTexture = 0;
        m_GraphSourceTexture = 0;
        return 0;
    }

    outW = m_Width;
    outH = m_Height;
    m_ExternalOutputTexture = 0;
    if (m_OutputTexture == texture) {
        m_OutputTexture = 0;
    }
    m_GraphSourceTexture = 0;
    return texture;
}

void RenderPipeline::UploadOutputFromPixels(const unsigned char* data, int w, int h, int ch) {
    if (!data || w <= 0 || h <= 0) {
        ClearOutput();
        return;
    }
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_ExternalOutputTexture = GLHelpers::CreateTextureFromPixels(data, w, h, ch);
    m_OutputTexture = m_ExternalOutputTexture;
    m_Width = w;
    m_Height = h;
}

void RenderPipeline::AdoptExternalOutputTexture(unsigned int texture, int w, int h) {
    if (m_ExternalOutputTexture && m_ExternalOutputTexture != texture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
    }
    m_ExternalOutputTexture = texture;
    m_OutputTexture = texture;
    m_Width = w;
    m_Height = h;
}

unsigned int RenderPipeline::PublishSharedOutputTexture(int& outW, int& outH) {
    outW = m_Width;
    outH = m_Height;
    if (m_OutputTexture == 0 || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    const unsigned int publishedTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    if (publishedTexture == 0) {
        return 0;
    }

    GLint prevReadFBO = 0;
    GLint prevDrawFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    unsigned int srcFBO = 0;
    unsigned int dstFBO = 0;
    glGenFramebuffers(1, &srcFBO);
    glGenFramebuffers(1, &dstFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, publishedTexture, 0);
    glBlitFramebuffer(
        0, 0, m_Width, m_Height,
        0, 0, m_Width, m_Height,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &dstFBO);
    return publishedTexture;
}
