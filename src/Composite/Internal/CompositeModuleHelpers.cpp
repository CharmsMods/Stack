#include "Composite/Internal/CompositeModuleInternal.h"

#include "Composite/EmbeddedCompositeFont.h"
#include "Editor/EditorModule.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <imstb_truetype.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <unordered_set>

using json = StackBinaryFormat::json;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTextRasterZoomThresholdUp = 1.2f;
constexpr float kTextRasterZoomThresholdDown = 0.6f;

void PngWriteCallback(void* context, void* data, int size) {
    if (!context || !data || size <= 0) {
        return;
    }

    auto* bytes = static_cast<std::vector<uint8_t>*>(context);
    const auto* begin = static_cast<const uint8_t*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

std::string TrimWhitespace(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }

    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}
std::string NewLayerId() {
    static int s_counter = 0;
    ++s_counter;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "layer-%d-%ld", s_counter, static_cast<long>(std::time(nullptr)));
    return std::string(buffer);
}

std::filesystem::path FindBundledCompositeFontPath() {
    std::filesystem::path current = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        const std::filesystem::path candidate = current / kDefaultCompositeFontRelativePath;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!current.has_parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return {};
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& outBytes) {
    outBytes.clear();
    if (path.empty()) {
        return false;
    }

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return false;
    }

    const std::streamsize size = stream.tellg();
    if (size <= 0) {
        return false;
    }

    outBytes.resize(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    return stream.read(reinterpret_cast<char*>(outBytes.data()), size).good();
}

std::vector<int> Utf8ToCodepoints(const std::string& text) {
    std::vector<int> codepoints;
    codepoints.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[index]);
        if (c < 0x80) {
            codepoints.push_back(static_cast<int>(c));
            ++index;
            continue;
        }

        int codepoint = 0;
        std::size_t extra = 0;
        if ((c & 0xE0u) == 0xC0u) {
            codepoint = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0u) == 0xE0u) {
            codepoint = c & 0x0Fu;
            extra = 2;
        } else if ((c & 0xF8u) == 0xF0u) {
            codepoint = c & 0x07u;
            extra = 3;
        } else {
            codepoints.push_back('?');
            ++index;
            continue;
        }

        if (index + extra >= text.size()) {
            codepoints.push_back('?');
            break;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset <= extra; ++offset) {
            const unsigned char cc = static_cast<unsigned char>(text[index + offset]);
            if ((cc & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (cc & 0x3Fu);
        }

        codepoints.push_back(valid ? codepoint : '?');
        index += valid ? (extra + 1) : 1;
    }
    return codepoints;
}

void FillSolidRgba(
    std::vector<uint8_t>& outPixels,
    const int width,
    const int height,
    const std::array<float, 4>& color,
    const bool circleMask) {
    outPixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    const uint8_t r = static_cast<uint8_t>(std::round(Clamp01(color[0]) * 255.0f));
    const uint8_t g = static_cast<uint8_t>(std::round(Clamp01(color[1]) * 255.0f));
    const uint8_t b = static_cast<uint8_t>(std::round(Clamp01(color[2]) * 255.0f));
    const float alpha = Clamp01(color[3]);

    const float cx = (static_cast<float>(width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(height) - 1.0f) * 0.5f;
    const float rx = std::max(0.5f, static_cast<float>(width) * 0.5f);
    const float ry = std::max(0.5f, static_cast<float>(height) * 0.5f);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float coverage = 1.0f;
            if (circleMask) {
                const float dx = (static_cast<float>(x) - cx) / rx;
                const float dy = (static_cast<float>(y) - cy) / ry;
                const float dist = std::sqrt(dx * dx + dy * dy);
                coverage = Clamp01(1.5f - dist);
                if (coverage <= 0.0f) {
                    continue;
                }
            }

            const std::size_t pixelIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            outPixels[pixelIndex + 0] = r;
            outPixels[pixelIndex + 1] = g;
            outPixels[pixelIndex + 2] = b;
            outPixels[pixelIndex + 3] = static_cast<uint8_t>(std::round(alpha * coverage * 255.0f));
        }
    }
}

bool BuildTextRgba(
    const std::vector<unsigned char>& fontBytes,
    const std::string& text,
    const float fontSize,
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
    const int textureLimit = SafeCompositeTextTextureLimit();
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
            int advance = 0;
            int leftSideBearing = 0;
            stbtt_GetCodepointHMetrics(&fontInfo, codepoint, &advance, &leftSideBearing);
            penX += advance * scaleX;
            maxLineWidth = std::max(maxLineWidth, penX);
            previousCodepoint = codepoint;
        }

        const int candidateWidth = std::max(
            1,
            static_cast<int>(std::ceil(maxLineWidth + pixelSize * 0.4f * requestedStretchX)));
        const int candidateHeight = std::max(
            1,
            static_cast<int>(std::ceil(penY + std::max(lineAdvance, pixelSize * 0.5f * requestedStretchY))));

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
            // Keep transparent texels tinted with the text color so linear sampling
            // around glyph edges does not pull in a dark fringe from black RGB.
            outPixels[pixelIndex + 0] = r;
            outPixels[pixelIndex + 1] = g;
            outPixels[pixelIndex + 2] = b;
        }

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

            const int destX = static_cast<int>(std::floor(placement.penX + static_cast<float>(glyphXOff)));
            const int destY = static_cast<int>(std::floor(placement.baselineY + static_cast<float>(glyphYOff)));
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

bool HasMeaningfulPixels(const std::vector<uint8_t>& pixels) {
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        if (pixels[index + 0] > 4 || pixels[index + 1] > 4 || pixels[index + 2] > 4 || pixels[index + 3] > 4) {
            return true;
        }
    }

    return false;
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

int SafeCompositeTextTextureLimit() {
    static int cachedLimit = 0;
    if (cachedLimit > 0) {
        return cachedLimit;
    }

    GLint textureLimit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
    if (textureLimit <= 0) {
        textureLimit = 8192;
    }

    cachedLimit = std::clamp(textureLimit, 1024, 16384);
    return cachedLimit;
}

float EffectiveTextRasterZoom(float viewZoom) {
    return std::max(1.0f, viewZoom);
}

float DegreesToRadians(const float degrees) {
    return degrees * kPi / 180.0f;
}

float RadiansToDegrees(const float radians) {
    return radians * 180.0f / kPi;
}

const char* LayerKindBadge(const LayerKind kind) {
    switch (kind) {
    case LayerKind::ShapeRect:
        return "Square";
    case LayerKind::ShapeCircle:
        return "Circle";
    case LayerKind::Text:
        return "Text";
    case LayerKind::EditorProject:
        return "Project";
    case LayerKind::Image:
    default:
        return "Image";
    }
}

const char* LayerKindToToken(const LayerKind kind) {
    switch (kind) {
    case LayerKind::EditorProject:
        return "editor-project";
    case LayerKind::ShapeRect:
        return "shape-rect";
    case LayerKind::ShapeCircle:
        return "shape-circle";
    case LayerKind::Text:
        return "text";
    case LayerKind::Image:
    default:
        return "image";
    }
}

LayerKind LayerKindFromToken(const std::string& token) {
    if (token == "editor-project") return LayerKind::EditorProject;
    if (token == "shape-rect") return LayerKind::ShapeRect;
    if (token == "shape-circle") return LayerKind::ShapeCircle;
    if (token == "text") return LayerKind::Text;
    return LayerKind::Image;
}

bool IsEditorBridgeLayer(const CompositeLayer& layer) {
    return layer.kind == LayerKind::Image || layer.kind == LayerKind::EditorProject;
}

bool IsGeneratedLayer(const CompositeLayer& layer) {
    return layer.kind == LayerKind::ShapeRect ||
           layer.kind == LayerKind::ShapeCircle ||
           layer.kind == LayerKind::Text;
}

const char* BlendModeToLabel(const CompositeBlendMode mode) {
    switch (mode) {
    case CompositeBlendMode::Multiply:
        return "Multiply";
    case CompositeBlendMode::Screen:
        return "Screen";
    case CompositeBlendMode::Add:
        return "Add";
    case CompositeBlendMode::Overlay:
        return "Overlay";
    case CompositeBlendMode::SoftLight:
        return "Soft Light";
    case CompositeBlendMode::HardLight:
        return "Hard Light";
    case CompositeBlendMode::Hue:
        return "Hue";
    case CompositeBlendMode::Color:
        return "Color";
    case CompositeBlendMode::Normal:
    default:
        return "Normal";
    }
}

const char* BlendModeToToken(const CompositeBlendMode mode) {
    switch (mode) {
    case CompositeBlendMode::Multiply:
        return "multiply";
    case CompositeBlendMode::Screen:
        return "screen";
    case CompositeBlendMode::Add:
        return "add";
    case CompositeBlendMode::Overlay:
        return "overlay";
    case CompositeBlendMode::SoftLight:
        return "soft-light";
    case CompositeBlendMode::HardLight:
        return "hard-light";
    case CompositeBlendMode::Hue:
        return "hue";
    case CompositeBlendMode::Color:
        return "color";
    case CompositeBlendMode::Normal:
    default:
        return "normal";
    }
}

CompositeBlendMode BlendModeFromToken(const std::string& token) {
    if (token == "multiply") return CompositeBlendMode::Multiply;
    if (token == "screen") return CompositeBlendMode::Screen;
    if (token == "add") return CompositeBlendMode::Add;
    if (token == "overlay") return CompositeBlendMode::Overlay;
    if (token == "soft-light") return CompositeBlendMode::SoftLight;
    if (token == "hard-light") return CompositeBlendMode::HardLight;
    if (token == "hue") return CompositeBlendMode::Hue;
    if (token == "color") return CompositeBlendMode::Color;
    return CompositeBlendMode::Normal;
}

const char* ExportBoundsModeToToken(const CompositeExportBoundsMode mode) {
    return mode == CompositeExportBoundsMode::Custom ? "custom" : "auto";
}

CompositeExportBoundsMode ExportBoundsModeFromToken(const std::string& token) {
    return token == "custom" ? CompositeExportBoundsMode::Custom : CompositeExportBoundsMode::Auto;
}

const char* ExportBackgroundModeToToken(const CompositeExportBackgroundMode mode) {
    return mode == CompositeExportBackgroundMode::Solid ? "solid" : "transparent";
}

CompositeExportBackgroundMode ExportBackgroundModeFromToken(const std::string& token) {
    return token == "solid" ? CompositeExportBackgroundMode::Solid : CompositeExportBackgroundMode::Transparent;
}

const char* ExportAspectPresetToToken(const CompositeExportAspectPreset preset) {
    switch (preset) {
    case CompositeExportAspectPreset::Ratio4x3:
        return "4:3";
    case CompositeExportAspectPreset::Ratio3x2:
        return "3:2";
    case CompositeExportAspectPreset::Ratio16x9:
        return "16:9";
    case CompositeExportAspectPreset::Ratio9x16:
        return "9:16";
    case CompositeExportAspectPreset::Ratio2x3:
        return "2:3";
    case CompositeExportAspectPreset::Ratio5x4:
        return "5:4";
    case CompositeExportAspectPreset::Ratio21x9:
        return "21:9";
    case CompositeExportAspectPreset::Custom:
        return "custom";
    case CompositeExportAspectPreset::Ratio1x1:
    default:
        return "1:1";
    }
}

CompositeExportAspectPreset ExportAspectPresetFromToken(const std::string& token) {
    if (token == "4:3") return CompositeExportAspectPreset::Ratio4x3;
    if (token == "3:2") return CompositeExportAspectPreset::Ratio3x2;
    if (token == "16:9") return CompositeExportAspectPreset::Ratio16x9;
    if (token == "9:16") return CompositeExportAspectPreset::Ratio9x16;
    if (token == "2:3") return CompositeExportAspectPreset::Ratio2x3;
    if (token == "5:4") return CompositeExportAspectPreset::Ratio5x4;
    if (token == "21:9") return CompositeExportAspectPreset::Ratio21x9;
    if (token == "custom") return CompositeExportAspectPreset::Custom;
    return CompositeExportAspectPreset::Ratio1x1;
}

const char* ExportAspectPresetToLabel(const CompositeExportAspectPreset preset) {
    switch (preset) {
    case CompositeExportAspectPreset::Ratio4x3:
        return "4:3";
    case CompositeExportAspectPreset::Ratio3x2:
        return "3:2";
    case CompositeExportAspectPreset::Ratio16x9:
        return "16:9";
    case CompositeExportAspectPreset::Ratio9x16:
        return "9:16";
    case CompositeExportAspectPreset::Ratio2x3:
        return "2:3";
    case CompositeExportAspectPreset::Ratio5x4:
        return "5:4";
    case CompositeExportAspectPreset::Ratio21x9:
        return "21:9";
    case CompositeExportAspectPreset::Custom:
        return "Custom";
    case CompositeExportAspectPreset::Ratio1x1:
    default:
        return "1:1";
    }
}

float ExportAspectRatioValue(const CompositeExportSettings& settings) {
    switch (settings.aspectPreset) {
    case CompositeExportAspectPreset::Ratio4x3:
        return 4.0f / 3.0f;
    case CompositeExportAspectPreset::Ratio3x2:
        return 3.0f / 2.0f;
    case CompositeExportAspectPreset::Ratio16x9:
        return 16.0f / 9.0f;
    case CompositeExportAspectPreset::Ratio9x16:
        return 9.0f / 16.0f;
    case CompositeExportAspectPreset::Ratio2x3:
        return 2.0f / 3.0f;
    case CompositeExportAspectPreset::Ratio5x4:
        return 5.0f / 4.0f;
    case CompositeExportAspectPreset::Ratio21x9:
        return 21.0f / 9.0f;
    case CompositeExportAspectPreset::Custom:
        return std::max(0.0001f, settings.customAspectRatio);
    case CompositeExportAspectPreset::Ratio1x1:
    default:
        return 1.0f;
    }
}

CompositeLayer* FindLayerById(std::vector<CompositeLayer>& layers, const std::string& layerId) {
    for (CompositeLayer& layer : layers) {
        if (layer.id == layerId) {
            return &layer;
        }
    }

    return nullptr;
}

const CompositeLayer* FindLayerById(const std::vector<CompositeLayer>& layers, const std::string& layerId) {
    for (const CompositeLayer& layer : layers) {
        if (layer.id == layerId) {
            return &layer;
        }
    }

    return nullptr;
}

float LayerBaseWidth(const CompositeLayer& layer) {
    return static_cast<float>((layer.logicalW > 0) ? layer.logicalW : layer.imgW);
}

float LayerBaseHeight(const CompositeLayer& layer) {
    return static_cast<float>((layer.logicalH > 0) ? layer.logicalH : layer.imgH);
}

float LayerWorldWidth(const CompositeLayer& layer) {
    return LayerBaseWidth(layer) * std::max(0.0001f, layer.scaleX);
}

float LayerWorldHeight(const CompositeLayer& layer) {
    return LayerBaseHeight(layer) * std::max(0.0001f, layer.scaleY);
}

AffineTransform2D IdentityTransform2D() {
    return {};
}

AffineTransform2D Multiply(const AffineTransform2D& a, const AffineTransform2D& b) {
    return {
        a.m00 * b.m00 + a.m01 * b.m10,
        a.m00 * b.m01 + a.m01 * b.m11,
        a.m00 * b.m02 + a.m01 * b.m12 + a.m02,
        a.m10 * b.m00 + a.m11 * b.m10,
        a.m10 * b.m01 + a.m11 * b.m11,
        a.m10 * b.m02 + a.m11 * b.m12 + a.m12
    };
}

AffineTransform2D Inverse(const AffineTransform2D& matrix) {
    const float determinant = matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10;
    if (std::abs(determinant) <= 1e-8f) {
        return IdentityTransform2D();
    }

    const float inverseDeterminant = 1.0f / determinant;
    AffineTransform2D inverse;
    inverse.m00 = matrix.m11 * inverseDeterminant;
    inverse.m01 = -matrix.m01 * inverseDeterminant;
    inverse.m02 = (matrix.m01 * matrix.m12 - matrix.m11 * matrix.m02) * inverseDeterminant;
    inverse.m10 = -matrix.m10 * inverseDeterminant;
    inverse.m11 = matrix.m00 * inverseDeterminant;
    inverse.m12 = (matrix.m10 * matrix.m02 - matrix.m00 * matrix.m12) * inverseDeterminant;
    return inverse;
}

ImVec2 TransformPoint(const AffineTransform2D& matrix, const ImVec2& point) {
    return {
        matrix.m00 * point.x + matrix.m01 * point.y + matrix.m02,
        matrix.m10 * point.x + matrix.m11 * point.y + matrix.m12
    };
}

ImVec2 TransformVector(const AffineTransform2D& matrix, const ImVec2& vector) {
    return {
        matrix.m00 * vector.x + matrix.m01 * vector.y,
        matrix.m10 * vector.x + matrix.m11 * vector.y
    };
}

AffineTransform2D BuildLocalTransform(const CompositeLayer& layer) {
    const float baseWidth = std::max(1.0f, LayerBaseWidth(layer));
    const float baseHeight = std::max(1.0f, LayerBaseHeight(layer));
    const float width = baseWidth * std::max(0.0001f, layer.scaleX);
    const float height = baseHeight * std::max(0.0001f, layer.scaleY);
    const float cosR = std::cos(layer.rotation);
    const float sinR = std::sin(layer.rotation);

    AffineTransform2D matrix;
    matrix.m00 = cosR * std::max(0.0001f, layer.scaleX);
    matrix.m01 = -sinR * std::max(0.0001f, layer.scaleY);
    matrix.m10 = sinR * std::max(0.0001f, layer.scaleX);
    matrix.m11 = cosR * std::max(0.0001f, layer.scaleY);
    matrix.m02 = layer.x + width * 0.5f - matrix.m00 * baseWidth * 0.5f - matrix.m01 * baseHeight * 0.5f;
    matrix.m12 = layer.y + height * 0.5f - matrix.m10 * baseWidth * 0.5f - matrix.m11 * baseHeight * 0.5f;
    return matrix;
}

AffineTransform2D BuildWorldTransform(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer) {
    std::vector<const CompositeLayer*> chain;
    chain.reserve(8);
    std::unordered_set<std::string> visited;

    const CompositeLayer* current = &layer;
    while (current != nullptr) {
        if (!visited.insert(current->id).second) {
            break;
        }
        chain.push_back(current);
        if (current->parentId.empty()) {
            break;
        }
        current = FindLayerById(layers, current->parentId);
    }

    AffineTransform2D world = IdentityTransform2D();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        world = Multiply(world, BuildLocalTransform(**it));
    }

    return world;
}

ImVec2 GetWorldCenter(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer) {
    const AffineTransform2D world = BuildWorldTransform(layers, layer);
    const ImVec2 localCenter(
        std::max(1.0f, LayerBaseWidth(layer)) * 0.5f,
        std::max(1.0f, LayerBaseHeight(layer)) * 0.5f);
    return TransformPoint(world, localCenter);
}

std::array<ImVec2, 4> ComputeLayerQuadWorld(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer) {
    const AffineTransform2D world = BuildWorldTransform(layers, layer);
    const float width = std::max(1.0f, LayerBaseWidth(layer));
    const float height = std::max(1.0f, LayerBaseHeight(layer));
    return {
        TransformPoint(world, ImVec2(0.0f, 0.0f)),
        TransformPoint(world, ImVec2(width, 0.0f)),
        TransformPoint(world, ImVec2(width, height)),
        TransformPoint(world, ImVec2(0.0f, height))
    };
}

FloatRect ComputeLayerBoundsWorld(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer) {
    const auto quad = ComputeLayerQuadWorld(layers, layer);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    for (const ImVec2& point : quad) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    return { minX, minY, std::max(0.0f, maxX - minX), std::max(0.0f, maxY - minY) };
}

AffineTransform2D GetParentWorldTransform(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer) {
    if (layer.parentId.empty()) {
        return IdentityTransform2D();
    }

    const CompositeLayer* parent = FindLayerById(layers, layer.parentId);
    if (!parent) {
        return IdentityTransform2D();
    }

    return BuildWorldTransform(layers, *parent);
}

std::array<ImVec2, 4> ComputeLayerQuadLocal(const CompositeLayer& layer) {
    const AffineTransform2D local = BuildLocalTransform(layer);
    const float width = std::max(1.0f, LayerBaseWidth(layer));
    const float height = std::max(1.0f, LayerBaseHeight(layer));
    return {
        TransformPoint(local, ImVec2(0.0f, 0.0f)),
        TransformPoint(local, ImVec2(width, 0.0f)),
        TransformPoint(local, ImVec2(width, height)),
        TransformPoint(local, ImVec2(0.0f, height))
    };
}

ImVec2 LayerLocalCenter(const CompositeLayer& layer) {
    return {
        layer.x + LayerWorldWidth(layer) * 0.5f,
        layer.y + LayerWorldHeight(layer) * 0.5f
    };
}

ImVec2 LayerLocalAxisX(const CompositeLayer& layer) {
    return {
        std::cos(layer.rotation),
        std::sin(layer.rotation)
    };
}

ImVec2 LayerLocalAxisY(const CompositeLayer& layer) {
    return {
        -std::sin(layer.rotation),
        std::cos(layer.rotation)
    };
}

std::array<float, 16> AffineToGlMatrix4(const AffineTransform2D& matrix) {
    return {
        matrix.m00, matrix.m10, 0.0f, 0.0f,
        matrix.m01, matrix.m11, 0.0f, 0.0f,
        0.0f,       0.0f,       1.0f, 0.0f,
        matrix.m02, matrix.m12, 0.0f, 1.0f
    };
}

void ApplyAffineToLayerParameters(CompositeLayer& layer, const AffineTransform2D& matrix) {
    layer.x = matrix.m02;
    layer.y = matrix.m12;

    const float scaleX = std::hypot(matrix.m00, matrix.m10);
    const float scaleY = std::hypot(matrix.m01, matrix.m11);
    layer.scaleX = std::max(0.0001f, scaleX);
    layer.scaleY = std::max(0.0001f, scaleY);
    layer.rotation = std::atan2(matrix.m10, matrix.m00);
}

bool WouldCreateParentCycle(
    const std::vector<CompositeLayer>& layers,
    const CompositeLayer& layer,
    const std::string& proposedParentId) {

    if (proposedParentId.empty()) {
        return false;
    }

    if (proposedParentId == layer.id) {
        return true;
    }

    std::unordered_set<std::string> visited;
    std::string currentParentId = proposedParentId;
    while (!currentParentId.empty()) {
        if (currentParentId == layer.id) {
            return true;
        }
        if (!visited.insert(currentParentId).second) {
            return true;
        }
        const CompositeLayer* parent = FindLayerById(layers, currentParentId);
        if (!parent) {
            return false;
        }
        currentParentId = parent->parentId;
    }

    return false;
}

bool IsLayerDescendantOf(
    const std::vector<CompositeLayer>& layers,
    const std::string& candidateId,
    const std::string& ancestorId) {

    if (candidateId.empty() || ancestorId.empty() || candidateId == ancestorId) {
        return false;
    }

    std::unordered_set<std::string> visited;
    std::string currentId = candidateId;
    while (!currentId.empty()) {
        if (!visited.insert(currentId).second) {
            return false;
        }

        const CompositeLayer* current = FindLayerById(layers, currentId);
        if (!current) {
            return false;
        }

        if (current->parentId == ancestorId) {
            return true;
        }

        currentId = current->parentId;
    }

    return false;
}

bool SetLayerParentPreserveWorld(
    std::vector<CompositeLayer>& layers,
    CompositeLayer& layer,
    const std::string& newParentId) {

    if (layer.parentId == newParentId) {
        return false;
    }

    if (WouldCreateParentCycle(layers, layer, newParentId)) {
        return false;
    }

    const AffineTransform2D worldBefore = BuildWorldTransform(layers, layer);
    layer.parentId = newParentId;
    const AffineTransform2D parentWorld = GetParentWorldTransform(layers, layer);
    const AffineTransform2D local = Multiply(Inverse(parentWorld), worldBefore);
    ApplyAffineToLayerParameters(layer, local);
    return true;
}

void DetachChildrenFromParent(std::vector<CompositeLayer>& layers, const std::string& parentId) {
    for (CompositeLayer& layer : layers) {
        if (layer.parentId == parentId) {
            SetLayerParentPreserveWorld(layers, layer, std::string());
        }
    }
}

bool IsRectValid(const FloatRect& rect) {
    return std::isfinite(rect.x) &&
           std::isfinite(rect.y) &&
           std::isfinite(rect.width) &&
           std::isfinite(rect.height) &&
           rect.width > 0.0f &&
           rect.height > 0.0f;
}

float RectAspectRatio(const FloatRect& rect) {
    return std::max(1.0f, rect.width) / std::max(1.0f, rect.height);
}

FloatRect MakeNormalizedRect(const float x1, const float y1, const float x2, const float y2) {
    const float left = std::min(x1, x2);
    const float top = std::min(y1, y2);
    const float right = std::max(x1, x2);
    const float bottom = std::max(y1, y2);
    return { left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top) };
}

FloatRect ComputeViewWorldRect(const float canvasWidth, const float canvasHeight, const float viewZoom, const float viewPanX, const float viewPanY) {
    const float width = std::max(1.0f, canvasWidth) / std::max(0.001f, viewZoom);
    const float height = std::max(1.0f, canvasHeight) / std::max(0.001f, viewZoom);
    const float x = (-canvasWidth * 0.5f - viewPanX) / std::max(0.001f, viewZoom);
    const float y = (-canvasHeight * 0.5f - viewPanY) / std::max(0.001f, viewZoom);
    return { x, y, width, height };
}

ImVec2 WorldToScreen(const ImVec2& canvasCenter, const float viewZoom, const float viewPanX, const float viewPanY, const ImVec2& worldPoint) {
    return ImVec2(
        canvasCenter.x + viewPanX + worldPoint.x * viewZoom,
        canvasCenter.y + viewPanY + worldPoint.y * viewZoom);
}

ImVec2 ScreenToWorld(const ImVec2& canvasCenter, const float viewZoom, const float viewPanX, const float viewPanY, const ImVec2& screenPoint) {
    return ImVec2(
        (screenPoint.x - canvasCenter.x - viewPanX) / std::max(0.001f, viewZoom),
        (screenPoint.y - canvasCenter.y - viewPanY) / std::max(0.001f, viewZoom));
}

bool MapWorldToLayerUv(
    const std::vector<CompositeLayer>& layers,
    const CompositeLayer& layer,
    const float worldX,
    const float worldY,
    float& outU,
    float& outV) {

    const float width = std::max(1.0f, LayerBaseWidth(layer));
    const float height = std::max(1.0f, LayerBaseHeight(layer));
    const AffineTransform2D world = BuildWorldTransform(layers, layer);
    const AffineTransform2D inverseWorld = Inverse(world);
    const ImVec2 local = TransformPoint(inverseWorld, ImVec2(worldX, worldY));
    if (local.x < 0.0f || local.x > width || local.y < 0.0f || local.y > height) {
        return false;
    }

    float u = local.x / width;
    float v = local.y / height;
    if (layer.flipX) {
        u = 1.0f - u;
    }
    if (layer.flipY) {
        v = 1.0f - v;
    }

    outU = Clamp01(u);
    outV = Clamp01(v);
    return true;
}

CompositeLayer* FindTopMostVisibleLayerAtWorldPoint(std::vector<CompositeLayer>& layers, const float worldX, const float worldY) {
    CompositeLayer* bestLayer = nullptr;
    int bestZ = std::numeric_limits<int>::lowest();
    for (CompositeLayer& layer : layers) {
        if (!layer.visible || layer.rgba.empty()) {
            continue;
        }

        float u = 0.0f;
        float v = 0.0f;
        if (MapWorldToLayerUv(layers, layer, worldX, worldY, u, v) && layer.z > bestZ) {
            bestLayer = &layer;
            bestZ = layer.z;
        }
    }

    return bestLayer;
}

void SampleLayerPixelBilinear(const CompositeLayer& layer, const float u, const float v, float outRgba[4]) {
    if (layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
        outRgba[0] = outRgba[1] = outRgba[2] = outRgba[3] = 0.0f;
        return;
    }

    const float srcX = Clamp01(u) * static_cast<float>(std::max(0, layer.imgW - 1));
    const float srcY = (1.0f - Clamp01(v)) * static_cast<float>(std::max(0, layer.imgH - 1));
    const int x0 = std::clamp(static_cast<int>(std::floor(srcX)), 0, layer.imgW - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(srcY)), 0, layer.imgH - 1);
    const int x1 = std::clamp(x0 + 1, 0, layer.imgW - 1);
    const int y1 = std::clamp(y0 + 1, 0, layer.imgH - 1);
    const float tx = srcX - static_cast<float>(x0);
    const float ty = srcY - static_cast<float>(y0);

    auto readTexel = [&](const int x, const int y, float rgba[4]) {
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.imgW) + static_cast<std::size_t>(x)) * 4;
        rgba[0] = layer.rgba[index + 0] / 255.0f;
        rgba[1] = layer.rgba[index + 1] / 255.0f;
        rgba[2] = layer.rgba[index + 2] / 255.0f;
        rgba[3] = layer.rgba[index + 3] / 255.0f;
    };

    float c00[4] = {};
    float c10[4] = {};
    float c01[4] = {};
    float c11[4] = {};
    readTexel(x0, y0, c00);
    readTexel(x1, y0, c10);
    readTexel(x0, y1, c01);
    readTexel(x1, y1, c11);

    for (int channel = 0; channel < 4; ++channel) {
        const float top = c00[channel] + (c10[channel] - c00[channel]) * tx;
        const float bottom = c01[channel] + (c11[channel] - c01[channel]) * tx;
        outRgba[channel] = top + (bottom - top) * ty;
    }
}

float BlendChannelOverlay(const float src, const float dst) {
    return (dst <= 0.5f) ? (2.0f * src * dst) : (1.0f - 2.0f * (1.0f - src) * (1.0f - dst));
}

float BlendChannelHardLight(const float src, const float dst) {
    return (src <= 0.5f) ? (2.0f * src * dst) : (1.0f - 2.0f * (1.0f - src) * (1.0f - dst));
}

float SoftLightCurve(const float value) {
    if (value <= 0.25f) {
        return ((16.0f * value - 12.0f) * value + 4.0f) * value;
    }

    return std::sqrt(value);
}

float BlendChannelSoftLight(const float src, const float dst) {
    if (src <= 0.5f) {
        return dst - (1.0f - 2.0f * src) * dst * (1.0f - dst);
    }

    return dst + (2.0f * src - 1.0f) * (SoftLightCurve(dst) - dst);
}

void RGBToHSL(const float rgb[3], float& outHue, float& outSaturation, float& outLightness) {
    const float r = Clamp01(rgb[0]);
    const float g = Clamp01(rgb[1]);
    const float b = Clamp01(rgb[2]);
    const float maxValue = std::max({ r, g, b });
    const float minValue = std::min({ r, g, b });
    const float delta = maxValue - minValue;

    outLightness = (maxValue + minValue) * 0.5f;
    if (delta <= 1e-6f) {
        outHue = 0.0f;
        outSaturation = 0.0f;
        return;
    }

    outSaturation = (outLightness > 0.5f)
        ? (delta / (2.0f - maxValue - minValue))
        : (delta / (maxValue + minValue));

    if (maxValue == r) {
        outHue = (g - b) / delta + (g < b ? 6.0f : 0.0f);
    } else if (maxValue == g) {
        outHue = (b - r) / delta + 2.0f;
    } else {
        outHue = (r - g) / delta + 4.0f;
    }

    outHue /= 6.0f;
}

float HueToRGB(const float p, const float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

void HSLToRGB(const float hue, const float saturation, const float lightness, float outRgb[3]) {
    if (saturation <= 1e-6f) {
        outRgb[0] = lightness;
        outRgb[1] = lightness;
        outRgb[2] = lightness;
        return;
    }

    const float q = (lightness < 0.5f)
        ? (lightness * (1.0f + saturation))
        : (lightness + saturation - lightness * saturation);
    const float p = 2.0f * lightness - q;

    outRgb[0] = HueToRGB(p, q, hue + 1.0f / 3.0f);
    outRgb[1] = HueToRGB(p, q, hue);
    outRgb[2] = HueToRGB(p, q, hue - 1.0f / 3.0f);
}

void BlendRgb(const CompositeBlendMode mode, const float srcRgb[3], const float dstRgb[3], float outRgb[3]) {
    switch (mode) {
    case CompositeBlendMode::Multiply:
        outRgb[0] = srcRgb[0] * dstRgb[0];
        outRgb[1] = srcRgb[1] * dstRgb[1];
        outRgb[2] = srcRgb[2] * dstRgb[2];
        return;
    case CompositeBlendMode::Screen:
        outRgb[0] = 1.0f - (1.0f - srcRgb[0]) * (1.0f - dstRgb[0]);
        outRgb[1] = 1.0f - (1.0f - srcRgb[1]) * (1.0f - dstRgb[1]);
        outRgb[2] = 1.0f - (1.0f - srcRgb[2]) * (1.0f - dstRgb[2]);
        return;
    case CompositeBlendMode::Add:
        outRgb[0] = std::min(1.0f, srcRgb[0] + dstRgb[0]);
        outRgb[1] = std::min(1.0f, srcRgb[1] + dstRgb[1]);
        outRgb[2] = std::min(1.0f, srcRgb[2] + dstRgb[2]);
        return;
    case CompositeBlendMode::Overlay:
        outRgb[0] = BlendChannelOverlay(srcRgb[0], dstRgb[0]);
        outRgb[1] = BlendChannelOverlay(srcRgb[1], dstRgb[1]);
        outRgb[2] = BlendChannelOverlay(srcRgb[2], dstRgb[2]);
        return;
    case CompositeBlendMode::SoftLight:
        outRgb[0] = BlendChannelSoftLight(srcRgb[0], dstRgb[0]);
        outRgb[1] = BlendChannelSoftLight(srcRgb[1], dstRgb[1]);
        outRgb[2] = BlendChannelSoftLight(srcRgb[2], dstRgb[2]);
        return;
    case CompositeBlendMode::HardLight:
        outRgb[0] = BlendChannelHardLight(srcRgb[0], dstRgb[0]);
        outRgb[1] = BlendChannelHardLight(srcRgb[1], dstRgb[1]);
        outRgb[2] = BlendChannelHardLight(srcRgb[2], dstRgb[2]);
        return;
    case CompositeBlendMode::Hue:
    case CompositeBlendMode::Color: {
        float srcHue = 0.0f;
        float srcSat = 0.0f;
        float srcLight = 0.0f;
        float dstHue = 0.0f;
        float dstSat = 0.0f;
        float dstLight = 0.0f;
        RGBToHSL(srcRgb, srcHue, srcSat, srcLight);
        RGBToHSL(dstRgb, dstHue, dstSat, dstLight);
        const float hue = (mode == CompositeBlendMode::Hue || mode == CompositeBlendMode::Color) ? srcHue : dstHue;
        const float sat = (mode == CompositeBlendMode::Color) ? srcSat : dstSat;
        const float light = dstLight;
        HSLToRGB(hue, sat, light, outRgb);
        return;
    }
    case CompositeBlendMode::Normal:
    default:
        outRgb[0] = srcRgb[0];
        outRgb[1] = srcRgb[1];
        outRgb[2] = srcRgb[2];
        return;
    }
}

void CompositeSourceOver(float* dstRgba, const float srcRgba[4], const CompositeBlendMode blendMode) {
    const float srcAlpha = Clamp01(srcRgba[3]);
    if (srcAlpha <= 1e-6f) {
        return;
    }

    const float dstAlpha = Clamp01(dstRgba[3]);
    const float srcRgb[3] = { Clamp01(srcRgba[0]), Clamp01(srcRgba[1]), Clamp01(srcRgba[2]) };
    const float dstRgb[3] = { Clamp01(dstRgba[0]), Clamp01(dstRgba[1]), Clamp01(dstRgba[2]) };
    float blendedRgb[3] = {};
    BlendRgb(blendMode, srcRgb, dstRgb, blendedRgb);

    const float outAlpha = srcAlpha + dstAlpha * (1.0f - srcAlpha);
    if (outAlpha <= 1e-6f) {
        dstRgba[0] = dstRgba[1] = dstRgba[2] = dstRgba[3] = 0.0f;
        return;
    }

    for (int channel = 0; channel < 3; ++channel) {
        const float numerator = blendedRgb[channel] * srcAlpha + dstRgb[channel] * dstAlpha * (1.0f - srcAlpha);
        dstRgba[channel] = Clamp01(numerator / outAlpha);
    }
    dstRgba[3] = Clamp01(outAlpha);
}

bool ComputeAutoBounds(
    const std::vector<CompositeLayer>& allLayers,
    const std::vector<const CompositeLayer*>& layers,
    FloatRect& outBounds) {
    if (layers.empty()) {
        return false;
    }

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    for (const CompositeLayer* layer : layers) {
        if (!layer || !layer->visible || layer->rgba.empty()) {
            continue;
        }

        const FloatRect bounds = ComputeLayerBoundsWorld(allLayers, *layer);
        minX = std::min(minX, bounds.x);
        minY = std::min(minY, bounds.y);
        maxX = std::max(maxX, bounds.x + bounds.width);
        maxY = std::max(maxY, bounds.y + bounds.height);
    }

    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY) || maxX <= minX || maxY <= minY) {
        return false;
    }

    outBounds = { minX, minY, maxX - minX, maxY - minY };
    return true;
}

bool EncodePngBytes(const std::vector<uint8_t>& rgbaTopLeft, const int width, const int height, std::vector<uint8_t>& outPng) {
    outPng.clear();
    if (rgbaTopLeft.empty() || width <= 0 || height <= 0) {
        return false;
    }

    stbi_write_png_to_func(PngWriteCallback, &outPng, width, height, 4, rgbaTopLeft.data(), width * 4);
    return !outPng.empty();
}

void ResizeNearestRgba(
    const std::vector<uint8_t>& src,
    const int sw,
    const int sh,
    const int dw,
    const int dh,
    std::vector<uint8_t>& dst) {

    dst.resize(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4);
    for (int y = 0; y < dh; ++y) {
        const int sy = std::clamp((y * sh) / std::max(1, dh), 0, std::max(0, sh - 1));
        for (int x = 0; x < dw; ++x) {
            const int sx = std::clamp((x * sw) / std::max(1, dw), 0, std::max(0, sw - 1));
            const std::size_t srcIndex = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(sx)) * 4;
            const std::size_t dstIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) + static_cast<std::size_t>(x)) * 4;
            dst[dstIndex + 0] = src[srcIndex + 0];
            dst[dstIndex + 1] = src[srcIndex + 1];
            dst[dstIndex + 2] = src[srcIndex + 2];
            dst[dstIndex + 3] = src[srcIndex + 3];
        }
    }
}

bool BuildTopLeftPngFromBottomLeftRgba(const std::vector<uint8_t>& rgbaBottomLeft, const int width, const int height, std::vector<uint8_t>& outPng) {
    if (rgbaBottomLeft.empty() || width <= 0 || height <= 0) {
        outPng.clear();
        return false;
    }

    std::vector<uint8_t> topLeft = rgbaBottomLeft;
    LibraryManager::FlipImageRowsInPlace(topLeft, width, height, 4);
    return EncodePngBytes(topLeft, width, height, outPng);
}

bool LoadCompositeDisplayRgbaImageFromFile(
    const std::filesystem::path& path,
    std::vector<uint8_t>& outPixels,
    int& outW,
    int& outH) {

    outPixels.clear();
    outW = 0;
    outH = 0;

    if (!std::filesystem::exists(path)) {
        return false;
    }

    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* pixels = stbi_load(path.string().c_str(), &outW, &outH, &channels, 4);
    if (!pixels) {
        return false;
    }

    outPixels.assign(pixels, pixels + static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4);
    stbi_image_free(pixels);
    return true;
}

bool IsPathInsideDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) {
    std::error_code ec;
    const std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }

    const std::filesystem::path normalizedDirectory = std::filesystem::weakly_canonical(directory, ec);
    if (ec) {
        return false;
    }

    auto pathIt = normalizedPath.begin();
    auto dirIt = normalizedDirectory.begin();
    for (; dirIt != normalizedDirectory.end(); ++dirIt, ++pathIt) {
        if (pathIt == normalizedPath.end() || *pathIt != *dirIt) {
            return false;
        }
    }

    return true;
}

void QueueImportedExternalAssetMirror(const std::string& sourcePath, const LayerFileLoadData& data) {
    if (sourcePath.empty() || data.originalSourcePng.empty()) {
        return;
    }

    const std::filesystem::path source(sourcePath);
    const std::filesystem::path libraryAssetsPath = LibraryManager::Get().GetAssetsPath();
    if (!libraryAssetsPath.empty() && std::filesystem::exists(libraryAssetsPath) && IsPathInsideDirectory(source, libraryAssetsPath)) {
        return;
    }

    LibraryManager::Get().QueueLooseAssetSave(
        data.name.empty() ? source.stem().string() : data.name,
        data.originalSourcePng,
        source.filename().string());
}

bool LoadImageLayerDataFromFile(const std::string& path, LayerFileLoadData& outData) {
    outData = LayerFileLoadData {};
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    outData.kind = LayerKind::Image;
    outData.name = std::filesystem::path(path).stem().string();
    outData.previewW = width;
    outData.previewH = height;
    outData.logicalW = width;
    outData.logicalH = height;
    outData.previewPixels.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_image_free(pixels);

    if (!BuildTopLeftPngFromBottomLeftRgba(outData.previewPixels, width, height, outData.originalSourcePng)) {
        return false;
    }

    return true;
}

bool LoadEditorProjectLayerDataFromFile(const std::string& path, const bool limitResolution, LayerFileLoadData& outData) {
    outData = LayerFileLoadData {};

    StackBinaryFormat::ProjectLoadOptions options;
    options.includeThumbnail = false;
    options.includeSourceImage = true;
    options.includePipelineData = true;

    StackBinaryFormat::ProjectDocument document;
    if (!StackBinaryFormat::ReadProjectFile(path, document, options)) {
        return false;
    }

    if (document.metadata.projectKind != StackBinaryFormat::kEditorProjectKind) {
        return false;
    }

    int srcW = 0;
    int srcH = 0;
    int srcC = 4;
    std::vector<uint8_t> srcPixels;
    if (!LibraryManager::DecodeImageBytes(document.sourceImageBytes, srcPixels, srcW, srcH, srcC)) {
        return false;
    }

    std::vector<uint8_t> previewPixels;
    int previewW = 0;
    int previewH = 0;

    const std::filesystem::path projectPath(path);
    const std::filesystem::path assetPath =
        projectPath.parent_path() / "Assets" / (projectPath.stem().string() + ".png");

    if (!LoadCompositeDisplayRgbaImageFromFile(assetPath, previewPixels, previewW, previewH)) {
        EditorModule previewEditor;
        previewEditor.Initialize();

        int renderW = srcW;
        int renderH = srcH;
        std::vector<uint8_t> renderSource = srcPixels;
        if (limitResolution && !renderSource.empty()) {
            const int limit = 4096;
            if (renderW > limit || renderH > limit) {
                const float scale = static_cast<float>(limit) / static_cast<float>(std::max(renderW, renderH));
                const int newW = std::max(1, static_cast<int>(renderW * scale));
                const int newH = std::max(1, static_cast<int>(renderH * scale));
                std::vector<uint8_t> resized;
                ResizeNearestRgba(renderSource, renderW, renderH, newW, newH, resized);
                renderSource = std::move(resized);
                renderW = newW;
                renderH = newH;
            }
        }

        previewEditor.LoadSourceFromPixels(renderSource.data(), renderW, renderH, srcC);
        previewEditor.DeserializePipeline(document.pipelineData);
        previewEditor.GetPipeline().Execute(previewEditor.GetLayers());
        glFinish();

        std::vector<uint8_t> topLeftPreview = previewEditor.GetPipeline().GetOutputPixels(previewW, previewH);
        const bool livePreviewLooksBlank =
            !topLeftPreview.empty() &&
            !HasMeaningfulPixels(topLeftPreview) &&
            HasMeaningfulPixels(srcPixels);
        if (topLeftPreview.empty() || livePreviewLooksBlank) {
            return false;
        }

        previewPixels = std::move(topLeftPreview);
        LibraryManager::FlipImageRowsInPlace(previewPixels, previewW, previewH, 4);
    }

    outData.kind = LayerKind::EditorProject;
    outData.name = document.metadata.projectName.empty() ? projectPath.stem().string() : document.metadata.projectName;
    outData.previewPixels = std::move(previewPixels);
    outData.previewW = previewW;
    outData.previewH = previewH;
    outData.logicalW = previewW;
    outData.logicalH = previewH;
    outData.embeddedProjectJson = document.pipelineData.dump();
    outData.originalSourcePng = std::move(document.sourceImageBytes);
    outData.generatedFromImage = false;

    const std::filesystem::path libraryPath = LibraryManager::Get().GetLibraryPath();
    if (IsPathInsideDirectory(projectPath, libraryPath)) {
        outData.linkedProjectFileName = projectPath.filename().string();
        outData.linkedProjectName = outData.name;
    }

    return true;
}

void ApplyImportDataToNewLayer(const LayerFileLoadData& data, const float canvasW, const float canvasH, std::vector<CompositeLayer>& layers, std::string& selectedId) {
    CompositeLayer layer;
    layer.id = NewLayerId();
    layer.name = data.name.empty() ? "Layer" : data.name;
    layer.kind = data.kind;
    layer.imgW = data.previewW;
    layer.imgH = data.previewH;
    layer.logicalW = data.logicalW > 0 ? data.logicalW : data.previewW;
    layer.logicalH = data.logicalH > 0 ? data.logicalH : data.previewH;
    layer.rgba = data.previewPixels;
    layer.visible = true;
    layer.locked = false;
    layer.opacity = 1.0f;
    layer.blendMode = CompositeBlendMode::Normal;
    layer.flipX = false;
    layer.flipY = false;
    layer.embeddedProjectJson = data.embeddedProjectJson;
    layer.originalSourcePng = data.originalSourcePng;
    layer.linkedProjectFileName = data.linkedProjectFileName;
    layer.linkedProjectName = data.linkedProjectName;
    layer.generatedFromImage = data.generatedFromImage;

    int maxZ = -1;
    for (const CompositeLayer& existing : layers) {
        maxZ = std::max(maxZ, existing.z);
    }
    layer.z = maxZ + 1;

    const float targetW = canvasW * 0.8f;
    const float targetH = canvasH * 0.8f;
    const float logicalW = std::max(1.0f, static_cast<float>(layer.logicalW));
    const float logicalH = std::max(1.0f, static_cast<float>(layer.logicalH));
    const float fitScale = std::min({ 1.0f, targetW / logicalW, targetH / logicalH });
    layer.scaleX = fitScale;
    layer.scaleY = fitScale;
    layer.x = -(logicalW * layer.scaleX) * 0.5f;
    layer.y = -(logicalH * layer.scaleY) * 0.5f;

    layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);
    layers.push_back(std::move(layer));
    selectedId = layers.back().id;
}

void ApplyImportDataToExistingLayer(const LayerFileLoadData& data, CompositeLayer& layer) {
    const float previousWorldW = LayerWorldWidth(layer);
    const float previousWorldH = LayerWorldHeight(layer);
    const float centerX = layer.x + previousWorldW * 0.5f;
    const float centerY = layer.y + previousWorldH * 0.5f;

    layer.name = data.name.empty() ? layer.name : data.name;
    layer.kind = data.kind;
    layer.imgW = data.previewW;
    layer.imgH = data.previewH;
    layer.logicalW = data.logicalW > 0 ? data.logicalW : data.previewW;
    layer.logicalH = data.logicalH > 0 ? data.logicalH : data.previewH;
    layer.rgba = data.previewPixels;
    layer.embeddedProjectJson = data.embeddedProjectJson;
    layer.originalSourcePng = data.originalSourcePng;
    layer.linkedProjectFileName = data.linkedProjectFileName;
    layer.linkedProjectName = data.linkedProjectName;
    layer.generatedFromImage = data.generatedFromImage;

    if (layer.tex != 0) {
        glDeleteTextures(1, &layer.tex);
        layer.tex = 0;
    }

    const float logicalW = std::max(1.0f, static_cast<float>(layer.logicalW));
    const float logicalH = std::max(1.0f, static_cast<float>(layer.logicalH));
    const float fitScaleX = previousWorldW / logicalW;
    const float fitScaleY = previousWorldH / logicalH;
    if (std::isfinite(fitScaleX) && fitScaleX > 0.0f) {
        layer.scaleX = fitScaleX;
    }
    if (std::isfinite(fitScaleY) && fitScaleY > 0.0f) {
        layer.scaleY = fitScaleY;
    }

    layer.x = centerX - logicalW * layer.scaleX * 0.5f;
    layer.y = centerY - logicalH * layer.scaleY * 0.5f;
    layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);
}

bool BuildLayerSourcePngIfMissing(CompositeLayer& layer) {
    if (!layer.originalSourcePng.empty()) {
        return true;
    }

    return BuildTopLeftPngFromBottomLeftRgba(layer.rgba, layer.imgW, layer.imgH, layer.originalSourcePng);
}

void ReassignZFromTopOrder(const std::vector<CompositeLayer*>& topToBottomLayers) {
    const int count = static_cast<int>(topToBottomLayers.size());
    for (int index = 0; index < count; ++index) {
        topToBottomLayers[index]->z = count - 1 - index;
    }
}

void RasterizeLayersToTopLeftRgba(
    const std::vector<CompositeLayer>& allLayers,
    const std::vector<const CompositeLayer*>& layers,
    const FloatRect& worldBounds,
    const int outW,
    const int outH,
    const CompositeExportBackgroundMode backgroundMode,
    const std::array<float, 4>& backgroundColor,
    std::vector<uint8_t>& outRgba) {

    outRgba.clear();
    if (layers.empty() || !IsRectValid(worldBounds) || outW <= 0 || outH <= 0) {
        return;
    }

    std::vector<float> accum(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4, 0.0f);
    if (backgroundMode == CompositeExportBackgroundMode::Solid) {
        const float bgR = Clamp01(backgroundColor[0]);
        const float bgG = Clamp01(backgroundColor[1]);
        const float bgB = Clamp01(backgroundColor[2]);
        for (std::size_t index = 0; index < accum.size(); index += 4) {
            accum[index + 0] = bgR;
            accum[index + 1] = bgG;
            accum[index + 2] = bgB;
            accum[index + 3] = 1.0f;
        }
    }

    for (const CompositeLayer* layer : layers) {
        if (!layer || !layer->visible || layer->rgba.empty() || layer->imgW <= 0 || layer->imgH <= 0) {
            continue;
        }

        const FloatRect layerBounds = ComputeLayerBoundsWorld(allLayers, *layer);
        const int minX = std::clamp(
            static_cast<int>(std::floor((layerBounds.x - worldBounds.x) / worldBounds.width * outW)) - 1,
            0,
            outW - 1);
        const int maxX = std::clamp(
            static_cast<int>(std::ceil((layerBounds.x + layerBounds.width - worldBounds.x) / worldBounds.width * outW)) + 1,
            0,
            outW - 1);
        const int minY = std::clamp(
            static_cast<int>(std::floor((layerBounds.y - worldBounds.y) / worldBounds.height * outH)) - 1,
            0,
            outH - 1);
        const int maxY = std::clamp(
            static_cast<int>(std::ceil((layerBounds.y + layerBounds.height - worldBounds.y) / worldBounds.height * outH)) + 1,
            0,
            outH - 1);

        for (int py = minY; py <= maxY; ++py) {
            const float worldY = worldBounds.y + (static_cast<float>(py) + 0.5f) / static_cast<float>(outH) * worldBounds.height;
            for (int px = minX; px <= maxX; ++px) {
                const float worldX = worldBounds.x + (static_cast<float>(px) + 0.5f) / static_cast<float>(outW) * worldBounds.width;
                float u = 0.0f;
                float v = 0.0f;
                if (!MapWorldToLayerUv(allLayers, *layer, worldX, worldY, u, v)) {
                    continue;
                }

                float src[4] = {};
                SampleLayerPixelBilinear(*layer, u, v, src);
                src[3] *= Clamp01(layer->opacity);
                if (src[3] <= 1e-6f) {
                    continue;
                }

                float* dst = &accum[(static_cast<std::size_t>(py) * static_cast<std::size_t>(outW) + static_cast<std::size_t>(px)) * 4];
                CompositeSourceOver(dst, src, layer->blendMode);
            }
        }
    }

    outRgba.resize(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4);
    for (std::size_t index = 0; index < accum.size(); index += 4) {
        outRgba[index + 0] = static_cast<uint8_t>(std::round(Clamp01(accum[index + 0]) * 255.0f));
        outRgba[index + 1] = static_cast<uint8_t>(std::round(Clamp01(accum[index + 1]) * 255.0f));
        outRgba[index + 2] = static_cast<uint8_t>(std::round(Clamp01(accum[index + 2]) * 255.0f));
        outRgba[index + 3] = static_cast<uint8_t>(std::round(Clamp01(accum[index + 3]) * 255.0f));
    }
}
