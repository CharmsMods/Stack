#include "Renderer/RenderPipeline.h"

#include "Composite/EmbeddedCompositeFont.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include <imstb_truetype.h>

namespace {

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
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

std::vector<float> BuildBlurKernel(const int radius) {
    if (radius <= 0) {
        return { 1.0f };
    }

    std::vector<float> kernel(static_cast<std::size_t>(radius) * 2u + 1u, 0.0f);
    const float sigma = std::max(0.5f, static_cast<float>(radius) * 0.5f);
    const float twoSigmaSq = 2.0f * sigma * sigma;
    float sum = 0.0f;
    for (int tap = -radius; tap <= radius; ++tap) {
        const float value = std::exp(-(static_cast<float>(tap * tap)) / twoSigmaSq);
        kernel[static_cast<std::size_t>(tap + radius)] = value;
        sum += value;
    }

    if (sum > 0.0f) {
        for (float& value : kernel) {
            value /= sum;
        }
    }
    return kernel;
}

void BlurAlphaMask(
    const std::vector<float>& input,
    const int width,
    const int height,
    const int radius,
    std::vector<float>& output) {
    output = input;
    if (radius <= 0 || width <= 0 || height <= 0 || input.empty()) {
        return;
    }

    const std::vector<float> kernel = BuildBlurKernel(radius);
    std::vector<float> temp(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int tap = -radius; tap <= radius; ++tap) {
                const int sampleX = std::clamp(x + tap, 0, width - 1);
                sum += input[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sampleX)] *
                    kernel[static_cast<std::size_t>(tap + radius)];
            }
            temp[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = sum;
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int tap = -radius; tap <= radius; ++tap) {
                const int sampleY = std::clamp(y + tap, 0, height - 1);
                sum += temp[static_cast<std::size_t>(sampleY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] *
                    kernel[static_cast<std::size_t>(tap + radius)];
            }
            output[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = sum;
        }
    }
}

void CompositeTextBackdrop(
    const RenderImageGeneratorSettings& settings,
    std::vector<unsigned char>& pixels,
    const int width,
    const int height) {
    if (pixels.empty() || width <= 0 || height <= 0) {
        return;
    }

    const float backdropOpacity = Clamp01(settings.textBackdropOpacity);
    const float backdropBlur = std::max(0.0f, settings.textBackdropBlur);
    const float paddingPx = std::max(0.0f, settings.textBackdropPadding);
    if (backdropOpacity <= 0.0f || (backdropBlur <= 0.0f && paddingPx <= 0.0f)) {
        return;
    }

    std::vector<float> alphaMask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            alphaMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                static_cast<float>(pixels[index + 3]) / 255.0f;
        }
    }

    const int dilationRadius = static_cast<int>(std::round(paddingPx));
    if (dilationRadius > 0) {
        std::vector<float> dilated = alphaMask;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float maxAlpha = alphaMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                for (int dy = -dilationRadius; dy <= dilationRadius; ++dy) {
                    const int sampleY = std::clamp(y + dy, 0, height - 1);
                    for (int dx = -dilationRadius; dx <= dilationRadius; ++dx) {
                        if (dx * dx + dy * dy > dilationRadius * dilationRadius) {
                            continue;
                        }
                        const int sampleX = std::clamp(x + dx, 0, width - 1);
                        maxAlpha = std::max(
                            maxAlpha,
                            alphaMask[static_cast<std::size_t>(sampleY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sampleX)]);
                    }
                }
                dilated[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = maxAlpha;
            }
        }
        alphaMask.swap(dilated);
    }

    std::vector<float> blurredMask;
    BlurAlphaMask(alphaMask, width, height, static_cast<int>(std::round(backdropBlur)), blurredMask);

    const float tintR = Clamp01(settings.colorB[0]);
    const float tintG = Clamp01(settings.colorB[1]);
    const float tintB = Clamp01(settings.colorB[2]);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t pixelIndex =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            const float glyphAlpha = static_cast<float>(pixels[pixelIndex + 3]) / 255.0f;
            const float backdropAlpha = Clamp01(
                blurredMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] * backdropOpacity);
            const float effectiveBackdropAlpha = backdropAlpha * (1.0f - glyphAlpha);
            if (effectiveBackdropAlpha <= 0.0f) {
                continue;
            }

            const float dstAlpha = glyphAlpha;
            const float outAlpha = effectiveBackdropAlpha + dstAlpha * (1.0f - effectiveBackdropAlpha);
            if (outAlpha <= 0.0f) {
                continue;
            }

            auto blendChannel = [&](const float srcNorm, const uint8_t dstChannel) {
                const float dstNorm = static_cast<float>(dstChannel) / 255.0f;
                return static_cast<uint8_t>(std::round(
                    Clamp01(((srcNorm * effectiveBackdropAlpha) + (dstNorm * dstAlpha * (1.0f - effectiveBackdropAlpha))) / outAlpha) * 255.0f));
            };

            pixels[pixelIndex + 0] = blendChannel(tintR, pixels[pixelIndex + 0]);
            pixels[pixelIndex + 1] = blendChannel(tintG, pixels[pixelIndex + 1]);
            pixels[pixelIndex + 2] = blendChannel(tintB, pixels[pixelIndex + 2]);
            pixels[pixelIndex + 3] = static_cast<uint8_t>(std::round(Clamp01(outAlpha) * 255.0f));
        }
    }
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

    CompositeTextBackdrop(node.imageGeneratorSettings, outPixels, canvasWidth, canvasHeight);
    return true;
}

} // namespace

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
