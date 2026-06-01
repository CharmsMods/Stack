#include "RenderPipeline.h"
#include "Composite/EmbeddedCompositeFont.h"
#include "Editor/LayerRegistry.h"
#include "Raw/RawLoader.h"
#include "ThirdParty/stb_image.h"
#include <imstb_truetype.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <functional>
#include <iostream>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

constexpr std::size_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr std::size_t kFnvPrime = 1099511628211ull;

inline void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

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

std::size_t HashJson(const nlohmann::json& value) {
    return HashValue(value.dump());
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SafeLog2(float value) {
    return std::log2(std::max(value, 0.000001f));
}

float PercentileFromSorted(const std::vector<float>& sorted, float percentile) {
    if (sorted.empty()) {
        return 0.0f;
    }
    const float p = std::clamp(percentile, 0.0f, 1.0f);
    const float scaled = p * static_cast<float>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(scaled));
    const std::size_t hi = std::min<std::size_t>(lo + 1, sorted.size() - 1);
    const float t = scaled - static_cast<float>(lo);
    return sorted[lo] * (1.0f - t) + sorted[hi] * t;
}

float LerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

std::vector<int> Utf8ToCodepoints(const std::string& text) {
    std::vector<int> codepoints;
    codepoints.reserve(text.size());

    for (std::size_t index = 0; index < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        if (lead < 0x80) {
            codepoints.push_back(static_cast<int>(lead));
            ++index;
            continue;
        }

        int codepoint = 0;
        std::size_t count = 0;
        if ((lead & 0xE0u) == 0xC0u) {
            codepoint = lead & 0x1Fu;
            count = 2;
        } else if ((lead & 0xF0u) == 0xE0u) {
            codepoint = lead & 0x0Fu;
            count = 3;
        } else if ((lead & 0xF8u) == 0xF0u) {
            codepoint = lead & 0x07u;
            count = 4;
        } else {
            codepoints.push_back('?');
            ++index;
            continue;
        }

        if (index + count > text.size()) {
            codepoints.push_back('?');
            break;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | static_cast<int>(continuation & 0x3Fu);
        }

        codepoints.push_back(valid ? codepoint : '?');
        index += valid ? count : 1;
    }

    return codepoints;
}

const std::vector<unsigned char>& DefaultGeneratorTextFontBytes() {
    static const std::vector<unsigned char> kFontBytes(
        std::begin(EmbeddedCompositeFont::kRobotoMediumTtf),
        std::end(EmbeddedCompositeFont::kRobotoMediumTtf));
    return kFontBytes;
}

bool BuildTextRgba(
    const std::vector<unsigned char>& fontBytes,
    const std::string& text,
    float fontSize,
    const std::array<float, 4>& color,
    const float stretchX,
    const float stretchY,
    std::vector<uint8_t>& outPixels,
    int& outWidth,
    int& outHeight) {
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;
    if (fontBytes.empty()) {
        return false;
    }

    stbtt_fontinfo fontInfo {};
    if (!stbtt_InitFont(&fontInfo, fontBytes.data(), 0)) {
        return false;
    }

    struct GlyphPlacement {
        int codepoint = 0;
        float penX = 0.0f;
        float baselineY = 0.0f;
    };

    const std::vector<int> codepoints = Utf8ToCodepoints(text.empty() ? std::string("Text") : text);
    const uint8_t r = static_cast<uint8_t>(std::round(Clamp01(color[0]) * 255.0f));
    const uint8_t g = static_cast<uint8_t>(std::round(Clamp01(color[1]) * 255.0f));
    const uint8_t b = static_cast<uint8_t>(std::round(Clamp01(color[2]) * 255.0f));
    const float baseAlpha = Clamp01(color[3]);
    const int textureLimit = 8192;
    const float requestedStretchX = std::max(0.01f, stretchX);
    const float requestedStretchY = std::max(0.01f, stretchY);
    float requestedPixelSize = std::max(1.0f, fontSize);

    for (int attempt = 0; attempt < 6; ++attempt) {
        const float pixelSize = std::max(1.0f, requestedPixelSize);
        const float baseScale = stbtt_ScaleForPixelHeight(&fontInfo, pixelSize);
        const float scaleX = baseScale * requestedStretchX;
        const float scaleY = baseScale * requestedStretchY;
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
        const float lineAdvance = std::max(1.0f, (ascent - descent + lineGap) * scaleY);
        const float baseline = ascent * scaleY;

        std::vector<GlyphPlacement> placements;
        placements.reserve(codepoints.size());

        float penX = 0.0f;
        float penY = baseline;
        float maxLineWidth = 0.0f;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        int previousCodepoint = 0;

        for (int codepoint : codepoints) {
            if (codepoint == '\r') {
                continue;
            }
            if (codepoint == '\n') {
                maxLineWidth = std::max(maxLineWidth, penX);
                penX = 0.0f;
                penY += lineAdvance;
                previousCodepoint = 0;
                continue;
            }

            if (previousCodepoint != 0) {
                penX += stbtt_GetCodepointKernAdvance(&fontInfo, previousCodepoint, codepoint) * scaleX;
            }

            placements.push_back({ codepoint, penX, penY });

            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            stbtt_GetCodepointBitmapBox(&fontInfo, codepoint, scaleX, scaleY, &x0, &y0, &x1, &y1);
            minX = std::min(minX, penX + static_cast<float>(x0));
            minY = std::min(minY, penY + static_cast<float>(y0));
            maxX = std::max(maxX, penX + static_cast<float>(x1));
            maxY = std::max(maxY, penY + static_cast<float>(y1));

            int advance = 0;
            int leftSideBearing = 0;
            stbtt_GetCodepointHMetrics(&fontInfo, codepoint, &advance, &leftSideBearing);
            penX += advance * scaleX;
            maxLineWidth = std::max(maxLineWidth, penX);
            previousCodepoint = codepoint;
        }

        const float padding = std::max(2.0f, pixelSize * 0.18f);
        const bool hasVisibleBounds = minX <= maxX && minY <= maxY;
        const float fallbackWidth = std::max(1.0f, maxLineWidth);
        const float fallbackHeight = std::max(1.0f, penY + lineAdvance - baseline);
        const float contentWidth = hasVisibleBounds ? (maxX - minX) : fallbackWidth;
        const float contentHeight = hasVisibleBounds ? (maxY - minY) : fallbackHeight;
        const int candidateWidth = std::max(1, static_cast<int>(std::ceil(contentWidth + padding * 2.0f)));
        const int candidateHeight = std::max(1, static_cast<int>(std::ceil(contentHeight + padding * 2.0f)));

        if ((candidateWidth > textureLimit || candidateHeight > textureLimit) && pixelSize > 1.0f) {
            const float fit = std::min(
                static_cast<float>(textureLimit) / static_cast<float>(std::max(1, candidateWidth)),
                static_cast<float>(textureLimit) / static_cast<float>(std::max(1, candidateHeight)));
            if (fit < 0.999f) {
                requestedPixelSize = std::max(1.0f, pixelSize * std::clamp(fit * 0.98f, 0.1f, 0.98f));
                continue;
            }
        }

        outWidth = std::min(candidateWidth, textureLimit);
        outHeight = std::min(candidateHeight, textureLimit);
        outPixels.assign(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4, 0);
        for (std::size_t pixelIndex = 0; pixelIndex < outPixels.size(); pixelIndex += 4) {
            outPixels[pixelIndex + 0] = r;
            outPixels[pixelIndex + 1] = g;
            outPixels[pixelIndex + 2] = b;
        }

        const float originX = hasVisibleBounds ? (padding - minX) : padding;
        const float originY = hasVisibleBounds ? (padding - minY) : padding;
        for (const GlyphPlacement& placement : placements) {
            int glyphW = 0;
            int glyphH = 0;
            int glyphXOff = 0;
            int glyphYOff = 0;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(
                &fontInfo,
                scaleX,
                scaleY,
                placement.codepoint,
                &glyphW,
                &glyphH,
                &glyphXOff,
                &glyphYOff);
            if (!bitmap || glyphW <= 0 || glyphH <= 0) {
                if (bitmap) {
                    stbtt_FreeBitmap(bitmap, nullptr);
                }
                continue;
            }

            const int destX = static_cast<int>(std::floor(placement.penX + static_cast<float>(glyphXOff) + originX));
            const int destY = static_cast<int>(std::floor(placement.baselineY + static_cast<float>(glyphYOff) + originY));
            for (int y = 0; y < glyphH; ++y) {
                const int py = destY + y;
                if (py < 0 || py >= outHeight) {
                    continue;
                }
                for (int x = 0; x < glyphW; ++x) {
                    const int px = destX + x;
                    if (px < 0 || px >= outWidth) {
                        continue;
                    }

                    const float glyphAlpha = static_cast<float>(bitmap[y * glyphW + x]) / 255.0f;
                    if (glyphAlpha <= 0.0f) {
                        continue;
                    }

                    const std::size_t pixelIndex =
                        (static_cast<std::size_t>(py) * static_cast<std::size_t>(outWidth) + static_cast<std::size_t>(px)) * 4;
                    const float srcAlpha = baseAlpha * glyphAlpha;
                    const float dstAlpha = static_cast<float>(outPixels[pixelIndex + 3]) / 255.0f;
                    const float outAlpha = srcAlpha + dstAlpha * (1.0f - srcAlpha);
                    if (outAlpha <= 0.0f) {
                        continue;
                    }

                    auto blendChannel = [&](const uint8_t srcChannel, const uint8_t dstChannel) {
                        const float srcNorm = static_cast<float>(srcChannel) / 255.0f;
                        const float dstNorm = static_cast<float>(dstChannel) / 255.0f;
                        return static_cast<uint8_t>(std::round(((srcNorm * srcAlpha) + (dstNorm * dstAlpha * (1.0f - srcAlpha))) / outAlpha * 255.0f));
                    };

                    outPixels[pixelIndex + 0] = blendChannel(r, outPixels[pixelIndex + 0]);
                    outPixels[pixelIndex + 1] = blendChannel(g, outPixels[pixelIndex + 1]);
                    outPixels[pixelIndex + 2] = blendChannel(b, outPixels[pixelIndex + 2]);
                    outPixels[pixelIndex + 3] = static_cast<uint8_t>(std::round(outAlpha * 255.0f));
                }
            }

            stbtt_FreeBitmap(bitmap, nullptr);
        }

        return !outPixels.empty();
    }

    return false;
}

bool BuildTextGeneratorCanvas(const RenderGraphNode& node, int canvasWidth, int canvasHeight, std::vector<unsigned char>& outPixels) {
    outPixels.clear();
    if (canvasWidth <= 0 || canvasHeight <= 0) {
        return false;
    }

    const std::array<float, 4> color = {
        node.imageGeneratorSettings.colorA[0],
        node.imageGeneratorSettings.colorA[1],
        node.imageGeneratorSettings.colorA[2],
        node.imageGeneratorSettings.colorA[3]
    };

    std::vector<uint8_t> textPixels;
    int textWidth = 0;
    int textHeight = 0;
    float effectiveFontSize = std::max(8.0f, node.imageGeneratorSettings.fontSize) *
        std::max(0.35f, std::min(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight)) / 256.0f);

    for (int attempt = 0; attempt < 6; ++attempt) {
        if (!BuildTextRgba(
                DefaultGeneratorTextFontBytes(),
                node.imageGeneratorSettings.text,
                effectiveFontSize,
                color,
                1.0f,
                1.0f,
                textPixels,
                textWidth,
                textHeight)) {
            return false;
        }

        if (textWidth <= std::max(1, canvasWidth - 6) && textHeight <= std::max(1, canvasHeight - 6)) {
            break;
        }

        const float fit = std::min(
            static_cast<float>(std::max(1, canvasWidth - 6)) / static_cast<float>(std::max(1, textWidth)),
            static_cast<float>(std::max(1, canvasHeight - 6)) / static_cast<float>(std::max(1, textHeight)));
        effectiveFontSize = std::max(6.0f, effectiveFontSize * std::clamp(fit * 0.97f, 0.1f, 0.97f));
    }

    outPixels.assign(static_cast<std::size_t>(canvasWidth) * static_cast<std::size_t>(canvasHeight) * 4, 0);
    const int offsetX = std::max(0, (canvasWidth - textWidth) / 2);
    const int offsetY = std::max(0, (canvasHeight - textHeight) / 2);
    for (int y = 0; y < textHeight; ++y) {
        const int dstY = offsetY + y;
        if (dstY < 0 || dstY >= canvasHeight) {
            continue;
        }
        for (int x = 0; x < textWidth; ++x) {
            const int dstX = offsetX + x;
            if (dstX < 0 || dstX >= canvasWidth) {
                continue;
            }
            const std::size_t srcIndex =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(textWidth) + static_cast<std::size_t>(x)) * 4;
            const std::size_t dstIndex =
                (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(canvasWidth) + static_cast<std::size_t>(dstX)) * 4;
            outPixels[dstIndex + 0] = textPixels[srcIndex + 0];
            outPixels[dstIndex + 1] = textPixels[srcIndex + 1];
            outPixels[dstIndex + 2] = textPixels[srcIndex + 2];
            outPixels[dstIndex + 3] = textPixels[srcIndex + 3];
        }
    }

    return true;
}

} // namespace

RenderPipeline::RenderPipeline()
    : m_Width(0), m_Height(0),
      m_SourceChannels(4),
      m_SourceTexture(0), m_PingTexture(0), m_PongTexture(0),
      m_PingFBO(0), m_PongFBO(0), m_OutputTexture(0), m_ExternalOutputTexture(0), m_GraphSourceTexture(0),
      m_MaskProgram(0), m_MaskBlendProgram(0), m_MixProgram(0),
      m_MaskUtilityProgram(0), m_ImageToMaskProgram(0), m_ImageGeneratorProgram(0),
      m_ChannelSplitProgram(0), m_ChannelCombineProgram(0),
      m_RawDetailFusionAnalysisProgram(0), m_RawDetailFusionMetricsProgram(0), m_RawDetailFusionSmoothProgram(0), m_RawDetailFusionApplyProgram(0),
      m_AutoGainStatsProgram(0)
{}

RenderPipeline::~RenderPipeline() {
    CleanupFBOs();
    InvalidateGraphCaches();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    if (m_ExternalOutputTexture) glDeleteTextures(1, &m_ExternalOutputTexture);
    if (m_MaskProgram) glDeleteProgram(m_MaskProgram);
    if (m_MaskBlendProgram) glDeleteProgram(m_MaskBlendProgram);
    if (m_MixProgram) glDeleteProgram(m_MixProgram);
    if (m_MaskUtilityProgram) glDeleteProgram(m_MaskUtilityProgram);
    if (m_ImageToMaskProgram) glDeleteProgram(m_ImageToMaskProgram);
    if (m_ImageGeneratorProgram) glDeleteProgram(m_ImageGeneratorProgram);
    if (m_ChannelSplitProgram) glDeleteProgram(m_ChannelSplitProgram);
    if (m_ChannelCombineProgram) glDeleteProgram(m_ChannelCombineProgram);
    if (m_RawDetailFusionAnalysisProgram) glDeleteProgram(m_RawDetailFusionAnalysisProgram);
    if (m_RawDetailFusionMetricsProgram) glDeleteProgram(m_RawDetailFusionMetricsProgram);
    if (m_RawDetailFusionSmoothProgram) glDeleteProgram(m_RawDetailFusionSmoothProgram);
    if (m_RawDetailFusionApplyProgram) glDeleteProgram(m_RawDetailFusionApplyProgram);
    if (m_AutoGainStatsProgram) glDeleteProgram(m_AutoGainStatsProgram);
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

void RenderPipeline::DestroyGraphCache(std::unordered_map<std::string, CachedGraphTexture>& cache) {
    for (auto& [key, entry] : cache) {
        if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
            glDeleteTextures(1, &entry.texture);
        }
    }
    cache.clear();
}

void RenderPipeline::InvalidateGraphCaches() {
    DestroyGraphCache(m_GraphImageCache);
    DestroyGraphCache(m_GraphMaskCache);
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
    m_SourcePixels.assign(data, data + (w * h * 4));
    m_SourceFingerprint = HashBytes(m_SourcePixels);
    stbi_image_free(data);

    Resize(w, h);

    std::cout << "[RenderPipeline] Loaded image " << w << "x" << h << " from: " << filepath << "\n";
    return true;
}

void RenderPipeline::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    const int clampedChannels = std::max(1, ch);
    const std::size_t incomingSize = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(clampedChannels);
    const std::size_t incomingFingerprint = (!data || incomingSize == 0) ? 0 : HashBytes(data, incomingSize);
    if (m_SourceTexture != 0 &&
        m_Width == w &&
        m_Height == h &&
        m_SourceChannels == ch &&
        m_SourceFingerprint == incomingFingerprint &&
        m_SourcePixels.size() == incomingSize) {
        return;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = GLHelpers::CreateTextureFromPixels(data, w, h, ch);
    m_SourceChannels = ch;
    m_SourcePixels.assign(data, data + (w * h * clampedChannels));
    m_SourceFingerprint = incomingFingerprint;
    Resize(w, h);
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
}

void RenderPipeline::ClearOutput() {
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_OutputTexture = 0;
    m_GraphSourceTexture = 0;
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

void RenderPipeline::Execute(const std::vector<std::shared_ptr<LayerBase>>& layers) {
    m_GraphSourceTexture = 0;
    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture; // Nothing to process
        return;
    }

    // Save previous GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    // Save and disable states that might interfere with full-screen quad rendering
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glViewport(0, 0, m_Width, m_Height);

    int activeCount = 0;
    for (auto& layer : layers) {
        if (layer->IsVisible()) activeCount++;
    }

    if (activeCount == 0) {
        // No layers active, output is just the source
        m_OutputTexture = m_SourceTexture;
        
        // Restore
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        if (prevScissor) glEnable(GL_SCISSOR_TEST);
        if (prevDepth) glEnable(GL_DEPTH_TEST);
        if (prevStencil) glEnable(GL_STENCIL_TEST);
        if (prevBlend) glEnable(GL_BLEND);
        return;
    }

    // Sequential ping-pong execution:
    // Layer 1 reads Source → writes Ping
    // Layer 2 reads Ping → writes Pong
    // Layer 3 reads Pong → writes Ping ... etc.
    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;

    for (auto& layer : layers) {
        if (!layer->IsVisible()) continue;

        unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
        unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glClear(GL_COLOR_BUFFER_BIT);

        layer->ExecuteWithSource(currentInput, m_SourceTexture, m_Width, m_Height, m_Quad);

        currentInput = targetTex;
        usePing = !usePing;
    }

    m_OutputTexture = currentInput;

    // Restore previous GL state
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

void RenderPipeline::EnsureMaskPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* maskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uKind;
        uniform float uValue;
        uniform float uAngle;
        uniform float uOffset;
        uniform float uScale;
        uniform vec2 uCenter;
        uniform float uRadius;
        uniform float uFeather;
        uniform int uInvert;
        float hash(vec2 p) {
            return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
        }
        float noise(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            vec2 u = f * f * (3.0 - 2.0 * f);
            return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
                       mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
        }
        void main() {
            vec2 uv = vTexCoord;
            float maskValue = clamp(uValue, 0.0, 1.0);
            if (uKind == 1) {
                float radiansAngle = radians(uAngle);
                vec2 dir = vec2(cos(radiansAngle), sin(radiansAngle));
                maskValue = dot(uv - vec2(0.5), dir) * max(uScale, 0.001) + 0.5 + uOffset;
                maskValue = clamp(maskValue, 0.0, 1.0);
            } else if (uKind == 2) {
                float d = distance(uv, uCenter);
                float feather = max(uFeather, 0.0001);
                maskValue = 1.0 - smoothstep(max(0.0, uRadius - feather), uRadius + feather, d);
                maskValue = clamp(maskValue, 0.0, 1.0);
            } else if (uKind == 3) {
                float n = noise(uv * max(uScale * 96.0, 1.0) + vec2(uOffset * 37.0, uAngle * 0.071));
                maskValue = clamp((n - 0.5) * max(uValue * 4.0, 0.001) + 0.5, 0.0, 1.0);
            }
            if (uInvert != 0) {
                maskValue = 1.0 - maskValue;
            }
            FragColor = vec4(maskValue, maskValue, maskValue, 1.0);
        }
    )";

    static const char* blendFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uOriginal;
        uniform sampler2D uProcessed;
        uniform sampler2D uMask;
        void main() {
            vec4 originalColor = texture(uOriginal, vTexCoord);
            vec4 processedColor = texture(uProcessed, vTexCoord);
            float maskValue = clamp(texture(uMask, vTexCoord).r, 0.0, 1.0);
            FragColor = mix(originalColor, processedColor, maskValue);
        }
    )";

    if (!m_MaskProgram) {
        m_MaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskFragSrc);
    }
    if (!m_MaskBlendProgram) {
        m_MaskBlendProgram = GLHelpers::CreateShaderProgram(vertexSrc, blendFragSrc);
    }
}

unsigned int RenderPipeline::GenerateMaskTexture(const RenderMaskSource& mask) {
    EnsureMaskPrograms();
    if (!m_MaskProgram || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    unsigned int texture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    unsigned int fbo = GLHelpers::CreateFBO(texture);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, m_Width, m_Height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_MaskProgram);
    glUniform1i(glGetUniformLocation(m_MaskProgram, "uKind"), static_cast<int>(mask.kind));
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uValue"), std::clamp(mask.settings.value, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uAngle"), mask.settings.angle);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uOffset"), mask.settings.offset);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uScale"), mask.settings.scale);
    glUniform2f(glGetUniformLocation(m_MaskProgram, "uCenter"), mask.settings.centerX, mask.settings.centerY);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uRadius"), mask.settings.radius);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uFeather"), mask.settings.feather);
    glUniform1i(glGetUniformLocation(m_MaskProgram, "uInvert"), mask.settings.invert ? 1 : 0);
    m_Quad.Draw();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return texture;
}

void RenderPipeline::RenderMaskBlend(unsigned int originalTexture, unsigned int processedTexture, unsigned int maskTexture, unsigned int targetFBO) {
    EnsureMaskPrograms();
    if (!m_MaskBlendProgram || !originalTexture || !processedTexture || !maskTexture) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MaskBlendProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, originalTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uOriginal"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, processedTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uProcessed"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, maskTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uMask"), 2);

    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::EnsureMixProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uImageA;
        uniform sampler2D uImageB;
        uniform sampler2D uFactorMask;
        uniform int uHasFactorMask;
        uniform float uFactor;
        uniform int uBlendMode;
        void main() {
            vec4 a = texture(uImageA, vTexCoord);
            vec4 b = texture(uImageB, vTexCoord);
            float factor = clamp(uFactor, 0.0, 1.0);
            if (uHasFactorMask != 0) {
                factor *= clamp(texture(uFactorMask, vTexCoord).r, 0.0, 1.0);
            }

            vec4 blended = b;
            if (uBlendMode == 1) {
                blended = a + b;
            } else if (uBlendMode == 2) {
                blended = a * b;
            } else if (uBlendMode == 3) {
                blended = 1.0 - (1.0 - a) * (1.0 - b);
            } else if (uBlendMode == 4) {
                float outA = b.a + a.a * (1.0 - b.a);
                vec3 outRgb = b.rgb * b.a + a.rgb * (1.0 - b.a);
                if (outA > 0.0001) {
                    outRgb /= outA;
                }
                blended = vec4(outRgb, outA);
            }
            FragColor = mix(a, blended, factor);
        }
    )";

    if (!m_MixProgram) {
        m_MixProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::RenderMixBlend(unsigned int textureA, unsigned int textureB, unsigned int factorTexture, float factor, RenderMixBlendMode mode, unsigned int targetFBO) {
    EnsureMixProgram();
    if (!m_MixProgram || !textureA || !textureB) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MixProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureA);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uImageA"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureB);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uImageB"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, factorTexture);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uFactorMask"), 2);

    glUniform1i(glGetUniformLocation(m_MixProgram, "uHasFactorMask"), factorTexture ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_MixProgram, "uFactor"), factor);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uBlendMode"), static_cast<int>(mode));
    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::EnsureUtilityPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* maskUtilityFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputMask;
        uniform int uKind;
        uniform float uBlackPoint;
        uniform float uWhitePoint;
        uniform float uGamma;
        uniform float uThreshold;
        uniform float uSoftness;
        uniform int uInvert;
        void main() {
            float v = clamp(texture(uInputMask, vTexCoord).r, 0.0, 1.0);
            if (uKind == 0) {
                v = 1.0 - v;
            } else if (uKind == 1) {
                float denom = max(uWhitePoint - uBlackPoint, 0.0001);
                v = clamp((v - uBlackPoint) / denom, 0.0, 1.0);
                v = pow(v, 1.0 / max(uGamma, 0.001));
                if (uInvert != 0) v = 1.0 - v;
            } else if (uKind == 2) {
                float softness = max(uSoftness, 0.0001);
                v = smoothstep(uThreshold - softness, uThreshold + softness, v);
                if (uInvert != 0) v = 1.0 - v;
            }
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* imageToMaskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uLow;
        uniform float uHigh;
        uniform float uSoftness;
        uniform int uInvert;
        void main() {
            vec4 c = texture(uInputImage, vTexCoord);
            float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
            float denom = max(uHigh - uLow, 0.0001);
            float v = clamp((lum - uLow) / denom, 0.0, 1.0);
            if (uSoftness > 0.0001) {
                v = smoothstep(0.5 - uSoftness, 0.5 + uSoftness, v);
            }
            if (uInvert != 0) v = 1.0 - v;
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* imageGeneratorFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uKind;
        uniform vec4 uColorA;
        uniform vec4 uColorB;
        uniform float uAngle;
        uniform float uOffset;
        void main() {
            if (uKind == 0) {
                FragColor = uColorA;
                return;
            }
            if (uKind == 2) {
                float dist = max(abs(vTexCoord.x - 0.5), abs(vTexCoord.y - 0.5)) - 0.34;
                float delta = fwidth(dist);
                float mask = 1.0 - smoothstep(-0.5 * delta, 0.5 * delta, dist);
                FragColor = vec4(uColorA.rgb, uColorA.a * mask);
                return;
            }
            if (uKind == 3) {
                float d = distance(vTexCoord, vec2(0.5));
                float dist = d - 0.34;
                float delta = fwidth(dist);
                float mask = 1.0 - smoothstep(-0.5 * delta, 0.5 * delta, dist);
                FragColor = vec4(uColorA.rgb, uColorA.a * mask);
                return;
            }
            float radiansAngle = radians(uAngle);
            vec2 dir = vec2(cos(radiansAngle), sin(radiansAngle));
            float t = clamp(dot(vTexCoord - vec2(0.5), dir) + 0.5 + uOffset, 0.0, 1.0);
            FragColor = mix(uColorA, uColorB, t);
        }
    )";

    if (!m_MaskUtilityProgram) {
        m_MaskUtilityProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskUtilityFragSrc);
    }
    if (!m_ImageToMaskProgram) {
        m_ImageToMaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, imageToMaskFragSrc);
    }
    if (!m_ImageGeneratorProgram) {
        m_ImageGeneratorProgram = GLHelpers::CreateShaderProgram(vertexSrc, imageGeneratorFragSrc);
    }
}

void RenderPipeline::RenderMaskUtility(unsigned int inputMask, const RenderGraphNode& node, unsigned int targetFBO) {
    EnsureUtilityPrograms();
    if (!m_MaskUtilityProgram || !inputMask) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MaskUtilityProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputMask);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uInputMask"), 0);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uKind"), static_cast<int>(node.maskUtilityKind));
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uBlackPoint"), node.maskUtilitySettings.blackPoint);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uWhitePoint"), node.maskUtilitySettings.whitePoint);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uGamma"), node.maskUtilitySettings.gamma);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uThreshold"), node.maskUtilitySettings.threshold);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uSoftness"), node.maskUtilitySettings.softness);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uInvert"), node.maskUtilitySettings.invert ? 1 : 0);
    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderImageToMask(unsigned int inputImage, const RenderGraphNode& node, unsigned int targetFBO) {
    EnsureUtilityPrograms();
    if (!m_ImageToMaskProgram || !inputImage) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ImageToMaskProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputImage);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uInputImage"), 0);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uLow"), node.imageToMaskSettings.low);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uHigh"), node.imageToMaskSettings.high);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uSoftness"), node.imageToMaskSettings.softness);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uInvert"), node.imageToMaskSettings.invert ? 1 : 0);
    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

unsigned int RenderPipeline::GenerateImageTexture(const RenderGraphNode& node) {
    EnsureUtilityPrograms();
    if (m_Width <= 0 || m_Height <= 0) {
        return 0;
    }
    if (node.imageGeneratorKind == RenderImageGeneratorKind::Text) {
        std::vector<unsigned char> pixels;
        if (!BuildTextGeneratorCanvas(node, m_Width, m_Height, pixels) || pixels.empty()) {
            return 0;
        }
        // Flip vertically to align with OpenGL's bottom-left standard
        int rowSize = m_Width * 4;
        std::vector<unsigned char> tempRow(rowSize);
        for (int y = 0; y < m_Height / 2; y++) {
            unsigned char* row1 = &pixels[y * rowSize];
            unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
            std::memcpy(tempRow.data(), row1, rowSize);
            std::memcpy(row1, row2, rowSize);
            std::memcpy(row2, tempRow.data(), rowSize);
        }
        return GLHelpers::CreateTextureFromPixels(pixels.data(), m_Width, m_Height, 4);
    }
    if (!m_ImageGeneratorProgram) {
        return 0;
    }
    unsigned int texture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    unsigned int fbo = GLHelpers::CreateFBO(texture);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, m_Width, m_Height);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ImageGeneratorProgram);
    glUniform1i(glGetUniformLocation(m_ImageGeneratorProgram, "uKind"), static_cast<int>(node.imageGeneratorKind));
    glUniform4f(
        glGetUniformLocation(m_ImageGeneratorProgram, "uColorA"),
        node.imageGeneratorSettings.colorA[0],
        node.imageGeneratorSettings.colorA[1],
        node.imageGeneratorSettings.colorA[2],
        node.imageGeneratorSettings.colorA[3]);
    glUniform4f(
        glGetUniformLocation(m_ImageGeneratorProgram, "uColorB"),
        node.imageGeneratorSettings.colorB[0],
        node.imageGeneratorSettings.colorB[1],
        node.imageGeneratorSettings.colorB[2],
        node.imageGeneratorSettings.colorB[3]);
    glUniform1f(glGetUniformLocation(m_ImageGeneratorProgram, "uAngle"), node.imageGeneratorSettings.angle);
    glUniform1f(glGetUniformLocation(m_ImageGeneratorProgram, "uOffset"), node.imageGeneratorSettings.offset);
    m_Quad.Draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return texture;
}

void RenderPipeline::EnsureChannelPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* splitFragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uChannel; // 0 = R, 1 = G, 2 = B, 3 = A
        void main() {
            vec4 col = texture(uInputImage, vTexCoord);
            float v = col.r;
            if (uChannel == 1) v = col.g;
            else if (uChannel == 2) v = col.b;
            else if (uChannel == 3) v = col.a;
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* combineFragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uTexR;
        uniform sampler2D uTexG;
        uniform sampler2D uTexB;
        uniform sampler2D uTexA;
        uniform int uHasR;
        uniform int uHasG;
        uniform int uHasB;
        uniform int uHasA;
        void main() {
            float r = (uHasR != 0) ? texture(uTexR, vTexCoord).r : 0.0;
            float g = (uHasG != 0) ? texture(uTexG, vTexCoord).r : 0.0;
            float b = (uHasB != 0) ? texture(uTexB, vTexCoord).r : 0.0;
            float a = (uHasA != 0) ? texture(uTexA, vTexCoord).r : 1.0;
            FragColor = vec4(r, g, b, a);
        }
    )";

    if (!m_ChannelSplitProgram) {
        m_ChannelSplitProgram = GLHelpers::CreateShaderProgram(vertexSrc, splitFragmentSrc);
    }
    if (!m_ChannelCombineProgram) {
        m_ChannelCombineProgram = GLHelpers::CreateShaderProgram(vertexSrc, combineFragmentSrc);
    }
}

void RenderPipeline::RenderChannelSplit(unsigned int inputTexture, int channel, unsigned int targetFBO) {
    EnsureChannelPrograms();
    if (!m_ChannelSplitProgram || !inputTexture) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ChannelSplitProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ChannelSplitProgram, "uInputImage"), 0);
    glUniform1i(glGetUniformLocation(m_ChannelSplitProgram, "uChannel"), channel);

    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderChannelCombine(unsigned int texR, unsigned int texG, unsigned int texB, unsigned int texA,
                                         bool hasR, bool hasG, bool hasB, bool hasA, unsigned int targetFBO) {
    EnsureChannelPrograms();
    if (!m_ChannelCombineProgram) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ChannelCombineProgram);

    int textureUnit = 0;
    if (hasR && texR) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texR);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexR"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasR"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasR"), 0);
    }

    if (hasG && texG) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texG);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexG"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasG"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasG"), 0);
    }

    if (hasB && texB) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texB);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexB"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasB"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasB"), 0);
    }

    if (hasA && texA) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texA);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexA"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasA"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasA"), 0);
    }

    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::EnsureAutoGainStatsProgram() {
    if (m_AutoGainStatsProgram) {
        return;
    }

    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform vec2 uSourceTexelSize;

        float luma(vec3 rgb) {
            return dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
        }

        float logLuma(vec2 uv) {
            return log2(max(luma(texture(uInputImage, uv).rgb), 0.00003));
        }

        void main() {
            vec3 rgb = max(texture(uInputImage, vTexCoord).rgb, vec3(0.0));
            float lum = luma(rgb);
            float maxChannel = max(max(rgb.r, rgb.g), rgb.b);
            float minChannel = min(min(rgb.r, rgb.g), rgb.b);
            float saturation = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float gx = abs(logLuma(vTexCoord + vec2(uSourceTexelSize.x, 0.0)) - logLuma(vTexCoord - vec2(uSourceTexelSize.x, 0.0)));
            float gy = abs(logLuma(vTexCoord + vec2(0.0, uSourceTexelSize.y)) - logLuma(vTexCoord - vec2(0.0, uSourceTexelSize.y)));
            float textureProxy = clamp((gx + gy) * 0.5, 0.0, 1.0);
            FragColor = vec4(lum, maxChannel, saturation, textureProxy);
        }
    )";

    m_AutoGainStatsProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
}

RenderPipeline::AutoGainSceneStats RenderPipeline::ComputeAutoGainSceneStats(unsigned int inputTexture) {
    AutoGainSceneStats fallback;
    if (!inputTexture || m_Width <= 0 || m_Height <= 0) {
        return fallback;
    }

    std::size_t cacheKey = HashValue(inputTexture);
    HashCombine(cacheKey, HashValue(m_Width));
    HashCombine(cacheKey, HashValue(m_Height));
    if (const auto cached = m_AutoGainSceneStatsCache.find(cacheKey);
        cached != m_AutoGainSceneStatsCache.end()) {
        return cached->second;
    }

    EnsureAutoGainStatsProgram();
    if (!m_AutoGainStatsProgram) {
        return fallback;
    }

    const int statsWidth = std::clamp(m_Width / 32, 64, 160);
    const int statsHeight = std::clamp(m_Height / 32, 36, 120);
    const unsigned int statsTexture = GLHelpers::CreateEmptyTexture(statsWidth, statsHeight);
    if (!statsTexture) {
        return fallback;
    }
    const unsigned int statsFbo = GLHelpers::CreateFBO(statsTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, statsFbo);
    glViewport(0, 0, statsWidth, statsHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_AutoGainStatsProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_AutoGainStatsProgram, "uInputImage"), 0);
    glUniform2f(glGetUniformLocation(m_AutoGainStatsProgram, "uSourceTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
    m_Quad.Draw();

    std::vector<float> pixels(static_cast<std::size_t>(statsWidth) * static_cast<std::size_t>(statsHeight) * 4u, 0.0f);
    glReadPixels(0, 0, statsWidth, statsHeight, GL_RGBA, GL_FLOAT, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glDeleteFramebuffers(1, &statsFbo);
    glDeleteTextures(1, &statsTexture);

    std::vector<float> lumas;
    lumas.reserve(pixels.size() / 4);
    float clipped = 0.0f;
    float saturated = 0.0f;
    float textureSum = 0.0f;
    float darkTextureSum = 0.0f;
    float darkCount = 0.0f;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const float lum = pixels[i + 0];
        const float maxChannel = pixels[i + 1];
        const float saturation = pixels[i + 2];
        const float texture = pixels[i + 3];
        if (!std::isfinite(lum) || lum <= 0.0f) {
            continue;
        }
        lumas.push_back(lum);
        clipped += maxChannel > 1.0f ? 1.0f : 0.0f;
        saturated += (maxChannel > 0.35f && saturation > 0.58f) ? 1.0f : 0.0f;
        textureSum += std::clamp(texture, 0.0f, 1.0f);
        if (lum < 0.08f) {
            darkTextureSum += std::clamp(texture, 0.0f, 1.0f);
            darkCount += 1.0f;
        }
    }

    AutoGainSceneStats stats = fallback;
    if (lumas.size() < 16) {
        m_AutoGainSceneStatsCache[cacheKey] = stats;
        return stats;
    }
    std::sort(lumas.begin(), lumas.end());
    const float count = static_cast<float>(lumas.size());
    stats.shadowPercentile = std::max(PercentileFromSorted(lumas, 0.10f), 0.00001f);
    stats.midtonePercentile = std::max(PercentileFromSorted(lumas, 0.50f), 0.00001f);
    stats.highlightPercentile = std::max(PercentileFromSorted(lumas, 0.98f), 0.00001f);
    const float p01 = std::max(PercentileFromSorted(lumas, 0.01f), 0.000001f);
    const float p05 = std::max(PercentileFromSorted(lumas, 0.05f), 0.000001f);
    stats.clippingRatio = Clamp01(clipped / count);
    stats.channelSaturationRatio = Clamp01(saturated / count);
    stats.textureConfidence = Clamp01(textureSum / count);

    const float darkTexture = darkCount > 0.0f ? darkTextureSum / darkCount : stats.textureConfidence;
    stats.estimatedNoiseFloor = std::clamp(std::max(p01, p05 * 0.45f) * (1.0f + darkTexture * 2.25f), 0.00003f, 0.08f);
    const float noiseRisk = Clamp01((stats.estimatedNoiseFloor * 5.5f) / std::max(stats.shadowPercentile, 0.00003f));
    const float highlightPressure = Clamp01(std::max(stats.clippingRatio * 8.0f, stats.channelSaturationRatio * 2.5f) +
        std::max(0.0f, SafeLog2(stats.highlightPercentile / 0.85f)) * 0.30f);

    const float target = 0.42f;
    stats.recommendedBaseEv = std::clamp(SafeLog2(target / stats.midtonePercentile), -2.0f, 3.0f);
    const float highlightTarget = LerpFloat(0.88f, 0.68f, highlightPressure);
    stats.recommendedMinEv = std::clamp(SafeLog2(highlightTarget / stats.highlightPercentile) - highlightPressure * 0.45f, -6.0f, 1.25f);
    const float shadowTarget = LerpFloat(0.22f, 0.38f, 1.0f - noiseRisk);
    float maxLift = SafeLog2(shadowTarget / std::max(stats.shadowPercentile, stats.estimatedNoiseFloor * 6.0f));
    maxLift *= LerpFloat(1.0f, 0.62f, noiseRisk);
    stats.recommendedMaxEv = std::clamp(maxLift, 0.75f, 6.5f);
    if (stats.recommendedMaxEv < stats.recommendedMinEv + 0.25f) {
        stats.recommendedMaxEv = std::min(8.0f, stats.recommendedMinEv + 0.25f);
    }
    stats.recommendedNoiseProtection = Clamp01(0.30f + noiseRisk * 0.62f + darkTexture * 0.22f);
    stats.recommendedHighlightProtection = Clamp01(0.58f + highlightPressure * 0.36f + stats.channelSaturationRatio * 0.20f);
    stats.recommendedShadowLiftLimit = Clamp01(0.88f - noiseRisk * 0.48f);
    stats.recommendedTarget = std::clamp(target + LerpFloat(0.03f, -0.08f, highlightPressure) - noiseRisk * 0.04f, 0.28f, 0.55f);
    stats.valid = true;

    m_AutoGainSceneStatsCache[cacheKey] = stats;
    return stats;
}

Raw::RawDetailFusionSettings RenderPipeline::ResolveAutoGainEffectiveSettings(
    unsigned int inputTexture,
    const Raw::RawDetailFusionSettings& settings) {
    Raw::RawDetailFusionSettings effective = settings;
    if (!settings.autoSafetyEnabled) {
        return effective;
    }

    const AutoGainSceneStats stats = ComputeAutoGainSceneStats(inputTexture);
    if (!stats.valid) {
        return effective;
    }

    effective.minEv = settings.overrideMinEv
        ? settings.minEv
        : std::clamp(stats.recommendedMinEv + settings.minEvBias, -8.0f, 2.0f);
    effective.maxEv = settings.overrideMaxEv
        ? settings.maxEv
        : std::clamp(stats.recommendedMaxEv + settings.maxEvBias, -2.0f, 8.0f);
    if (effective.maxEv < effective.minEv + 0.25f) {
        effective.maxEv = std::min(8.0f, effective.minEv + 0.25f);
    }
    effective.baseEv = settings.overrideBaseEv
        ? settings.baseEv
        : std::clamp(stats.recommendedBaseEv + settings.baseEvBias, -8.0f, 8.0f);
    effective.noiseProtection = settings.overrideNoiseProtection
        ? settings.noiseProtection
        : Clamp01(stats.recommendedNoiseProtection + settings.noiseProtectionBias);
    effective.highlightProtection = settings.overrideHighlightProtection
        ? settings.highlightProtection
        : Clamp01(stats.recommendedHighlightProtection + settings.highlightProtectionBias);
    effective.shadowLiftLimit = settings.overrideShadowLiftLimit
        ? settings.shadowLiftLimit
        : Clamp01(stats.recommendedShadowLiftLimit + settings.shadowLiftLimitBias);
    effective.wellExposedTarget = settings.overrideWellExposedTarget
        ? settings.wellExposedTarget
        : std::clamp(stats.recommendedTarget + settings.wellExposedTargetBias, 0.05f, 0.95f);
    return effective;
}

void RenderPipeline::EnsureRawDetailFusionPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* metricsFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uSmoothGradientProtection;
        uniform float uTextureSensitivity;
        uniform float uSkyBias;
        uniform float uEstimatedNoiseFloor;
        uniform float uAutoNoiseProtection;
        uniform float uAutoHighlightProtection;
        uniform float uChannelSaturationRisk;
        uniform vec2 uTexelSize;

        vec3 rgbAt(vec2 uv) {
            return max(texture(uInputImage, uv).rgb, vec3(0.0));
        }

        float luma(vec3 rgb) {
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        float lumaAt(vec2 uv) {
            return luma(rgbAt(uv));
        }

        float logLumaAt(vec2 uv) {
            return log2(max(lumaAt(uv), 0.00003));
        }

        void main() {
            vec3 centerRgb = rgbAt(vTexCoord);
            float centerLum = luma(centerRgb);
            float centerLog = log2(max(centerLum, 0.00003));
            vec2 texel = uTexelSize;

            float l = logLumaAt(vTexCoord - vec2(texel.x, 0.0));
            float r = logLumaAt(vTexCoord + vec2(texel.x, 0.0));
            float d = logLumaAt(vTexCoord - vec2(0.0, texel.y));
            float u = logLumaAt(vTexCoord + vec2(0.0, texel.y));
            float l2 = logLumaAt(vTexCoord - vec2(texel.x * 2.0, 0.0));
            float r2 = logLumaAt(vTexCoord + vec2(texel.x * 2.0, 0.0));
            float d2 = logLumaAt(vTexCoord - vec2(0.0, texel.y * 2.0));
            float u2 = logLumaAt(vTexCoord + vec2(0.0, texel.y * 2.0));

            float gradient = length(vec2(r - l, u - d));
            float second = abs(l - centerLog * 2.0 + r) + abs(d - centerLog * 2.0 + u);
            float broadGradient = length(vec2(r2 - l2, u2 - d2));
            float broadSecond = abs(l2 - centerLog * 2.0 + r2) + abs(d2 - centerLog * 2.0 + u2);

            vec3 meanRgb = vec3(0.0);
            float meanLum = 0.0;
            float meanLog = 0.0;
            float samples = 0.0;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    vec2 uv = vTexCoord + vec2(x, y) * texel;
                    vec3 rgb = rgbAt(uv);
                    float lum = luma(rgb);
                    meanRgb += rgb;
                    meanLum += lum;
                    meanLog += log2(max(lum, 0.00003));
                    samples += 1.0;
                }
            }
            meanRgb /= max(samples, 1.0);
            meanLum /= max(samples, 1.0);
            meanLog /= max(samples, 1.0);

            float variance = 0.0;
            float chromaVariance = 0.0;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    vec2 uv = vTexCoord + vec2(x, y) * texel;
                    vec3 rgb = rgbAt(uv);
                    float logLum = log2(max(luma(rgb), 0.00003));
                    variance += pow(logLum - meanLog, 2.0);
                    chromaVariance += length((rgb - vec3(luma(rgb))) - (meanRgb - vec3(meanLum)));
                }
            }
            variance /= max(samples, 1.0);
            chromaVariance /= max(samples, 1.0);

            float textureSensitivity = clamp(uTextureSensitivity, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);
            float skyBias = clamp(uSkyBias, 0.0, 1.0);

            float trueEdge = smoothstep(mix(0.08, 0.025, textureSensitivity), mix(0.42, 0.15, textureSensitivity), gradient + second * 3.25);
            trueEdge = max(trueEdge, smoothstep(0.18, 0.95, broadSecond * 4.0));

            float textureDetail = smoothstep(mix(0.010, 0.003, textureSensitivity), mix(0.090, 0.030, textureSensitivity), sqrt(max(variance, 0.0)) + second * 1.5);
            textureDetail *= 1.0 - smoothstep(0.035, 0.22, broadGradient) * 0.55;

            float lowTexture = 1.0 - smoothstep(mix(0.005, 0.018, textureSensitivity), mix(0.060, 0.16, textureSensitivity), sqrt(max(variance, 0.0)) + chromaVariance);
            float rampLike = smoothstep(0.010, 0.18, broadGradient) * (1.0 - smoothstep(0.050, 0.34, broadSecond * 2.0));
            float lowSaturation = 1.0 - smoothstep(0.08, 0.42, length(centerRgb - vec3(centerLum)));
            float blueSkyHint = smoothstep(0.0, 0.14, centerRgb.b - max(centerRgb.r, centerRgb.g) * 0.72);
            float brightEnough = smoothstep(0.010, 0.22, centerLum);
            float smoothGradient = max(lowTexture * rampLike, lowTexture * mix(lowSaturation, max(lowSaturation, blueSkyHint), skyBias) * brightEnough);
            smoothGradient = clamp(smoothGradient * mix(0.35, 1.35, smoothProtect) * (1.0 - trueEdge * 0.92), 0.0, 1.0);

            float debandRisk = smoothGradient * (1.0 - textureDetail) * smoothstep(0.012, 0.30, broadGradient);
            float centerChroma = length(centerRgb - vec3(centerLum));
            float maxChannel = max(max(centerRgb.r, centerRgb.g), centerRgb.b);
            float minChannel = min(min(centerRgb.r, centerRgb.g), centerRgb.b);
            float saturation = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float shadowNoise = (1.0 - smoothstep(uEstimatedNoiseFloor * 2.0, uEstimatedNoiseFloor * 9.0, centerLum)) *
                (1.0 - trueEdge * 0.65);
            float sceneSaturationRisk = smoothstep(0.35, 0.92, saturation) * smoothstep(0.18, 1.08, maxChannel);
            float chromaArtifact = trueEdge *
                smoothstep(0.010 + centerLum * 0.025, 0.16 + centerLum * 0.20, centerChroma + chromaVariance * 1.8) *
                (1.0 - smoothstep(0.08, 0.85, lowTexture));
            chromaArtifact = max(chromaArtifact, sceneSaturationRisk * clamp(uChannelSaturationRisk, 0.0, 1.0) * 0.75);
            textureDetail *= 1.0 - chromaArtifact * mix(0.45, 0.85, clamp(uAutoHighlightProtection, 0.0, 1.0));
            textureDetail *= 1.0 - shadowNoise * mix(0.25, 0.95, clamp(uAutoNoiseProtection, 0.0, 1.0));
            FragColor = vec4(clamp(trueEdge, 0.0, 1.0), clamp(textureDetail, 0.0, 1.0), smoothGradient, clamp(max(max(debandRisk, chromaArtifact), shadowNoise * 0.75), 0.0, 1.0));
        }
    )";

    static const char* analysisFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform sampler2D uMetrics;
        uniform sampler2D uManualMask;
        uniform int uHasManualMask;
        uniform int uMode;
        uniform float uMinEv;
        uniform float uMaxEv;
        uniform float uBaseEv;
        uniform int uSampleCount;
        uniform float uHighlightProtection;
        uniform float uShadowLiftLimit;
        uniform float uNoiseProtection;
        uniform float uDetailWeight;
        uniform float uWellExposedTarget;
        uniform float uSmoothGradientProtection;
        uniform float uSkyBias;
        uniform float uEstimatedNoiseFloor;
        uniform float uChannelSaturationRisk;
        uniform float uClippingRatio;
        uniform int uInvertMask;
        uniform float uMaskBlackPoint;
        uniform float uMaskWhitePoint;
        uniform float uMaskGamma;
        uniform float uManualBlend;
        uniform vec2 uTexelSize;

        float lumaAt(vec2 uv) {
            vec3 rgb = max(texture(uInputImage, uv).rgb, vec3(0.0));
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        vec3 rgbAt(vec2 uv) {
            return max(texture(uInputImage, uv).rgb, vec3(0.0));
        }

        float shapedMask() {
            float v = uHasManualMask != 0 ? texture(uManualMask, vTexCoord).r : 0.5;
            float denom = max(uMaskWhitePoint - uMaskBlackPoint, 0.0001);
            v = clamp((v - uMaskBlackPoint) / denom, 0.0, 1.0);
            v = pow(v, 1.0 / max(uMaskGamma, 0.001));
            if (uInvertMask != 0) v = 1.0 - v;
            return v;
        }

        void main() {
            vec3 centerRgb = rgbAt(vTexCoord);
            float lum = dot(centerRgb, vec3(0.2126, 0.7152, 0.0722));
            float maxChannel = max(max(centerRgb.r, centerRgb.g), centerRgb.b);
            float minChannel = min(min(centerRgb.r, centerRgb.g), centerRgb.b);
            float channelDominance = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float saturatedBright = smoothstep(0.45, 0.92, channelDominance) * smoothstep(0.28, 1.08, maxChannel);
            float globalHighlightPressure = clamp(uClippingRatio * 8.0 + uChannelSaturationRisk * 2.2, 0.0, 1.0);
            vec4 metrics = texture(uMetrics, vTexCoord);
            float trueEdge = clamp(metrics.r, 0.0, 1.0);
            float textureDetail = clamp(metrics.g, 0.0, 1.0);
            float smoothGradient = clamp(metrics.b, 0.0, 1.0);
            float chromaArtifact = clamp(metrics.a, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);

            int count = clamp(uSampleCount, 2, 33);
            float evSpan = max(0.0001, uMaxEv - uMinEv);
            float minAbsEv = min(uMinEv, uMaxEv) + uBaseEv;
            float maxAbsEv = max(uMinEv, uMaxEv) + uBaseEv;
            float target = clamp(uWellExposedTarget, 0.02, 1.5);
            float safeLum = max(lum, 0.00003);
            float targetEv = clamp(log2(target / safeLum), minAbsEv, maxAbsEv);
            float originalClipRisk = smoothstep(0.70, 1.05, lum);
            float weightedEv = 0.0;
            float weightSum = 0.0;
            float bestEv = uBaseEv;
            float bestScore = -1.0;
            float highlightSafety = 1.0;
            float shadowProtection = 1.0;
            for (int i = 0; i < 33; ++i) {
                if (i >= count) break;
                float t = count <= 1 ? 0.5 : float(i) / float(count - 1);
                float ev = mix(uMinEv, uMaxEv, t) + uBaseEv;
                float exposed = lum * exp2(ev);
                float clipRisk = smoothstep(0.82, 1.18, exposed);
                float adaptiveNoiseFloor = max(0.00003, uEstimatedNoiseFloor);
                float deepShadow = 1.0 - smoothstep(adaptiveNoiseFloor * 1.5, mix(adaptiveNoiseFloor * 5.0, adaptiveNoiseFloor * 18.0, clamp(uNoiseProtection, 0.0, 1.0)), lum);
                float blackRisk = 1.0 - smoothstep(0.004, mix(0.018, 0.12, clamp(uNoiseProtection, 0.0, 1.0)), exposed);
                float stopError = log2(max(exposed, 0.00003) / target);
                float well = exp(-pow(stopError * 0.92, 2.0));
                float saturatedClipRisk = max(clipRisk, saturatedBright * mix(0.35, 1.0, globalHighlightPressure));
                float protect = mix(1.0, 1.0 - saturatedClipRisk, clamp(uHighlightProtection, 0.0, 1.0));
                float positiveLift = smoothstep(0.0, max(0.001, maxAbsEv), max(ev, 0.0));
                float shadow = 1.0 - deepShadow * positiveLift * clamp(uShadowLiftLimit, 0.0, 1.0) * mix(0.45, 0.82, clamp(uNoiseProtection, 0.0, 1.0));
                float targetPrior = exp(-pow((ev - targetEv) * 0.55, 2.0));
                float detailVisibility = smoothstep(0.010, 0.160, exposed) * (1.0 - clipRisk);
                float reliableTexture = textureDetail * (1.0 - deepShadow * clamp(uNoiseProtection, 0.0, 1.0)) * (1.0 - saturatedBright * 0.70);
                float detailGain = mix(1.0, mix(0.85, 1.35, detailVisibility), clamp(uDetailWeight, 0.0, 1.0) * reliableTexture * (1.0 - chromaArtifact * 0.80));
                float gradientStability = mix(1.0, 1.0 - max(smoothGradient * 0.65, chromaArtifact * 0.55), smoothProtect);
                float edgeStability = mix(1.0, 1.0 - trueEdge * 0.25, clamp(uSkyBias, 0.0, 1.0));
                float score = max(0.00001, well * protect * shadow * detailGain * gradientStability * edgeStability * mix(0.75, 1.75, targetPrior));
                weightedEv += ev * score;
                weightSum += score;
                if (score > bestScore) {
                    bestScore = score;
                    bestEv = ev;
                    highlightSafety = 1.0 - clipRisk;
                    shadowProtection = 1.0 - blackRisk;
                }
            }
            float autoEv = weightSum > 0.0 ? weightedEv / weightSum : uBaseEv;
            float liftNeed = smoothstep(0.0, target, target - lum);
            float highlightRoom = 1.0 - smoothstep(0.60, 1.0, lum);
            autoEv = mix(autoEv, bestEv, mix(0.35, 0.12, smoothGradient * smoothProtect));
            float smoothLiftLimit = mix(1.0, 0.55, smoothGradient * smoothProtect);
            autoEv = mix(autoEv, targetEv, liftNeed * highlightRoom * (1.0 - originalClipRisk) * 0.65 * smoothLiftLimit);
            autoEv = mix(autoEv, uBaseEv + clamp(targetEv - uBaseEv, -1.5, 1.5), smoothGradient * smoothProtect * 0.18);
            float manualEv = mix(uMinEv, uMaxEv, shapedMask()) + uBaseEv;
            float ev = autoEv;
            if (uMode == 0) {
                ev = manualEv;
            } else if (uMode == 2) {
                ev = mix(autoEv, manualEv, clamp(uManualBlend, 0.0, 1.0));
            }
            ev = clamp(ev, minAbsEv, maxAbsEv);
            float evNorm = clamp((ev - (uMinEv + uBaseEv)) / evSpan, 0.0, 1.0);
            float confidence = clamp(weightSum / max(0.0001, float(count)), 0.0, 1.0);
            float sampleNorm = clamp((bestEv - (uMinEv + uBaseEv)) / evSpan, 0.0, 1.0);
            FragColor = vec4(evNorm, confidence, highlightSafety, mix(shadowProtection, sampleNorm, 0.45));
        }
    )";

    static const char* smoothFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uAnalysis;
        uniform sampler2D uMetrics;
        uniform sampler2D uInputImage;
        uniform int uRadius;
        uniform int uSmoothAreaRadius;
        uniform float uEdgeAwareness;
        uniform float uHaloGuard;
        uniform float uSmoothGradientProtection;
        uniform float uMaskDebandDither;
        uniform vec2 uTexelSize;

        float lumaAt(vec2 uv) {
            vec3 rgb = max(texture(uInputImage, uv).rgb, vec3(0.0));
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        float logLumaAt(vec2 uv) {
            return log2(max(lumaAt(uv), 0.00003));
        }

        void main() {
            vec4 center = texture(uAnalysis, vTexCoord);
            vec4 centerMetrics = texture(uMetrics, vTexCoord);
            int radius = clamp(uRadius, 0, 16);
            int smoothAreaRadius = clamp(uSmoothAreaRadius, 0, 32);
            float smoothGradient = clamp(centerMetrics.b, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);
            int effectiveRadius = max(radius, int(round(float(smoothAreaRadius) * smoothGradient * smoothProtect)));
            if (effectiveRadius <= 0) {
                FragColor = center;
                return;
            }
            float centerLum = lumaAt(vTexCoord);
            float centerLogLum = logLumaAt(vTexCoord);
            float edgeAware = clamp(uEdgeAwareness, 0.0, 1.0);
            float haloGuard = clamp(uHaloGuard, 0.0, 1.0);
            float localEdge = max(centerMetrics.r, clamp((abs(logLumaAt(vTexCoord + vec2(uTexelSize.x, 0.0)) - logLumaAt(vTexCoord - vec2(uTexelSize.x, 0.0))) +
                abs(logLumaAt(vTexCoord + vec2(0.0, uTexelSize.y)) - logLumaAt(vTexCoord - vec2(0.0, uTexelSize.y)))) * 0.55, 0.0, 1.0));
            float edgeScale = mix(2.2, 0.22, edgeAware);
            float linearEdgeScale = mix(0.55, 0.035, edgeAware);
            float haloScale = mix(3.0, 0.75, haloGuard);
            float smoothRadiusScale = mix(1.0, mix(1.0, 0.20, localEdge), haloGuard);
            smoothRadiusScale *= mix(1.0, 1.85, smoothGradient * smoothProtect);
            float sum = 0.0;
            float weightSum = 0.0;
            for (int y = -32; y <= 32; ++y) {
                for (int x = -32; x <= 32; ++x) {
                    if (abs(x) > effectiveRadius || abs(y) > effectiveRadius) continue;
                    vec2 uv = vTexCoord + vec2(x, y) * uTexelSize;
                    vec4 sampleMetrics = texture(uMetrics, uv);
                    float distance2 = float(x * x + y * y);
                    float spatial = exp(-distance2 / max(1.0, float(effectiveRadius * effectiveRadius) * smoothRadiusScale) * haloScale);
                    float logDiff = abs(logLumaAt(uv) - centerLogLum);
                    float linearDiff = abs(lumaAt(uv) - centerLum);
                    float sameSmoothRegion = 1.0 - abs(clamp(sampleMetrics.b, 0.0, 1.0) - smoothGradient);
                    float edgeStop = max(localEdge, clamp(sampleMetrics.r, 0.0, 1.0));
                    float rangeW = exp(-logDiff / max(0.0001, edgeScale)) *
                        exp(-linearDiff / max(0.0001, linearEdgeScale));
                    rangeW = mix(rangeW, max(rangeW, 0.35 + sameSmoothRegion * 0.45), smoothGradient * smoothProtect * (1.0 - edgeStop));
                    rangeW *= 1.0 - smoothstep(mix(3.0, 0.75, edgeAware), mix(5.0, 1.55, edgeAware), logDiff) * haloGuard;
                    rangeW *= 1.0 - edgeStop * haloGuard * 0.75;
                    float w = spatial * rangeW;
                    sum += texture(uAnalysis, uv).r * w;
                    weightSum += w;
                }
            }
            float smoothed = weightSum > 0.0 ? sum / weightSum : center.r;
            float preserve = smoothstep(0.12, 0.88, localEdge) * haloGuard;
            preserve *= 1.0 - smoothGradient * smoothProtect * 0.75;
            center.r = mix(smoothed, center.r, preserve);
            if (uMaskDebandDither > 0.0 && centerMetrics.a > 0.0) {
                float n = fract(sin(dot(vTexCoord * vec2(8192.0, 4096.0), vec2(12.9898, 78.233))) * 43758.5453);
                center.r = clamp(center.r + (n - 0.5) * uMaskDebandDither * centerMetrics.a * 0.006, 0.0, 1.0);
            }
            FragColor = center;
        }
    )";

    static const char* applyFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform sampler2D uExposureMap;
        uniform sampler2D uMetrics;
        uniform int uHasMask;
        uniform float uMinEv;
        uniform float uMaxEv;
        uniform float uBaseEv;
        uniform float uStrength;
        uniform float uEstimatedNoiseFloor;
        uniform float uChannelSaturationRisk;
        uniform int uDebugView;
        uniform int uMaskOutput;

        void main() {
            vec4 inputColor = texture(uInputImage, vTexCoord);
            vec4 map = texture(uExposureMap, vTexCoord);
            vec4 metrics = texture(uMetrics, vTexCoord);
            float ev = uHasMask != 0 ? mix(uMinEv + uBaseEv, uMaxEv + uBaseEv, clamp(map.r, 0.0, 1.0)) : 0.0;
            float gain = exp2(ev * clamp(uStrength, 0.0, 2.0));
            vec3 fused = max(inputColor.rgb, vec3(0.0)) * gain;
            if (uMaskOutput != 0 || uDebugView == 1) {
                FragColor = vec4(vec3(map.r), 1.0);
            } else if (uDebugView == 2) {
                FragColor = vec4(vec3(map.g), 1.0);
            } else if (uDebugView == 3) {
                FragColor = vec4(vec3(map.b), 1.0);
            } else if (uDebugView == 4) {
                FragColor = vec4(vec3(map.a), 1.0);
            } else if (uDebugView == 5) {
                float bands = floor(map.r * 8.999) / 8.0;
                FragColor = vec4(bands, 1.0 - bands, abs(0.5 - bands) * 2.0, 1.0);
            } else if (uDebugView == 6) {
                FragColor = vec4(vec3(metrics.b), 1.0);
            } else if (uDebugView == 7) {
                FragColor = vec4(vec3(metrics.r), 1.0);
            } else if (uDebugView == 8) {
                FragColor = vec4(vec3(metrics.g), 1.0);
            } else if (uDebugView == 9) {
                FragColor = vec4(vec3(metrics.a), 1.0);
            } else if (uDebugView == 10) {
                float rangePreview = clamp((uMaxEv - uMinEv) / 12.0, 0.0, 1.0);
                FragColor = vec4(map.r, rangePreview, clamp((uBaseEv + 4.0) / 8.0, 0.0, 1.0), 1.0);
            } else if (uDebugView == 11) {
                float lum = dot(max(inputColor.rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
                float snr = smoothstep(uEstimatedNoiseFloor * 2.0, uEstimatedNoiseFloor * 18.0, lum);
                FragColor = vec4(vec3(snr * (1.0 - metrics.a * 0.45)), 1.0);
            } else if (uDebugView == 12) {
                FragColor = vec4(vec3(map.b), 1.0);
            } else if (uDebugView == 13) {
                float maxChannel = max(max(inputColor.r, inputColor.g), inputColor.b);
                float minChannel = min(min(inputColor.r, inputColor.g), inputColor.b);
                float sat = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
                FragColor = vec4(vec3(clamp(max(sat, uChannelSaturationRisk) * smoothstep(0.25, 1.05, maxChannel), 0.0, 1.0)), 1.0);
            } else if (uDebugView == 14) {
                FragColor = vec4(vec3(clamp(metrics.a * (1.0 - metrics.g), 0.0, 1.0)), 1.0);
            } else {
                FragColor = vec4(fused, inputColor.a);
            }
        }
    )";

    if (!m_RawDetailFusionAnalysisProgram) {
        m_RawDetailFusionAnalysisProgram = GLHelpers::CreateShaderProgram(vertexSrc, analysisFragSrc);
    }
    if (!m_RawDetailFusionMetricsProgram) {
        m_RawDetailFusionMetricsProgram = GLHelpers::CreateShaderProgram(vertexSrc, metricsFragSrc);
    }
    if (!m_RawDetailFusionSmoothProgram) {
        m_RawDetailFusionSmoothProgram = GLHelpers::CreateShaderProgram(vertexSrc, smoothFragSrc);
    }
    if (!m_RawDetailFusionApplyProgram) {
        m_RawDetailFusionApplyProgram = GLHelpers::CreateShaderProgram(vertexSrc, applyFragSrc);
    }
}

unsigned int RenderPipeline::RenderRawDetailAutoMask(
    unsigned int inputTexture,
    const RenderGraphNode& node,
    unsigned int manualMaskTexture,
    bool debugPreview) {
    EnsureRawDetailFusionPrograms();
    if (!inputTexture || !m_RawDetailFusionAnalysisProgram || !m_RawDetailFusionMetricsProgram ||
        !m_RawDetailFusionSmoothProgram || (debugPreview && !m_RawDetailFusionApplyProgram)) {
        return 0;
    }
    const Raw::RawDetailFusionSettings& settings = node.kind == RenderGraphNodeKind::RawDetailFusion
        ? node.rawDetailFusion.settings
        : node.rawDetailAutoMask.settings;
    const AutoGainSceneStats sceneStats = ComputeAutoGainSceneStats(inputTexture);
    const Raw::RawDetailFusionSettings effectiveSettings = ResolveAutoGainEffectiveSettings(inputTexture, settings);
    const unsigned int metricsTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    const unsigned int analysisTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    const unsigned int smoothTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    if (!metricsTexture || !analysisTexture || !smoothTexture) {
        if (metricsTexture) glDeleteTextures(1, &metricsTexture);
        if (analysisTexture) glDeleteTextures(1, &analysisTexture);
        if (smoothTexture) glDeleteTextures(1, &smoothTexture);
        return 0;
    }

    auto renderPass = [&](unsigned int texture, const std::function<void(unsigned int)>& fn) {
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, m_Width, m_Height);
        glClear(GL_COLOR_BUFFER_BIT);
        fn(fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    };

    renderPass(metricsTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionMetricsProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uInputImage"), 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uTextureSensitivity"), settings.textureSensitivity);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uSkyBias"), settings.skyBias);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uAutoNoiseProtection"), effectiveSettings.noiseProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uAutoHighlightProtection"), effectiveSettings.highlightProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    renderPass(analysisTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionAnalysisProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uInputImage"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, metricsTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMetrics"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, manualMaskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uManualMask"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uHasManualMask"), manualMaskTexture ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMode"),
            static_cast<int>(manualMaskTexture ? Raw::RawDetailFusionMode::Hybrid : Raw::RawDetailFusionMode::AutoAnalyze));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMinEv"), effectiveSettings.minEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaxEv"), effectiveSettings.maxEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uBaseEv"), effectiveSettings.baseEv);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSampleCount"), std::clamp(effectiveSettings.sampleCount, 2, 33));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uHighlightProtection"), effectiveSettings.highlightProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uShadowLiftLimit"), effectiveSettings.shadowLiftLimit);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uNoiseProtection"), effectiveSettings.noiseProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uDetailWeight"), settings.detailWeight);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uWellExposedTarget"), effectiveSettings.wellExposedTarget);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSkyBias"), settings.skyBias);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uClippingRatio"), sceneStats.clippingRatio);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uInvertMask"), settings.invertMask ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskBlackPoint"), settings.maskBlackPoint);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskWhitePoint"), settings.maskWhitePoint);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskGamma"), settings.maskGamma);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uManualBlend"), settings.manualBlend);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    renderPass(smoothTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionSmoothProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, analysisTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uAnalysis"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, metricsTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uMetrics"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uInputImage"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uRadius"), std::clamp(settings.smoothnessRadius, 0, 16));
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uSmoothAreaRadius"), std::clamp(settings.smoothAreaRadius, 0, 32));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uEdgeAwareness"), settings.edgeAwareness);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uHaloGuard"), settings.haloGuard);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uMaskDebandDither"), settings.maskDebandDither);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    if (debugPreview) {
        const unsigned int previewTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
        if (previewTexture) {
            renderPass(previewTexture, [&](unsigned int) {
                glUseProgram(m_RawDetailFusionApplyProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, inputTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uInputImage"), 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, smoothTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uExposureMap"), 1);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, metricsTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMetrics"), 2);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uHasMask"), 1);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMinEv"), effectiveSettings.minEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaxEv"), effectiveSettings.maxEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uBaseEv"), effectiveSettings.baseEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uStrength"), effectiveSettings.strength);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uDebugView"), static_cast<int>(settings.debugView));
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaskOutput"), 1);
                m_Quad.Draw();
            });
            glDeleteTextures(1, &smoothTexture);
            glDeleteTextures(1, &analysisTexture);
            glDeleteTextures(1, &metricsTexture);
            glUseProgram(0);
            glActiveTexture(GL_TEXTURE0);
            return previewTexture;
        }
    }
    glDeleteTextures(1, &analysisTexture);
    glDeleteTextures(1, &metricsTexture);
    return smoothTexture;
}

unsigned int RenderPipeline::RenderRawDetailFusion(
    unsigned int inputTexture,
    unsigned int maskTexture,
    const Raw::RawDetailFusionSettings& settings) {
    EnsureRawDetailFusionPrograms();
    if (!inputTexture || !m_RawDetailFusionApplyProgram) {
        return 0;
    }
    const unsigned int outputTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    if (!outputTexture) {
        return 0;
    }
    const AutoGainSceneStats sceneStats = ComputeAutoGainSceneStats(inputTexture);
    const Raw::RawDetailFusionSettings effectiveSettings = ResolveAutoGainEffectiveSettings(inputTexture, settings);
    auto renderPass = [&](unsigned int texture, const std::function<void(unsigned int)>& fn) {
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, m_Width, m_Height);
        glClear(GL_COLOR_BUFFER_BIT);
        fn(fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    };

    renderPass(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionApplyProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uInputImage"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, maskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uExposureMap"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, maskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMetrics"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uHasMask"), maskTexture ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMinEv"), effectiveSettings.minEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaxEv"), effectiveSettings.maxEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uBaseEv"), effectiveSettings.baseEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uStrength"), effectiveSettings.strength);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uDebugView"), static_cast<int>(Raw::RawDetailFusionDebugView::FinalImage));
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaskOutput"), 0);
        m_Quad.Draw();
    });

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    return outputTexture;
}

void RenderPipeline::ExecuteGraph(const RenderGraphSnapshot& graph) {
    m_GraphSourceTexture = 0;
    m_AutoGainSceneStatsCache.clear();
    if (!m_SourceTexture || m_Width == 0 || m_Height == 0 || graph.outputNodeId <= 0) {
        m_OutputTexture = m_SourceTexture;
        return;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }

    std::unordered_map<int, const RenderGraphNode*> nodes;
    std::set<int> activeNodeIds;
    for (const RenderGraphNode& node : graph.nodes) {
        nodes[node.nodeId] = &node;
        activeNodeIds.insert(node.nodeId);
    }

    auto findInputLink = [&](int nodeId, const std::string& socketId) -> const RenderGraphLink* {
        for (const RenderGraphLink& link : graph.links) {
            if (link.toNodeId == nodeId && link.toSocketId == socketId) {
                return &link;
            }
        }
        return nullptr;
    };

    std::function<int(int)> findReferenceSourceNode = [&](int nodeId) -> int {
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || !nodeIt->second) {
            return -1;
        }
        const RenderGraphNode& node = *nodeIt->second;
        switch (node.kind) {
            case RenderGraphNodeKind::Image:
            case RenderGraphNodeKind::RawSource:
            case RenderGraphNodeKind::ImageGenerator:
            case RenderGraphNodeKind::RawDevelop:
                return node.nodeId;
            case RenderGraphNodeKind::RawDetailFusion: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::RawDetailAutoMask: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::RawNeuralDenoise: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "rawIn");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::Layer:
            case RenderGraphNodeKind::Output:
            case RenderGraphNodeKind::ImageToMask:
            case RenderGraphNodeKind::MaskUtility:
            case RenderGraphNodeKind::ChannelSplit: {
                const RenderGraphLink* input = findInputLink(node.nodeId, node.kind == RenderGraphNodeKind::Output ? "imageIn" : "imageIn");
                if (!input && node.kind == RenderGraphNodeKind::MaskUtility) input = findInputLink(node.nodeId, "maskIn");
                if (!input && node.kind == RenderGraphNodeKind::ChannelSplit) input = findInputLink(node.nodeId, "imageIn");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::Mix: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "imageA");
                if (!input) input = findInputLink(node.nodeId, "imageB");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::ChannelCombine: {
                const char* sockets[] = { "r", "g", "b", "a" };
                for (const char* socket : sockets) {
                    if (const RenderGraphLink* input = findInputLink(node.nodeId, socket)) {
                        const int source = findReferenceSourceNode(input->fromNodeId);
                        if (source > 0) return source;
                    }
                }
                return -1;
            }
            case RenderGraphNodeKind::MaskGenerator:
            default:
                return -1;
        }
    };

    std::function<int(int, const std::string&)> findRawDetailAutoMaskSource = [&](int nodeId, const std::string& socketId) -> int {
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || !nodeIt->second) {
            return -1;
        }
        const RenderGraphNode& node = *nodeIt->second;
        if (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") {
            return node.nodeId;
        }
        if (node.kind == RenderGraphNodeKind::MaskUtility && socketId == "maskOut") {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            return input ? findRawDetailAutoMaskSource(input->fromNodeId, input->fromSocketId) : -1;
        }
        return -1;
    };

    auto resolveRawDetailFusionApplySettings = [&](const RenderGraphNode& node) {
        Raw::RawDetailFusionSettings settings = node.rawDetailFusion.settings;
        if (const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn")) {
            const int autoMaskNodeId = findRawDetailAutoMaskSource(maskLink->fromNodeId, maskLink->fromSocketId);
            const auto autoIt = nodes.find(autoMaskNodeId);
            if (autoIt != nodes.end() && autoIt->second) {
                const Raw::RawDetailFusionSettings& autoSettings = autoIt->second->rawDetailAutoMask.settings;
                settings.minEv = autoSettings.minEv;
                settings.maxEv = autoSettings.maxEv;
                settings.baseEv = autoSettings.baseEv;
            }
        }
        return settings;
    };

    std::unordered_map<std::string, unsigned int> imageCache;
    std::unordered_map<std::string, unsigned int> maskCache;
    std::unordered_map<std::string, std::size_t> imageFingerprintCache;
    std::unordered_map<std::string, std::size_t> maskFingerprintCache;
    std::set<std::string> visitingImages;
    std::set<std::string> visitingMasks;
    std::set<std::string> fingerprintingImages;
    std::set<std::string> fingerprintingMasks;

    auto releaseCacheEntry = [&](std::unordered_map<std::string, CachedGraphTexture>& cache, const std::string& key) {
        auto it = cache.find(key);
        if (it == cache.end()) {
            return;
        }
        if (it->second.owned && it->second.texture != 0 && it->second.texture != m_SourceTexture && it->second.texture != m_ExternalOutputTexture) {
            glDeleteTextures(1, &it->second.texture);
        }
        cache.erase(it);
    };

    auto storeCacheEntry = [&](std::unordered_map<std::string, CachedGraphTexture>& cache, const std::string& key, unsigned int texture, std::size_t fingerprint, bool owned) {
        auto& entry = cache[key];
        if (entry.owned && entry.texture != 0 && entry.texture != texture && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
            glDeleteTextures(1, &entry.texture);
        }
        entry.texture = texture;
        entry.fingerprint = fingerprint;
        entry.owned = owned;
    };

    auto createTarget = [&]() {
        return GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    };

    auto renderToTexture = [&](unsigned int texture, const std::function<void(unsigned int)>& renderFn) {
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClear(GL_COLOR_BUFFER_BIT);
        renderFn(fbo);
        glDeleteFramebuffers(1, &fbo);
    };

    std::function<unsigned int(int, const std::string&)> evalMask;
    std::function<unsigned int(int, const std::string&)> evalImage;
    std::function<std::size_t(int, const std::string&)> fingerprintMask;
    std::function<std::size_t(int, const std::string&)> fingerprintImage;

    fingerprintMask = [&](int nodeId, const std::string& socketId) -> std::size_t {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        auto cached = maskFingerprintCache.find(key);
        if (cached != maskFingerprintCache.end()) {
            return cached->second;
        }
        if (fingerprintingMasks.count(key)) {
            return 0;
        }
        fingerprintingMasks.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            fingerprintingMasks.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::Image ||
            node.kind == RenderGraphNodeKind::RawNeuralDenoise ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            std::size_t imgFp = fingerprintImage(nodeId, socketId);
            fingerprintingMasks.erase(key);
            maskFingerprintCache[key] = imgFp;
            return imgFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(socketId));

        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskKind)));
            HashCombine(fingerprint, HashValue(node.maskSettings.value));
            HashCombine(fingerprint, HashValue(node.maskSettings.angle));
            HashCombine(fingerprint, HashValue(node.maskSettings.offset));
            HashCombine(fingerprint, HashValue(node.maskSettings.scale));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerX));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerY));
            HashCombine(fingerprint, HashValue(node.maskSettings.radius));
            HashCombine(fingerprint, HashValue(node.maskSettings.feather));
            HashCombine(fingerprint, HashValue(node.maskSettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, input ? fingerprintMask(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskUtilityKind)));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.blackPoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.whitePoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.gamma));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.threshold));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.softness));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.imageToMaskKind)));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.low));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.high));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.softness));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            const Raw::RawDetailFusionSettings& settings = node.rawDetailAutoMask.settings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.mode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.strength));
            HashCombine(fingerprint, HashValue(settings.sampleCount));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.detailWeight));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.smoothGradientProtection));
            HashCombine(fingerprint, HashValue(settings.textureSensitivity));
            HashCombine(fingerprint, HashValue(settings.skyBias));
            HashCombine(fingerprint, HashValue(settings.invertMask));
            HashCombine(fingerprint, HashValue(settings.maskBlackPoint));
            HashCombine(fingerprint, HashValue(settings.maskWhitePoint));
            HashCombine(fingerprint, HashValue(settings.maskGamma));
            HashCombine(fingerprint, HashValue(settings.smoothnessRadius));
            HashCombine(fingerprint, HashValue(settings.smoothAreaRadius));
            HashCombine(fingerprint, HashValue(settings.edgeAwareness));
            HashCombine(fingerprint, HashValue(settings.haloGuard));
            HashCombine(fingerprint, HashValue(settings.maskDebandDither));
            HashCombine(fingerprint, HashValue(settings.manualBlend));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            const Raw::RawDetailFusionSettings& settings = node.rawDetailFusion.settings;
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.sampleCount));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.detailWeight));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.smoothGradientProtection));
            HashCombine(fingerprint, HashValue(settings.textureSensitivity));
            HashCombine(fingerprint, HashValue(settings.skyBias));
            HashCombine(fingerprint, HashValue(settings.invertMask));
            HashCombine(fingerprint, HashValue(settings.maskBlackPoint));
            HashCombine(fingerprint, HashValue(settings.maskWhitePoint));
            HashCombine(fingerprint, HashValue(settings.maskGamma));
            HashCombine(fingerprint, HashValue(settings.smoothnessRadius));
            HashCombine(fingerprint, HashValue(settings.smoothAreaRadius));
            HashCombine(fingerprint, HashValue(settings.edgeAwareness));
            HashCombine(fingerprint, HashValue(settings.haloGuard));
            HashCombine(fingerprint, HashValue(settings.maskDebandDither));
            HashCombine(fingerprint, HashValue(settings.manualBlend));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        }

        fingerprintingMasks.erase(key);
        maskFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    fingerprintImage = [&](int nodeId, const std::string& socketId) -> std::size_t {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        auto cached = imageFingerprintCache.find(key);
        if (cached != imageFingerprintCache.end()) {
            return cached->second;
        }
        if (fingerprintingImages.count(key)) {
            return 0;
        }
        fingerprintingImages.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            fingerprintingImages.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            std::size_t maskFp = fingerprintMask(nodeId, socketId);
            fingerprintingImages.erase(key);
            imageFingerprintCache[key] = maskFp;
            return maskFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(socketId));

        if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                HashCombine(fingerprint, HashValue(node.image.width));
                HashCombine(fingerprint, HashValue(node.image.height));
                HashCombine(fingerprint, HashValue(node.image.channels));
                HashCombine(fingerprint, HashBytes(node.image.pixels));
            } else {
                HashCombine(fingerprint, m_SourceFingerprint);
                HashCombine(fingerprint, HashValue(m_Width));
                HashCombine(fingerprint, HashValue(m_Height));
                HashCombine(fingerprint, HashValue(m_SourceChannels));
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            HashCombine(fingerprint, HashValue(node.rawSource.sourcePath));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.rawWidth));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.rawHeight));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.visibleWidth));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.visibleHeight));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.orientation));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawSource.metadata.cfaPattern)));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.blackLevel));
            for (float value : node.rawSource.metadata.perChannelBlack) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.whiteLevel));
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(NeuralDenoise::SerializeSettings(node.rawNeuralDenoise.settings)));
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.exposureStops));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawDevelop.settings.whiteBalanceMode)));
            for (float value : node.rawDevelop.settings.manualWhiteBalance) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.overrideBlackLevel));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.blackLevelOverride));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.overrideWhiteLevel));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.whiteLevelOverride));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawDevelop.settings.highlightMode)));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.highlightStrength));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.highlightThreshold));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawDevelop.settings.demosaicMethod)));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.cameraTransformEnabled));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.debugBypassCameraTransform));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.debugTransposeCameraMatrix));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawDevelop.settings.debugView)));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.rotationDegrees));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.rotateToFitFrame));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.flipHorizontally));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.flipVertically));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.falseColorSuppression));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.defringeStrength));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.highlightEdgeCleanup));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.chromaRadius));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.preserveRealColor));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.lateralRedCyan));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.lateralBlueYellow));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawDevelop.settings.cameraTransformSource)));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.mosaicDenoise.enabled));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.mosaicDenoise.hotPixelSuppression));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.mosaicDenoise.hotPixelThreshold));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.mosaicDenoise.lumaStrength));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.mosaicDenoise.chromaStrength));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.mosaicDenoise.radius));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.mosaicDenoise.edgeProtection));
            HashCombine(fingerprint, HashValue(node.rawDevelop.settings.mosaicDenoise.iterations));
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, fingerprintMask(node.nodeId, "maskOut"));
            const Raw::RawDetailFusionSettings settings = resolveRawDetailFusionApplySettings(node);
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.strength));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.imageGeneratorKind)));
            for (float channel : node.imageGeneratorSettings.colorA) {
                HashCombine(fingerprint, HashValue(channel));
            }
            for (float channel : node.imageGeneratorSettings.colorB) {
                HashCombine(fingerprint, HashValue(channel));
            }
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.angle));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.offset));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.text));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.fontSize));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(node.layerJson));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
            HashCombine(fingerprint, inputA ? fingerprintImage(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintImage(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, factorLink ? fingerprintMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.mixBlendMode)));
            HashCombine(fingerprint, HashValue(node.mixFactor));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                HashCombine(fingerprint, fingerprintImage(input->fromNodeId, input->fromSocketId));
            } else {
                const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
                const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
                const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
                const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
                HashCombine(fingerprint, linkR ? fingerprintMask(linkR->fromNodeId, linkR->fromSocketId) : 0);
                HashCombine(fingerprint, linkG ? fingerprintMask(linkG->fromNodeId, linkG->fromSocketId) : 0);
                HashCombine(fingerprint, linkB ? fingerprintMask(linkB->fromNodeId, linkB->fromSocketId) : 0);
                HashCombine(fingerprint, linkA ? fingerprintMask(linkA->fromNodeId, linkA->fromSocketId) : 0);
            }
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
            HashCombine(fingerprint, linkR ? fingerprintMask(linkR->fromNodeId, linkR->fromSocketId) : 0);
            HashCombine(fingerprint, linkG ? fingerprintMask(linkG->fromNodeId, linkG->fromSocketId) : 0);
            HashCombine(fingerprint, linkB ? fingerprintMask(linkB->fromNodeId, linkB->fromSocketId) : 0);
            HashCombine(fingerprint, linkA ? fingerprintMask(linkA->fromNodeId, linkA->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        }

        fingerprintingImages.erase(key);
        imageFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    evalMask = [&](int nodeId, const std::string& socketId) -> unsigned int {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        if (maskCache.count(key)) {
            return maskCache[key];
        }
        if (visitingMasks.count(key)) {
            return 0;
        }
        visitingMasks.insert(key);
        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingMasks.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::Image ||
            node.kind == RenderGraphNodeKind::RawNeuralDenoise ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            unsigned int imgTex = evalImage(nodeId, socketId);
            visitingMasks.erase(key);
            return imgTex;
        }
        const std::size_t fingerprint = fingerprintMask(nodeId, socketId);
        if (const auto cached = m_GraphMaskCache.find(key);
            cached != m_GraphMaskCache.end() &&
            cached->second.fingerprint == fingerprint &&
            cached->second.texture != 0) {
            maskCache[key] = cached->second.texture;
            visitingMasks.erase(key);
            return cached->second.texture;
        }

        unsigned int result = 0;
        bool resultOwned = false;
        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            RenderMaskSource mask;
            mask.nodeId = node.nodeId;
            mask.kind = node.maskKind;
            mask.settings = node.maskSettings;
            result = GenerateMaskTexture(mask);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            const unsigned int inputMask = input ? evalMask(input->fromNodeId, input->fromSocketId) : 0;
            if (inputMask) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMaskUtility(inputMask, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderImageToMask(inputImage, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                int channelIdx = 0;
                if (socketId == "g") channelIdx = 1;
                else if (socketId == "b") channelIdx = 2;
                else if (socketId == "a") channelIdx = 3;
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderChannelSplit(inputImage, channelIdx, fbo);
                });
                resultOwned = result != 0;
            } else {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    GLfloat previousClearColor[4];
                    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
                    if (socketId == "a") {
                        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
                    } else {
                        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    }
                    glClear(GL_COLOR_BUFFER_BIT);
                    glClearColor(
                        previousClearColor[0],
                        previousClearColor[1],
                        previousClearColor[2],
                        previousClearColor[3]);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                const bool debugPreview = graph.autoGainMaskPreview &&
                    graph.outputNodeId == node.nodeId &&
                    graph.outputSocketId == "maskOut";
                result = RenderRawDetailAutoMask(inputImage, node, 0, debugPreview);
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            const unsigned int manualMask = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
            if (inputImage) {
                const bool debugPreview = graph.autoGainMaskPreview &&
                    graph.outputNodeId == node.nodeId &&
                    graph.outputSocketId == "maskOut";
                result = RenderRawDetailAutoMask(inputImage, node, manualMask, debugPreview);
                resultOwned = result != 0;
            }
        }

        if (result) {
            maskCache[key] = result;
            storeCacheEntry(m_GraphMaskCache, key, result, fingerprint, resultOwned);
        } else {
            releaseCacheEntry(m_GraphMaskCache, key);
        }
        visitingMasks.erase(key);
        return result;
    };

    evalImage = [&](int nodeId, const std::string& socketId) -> unsigned int {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        if (imageCache.count(key)) {
            return imageCache[key];
        }
        if (visitingImages.count(key)) {
            return 0;
        }
        visitingImages.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingImages.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            unsigned int maskTex = evalMask(nodeId, socketId);
            visitingImages.erase(key);
            return maskTex;
        }
        const std::size_t fingerprint = fingerprintImage(nodeId, socketId);
        if (const auto cached = m_GraphImageCache.find(key);
            cached != m_GraphImageCache.end() &&
            cached->second.fingerprint == fingerprint &&
            cached->second.texture != 0) {
            imageCache[key] = cached->second.texture;
            visitingImages.erase(key);
            return cached->second.texture;
        }

        unsigned int result = 0;
        bool resultOwned = false;
        if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                result = GLHelpers::CreateTextureFromPixels(node.image.pixels.data(), node.image.width, node.image.height, node.image.channels);
                resultOwned = result != 0;
            } else {
                result = m_SourceTexture;
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            result = 0;
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            result = 0;
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            const RenderGraphNode* rawSource = nullptr;
            std::set<int> rawVisit;
            while (rawInput) {
                if (!rawVisit.insert(rawInput->fromNodeId).second) {
                    break;
                }
                const auto rawIt = nodes.find(rawInput->fromNodeId);
                if (rawIt == nodes.end() || !rawIt->second) {
                    break;
                }
                if (rawIt->second->kind == RenderGraphNodeKind::RawSource) {
                    rawSource = rawIt->second;
                    break;
                }
                if (rawIt->second->kind != RenderGraphNodeKind::RawNeuralDenoise) {
                    break;
                }
                rawInput = findInputLink(rawIt->second->nodeId, "rawIn");
            }
            if (rawSource) {
                const std::string path = rawSource->rawSource.sourcePath.empty()
                    ? rawSource->rawSource.metadata.sourcePath
                    : rawSource->rawSource.sourcePath;
                Raw::RawImageData& rawData = m_RawDataCache[rawSource->nodeId];
                std::string& cachedPath = m_RawDataCachePaths[rawSource->nodeId];
                if (cachedPath != path || rawData.rawBuffer.empty()) {
                    Raw::RawImageData loadedRaw;
                    if (Raw::RawLoader::LoadFile(path, loadedRaw)) {
                        rawData = std::move(loadedRaw);
                        cachedPath = path;
                    } else {
                        rawData = std::move(loadedRaw);
                        cachedPath = path;
                    }
                }
                if (!rawData.rawBuffer.empty() && rawData.metadata.error.empty()) {
                    if (rawSource->rawSource.metadata.visibleWidth > 0) {
                        rawData.metadata.visibleWidth = rawSource->rawSource.metadata.visibleWidth;
                    }
                    if (rawSource->rawSource.metadata.visibleHeight > 0) {
                        rawData.metadata.visibleHeight = rawSource->rawSource.metadata.visibleHeight;
                    }
                    result = m_RawPipelines[node.nodeId].Render(rawData, node.rawDevelop.settings);
                    if (result == 0) {
                        const std::string& error = m_RawPipelines[node.nodeId].GetLastError();
                        std::cerr << "[RAW] Render failed for develop node " << node.nodeId
                                  << " (" << path << "): "
                                  << (error.empty() ? "unknown RAW GPU failure" : error)
                                  << "\n";
                    }
                    resultOwned = false;
                } else {
                    const std::string error = !rawData.metadata.error.empty()
                        ? rawData.metadata.error
                        : "LibRaw did not produce a usable raw buffer.";
                    std::cerr << "[RAW] Load failed for source node " << rawSource->nodeId
                              << " (" << path << "): " << error << "\n";
                }
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            const unsigned int generatedMask = inputImage ? evalMask(node.nodeId, "maskOut") : 0;
            if (inputImage) {
                result = RenderRawDetailFusion(inputImage, generatedMask, resolveRawDetailFusionApplySettings(node));
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            result = GenerateImageTexture(node);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputTexture && node.layerJson.is_object()) {
                const std::string type = node.layerJson.value("type", std::string());
                std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
                if (layer) {
                    layer->InitializeGL();
                    layer->Deserialize(node.layerJson);
                    unsigned int processed = createTarget();
                    renderToTexture(processed, [&](unsigned int) {
                        layer->ExecuteWithSource(inputTexture, m_SourceTexture, m_Width, m_Height, m_Quad);
                    });
                    result = processed;
                    resultOwned = result != 0;
                    const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
                    const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
                    if (maskTexture) {
                        unsigned int blended = createTarget();
                        renderToTexture(blended, [&](unsigned int fbo) {
                            RenderMaskBlend(inputTexture, processed, maskTexture, fbo);
                        });
                        if (processed != 0) {
                            glDeleteTextures(1, &processed);
                        }
                        result = blended;
                        resultOwned = result != 0;
                    }
                }
            }
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const unsigned int textureA = inputA ? evalImage(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const unsigned int textureB = inputB ? evalImage(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (textureA && textureB) {
                const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
                const unsigned int factorTexture = factorLink ? evalMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0;
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMixBlend(textureA, textureB, factorTexture, node.mixFactor, node.mixBlendMode, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");

            const unsigned int texR = linkR ? evalMask(linkR->fromNodeId, linkR->fromSocketId) : 0;
            const unsigned int texG = linkG ? evalMask(linkG->fromNodeId, linkG->fromSocketId) : 0;
            const unsigned int texB = linkB ? evalMask(linkB->fromNodeId, linkB->fromSocketId) : 0;
            const unsigned int texA = linkA ? evalMask(linkA->fromNodeId, linkA->fromSocketId) : 0;

            result = createTarget();
            renderToTexture(result, [&](unsigned int fbo) {
                RenderChannelCombine(texR, texG, texB, texA,
                                     linkR != nullptr, linkG != nullptr, linkB != nullptr, linkA != nullptr,
                                     fbo);
            });
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                result = evalImage(input->fromNodeId, input->fromSocketId);
            } else {
                const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
                const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
                const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
                const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");

                const unsigned int texR = linkR ? evalMask(linkR->fromNodeId, linkR->fromSocketId) : 0;
                const unsigned int texG = linkG ? evalMask(linkG->fromNodeId, linkG->fromSocketId) : 0;
                const unsigned int texB = linkB ? evalMask(linkB->fromNodeId, linkB->fromSocketId) : 0;
                const unsigned int texA = linkA ? evalMask(linkA->fromNodeId, linkA->fromSocketId) : 0;

                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderChannelCombine(texR, texG, texB, texA,
                                         linkR != nullptr, linkG != nullptr, linkB != nullptr, linkA != nullptr,
                                         fbo);
                });
                resultOwned = result != 0;
            }
        }

        if (result) {
            imageCache[key] = result;
            storeCacheEntry(m_GraphImageCache, key, result, fingerprint, resultOwned);
        } else {
            releaseCacheEntry(m_GraphImageCache, key);
        }
        visitingImages.erase(key);
        return result;
    };

    unsigned int finalTexture = 0;
    const auto outputIt = nodes.find(graph.outputNodeId);
    if (outputIt != nodes.end() &&
        (outputIt->second->kind == RenderGraphNodeKind::MaskGenerator ||
         outputIt->second->kind == RenderGraphNodeKind::MaskUtility ||
         outputIt->second->kind == RenderGraphNodeKind::ImageToMask ||
         outputIt->second->kind == RenderGraphNodeKind::ChannelSplit ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailAutoMask && graph.outputSocketId == "maskOut") ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailFusion && graph.outputSocketId == "maskOut"))) {
        finalTexture = evalMask(graph.outputNodeId, graph.outputSocketId);
    } else {
        finalTexture = evalImage(graph.outputNodeId, graph.outputSocketId);
    }
    m_OutputTexture = finalTexture ? finalTexture : 0;
    m_GraphSourceTexture = 0;
    const int referenceSourceNodeId = findReferenceSourceNode(graph.outputNodeId);
    if (referenceSourceNodeId > 0) {
        const auto referenceIt = nodes.find(referenceSourceNodeId);
        if (referenceIt != nodes.end() &&
            referenceIt->second &&
            referenceIt->second->kind == RenderGraphNodeKind::RawSource) {
            m_GraphSourceTexture = m_SourceTexture;
        } else {
            m_GraphSourceTexture = evalImage(referenceSourceNodeId, "imageOut");
        }
    }

    for (auto it = m_GraphImageCache.begin(); it != m_GraphImageCache.end(); ) {
        size_t colonPos = it->first.find(':');
        int nodeId = (colonPos != std::string::npos) ? std::stoi(it->first.substr(0, colonPos)) : -1;
        if (!activeNodeIds.count(nodeId)) {
            if (it->second.owned && it->second.texture != 0 && it->second.texture != m_SourceTexture && it->second.texture != m_ExternalOutputTexture) {
                glDeleteTextures(1, &it->second.texture);
            }
            it = m_GraphImageCache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_GraphMaskCache.begin(); it != m_GraphMaskCache.end(); ) {
        size_t colonPos = it->first.find(':');
        int nodeId = (colonPos != std::string::npos) ? std::stoi(it->first.substr(0, colonPos)) : -1;
        if (!activeNodeIds.count(nodeId)) {
            if (it->second.owned && it->second.texture != 0 && it->second.texture != m_SourceTexture && it->second.texture != m_ExternalOutputTexture) {
                glDeleteTextures(1, &it->second.texture);
            }
            it = m_GraphMaskCache.erase(it);
        } else {
            ++it;
        }
    }

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

void RenderPipeline::ExecuteMasked(const std::vector<RenderLayerStep>& steps, const std::vector<RenderMaskSource>& masks) {
    m_GraphSourceTexture = 0;
    bool hasConnectedMask = false;
    for (const RenderLayerStep& step : steps) {
        if (step.maskNodeId > 0) {
            hasConnectedMask = true;
            break;
        }
    }
    if (!hasConnectedMask) {
        std::vector<std::shared_ptr<LayerBase>> layers;
        layers.reserve(steps.size());
        for (const RenderLayerStep& step : steps) {
            if (step.layer) {
                layers.push_back(step.layer);
            }
        }
        Execute(layers);
        return;
    }

    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture;
        return;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    std::vector<std::pair<int, unsigned int>> maskTextures;
    for (const RenderMaskSource& mask : masks) {
        unsigned int texture = GenerateMaskTexture(mask);
        if (texture) {
            maskTextures.push_back({ mask.nodeId, texture });
        }
    }

    auto findMaskTexture = [&](int nodeId) -> unsigned int {
        for (const auto& item : maskTextures) {
            if (item.first == nodeId) {
                return item.second;
            }
        }
        return 0;
    };

    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;
    int activeCount = 0;
    for (const RenderLayerStep& step : steps) {
        if (step.layer && step.layer->IsVisible()) {
            ++activeCount;
        }
    }

    if (activeCount == 0) {
        m_OutputTexture = m_SourceTexture;
    } else {
        for (const RenderLayerStep& step : steps) {
            if (!step.layer || !step.layer->IsVisible()) {
                continue;
            }

            const unsigned int originalInput = currentInput;
            unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
            unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;
            usePing = !usePing;

            glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
            glClear(GL_COLOR_BUFFER_BIT);
            step.layer->ExecuteWithSource(originalInput, m_SourceTexture, m_Width, m_Height, m_Quad);

            const unsigned int maskTexture = findMaskTexture(step.maskNodeId);
            if (maskTexture) {
                unsigned int blendFBO = usePing ? m_PingFBO : m_PongFBO;
                unsigned int blendTex = usePing ? m_PingTexture : m_PongTexture;
                usePing = !usePing;
                RenderMaskBlend(originalInput, targetTex, maskTexture, blendFBO);
                currentInput = blendTex;
            } else {
                currentInput = targetTex;
            }
        }
        m_OutputTexture = currentInput;
    }

    for (const auto& item : maskTextures) {
        unsigned int texture = item.second;
        glDeleteTextures(1, &texture);
    }

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

std::vector<unsigned char> RenderPipeline::GetOutputPixels(int& outW, int& outH) {
    outW = m_Width;
    outH = m_Height;
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};

    std::vector<unsigned char> pixels(m_Width * m_Height * 4);
    
    // Restore previous FBO binding
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    // Create a temporary FBO for reading
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[RenderPipeline] Warning: GetOutputPixels FBO incomplete (status " << fboStatus << "). Texture: " << m_OutputTexture << ". Attempting read anyway." << std::endl;
    }

    // Clear previous errors
    while (glGetError() != GL_NO_ERROR) {}

    glReadPixels(0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        std::cerr << "[RenderPipeline] glReadPixels error in GetOutputPixels: " << err << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glDeleteFramebuffers(1, &tempFBO);
        
        // Before our strict error checking, the code would just return the zeroed pixels array
        // on failure. Return the zeroed array instead of empty {} so downstream doesn't completely abort.
        return pixels;
    }

    // Validate that we didn't just read empty pixels (if alpha is completely 0 for the entire image)
    bool hasData = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] > 0) {
            hasData = true;
            break;
        }
    }
    if (!hasData) {
        std::cerr << "[RenderPipeline] Warning: GetOutputPixels read completely transparent image." << std::endl;
    }

    // Flip vertically
    int rowSize = m_Width * 4;
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; y++) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glDeleteFramebuffers(1, &tempFBO);

    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetCompareSourcePixels(int& outW, int& outH) {
    outW = m_Width;
    outH = m_Height;
    unsigned int compareTex = GetCompareSourceTexture();
    if (compareTex == 0 || m_Width == 0 || m_Height == 0) return {};

    std::vector<unsigned char> pixels(m_Width * m_Height * 4);
    
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, compareTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[RenderPipeline] GetCompareSourcePixels FBO incomplete. Texture: " << compareTex << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glDeleteFramebuffers(1, &tempFBO);
        return {};
    }

    while (glGetError() != GL_NO_ERROR) {}

    glReadPixels(0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        std::cerr << "[RenderPipeline] glReadPixels error in GetCompareSourcePixels: " << err << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glDeleteFramebuffers(1, &tempFBO);
        return {};
    }

    int rowSize = m_Width * 4;
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; y++) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glDeleteFramebuffers(1, &tempFBO);

    return pixels;
}

RenderTextureStats RenderPipeline::GetOutputTextureStats() {
    RenderTextureStats stats;
    if (m_OutputTexture == 0 || m_Width <= 0 || m_Height <= 0) {
        return stats;
    }

    constexpr int kMaxProbeEdge = 512;
    const float scale = std::min(
        1.0f,
        static_cast<float>(kMaxProbeEdge) / static_cast<float>(std::max(m_Width, m_Height)));
    const int probeW = std::max(1, static_cast<int>(std::round(static_cast<float>(m_Width) * scale)));
    const int probeH = std::max(1, static_cast<int>(std::round(static_cast<float>(m_Height) * scale)));

    GLint prevReadFBO = 0;
    GLint prevDrawFBO = 0;
    GLint prevFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    unsigned int readFBO = 0;
    unsigned int probeFBO = 0;
    unsigned int probeTex = 0;
    glGenFramebuffers(1, &readFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    if (probeW == m_Width && probeH == m_Height) {
        glBindFramebuffer(GL_FRAMEBUFFER, readFBO);
    } else {
        probeTex = GLHelpers::CreateEmptyTexture(probeW, probeH);
        glGenFramebuffers(1, &probeFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, probeFBO);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probeTex, 0);
        glBlitFramebuffer(
            0, 0, m_Width, m_Height,
            0, 0, probeW, probeH,
            GL_COLOR_BUFFER_BIT,
            GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, probeFBO);
    }

    std::vector<float> pixels(static_cast<std::size_t>(probeW) * static_cast<std::size_t>(probeH) * 4u, 0.0f);
    
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(0, 0, probeW, probeH, GL_RGBA, GL_FLOAT, pixels.data());
        if (GLenum err = glGetError(); err != GL_NO_ERROR) {
            std::cerr << "[RenderPipeline] glReadPixels error in GetOutputTextureStats: " << err << std::endl;
            pixels.clear();
        }
    } else {
        std::cerr << "[RenderPipeline] GetOutputTextureStats FBO incomplete." << std::endl;
        pixels.clear();
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (probeFBO != 0) {
        glDeleteFramebuffers(1, &probeFBO);
    }
    if (readFBO != 0) {
        glDeleteFramebuffers(1, &readFBO);
    }
    if (probeTex != 0) {
        glDeleteTextures(1, &probeTex);
    }

    if (pixels.empty()) {
        return stats;
    }

    stats.valid = true;
    stats.minRgb = std::numeric_limits<float>::max();
    stats.maxRgb = -std::numeric_limits<float>::max();
    stats.minLuma = std::numeric_limits<float>::max();
    stats.maxLuma = -std::numeric_limits<float>::max();

    std::vector<float> lumas;
    lumas.reserve(static_cast<std::size_t>(probeW) * static_cast<std::size_t>(probeH));
    int hdrPixels = 0;
    int displayEdgePixels = 0;
    int validPixels = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        float r = pixels[i + 0];
        float g = pixels[i + 1];
        float b = pixels[i + 2];
        if (!std::isfinite(r)) r = 0.0f;
        if (!std::isfinite(g)) g = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

        const float minChannel = std::min({ r, g, b });
        const float maxChannel = std::max({ r, g, b });
        const float luma = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
        stats.minRgb = std::min(stats.minRgb, minChannel);
        stats.maxRgb = std::max(stats.maxRgb, maxChannel);
        stats.minLuma = std::min(stats.minLuma, luma);
        stats.maxLuma = std::max(stats.maxLuma, luma);
        lumas.push_back(luma);
        if (maxChannel > 1.0f) {
            ++hdrPixels;
        }
        if (maxChannel >= 0.999f || minChannel <= 0.001f) {
            ++displayEdgePixels;
        }
        ++validPixels;
    }

    if (validPixels <= 0 || lumas.empty()) {
        stats.valid = false;
        return stats;
    }

    std::sort(lumas.begin(), lumas.end());
    auto percentile = [&](float p) {
        const float clamped = std::clamp(p, 0.0f, 1.0f);
        const std::size_t index = static_cast<std::size_t>(
            std::round(clamped * static_cast<float>(lumas.size() - 1)));
        return lumas[index];
    };
    stats.p01Luma = percentile(0.01f);
    stats.p50Luma = percentile(0.50f);
    stats.p99Luma = percentile(0.99f);
    stats.hdrPixelPercent = 100.0f * static_cast<float>(hdrPixels) / static_cast<float>(validPixels);
    stats.displayClipPercent = 100.0f * static_cast<float>(displayEdgePixels) / static_cast<float>(validPixels);
    return stats;
}

std::vector<unsigned char> RenderPipeline::GetScopesPixels(int& outW, int& outH) {
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};

    // Target a small size for analysis efficiency
    outW = 256;
    outH = 256;
    if (m_Width < outW) outW = m_Width;
    if (m_Height < outH) outH = m_Height;

    std::vector<unsigned char> pixels(outW * outH * 4);
    
    // Create a temporary FBO/Texture for downsampling
    unsigned int tempTex = GLHelpers::CreateEmptyTexture(outW, outH);
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tempTex, 0);

    // Blit from current output to small target (GPU downsample)
    GLint prevReadFBO, prevDrawFBO;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    unsigned int srcFBO;
    glGenFramebuffers(1, &srcFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempFBO);
    glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, outW, outH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Read small pixels
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[RenderPipeline] GetScopesPixels FBO incomplete." << std::endl;
        pixels.clear();
    } else {
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (GLenum err = glGetError(); err != GL_NO_ERROR) {
            std::cerr << "[RenderPipeline] glReadPixels error in GetScopesPixels: " << err << std::endl;
            pixels.clear();
        }
    }

    // Cleanup
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &tempFBO);
    glDeleteTextures(1, &tempTex);

    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetSourcePixels(int& outW, int& outH) {
    if (m_SourceTexture == 0 || m_Width == 0 || m_Height == 0 || m_SourcePixels.empty()) {
        outW = outH = 0;
        return {};
    }

    outW = m_Width;
    outH = m_Height;

    std::vector<unsigned char> pixels = m_SourcePixels;
    const int rowSize = m_Width * std::max(1, m_SourceChannels);
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; ++y) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    return pixels;
}
