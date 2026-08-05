#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PixelBufferUtils.h"

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
      m_BaseCanvasWidth(0), m_BaseCanvasHeight(0),
      m_SourceChannels(4),
      m_SourceTexture(0), m_PingTexture(0), m_PongTexture(0),
      m_PingFBO(0), m_PongFBO(0), m_OutputTexture(0), m_ExternalOutputTexture(0), m_GraphSourceTexture(0),
      m_MaskProgram(0), m_MaskCombineProgram(0), m_MaskBlendProgram(0), m_MixProgram(0),
      m_MaskUtilityProgram(0), m_ImageToMaskProgram(0), m_ImageGeneratorProgram(0),
      m_DataMathProgram(0), m_TechnicalImageProgram(0), m_ReformatProgram(0),
      m_SpectrumViewProgram(0), m_FrequencyMaskProgram(0), m_SpectrumMathProgram(0),
      m_MagnitudePhaseProgram(0), m_FrequencyIfftProjectProgram(0),
      m_ChannelSplitProgram(0), m_ChannelCombineProgram(0), m_LutProgram(0),
      m_HdrMergeProgram(0),
      m_RawDetailFusionAnalysisProgram(0), m_RawDetailFusionMetricsProgram(0), m_RawDetailFusionSmoothProgram(0), m_RawDetailFusionApplyProgram(0),
      m_AutoGainStatsProgram(0), m_RawDevelopmentToneCurveProgram(0), m_RawDevelopmentLocalRangeProgram(0),
      m_RawDevelopmentLocalRangeOverlayProgram(0),
      m_RawDevelopmentLocalRangeQualifierProgram(0),
      m_RawDevelopmentRgbDenoiseConvertProgram(0),
      m_RawDevelopmentRgbDenoiseBlurProgram(0),
      m_RawDevelopmentRgbDenoiseBandProgram(0),
      m_RawDevelopmentRgbDenoiseReconstructProgram(0),
      m_RawDevelopmentExposureProgram(0)
{}

RenderPipeline::~RenderPipeline() {
    Shutdown();
}

void RenderPipeline::Shutdown() {
    if (m_Shutdown) {
        return;
    }
    m_Shutdown = true;

    if (m_RestormerAsyncCancel) {
        m_RestormerAsyncCancel->store(true, std::memory_order_relaxed);
    }
    if (m_RestormerAsyncFuture.valid()) {
        try {
            m_RestormerAsyncFuture.wait();
            (void)m_RestormerAsyncFuture.get();
        } catch (...) {
            // Shutdown must remain noexcept even if an asynchronous provider
            // surfaced an exception through its future.
        }
    }
    m_RestormerAsyncPending = false;
    m_RestormerAsyncModelFingerprint = 0;
    m_RestormerAsyncApplicationFingerprint = 0;
    m_RestormerAsyncCancel.reset();

    CleanupFBOs();
    InvalidateGraphCaches();
    DestroyGraphTransientTargets();
    DestroyPointwiseProgramCache();
    // Recipe-layer instances own GL programs (and Tone Curve owns its LUT).
    // Release them while the renderer's GL context is still current.
    m_RawDevelopmentRecipeLayerCache.clear();
    ClearRawDevelopmentLocalRangeOverlay();
    ClearRawDevelopmentLocalRangeSelectionBits();
    ClearRawDevelopmentLocalRangeTargetPreviewSelection();

    for (auto& [nodeId, rawPipeline] : m_RawPipelines) {
        (void)nodeId;
        rawPipeline.Clear();
    }
    m_RawPipelines.clear();
    m_RawDataCache.clear();
    m_RawDataCachePaths.clear();
    m_RawPreviewDataCache.clear();
    m_RawPreviewDataCacheKeys.clear();

    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    if (m_ExternalOutputTexture) glDeleteTextures(1, &m_ExternalOutputTexture);
    m_SourceTexture = 0;
    m_ExternalOutputTexture = 0;
    m_OutputTexture = 0;
    m_GraphSourceTexture = 0;

    const auto deleteProgram = [](unsigned int& program) {
        if (program) {
            glDeleteProgram(program);
            program = 0;
        }
    };
    deleteProgram(m_MaskProgram);
    deleteProgram(m_MaskCombineProgram);
    deleteProgram(m_MaskBlendProgram);
    deleteProgram(m_MixProgram);
    deleteProgram(m_MaskUtilityProgram);
    deleteProgram(m_ImageToMaskProgram);
    deleteProgram(m_ImageGeneratorProgram);
    deleteProgram(m_DataMathProgram);
    deleteProgram(m_TechnicalImageProgram);
    deleteProgram(m_ReformatProgram);
    deleteProgram(m_SpectrumViewProgram);
    deleteProgram(m_FrequencyMaskProgram);
    deleteProgram(m_SpectrumMathProgram);
    deleteProgram(m_MagnitudePhaseProgram);
    deleteProgram(m_FrequencyIfftProjectProgram);
    deleteProgram(m_ChannelSplitProgram);
    deleteProgram(m_ChannelCombineProgram);
    deleteProgram(m_LutProgram);
    deleteProgram(m_HdrMergeProgram);
    deleteProgram(m_RawDetailFusionAnalysisProgram);
    deleteProgram(m_RawDetailFusionMetricsProgram);
    deleteProgram(m_RawDetailFusionSmoothProgram);
    deleteProgram(m_RawDetailFusionApplyProgram);
    deleteProgram(m_AutoGainStatsProgram);
    deleteProgram(m_RawDevelopmentToneCurveProgram);
    deleteProgram(m_RawDevelopmentLocalRangeProgram);
    deleteProgram(m_RawDevelopmentLocalRangeOverlayProgram);
    deleteProgram(m_RawDevelopmentLocalRangeQualifierProgram);
    deleteProgram(m_RawDevelopmentRgbDenoiseConvertProgram);
    deleteProgram(m_RawDevelopmentRgbDenoiseBlurProgram);
    deleteProgram(m_RawDevelopmentRgbDenoiseBandProgram);
    deleteProgram(m_RawDevelopmentRgbDenoiseReconstructProgram);
    deleteProgram(m_RawDevelopmentExposureProgram);

    m_GpuFft.Shutdown();
    m_Quad.Shutdown();
    m_Width = 0;
    m_Height = 0;
    m_BaseCanvasWidth = 0;
    m_BaseCanvasHeight = 0;
    m_GraphSourceWidth = 0;
    m_GraphSourceHeight = 0;
    m_SourceChannels = 4;
    m_SourcePixels.clear();
    m_SourcePixelsShared.reset();
    m_SourceFingerprint = 0;
}

void RenderPipeline::Initialize() {
    m_Shutdown = false;
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
    DestroyFrequencyCache();
    m_GraphScalarCache.clear();
    DestroyGraphCache(m_LutTextureCache);
    DestroyRawDevelopStageCache();
    m_LastGraphImageCacheHits.clear();
    m_AutoGainSceneStatsCache.clear();
}

bool RenderPipeline::Resize(int width, int height) {
    if (width == m_BaseCanvasWidth && height == m_BaseCanvasHeight &&
        m_PingTexture != 0 && m_PongTexture != 0 &&
        m_PingFBO != 0 && m_PongFBO != 0) {
        m_Width = m_BaseCanvasWidth;
        m_Height = m_BaseCanvasHeight;
        return true;
    }
    CleanupFBOs();
    DestroyGraphTransientTargets();

    m_Width = 0;
    m_Height = 0;
    m_BaseCanvasWidth = 0;
    m_BaseCanvasHeight = 0;
    if (width <= 0 || height <= 0) {
        return false;
    }

    m_PingTexture = GLHelpers::CreateEmptyTexture(width, height);
    m_PongTexture = GLHelpers::CreateEmptyTexture(width, height);
    m_PingFBO = GLHelpers::CreateFBO(m_PingTexture);
    m_PongFBO = GLHelpers::CreateFBO(m_PongTexture);
    if (m_PingTexture == 0 || m_PongTexture == 0 ||
        m_PingFBO == 0 || m_PongFBO == 0) {
        std::cerr << "[RenderPipeline] Failed to allocate render targets for "
                  << width << "x" << height << ".\n";
        CleanupFBOs();
        return false;
    }

    m_Width = width;
    m_Height = height;
    m_BaseCanvasWidth = width;
    m_BaseCanvasHeight = height;
    return true;
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

    std::size_t decodedByteCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            w, h, 4, decodedByteCount)) {
        std::cerr << "[RenderPipeline] Rejected invalid decoded image dimensions: "
                  << w << "x" << h << "\n";
        stbi_image_free(data);
        return false;
    }

    std::vector<unsigned char> decodedPixels;
    if (!Stack::PixelBuffer::CopyInterleavedPixels(
            data, w, h, 4, decodedPixels)) {
        std::cerr << "[RenderPipeline] Failed to copy decoded image pixels.\n";
        stbi_image_free(data);
        return false;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) {
        glDeleteTextures(1, &m_SourceTexture);
        m_SourceTexture = 0;
    }
    std::vector<unsigned char>().swap(m_SourcePixels);
    m_SourcePixelsShared.reset();
    if (!Resize(w, h)) {
        stbi_image_free(data);
        Clear();
        return false;
    }

    const unsigned int sourceTexture =
        GLHelpers::CreateTextureFromPixels(data, w, h, 4);
    stbi_image_free(data);
    if (sourceTexture == 0) {
        Clear();
        return false;
    }

    m_SourceTexture = sourceTexture;
    m_SourceChannels = 4;
    m_SourcePixelsShared.reset();
    m_SourcePixels = std::move(decodedPixels);
    m_SourceFingerprint = HashBytes(m_SourcePixels);

    std::cout << "[RenderPipeline] Loaded image " << w << "x" << h << " from: " << filepath << "\n";
    return true;
}

void RenderPipeline::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    std::size_t incomingSize = 0;
    if (w <= 0 || h <= 0 ||
        !Stack::PixelBuffer::IsSupportedInterleavedChannelCount(ch) ||
        (data != nullptr &&
         !Stack::PixelBuffer::TryComputePixelByteCount(
             w, h, ch, incomingSize))) {
        std::cerr << "[RenderPipeline] Rejected invalid source pixel layout: "
                  << w << "x" << h << "x" << ch << "\n";
        Clear();
        return;
    }
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
    if (!data) {
        incomingSize = 0;
    }
    const std::size_t incomingFingerprint = (!data || incomingSize == 0) ? 0 : HashBytes(data, incomingSize);
    const bool sourceTextureStateMatches =
        data ? m_SourceTexture != 0 : m_SourceTexture == 0;
    if (sourceTextureStateMatches &&
        m_BaseCanvasWidth == targetWidth &&
        m_BaseCanvasHeight == targetHeight &&
        m_SourceChannels == ch &&
        m_SourceFingerprint == incomingFingerprint &&
        m_SourcePixels.size() == incomingSize) {
        m_Width = m_BaseCanvasWidth;
        m_Height = m_BaseCanvasHeight;
        return;
    }

    std::vector<unsigned char> copiedPixels;
    if (data && !Stack::PixelBuffer::CopyInterleavedPixels(
            data, w, h, ch, copiedPixels)) {
        std::cerr << "[RenderPipeline] Failed to copy source pixel buffer.\n";
        Clear();
        return;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) {
        glDeleteTextures(1, &m_SourceTexture);
        m_SourceTexture = 0;
    }
    std::vector<unsigned char>().swap(m_SourcePixels);
    m_SourcePixelsShared.reset();
    if (!Resize(targetWidth, targetHeight)) {
        Clear();
        return;
    }

    const unsigned int sourceTexture =
        data ? GLHelpers::CreateTextureFromPixels(data, w, h, ch) : 0;
    if (data && sourceTexture == 0) {
        Clear();
        return;
    }

    m_SourceTexture = sourceTexture;
    m_SourceChannels = ch;
    m_SourcePixelsShared.reset();
    m_SourcePixels = std::move(copiedPixels);
    m_SourceFingerprint = incomingFingerprint;
}

void RenderPipeline::LoadSourceFromSharedPixels(const SharedPixelBuffer& data, int w, int h, int ch) {
    if (w <= 0 || h <= 0 ||
        !Stack::PixelBuffer::IsSupportedInterleavedChannelCount(ch) ||
        (!data.empty() &&
         !Stack::PixelBuffer::HasCompletePixelBuffer(
             data.size(), w, h, ch))) {
        std::cerr << "[RenderPipeline] Rejected incomplete shared source buffer: "
                  << w << "x" << h << "x" << ch
                  << " with " << data.size() << " bytes\n";
        Clear();
        return;
    }
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
    const bool sourceTextureStateMatches =
        data.empty() ? m_SourceTexture == 0 : m_SourceTexture != 0;
    if (sourceTextureStateMatches &&
        m_BaseCanvasWidth == targetWidth &&
        m_BaseCanvasHeight == targetHeight &&
        m_SourceChannels == ch &&
        m_SourceFingerprint == incomingFingerprint &&
        m_SourcePixelsShared == data.bytes &&
        m_SourcePixels.empty()) {
        m_Width = m_BaseCanvasWidth;
        m_Height = m_BaseCanvasHeight;
        return;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) {
        glDeleteTextures(1, &m_SourceTexture);
        m_SourceTexture = 0;
    }
    std::vector<unsigned char>().swap(m_SourcePixels);
    m_SourcePixelsShared.reset();
    if (!Resize(targetWidth, targetHeight)) {
        Clear();
        return;
    }

    const unsigned int sourceTexture = !data.empty()
        ? GLHelpers::CreateTextureFromPixels(data.data(), w, h, ch)
        : 0;
    if (!data.empty() && sourceTexture == 0) {
        Clear();
        return;
    }
    m_SourceTexture = sourceTexture;
    m_SourceChannels = ch;
    m_SourcePixels.clear();
    m_SourcePixelsShared = data.bytes;
    m_SourceFingerprint = incomingFingerprint;
}

SharedPixelBuffer RenderPipeline::ShareSourcePixels(
    int& outW,
    int& outH,
    int& outChannels) {
    outW = 0;
    outH = 0;
    outChannels = 4;
    if (m_SourceTexture == 0 ||
        m_BaseCanvasWidth <= 0 ||
        m_BaseCanvasHeight <= 0 ||
        GetSourcePixelsRaw().empty()) {
        return {};
    }

    if (!m_SourcePixelsShared) {
        m_SourcePixelsShared =
            std::make_shared<std::vector<unsigned char>>(std::move(m_SourcePixels));
        m_SourcePixels.clear();
    }

    outW = m_BaseCanvasWidth;
    outH = m_BaseCanvasHeight;
    outChannels = m_SourceChannels;
    return MakeSharedPixelBufferAlias(
        m_SourcePixelsShared,
        m_SourceFingerprint);
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
    m_BaseCanvasWidth = 0;
    m_BaseCanvasHeight = 0;
    m_GraphSourceWidth = 0;
    m_GraphSourceHeight = 0;
    CleanupFBOs();
    DestroyGraphTransientTargets();
    InvalidateGraphCaches();
    m_RawPipelines.clear();
    m_RawDataCache.clear();
    m_RawDataCachePaths.clear();
    m_RawPreviewDataCache.clear();
    m_RawPreviewDataCacheKeys.clear();
    ClearRawDevelopmentStageStatsReadbacks();
    ClearRawDevelopmentLocalRangeOverlay();
    ClearRawDevelopmentLocalRangeSelectionBits();
    ClearRawDevelopmentLocalRangeTargetPreviewSelection();
    ClearRawDevelopmentLocalRangeTargetSample();
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

void RenderPipeline::ClearRawDevelopmentLocalRangeSelectionBits() {
    if (m_RawDevelopmentLocalRangeSelectionBitsTexture != 0) {
        glDeleteTextures(1, &m_RawDevelopmentLocalRangeSelectionBitsTexture);
        m_RawDevelopmentLocalRangeSelectionBitsTexture = 0;
    }
    m_RawDevelopmentLocalRangeSelectionBitsInputTexture = 0;
    m_RawDevelopmentLocalRangeSelectionBitsWidth = 0;
    m_RawDevelopmentLocalRangeSelectionBitsHeight = 0;
    m_RawDevelopmentLocalRangeSelectionBitsTextureWidth = 0;
    m_RawDevelopmentLocalRangeSelectionBitsTextureHeight = 0;
    m_RawDevelopmentLocalRangeSelectionBitsInputFingerprint = 0;
    m_RawDevelopmentLocalRangeSelectionBitsFingerprint = 0;
}

void RenderPipeline::ClearRawDevelopmentLocalRangeTargetPreviewSelection() {
    if (m_RawDevelopmentLocalRangeTargetPreviewCpuFuture.valid()) {
        try {
            m_RawDevelopmentLocalRangeTargetPreviewCpuFuture.wait();
            (void)m_RawDevelopmentLocalRangeTargetPreviewCpuFuture.get();
        } catch (...) {
            // Resource teardown must not terminate the application if a
            // background selection calculation failed exceptionally.
        }
    }
    m_RawDevelopmentLocalRangeTargetPreviewCpuPending = false;
    m_RawDevelopmentLocalRangeTargetPreviewCpuGeneration = 0;
    if (m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture != 0) {
        glDeleteTextures(
            1,
            &m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture);
        m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture = 0;
    }
    for (RawLocalRangeTargetPreviewReadbackSlot& slot :
         m_RawDevelopmentLocalRangeTargetPreviewReadbackSlots) {
        if (slot.fence != nullptr) {
            glDeleteSync(slot.fence);
            slot.fence = nullptr;
        }
        if (slot.pbo != 0) {
            glDeleteBuffers(1, &slot.pbo);
            slot.pbo = 0;
        }
        slot = {};
    }
    m_RawDevelopmentLocalRangeTargetPreviewNextReadbackSlot = 0;
    m_RawDevelopmentLocalRangeTargetPreviewSelectionWidth = 0;
    m_RawDevelopmentLocalRangeTargetPreviewSelectionHeight = 0;
    m_RawDevelopmentLocalRangeTargetPreviewSelectionReadyGeneration = 0;
    m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = false;
    m_RawDevelopmentLocalRangeTargetPreviewMetrics = {};
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
    m_RawDevelopmentLocalRangeTargetSampleAuthoredZoneHitBits = 0;
    m_RawDevelopmentLocalRangeTargetSampleStrongestAuthoredZoneWeight = 0.0f;
}

void RenderPipeline::ClearOutput() {
    ClearRawDevelopmentStageStatsReadbacks();
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

unsigned int RenderPipeline::TakeRawDevelopmentLocalRangeOverlayTexture(
    int& outW,
    int& outH) {
    outW = m_RawDevelopmentLocalRangeOverlayWidth;
    outH = m_RawDevelopmentLocalRangeOverlayHeight;
    const unsigned int texture = m_RawDevelopmentLocalRangeOverlayTexture;
    m_RawDevelopmentLocalRangeOverlayTexture = 0;
    m_RawDevelopmentLocalRangeOverlayWidth = 0;
    m_RawDevelopmentLocalRangeOverlayHeight = 0;
    m_RawDevelopmentLocalRangeOverlayMode.clear();
    if (texture == 0) {
        outW = 0;
        outH = 0;
    }
    return texture;
}

bool RenderPipeline::UploadOutputFromPixels(
    const unsigned char* data,
    int w,
    int h,
    int ch) {
    if (!data || w <= 0 || h <= 0) {
        return false;
    }
    Stack::Renderer::ScopedGLTexture replacement(
        GLHelpers::CreateTextureFromPixels(
            data,
            w,
            h,
            ch));
    if (!replacement) {
        return false;
    }
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
    }
    m_ExternalOutputTexture = replacement.Release();
    m_OutputTexture = m_ExternalOutputTexture;
    m_Width = w;
    m_Height = h;
    return true;
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

unsigned int RenderPipeline::PublishSharedOutputTexture(
    int& outW,
    int& outH,
    bool forceOpaqueSampling) {
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
    GLint prevReadBuffer = 0;
    GLint prevDrawBuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);
    glGetIntegerv(GL_DRAW_BUFFER, &prevDrawBuffer);

    const unsigned int srcFBO = GLHelpers::CreateFBO(m_OutputTexture);
    const unsigned int dstFBO = GLHelpers::CreateFBO(publishedTexture);
    if (srcFBO == 0 || dstFBO == 0) {
        if (srcFBO != 0) glDeleteFramebuffers(1, &srcFBO);
        if (dstFBO != 0) glDeleteFramebuffers(1, &dstFBO);
        glDeleteTextures(1, &publishedTexture);
        outW = 0;
        outH = 0;
        return 0;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    const bool framebuffersComplete =
        glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    while (glGetError() != GL_NO_ERROR) {}
    if (framebuffersComplete) {
        glBlitFramebuffer(
            0, 0, m_Width, m_Height,
            0, 0, m_Width, m_Height,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST);
    }
    const GLenum copyError = glGetError();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glReadBuffer(static_cast<GLenum>(prevReadBuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glDrawBuffer(static_cast<GLenum>(prevDrawBuffer));
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &dstFBO);

    if (!framebuffersComplete || copyError != GL_NO_ERROR) {
        glDeleteTextures(1, &publishedTexture);
        outW = 0;
        outH = 0;
        return 0;
    }

    if (forceOpaqueSampling) {
        GLint previousTextureBinding = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
        while (glGetError() != GL_NO_ERROR) {}
        glBindTexture(GL_TEXTURE_2D, publishedTexture);
        // RAW photographs are an opaque presentation surface. Some
        // interactive preview paths preserve an undefined/zero alpha channel
        // even though their RGB result is valid; ImGui would then blend the
        // entire image away for one frame. Keep the RGB data untouched while
        // making the sampling contract explicitly opaque.
        constexpr GLenum kTextureSwizzleAlpha = 0x8E45; // GL_TEXTURE_SWIZZLE_A
        glTexParameteri(GL_TEXTURE_2D, kTextureSwizzleAlpha, GL_ONE);
        glBindTexture(
            GL_TEXTURE_2D,
            static_cast<unsigned int>(previousTextureBinding));
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &publishedTexture);
            outW = 0;
            outH = 0;
            return 0;
        }
    }
    return publishedTexture;
}
