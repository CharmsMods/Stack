#include "Composite/CompositeModule.h"

#include "Editor/EditorModule.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imstb_truetype.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_set>

namespace {

using json = StackBinaryFormat::json;

constexpr int kCompositeFormatVersion = 4;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kCompositeToolbarHeight = 68.0f;
constexpr float kCanvasContextDragThreshold = 6.0f;
constexpr float kExportBoundsHandleScreenRadius = 7.0f;
constexpr float kLayerHandleScreenRadius = 5.0f;
constexpr float kSnapThresholdScreenPixels = 10.0f;
constexpr float kTextRasterZoomThresholdUp = 1.2f;
constexpr float kTextRasterZoomThresholdDown = 0.6f;
constexpr const char* kDefaultCompositeFontRelativePath = "Assets/Fonts/Roboto-Medium.ttf";
constexpr const char* kCompositeLayersWindowName = "Composite Layers";
constexpr const char* kCompositeSelectedWindowName = "Composite Selected";
constexpr const char* kCompositeViewWindowName = "Composite View";
constexpr const char* kCompositeExportWindowName = "Composite Export";
constexpr const char* kCompositeCanvasWindowName = "Composite Canvas";
constexpr const char* kCompositeDockSpaceName = "CompositeDockSpace";
constexpr const char* kStagePreviewVertexShader = R"GLSL(
#version 430 core
layout(location = 0) out vec2 vUv;
void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    const vec2 pos = positions[gl_VertexID];
    gl_Position = vec4(pos, 0.0, 1.0);
    vUv = pos * 0.5 + 0.5;
}
)GLSL";
constexpr const char* kStagePreviewFragmentShader = R"GLSL(
#version 430 core
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

uniform sampler2D uPrevTex;
uniform sampler2D uLayerTex;
uniform vec4 uWorldRect;
uniform vec4 uLayerRect;
uniform float uLayerRotation;
uniform float uLayerOpacity;
uniform int uFlipX;
uniform int uFlipY;
uniform int uBlendMode;

float clamp01(const float value) {
    return clamp(value, 0.0, 1.0);
}

float blendOverlay(const float src, const float dst) {
    return (dst <= 0.5) ? (2.0 * src * dst) : (1.0 - 2.0 * (1.0 - src) * (1.0 - dst));
}

float blendHardLight(const float src, const float dst) {
    return (src <= 0.5) ? (2.0 * src * dst) : (1.0 - 2.0 * (1.0 - src) * (1.0 - dst));
}

float softLightCurve(const float value) {
    if (value <= 0.25) {
        return ((16.0 * value - 12.0) * value + 4.0) * value;
    }
    return sqrt(value);
}

float blendSoftLight(const float src, const float dst) {
    if (src <= 0.5) {
        return dst - (1.0 - 2.0 * src) * dst * (1.0 - dst);
    }
    return dst + (2.0 * src - 1.0) * (softLightCurve(dst) - dst);
}

void rgbToHsl(const vec3 rgb, out float hue, out float saturation, out float lightness) {
    const float r = clamp01(rgb.r);
    const float g = clamp01(rgb.g);
    const float b = clamp01(rgb.b);
    const float maxValue = max(max(r, g), b);
    const float minValue = min(min(r, g), b);
    const float delta = maxValue - minValue;

    lightness = (maxValue + minValue) * 0.5;
    if (delta <= 1e-6) {
        hue = 0.0;
        saturation = 0.0;
        return;
    }

    saturation = (lightness > 0.5)
        ? (delta / (2.0 - maxValue - minValue))
        : (delta / (maxValue + minValue));

    if (maxValue == r) {
        hue = (g - b) / delta + (g < b ? 6.0 : 0.0);
    } else if (maxValue == g) {
        hue = (b - r) / delta + 2.0;
    } else {
        hue = (r - g) / delta + 4.0;
    }

    hue /= 6.0;
}

float hueToRgb(const float p, const float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

vec3 hslToRgb(const float hue, const float saturation, const float lightness) {
    if (saturation <= 1e-6) {
        return vec3(lightness);
    }

    const float q = (lightness < 0.5)
        ? (lightness * (1.0 + saturation))
        : (lightness + saturation - lightness * saturation);
    const float p = 2.0 * lightness - q;

    return vec3(
        hueToRgb(p, q, hue + 1.0 / 3.0),
        hueToRgb(p, q, hue),
        hueToRgb(p, q, hue - 1.0 / 3.0));
}

vec3 blendRgb(const int mode, const vec3 srcRgb, const vec3 dstRgb) {
    switch (mode) {
    case 1:
        return srcRgb * dstRgb;
    case 2:
        return 1.0 - (1.0 - srcRgb) * (1.0 - dstRgb);
    case 3:
        return min(vec3(1.0), srcRgb + dstRgb);
    case 4:
        return vec3(
            blendOverlay(srcRgb.r, dstRgb.r),
            blendOverlay(srcRgb.g, dstRgb.g),
            blendOverlay(srcRgb.b, dstRgb.b));
    case 5:
        return vec3(
            blendSoftLight(srcRgb.r, dstRgb.r),
            blendSoftLight(srcRgb.g, dstRgb.g),
            blendSoftLight(srcRgb.b, dstRgb.b));
    case 6:
        return vec3(
            blendHardLight(srcRgb.r, dstRgb.r),
            blendHardLight(srcRgb.g, dstRgb.g),
            blendHardLight(srcRgb.b, dstRgb.b));
    case 7:
    case 8: {
        float srcHue = 0.0;
        float srcSat = 0.0;
        float srcLight = 0.0;
        float dstHue = 0.0;
        float dstSat = 0.0;
        float dstLight = 0.0;
        rgbToHsl(srcRgb, srcHue, srcSat, srcLight);
        rgbToHsl(dstRgb, dstHue, dstSat, dstLight);
        const float hue = srcHue;
        const float saturation = (mode == 8) ? srcSat : dstSat;
        return hslToRgb(hue, saturation, dstLight);
    }
    case 0:
    default:
        return srcRgb;
    }
}

vec4 compositeSourceOver(const vec4 dst, const vec4 src, const int mode) {
    const float srcAlpha = clamp01(src.a);
    if (srcAlpha <= 1e-6) {
        return dst;
    }

    const float dstAlpha = clamp01(dst.a);
    const vec3 srcRgb = clamp(src.rgb, 0.0, 1.0);
    const vec3 dstRgb = clamp(dst.rgb, 0.0, 1.0);
    const vec3 blendedRgb = blendRgb(mode, srcRgb, dstRgb);
    const float outAlpha = srcAlpha + dstAlpha * (1.0 - srcAlpha);
    if (outAlpha <= 1e-6) {
        return vec4(0.0);
    }

    const vec3 outRgb = (blendedRgb * srcAlpha + dstRgb * dstAlpha * (1.0 - srcAlpha)) / outAlpha;
    return vec4(clamp(outRgb, 0.0, 1.0), clamp01(outAlpha));
}

vec4 sampleLayer(const vec2 worldPoint) {
    const float width = max(uLayerRect.z, 0.0001);
    const float height = max(uLayerRect.w, 0.0001);
    const float centerX = uLayerRect.x + width * 0.5;
    const float centerY = uLayerRect.y + height * 0.5;
    const float dx = worldPoint.x - centerX;
    const float dy = worldPoint.y - centerY;
    const float cosR = cos(uLayerRotation);
    const float sinR = sin(uLayerRotation);
    const float localX = dx * cosR + dy * sinR + width * 0.5;
    const float localY = -dx * sinR + dy * cosR + height * 0.5;

    if (localX < 0.0 || localX > width || localY < 0.0 || localY > height) {
        return vec4(0.0);
    }

    float u = clamp(localX / width, 0.0, 1.0);
    float v = clamp(localY / height, 0.0, 1.0);
    if (uFlipX != 0) {
        u = 1.0 - u;
    }
    if (uFlipY != 0) {
        v = 1.0 - v;
    }

    vec4 sampled = texture(uLayerTex, vec2(u, 1.0 - v));
    sampled.a *= clamp01(uLayerOpacity);
    return sampled;
}

void main() {
    const vec4 dst = texture(uPrevTex, vUv);
    const vec2 worldPoint = vec2(
        uWorldRect.x + vUv.x * uWorldRect.z,
        uWorldRect.y + (1.0 - vUv.y) * uWorldRect.w);
    const vec4 src = sampleLayer(worldPoint);
    fragColor = compositeSourceOver(dst, src, uBlendMode);
}
)GLSL";

struct FloatRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct LayerFileLoadData {
    LayerKind kind = LayerKind::Image;
    std::string name;
    std::vector<uint8_t> previewPixels; // Bottom-left oriented for runtime texture/storage
    int previewW = 0;
    int previewH = 0;
    int logicalW = 0;
    int logicalH = 0;
    std::string embeddedProjectJson = json::array().dump();
    std::vector<uint8_t> originalSourcePng; // Top-left oriented PNG bytes for editor hand-off
    std::string linkedProjectFileName;
    std::string linkedProjectName;
    bool generatedFromImage = false;
};

float Clamp01(float value);
int SafeCompositeTextTextureLimit();
float EffectiveTextRasterZoom(float viewZoom);

void png_write_vec(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const unsigned char*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
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

std::string TrimLeadingWhitespace(const std::string& value) {
    std::size_t index = 0;
    while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    return value.substr(index);
}

bool StartsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
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

bool IsCompositeDockWindowName(const std::string& name) {
    return StartsWith(name, "Composite ");
}

bool ParseDockSettingsLine(const std::string& rawLine, ImGuiID& outId, ImGuiID& outParentId) {
    const std::string line = TrimLeadingWhitespace(rawLine);
    const char* cursor = line.c_str();
    if (StartsWith(line, "DockNode")) {
        cursor += 8;
    } else if (StartsWith(line, "DockSpace")) {
        cursor += 9;
    } else {
        return false;
    }

    while (*cursor == ' ') {
        ++cursor;
    }

    unsigned int id = 0;
    unsigned int parentId = 0;
    int read = 0;
    if (std::sscanf(cursor, "ID=0x%08X%n", &id, &read) != 1) {
        return false;
    }
    cursor += read;

    if (std::sscanf(cursor, " Parent=0x%08X%n", &parentId, &read) == 1) {
        outParentId = parentId;
    } else {
        outParentId = 0;
    }

    outId = id;
    return true;
}

std::string CaptureCompositeWorkspaceIni(const ImGuiID dockspaceId) {
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    if (iniData == nullptr || iniSize == 0) {
        return {};
    }

    std::istringstream stream(std::string(iniData, iniSize));
    std::vector<std::string> windowSections;

    struct DockEntry {
        std::string line;
        ImGuiID id = 0;
        ImGuiID parentId = 0;
        bool parsed = false;
    };
    std::vector<DockEntry> dockingEntries;

    bool inWindowSection = false;
    bool keepWindowSection = false;
    bool inDockingSection = false;
    std::string currentWindowSection;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!line.empty() && line.front() == '[') {
            if (inWindowSection && keepWindowSection && !currentWindowSection.empty()) {
                windowSections.push_back(currentWindowSection);
            }

            inWindowSection = false;
            keepWindowSection = false;
            inDockingSection = false;
            currentWindowSection.clear();

            if (StartsWith(line, "[Window][")) {
                const std::string windowName = line.substr(9, line.size() - 10);
                inWindowSection = true;
                keepWindowSection = IsCompositeDockWindowName(windowName);
                if (keepWindowSection) {
                    currentWindowSection = line + "\n";
                }
                continue;
            }

            if (line == "[Docking][Data]") {
                inDockingSection = true;
                continue;
            }
        }

        if (inWindowSection) {
            if (keepWindowSection) {
                currentWindowSection += line + "\n";
            }
            continue;
        }

        if (inDockingSection) {
            if (line.empty()) {
                continue;
            }
            DockEntry entry;
            entry.line = line;
            entry.parsed = ParseDockSettingsLine(line, entry.id, entry.parentId);
            dockingEntries.push_back(std::move(entry));
        }
    }

    if (inWindowSection && keepWindowSection && !currentWindowSection.empty()) {
        windowSections.push_back(currentWindowSection);
    }

    std::unordered_set<ImGuiID> keepDockNodeIds;
    if (dockspaceId != 0) {
        keepDockNodeIds.insert(dockspaceId);
        bool added = true;
        while (added) {
            added = false;
            for (const DockEntry& entry : dockingEntries) {
                if (!entry.parsed || keepDockNodeIds.find(entry.id) != keepDockNodeIds.end()) {
                    continue;
                }
                if (entry.parentId != 0 && keepDockNodeIds.find(entry.parentId) != keepDockNodeIds.end()) {
                    keepDockNodeIds.insert(entry.id);
                    added = true;
                }
            }
        }
    }

    std::string filtered;
    for (const std::string& section : windowSections) {
        filtered += section;
        filtered += "\n";
    }

    bool wroteDockingHeader = false;
    for (const DockEntry& entry : dockingEntries) {
        if (dockspaceId != 0 && (!entry.parsed || keepDockNodeIds.find(entry.id) == keepDockNodeIds.end())) {
            continue;
        }
        if (!wroteDockingHeader) {
            filtered += "[Docking][Data]\n";
            wroteDockingHeader = true;
        }
        filtered += entry.line;
        filtered += "\n";
    }
    if (wroteDockingHeader) {
        filtered += "\n";
    }

    return filtered;
}

bool HasMeaningfulPixels(const std::vector<uint8_t>& pixels) {
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        if (pixels[index + 0] > 4 || pixels[index + 1] > 4 || pixels[index + 2] > 4 || pixels[index + 3] > 4) {
            return true;
        }
    }

    return false;
}

float Clamp01(const float value) {
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

float EffectiveTextRasterZoom(const float viewZoom) {
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

ImVec2 LayerCenterWorld(const CompositeLayer& layer) {
    return ImVec2(layer.x + LayerWorldWidth(layer) * 0.5f, layer.y + LayerWorldHeight(layer) * 0.5f);
}

ImVec2 LayerAxisX(const CompositeLayer& layer) {
    return ImVec2(std::cos(layer.rotation), std::sin(layer.rotation));
}

ImVec2 LayerAxisY(const CompositeLayer& layer) {
    return ImVec2(-std::sin(layer.rotation), std::cos(layer.rotation));
}

ImVec2 RotatePoint(const ImVec2& point, const ImVec2& pivot, const float radians) {
    const float tx = point.x - pivot.x;
    const float ty = point.y - pivot.y;
    const float cosR = std::cos(radians);
    const float sinR = std::sin(radians);
    return ImVec2(
        pivot.x + tx * cosR - ty * sinR,
        pivot.y + tx * sinR + ty * cosR);
}

std::array<ImVec2, 4> ComputeLayerQuadWorld(const CompositeLayer& layer) {
    const float width = LayerWorldWidth(layer);
    const float height = LayerWorldHeight(layer);
    const ImVec2 topLeft(layer.x, layer.y);
    const ImVec2 topRight(layer.x + width, layer.y);
    const ImVec2 bottomRight(layer.x + width, layer.y + height);
    const ImVec2 bottomLeft(layer.x, layer.y + height);
    const ImVec2 pivot(layer.x + width * 0.5f, layer.y + height * 0.5f);

    return {
        RotatePoint(topLeft, pivot, layer.rotation),
        RotatePoint(topRight, pivot, layer.rotation),
        RotatePoint(bottomRight, pivot, layer.rotation),
        RotatePoint(bottomLeft, pivot, layer.rotation)
    };
}

FloatRect ComputeLayerBoundsWorld(const CompositeLayer& layer) {
    const auto quad = ComputeLayerQuadWorld(layer);

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

bool MapWorldToLayerUv(const CompositeLayer& layer, const float worldX, const float worldY, float& outU, float& outV) {
    const float width = LayerWorldWidth(layer);
    const float height = LayerWorldHeight(layer);
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    const float centerX = layer.x + width * 0.5f;
    const float centerY = layer.y + height * 0.5f;
    const float dx = worldX - centerX;
    const float dy = worldY - centerY;
    const float cosR = std::cos(layer.rotation);
    const float sinR = std::sin(layer.rotation);
    const float localX = dx * cosR + dy * sinR + width * 0.5f;
    const float localY = -dx * sinR + dy * cosR + height * 0.5f;

    if (localX < 0.0f || localX > width || localY < 0.0f || localY > height) {
        return false;
    }

    float u = localX / width;
    float v = localY / height;
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
        if (MapWorldToLayerUv(layer, worldX, worldY, u, v) && layer.z > bestZ) {
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

bool ComputeAutoBounds(const std::vector<const CompositeLayer*>& layers, FloatRect& outBounds) {
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

        const FloatRect bounds = ComputeLayerBoundsWorld(*layer);
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

    stbi_write_png_to_func(png_write_vec, &outPng, width, height, 4, rgbaTopLeft.data(), width * 4);
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

        const FloatRect layerBounds = ComputeLayerBoundsWorld(*layer);
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
                if (!MapWorldToLayerUv(*layer, worldX, worldY, u, v)) {
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

} // namespace

CompositeModule::CompositeModule() = default;
CompositeModule::~CompositeModule() { Shutdown(); }

void CompositeModule::Initialize() {
    if (m_Initialized) {
        return;
    }

    m_Initialized = true;
    m_StagePreviewDirty = true;
}

void CompositeModule::Shutdown() {
    ClearLayersGpu();
    ClearStagePreviewGpu();
    if (m_StagePreviewProgram != 0) {
        glDeleteProgram(m_StagePreviewProgram);
        m_StagePreviewProgram = 0;
    }
    if (m_StagePreviewVao != 0) {
        glDeleteVertexArrays(1, &m_StagePreviewVao);
        m_StagePreviewVao = 0;
    }
    m_Layers.clear();
    m_Initialized = false;
}

void CompositeModule::MarkDocumentDirty() {
    m_Dirty = true;
    m_StagePreviewDirty = true;
}

void CompositeModule::MarkStageDirty() {
    m_StagePreviewDirty = true;
}

bool CompositeModule::EnsureDefaultTextFontLoaded() {
    if (!m_DefaultTextFontBytes.empty()) {
        return true;
    }
    if (m_DefaultTextFontLoadAttempted) {
        return false;
    }

    m_DefaultTextFontLoadAttempted = true;
    return ReadFileBytes(FindBundledCompositeFontPath(), m_DefaultTextFontBytes);
}

bool CompositeModule::RegenerateGeneratedLayerTexture(CompositeLayer& layer) {
    if (!IsGeneratedLayer(layer)) {
        return false;
    }

    std::vector<uint8_t> rgba;
    int width = std::max(1, layer.logicalW);
    int height = std::max(1, layer.logicalH);

    if (layer.kind == LayerKind::ShapeRect) {
        FillSolidRgba(rgba, width, height, layer.fillColor, false);
    } else if (layer.kind == LayerKind::ShapeCircle) {
        FillSolidRgba(rgba, width, height, layer.fillColor, true);
    } else {
        if (!EnsureDefaultTextFontLoaded()) {
            return false;
        }
        std::vector<uint8_t> baseRgba;
        int baseWidth = 0;
        int baseHeight = 0;
        if (!BuildTextRgba(
                m_DefaultTextFontBytes,
                layer.textContent,
                layer.textFontSize,
                layer.fillColor,
                1.0f,
                1.0f,
                baseRgba,
                baseWidth,
                baseHeight)) {
            return false;
        }
        layer.logicalW = baseWidth;
        layer.logicalH = baseHeight;

        const float qualityZoom = EffectiveTextRasterZoom(m_ViewZoom);
        const float renderStretchX = std::max(0.01f, layer.scaleX * qualityZoom);
        const float renderStretchY = std::max(0.01f, layer.scaleY * qualityZoom);
        if (std::abs(renderStretchX - 1.0f) <= 0.0001f && std::abs(renderStretchY - 1.0f) <= 0.0001f) {
            rgba = std::move(baseRgba);
            width = baseWidth;
            height = baseHeight;
        } else if (!BuildTextRgba(
                       m_DefaultTextFontBytes,
                       layer.textContent,
                       layer.textFontSize,
                       layer.fillColor,
                       renderStretchX,
                       renderStretchY,
                       rgba,
                       width,
                       height)) {
            return false;
        }
        layer.textRenderStretchX = renderStretchX;
        layer.textRenderStretchY = renderStretchY;
    }

    if (!rgba.empty()) {
        LibraryManager::FlipImageRowsInPlace(rgba, width, height, 4);
    }

    layer.rgba = std::move(rgba);
    layer.imgW = width;
    layer.imgH = height;
    if (layer.kind != LayerKind::Text) {
        layer.logicalW = width;
        layer.logicalH = height;
        layer.textRenderStretchX = 0.0f;
        layer.textRenderStretchY = 0.0f;
    }
    layer.originalSourcePng.clear();
    if (layer.tex != 0) {
        glDeleteTextures(1, &layer.tex);
        layer.tex = 0;
    }
    if (!layer.rgba.empty()) {
        layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);
    }
    MarkStageDirty();
    return layer.tex != 0;
}

void CompositeModule::RefreshVisibleTextRasterQuality() {
    const float qualityZoom = EffectiveTextRasterZoom(m_ViewZoom);
    for (CompositeLayer& layer : m_Layers) {
        if (!layer.visible || layer.kind != LayerKind::Text) {
            continue;
        }

        const float targetStretchX = std::max(0.01f, layer.scaleX * qualityZoom);
        const float targetStretchY = std::max(0.01f, layer.scaleY * qualityZoom);
        auto needsReraster = [](const float currentStretch, const float targetStretch) {
            if (currentStretch <= 0.0f) {
                return true;
            }
            const float ratio = targetStretch / std::max(0.01f, currentStretch);
            return ratio >= kTextRasterZoomThresholdUp || ratio <= kTextRasterZoomThresholdDown;
        };

        if (needsReraster(layer.textRenderStretchX, targetStretchX) ||
            needsReraster(layer.textRenderStretchY, targetStretchY)) {
            RegenerateGeneratedLayerTexture(layer);
        }
    }
}

CompositeSnapModePreset CompositeModule::GetSnapModePreset() const {
    if (!m_SnapEnabled) {
        return CompositeSnapModePreset::Off;
    }

    const bool stepSnapsDisabled =
        m_GridSize <= 0.0f &&
        m_RotateSnapStep <= 0.0f &&
        m_ScaleSnapStep <= 0.0f;
    if (m_SnapToObjects &&
        m_SnapToCenters &&
        m_SnapToCanvasCenter &&
        !m_SnapToSpacing &&
        stepSnapsDisabled) {
        return CompositeSnapModePreset::ObjectOnly;
    }

    if (m_SnapToObjects &&
        m_SnapToCenters &&
        m_SnapToCanvasCenter &&
        m_SnapToSpacing &&
        m_GridSize > 0.0f &&
        m_RotateSnapStep > 0.0f &&
        m_ScaleSnapStep > 0.0f) {
        return CompositeSnapModePreset::Full;
    }

    return CompositeSnapModePreset::Custom;
}

void CompositeModule::RememberSnapStepDefaults() {
    if (m_GridSize > 0.0f) {
        m_LastNonZeroGridSize = m_GridSize;
    }
    if (m_RotateSnapStep > 0.0f) {
        m_LastNonZeroRotateSnapStep = m_RotateSnapStep;
    }
    if (m_ScaleSnapStep > 0.0f) {
        m_LastNonZeroScaleSnapStep = m_ScaleSnapStep;
    }
}

void CompositeModule::ApplySnapModePreset(const CompositeSnapModePreset preset) {
    if (preset == CompositeSnapModePreset::Custom) {
        return;
    }

    RememberSnapStepDefaults();
    switch (preset) {
    case CompositeSnapModePreset::Full:
        m_SnapEnabled = true;
        m_SnapToObjects = true;
        m_SnapToCenters = true;
        m_SnapToCanvasCenter = true;
        m_SnapToSpacing = true;
        m_GridSize = (m_LastNonZeroGridSize > 0.0f) ? m_LastNonZeroGridSize : 24.0f;
        m_RotateSnapStep = (m_LastNonZeroRotateSnapStep > 0.0f) ? m_LastNonZeroRotateSnapStep : 15.0f;
        m_ScaleSnapStep = (m_LastNonZeroScaleSnapStep > 0.0f) ? m_LastNonZeroScaleSnapStep : 0.1f;
        break;
    case CompositeSnapModePreset::ObjectOnly:
        m_SnapEnabled = true;
        m_SnapToObjects = true;
        m_SnapToCenters = true;
        m_SnapToCanvasCenter = true;
        m_SnapToSpacing = false;
        m_GridSize = 0.0f;
        m_RotateSnapStep = 0.0f;
        m_ScaleSnapStep = 0.0f;
        break;
    case CompositeSnapModePreset::Off:
        m_SnapEnabled = false;
        break;
    case CompositeSnapModePreset::Custom:
        break;
    }
}

void CompositeModule::ClearLayersGpu() {
    for (CompositeLayer& layer : m_Layers) {
        if (layer.tex != 0) {
            glDeleteTextures(1, &layer.tex);
            layer.tex = 0;
        }
    }
}

void CompositeModule::ClearStagePreviewGpu() {
    for (int index = 0; index < 2; ++index) {
        if (m_StagePreviewFbo[index] != 0) {
            glDeleteFramebuffers(1, &m_StagePreviewFbo[index]);
            m_StagePreviewFbo[index] = 0;
        }
        if (m_StagePreviewTex[index] != 0) {
            glDeleteTextures(1, &m_StagePreviewTex[index]);
            m_StagePreviewTex[index] = 0;
        }
    }

    m_StagePreviewDisplayIndex = 0;
    m_StagePreviewTexW = 0;
    m_StagePreviewTexH = 0;
}

void CompositeModule::SyncLayerTextures() {
    for (CompositeLayer& layer : m_Layers) {
        if (layer.tex != 0 || layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
            continue;
        }

        layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);
    }
}

bool CompositeModule::EnsureStageCompositeProgram() {
    if (m_StagePreviewProgram != 0) {
        return true;
    }

    m_StagePreviewProgram = GLHelpers::CreateShaderProgram(kStagePreviewVertexShader, kStagePreviewFragmentShader);
    if (m_StagePreviewProgram == 0) {
        return false;
    }

    glUseProgram(m_StagePreviewProgram);
    glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uPrevTex"), 0);
    glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uLayerTex"), 1);
    glUseProgram(0);

    if (m_StagePreviewVao == 0) {
        glGenVertexArrays(1, &m_StagePreviewVao);
    }

    return m_StagePreviewVao != 0;
}

bool CompositeModule::EnsureStagePreviewTargets(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (m_StagePreviewTexW == width &&
        m_StagePreviewTexH == height &&
        m_StagePreviewTex[0] != 0 &&
        m_StagePreviewTex[1] != 0 &&
        m_StagePreviewFbo[0] != 0 &&
        m_StagePreviewFbo[1] != 0) {
        return true;
    }

    ClearStagePreviewGpu();

    for (int index = 0; index < 2; ++index) {
        m_StagePreviewTex[index] = GLHelpers::CreateEmptyTexture(width, height);
        if (m_StagePreviewTex[index] == 0) {
            ClearStagePreviewGpu();
            return false;
        }

        m_StagePreviewFbo[index] = GLHelpers::CreateFBO(m_StagePreviewTex[index]);
        if (m_StagePreviewFbo[index] == 0) {
            ClearStagePreviewGpu();
            return false;
        }
    }

    m_StagePreviewTexW = width;
    m_StagePreviewTexH = height;
    m_StagePreviewDisplayIndex = 0;
    return true;
}

void CompositeModule::RenderStagePreviewTexture(
    const std::vector<const CompositeLayer*>& layers,
    const float viewX,
    const float viewY,
    const float viewWidth,
    const float viewHeight,
    const int width,
    const int height) {

    if (!EnsureStageCompositeProgram() || !EnsureStagePreviewTargets(width, height)) {
        return;
    }

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(m_StagePreviewVao);
    glUseProgram(m_StagePreviewProgram);
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, m_StagePreviewFbo[0]);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int srcIndex = 0;
    int dstIndex = 1;
    for (const CompositeLayer* layer : layers) {
        if (!layer || !layer->visible || layer->tex == 0) {
            continue;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, m_StagePreviewFbo[dstIndex]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_StagePreviewTex[srcIndex]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, layer->tex);

        glUniform4f(glGetUniformLocation(m_StagePreviewProgram, "uWorldRect"), viewX, viewY, viewWidth, viewHeight);
        glUniform4f(
            glGetUniformLocation(m_StagePreviewProgram, "uLayerRect"),
            layer->x,
            layer->y,
            LayerWorldWidth(*layer),
            LayerWorldHeight(*layer));
        glUniform1f(glGetUniformLocation(m_StagePreviewProgram, "uLayerRotation"), layer->rotation);
        glUniform1f(glGetUniformLocation(m_StagePreviewProgram, "uLayerOpacity"), Clamp01(layer->opacity));
        glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uFlipX"), layer->flipX ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uFlipY"), layer->flipY ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uBlendMode"), static_cast<int>(layer->blendMode));
        glDrawArrays(GL_TRIANGLES, 0, 3);

        std::swap(srcIndex, dstIndex);
    }

    m_StagePreviewDisplayIndex = srcIndex;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

void CompositeModule::NewProject() {
    ClearLayersGpu();
    ClearStagePreviewGpu();
    m_Layers.clear();
    m_SelectedId.clear();
    m_ShowLayersWindow = true;
    m_ShowSelectedWindow = true;
    m_ShowViewWindow = true;
    m_ShowExportWindow = true;
    m_WorkspaceLayoutIni.clear();
    m_PendingWorkspaceLayoutLoad = false;
    m_PendingWorkspaceLayoutReset = true;
    m_SuspendWorkspaceLayoutDirtyTracking = true;
    m_RightMousePressedOnCanvas = false;
    m_ViewZoom = 1.0f;
    m_ViewPanX = 0.0f;
    m_ViewPanY = 0.0f;
    m_ShowChecker = true;
    m_SnapEnabled = false;
    m_SnapToObjects = true;
    m_SnapToCenters = true;
    m_SnapToCanvasCenter = true;
    m_SnapToSpacing = true;
    m_LimitProjectResolution = true;
    m_GridSize = 24.0f;
    m_RotateSnapStep = 15.0f;
    m_ScaleSnapStep = 0.1f;
    m_LastNonZeroGridSize = m_GridSize;
    m_LastNonZeroRotateSnapStep = m_RotateSnapStep;
    m_LastNonZeroScaleSnapStep = m_ScaleSnapStep;
    m_ExportSettings = CompositeExportSettings {};
    m_ActiveExportHandle = ExportHandleType::None;
    m_ExportPanelActive = false;
    m_MiddleMousePanActive = false;
    m_OpenSavePopup = false;
    m_ProjectName = "Untitled Composite";
    m_ProjectFileName.clear();
    m_PendingOpenInEditorRequest = false;
    m_Dirty = false;
    m_StagePreviewDirty = true;
    m_LastStagePreviewZoom = -1.0f;
    m_LastStagePreviewPanX = 0.0f;
    m_LastStagePreviewPanY = 0.0f;
    m_LastStagePreviewCanvasW = 0;
    m_LastStagePreviewCanvasH = 0;
}

bool CompositeModule::HasLayers() const {
    return !m_Layers.empty();
}

CompositeLayer* CompositeModule::GetSelectedLayer() {
    return FindLayerById(m_Layers, m_SelectedId);
}

bool CompositeModule::ConsumePendingOpenInEditorRequest() {
    const bool pending = m_PendingOpenInEditorRequest;
    m_PendingOpenInEditorRequest = false;
    return pending;
}

void CompositeModule::UpdateLayerData(
    const std::string& layerId,
    const std::string& projectJson,
    const std::vector<uint8_t>& previewPixels,
    const int w,
    const int h) {

    CompositeLayer* layer = FindLayerById(m_Layers, layerId);
    if (!layer || previewPixels.empty() || w <= 0 || h <= 0) {
        return;
    }

    const float previousWorldW = LayerWorldWidth(*layer);
    const float previousWorldH = LayerWorldHeight(*layer);
    const float centerX = layer->x + previousWorldW * 0.5f;
    const float centerY = layer->y + previousWorldH * 0.5f;

    std::vector<uint8_t> runtimePixels = previewPixels;
    LibraryManager::FlipImageRowsInPlace(runtimePixels, w, h, 4);

    layer->embeddedProjectJson = projectJson;
    layer->kind = LayerKind::EditorProject;
    layer->rgba = std::move(runtimePixels);
    layer->imgW = w;
    layer->imgH = h;
    layer->logicalW = w;
    layer->logicalH = h;

    const float logicalW = std::max(1.0f, static_cast<float>(layer->logicalW));
    const float logicalH = std::max(1.0f, static_cast<float>(layer->logicalH));
    const float fitScaleX = previousWorldW / logicalW;
    const float fitScaleY = previousWorldH / logicalH;
    if (std::isfinite(fitScaleX) && fitScaleX > 0.0f) {
        layer->scaleX = fitScaleX;
    }
    if (std::isfinite(fitScaleY) && fitScaleY > 0.0f) {
        layer->scaleY = fitScaleY;
    }

    layer->x = centerX - logicalW * layer->scaleX * 0.5f;
    layer->y = centerY - logicalH * layer->scaleY * 0.5f;

    if (layer->tex != 0) {
        glDeleteTextures(1, &layer->tex);
        layer->tex = 0;
    }
    layer->tex = GLHelpers::CreateTextureFromPixels(layer->rgba.data(), layer->imgW, layer->imgH, 4);
    MarkDocumentDirty();
}

void CompositeModule::ResizeNearest(
    const std::vector<uint8_t>& src,
    const int sw,
    const int sh,
    const int dw,
    const int dh,
    std::vector<uint8_t>& dst) {
    ResizeNearestRgba(src, sw, sh, dw, dh, dst);
}

void CompositeModule::EncodePng(const std::vector<uint8_t>& rgba, const int w, const int h, std::vector<uint8_t>& outPng) const {
    EncodePngBytes(rgba, w, h, outPng);
}

void CompositeModule::TriggerAddImage() {
    const std::string path = FileDialogs::OpenImageFileDialog("Add image to composite");
    if (!path.empty()) {
        AddImageLayerFromFile(path);
    }
}

void CompositeModule::TriggerAddProject() {
    const std::string path = FileDialogs::OpenProjectFileDialog("Add project to composite");
    if (!path.empty()) {
        AddProjectLayerFromFile(path);
    }
}

void CompositeModule::TriggerAddFromLibrary() {
    m_LibraryPickerMode = LibraryPickerMode::AddProjectLayer;
    m_LibraryPickerTargetLayerId.clear();
    m_ShowLibraryPicker = true;
}

void CompositeModule::TriggerSaveToLibrary() {
    if (!HasLayers()) {
        return;
    }
    if (m_ProjectFileName.empty()) {
        m_OpenSavePopup = true;
        return;
    }

    LibraryManager::Get().RequestSaveCompositeProject(m_ProjectName, this, m_ProjectFileName);
}

void CompositeModule::TriggerExportPng() {
    if (!HasLayers()) {
        return;
    }
    const std::string exportPath = FileDialogs::SavePngFileDialog("Export Composite PNG", "composite_export.png");
    if (!exportPath.empty()) {
        ExportCurrentPng(exportPath);
    }
}

void CompositeModule::AddImageLayerFromFile(const std::string& path) {
    LayerFileLoadData data;
    if (!LoadImageLayerDataFromFile(path, data)) {
        return;
    }

    ApplyImportDataToNewLayer(data, m_CanvasW, m_CanvasH, m_Layers, m_SelectedId);
    QueueImportedExternalAssetMirror(path, data);
    MarkDocumentDirty();
}

void CompositeModule::AddProjectLayerFromFile(const std::string& path) {
    LayerFileLoadData data;
    if (!LoadEditorProjectLayerDataFromFile(path, m_LimitProjectResolution, data)) {
        return;
    }

    ApplyImportDataToNewLayer(data, m_CanvasW, m_CanvasH, m_Layers, m_SelectedId);
    MarkDocumentDirty();
}

void CompositeModule::AddShapeLayer(const LayerKind kind) {
    if (kind != LayerKind::ShapeRect && kind != LayerKind::ShapeCircle) {
        return;
    }

    CompositeLayer layer;
    layer.id = NewLayerId();
    layer.kind = kind;
    layer.name = (kind == LayerKind::ShapeCircle) ? "Circle" : "Square";
    layer.fillColor = { 0.86f, 0.46f, 0.18f, 1.0f };
    layer.logicalW = 256;
    layer.logicalH = 256;
    layer.scaleX = 1.0f;
    layer.scaleY = 1.0f;

    int maxZ = -1;
    for (const CompositeLayer& existing : m_Layers) {
        maxZ = std::max(maxZ, existing.z);
    }
    layer.z = maxZ + 1;
    const FloatRect viewRect = ComputeViewWorldRect(m_CanvasW, m_CanvasH, m_ViewZoom, m_ViewPanX, m_ViewPanY);
    const ImVec2 viewCenter(viewRect.x + viewRect.width * 0.5f, viewRect.y + viewRect.height * 0.5f);
    layer.x = viewCenter.x - LayerWorldWidth(layer) * 0.5f;
    layer.y = viewCenter.y - LayerWorldHeight(layer) * 0.5f;

    if (!RegenerateGeneratedLayerTexture(layer)) {
        return;
    }

    m_Layers.push_back(std::move(layer));
    m_SelectedId = m_Layers.back().id;
    MarkDocumentDirty();
}

void CompositeModule::AddTextLayer() {
    CompositeLayer layer;
    layer.id = NewLayerId();
    layer.kind = LayerKind::Text;
    layer.name = "Text";
    layer.textContent = "Text";
    layer.textFontSize = 72.0f;
    layer.fillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    layer.scaleX = 1.0f;
    layer.scaleY = 1.0f;
    layer.preserveAspectRatio = true;

    int maxZ = -1;
    for (const CompositeLayer& existing : m_Layers) {
        maxZ = std::max(maxZ, existing.z);
    }
    layer.z = maxZ + 1;

    if (!RegenerateGeneratedLayerTexture(layer)) {
        return;
    }

    const FloatRect viewRect = ComputeViewWorldRect(m_CanvasW, m_CanvasH, m_ViewZoom, m_ViewPanX, m_ViewPanY);
    const ImVec2 viewCenter(viewRect.x + viewRect.width * 0.5f, viewRect.y + viewRect.height * 0.5f);
    layer.x = viewCenter.x - LayerWorldWidth(layer) * 0.5f;
    layer.y = viewCenter.y - LayerWorldHeight(layer) * 0.5f;

    m_Layers.push_back(std::move(layer));
    m_SelectedId = m_Layers.back().id;
    MarkDocumentDirty();
}

bool CompositeModule::ReplaceSelectedLayerWithImageFile(const std::string& path) {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return false;
    }

    LayerFileLoadData data;
    if (!LoadImageLayerDataFromFile(path, data)) {
        return false;
    }

    ApplyImportDataToExistingLayer(data, *selected);
    QueueImportedExternalAssetMirror(path, data);
    MarkDocumentDirty();
    return true;
}

bool CompositeModule::ReplaceSelectedLayerWithProjectFile(const std::string& path) {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return false;
    }

    LayerFileLoadData data;
    if (!LoadEditorProjectLayerDataFromFile(path, m_LimitProjectResolution, data)) {
        return false;
    }

    ApplyImportDataToExistingLayer(data, *selected);
    MarkDocumentDirty();
    return true;
}

bool CompositeModule::ConvertSelectedLayerToEditorProject() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return false;
    }

    if (selected->kind == LayerKind::EditorProject) {
        return true;
    }

    if (!BuildLayerSourcePngIfMissing(*selected)) {
        return false;
    }

    selected->kind = LayerKind::EditorProject;
    selected->embeddedProjectJson = json::array().dump();
    selected->generatedFromImage = true;
    selected->linkedProjectFileName.clear();
    selected->linkedProjectName.clear();
    MarkDocumentDirty();
    return true;
}

bool CompositeModule::OpenSelectedLayerInEditor() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return false;
    }

    if (selected->kind != LayerKind::EditorProject && !ConvertSelectedLayerToEditorProject()) {
        return false;
    }

    m_PendingOpenInEditorRequest = true;
    return true;
}

void CompositeModule::DuplicateSelectedLayer() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return;
    }

    CompositeLayer copy = *selected;
    copy.id = NewLayerId();
    copy.name = selected->name + " Copy";
    copy.tex = 0;
    copy.linkedProjectFileName.clear();
    copy.linkedProjectName.clear();

    int maxZ = -1;
    for (const CompositeLayer& layer : m_Layers) {
        maxZ = std::max(maxZ, layer.z);
    }
    copy.z = maxZ + 1;

    if (!copy.rgba.empty() && copy.imgW > 0 && copy.imgH > 0) {
        copy.tex = GLHelpers::CreateTextureFromPixels(copy.rgba.data(), copy.imgW, copy.imgH, 4);
    }

    m_Layers.push_back(std::move(copy));
    m_SelectedId = m_Layers.back().id;
    MarkDocumentDirty();
}

void CompositeModule::RemoveSelectedLayers() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected || selected->locked) {
        return;
    }

    const std::string selectedId = m_SelectedId;
    m_Layers.erase(
        std::remove_if(
            m_Layers.begin(),
            m_Layers.end(),
            [&](CompositeLayer& layer) {
                if (layer.id != selectedId) {
                    return false;
                }

                if (layer.tex != 0) {
                    glDeleteTextures(1, &layer.tex);
                    layer.tex = 0;
                }
                return true;
            }),
        m_Layers.end());

    m_SelectedId.clear();
    MarkDocumentDirty();
}

void CompositeModule::BeginRenameSelectedLayer() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return;
    }

    m_RenameLayerId = selected->id;
    std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", selected->name.c_str());
    m_OpenRenamePopup = true;
}

bool CompositeModule::UpdateLinkedProjectFromSelectedLayer() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected ||
        selected->kind != LayerKind::EditorProject ||
        selected->linkedProjectFileName.empty() ||
        selected->embeddedProjectJson.empty()) {
        return false;
    }

    if (!BuildLayerSourcePngIfMissing(*selected)) {
        return false;
    }

    json pipelineData;
    try {
        pipelineData = json::parse(selected->embeddedProjectJson);
    } catch (...) {
        return false;
    }

    std::vector<uint8_t> renderedTopLeft = selected->rgba;
    LibraryManager::FlipImageRowsInPlace(renderedTopLeft, selected->imgW, selected->imgH, 4);

    const std::string projectName = selected->linkedProjectName.empty() ? selected->name : selected->linkedProjectName;
    const bool success = LibraryManager::Get().OverwriteEditorProject(
        selected->linkedProjectFileName,
        projectName,
        selected->originalSourcePng,
        pipelineData,
        renderedTopLeft,
        selected->imgW,
        selected->imgH);
    if (success) {
        selected->linkedProjectName = projectName;
    }
    return success;
}

bool CompositeModule::BuildExportRaster(
    std::vector<uint8_t>& outRgba,
    int& outW,
    int& outH,
    const bool useExportSettings) const {

    outRgba.clear();
    outW = 0;
    outH = 0;

    std::vector<const CompositeLayer*> layers;
    layers.reserve(m_Layers.size());
    for (const CompositeLayer& layer : m_Layers) {
        if (!layer.visible || layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
            continue;
        }
        layers.push_back(&layer);
    }

    if (layers.empty()) {
        return false;
    }

    std::sort(layers.begin(), layers.end(), [](const CompositeLayer* a, const CompositeLayer* b) {
        return a->z < b->z;
    });

    FloatRect worldBounds;
    if (useExportSettings && m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        worldBounds = {
            m_ExportSettings.customX,
            m_ExportSettings.customY,
            m_ExportSettings.customWidth,
            m_ExportSettings.customHeight
        };
        if (!IsRectValid(worldBounds)) {
            return false;
        }

        outW = std::max(1, m_ExportSettings.outputWidth);
        outH = std::max(
            1,
            static_cast<int>(std::round(static_cast<float>(outW) / std::max(0.0001f, RectAspectRatio(worldBounds)))));
    } else {
        if (!ComputeAutoBounds(layers, worldBounds)) {
            return false;
        }

        if (useExportSettings) {
            outW = std::max(1, m_ExportSettings.outputWidth);
            outH = std::max(
                1,
                static_cast<int>(std::round(static_cast<float>(outW) / std::max(0.0001f, RectAspectRatio(worldBounds)))));
        } else {
            outW = std::max(1, static_cast<int>(std::ceil(worldBounds.width)));
            outH = std::max(1, static_cast<int>(std::ceil(worldBounds.height)));
        }
    }

    const CompositeExportBackgroundMode backgroundMode = useExportSettings
        ? m_ExportSettings.backgroundMode
        : CompositeExportBackgroundMode::Transparent;
    const std::array<float, 4> backgroundColor = useExportSettings
        ? m_ExportSettings.backgroundColor
        : std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f };

    RasterizeLayersToTopLeftRgba(
        layers,
        worldBounds,
        outW,
        outH,
        backgroundMode,
        backgroundColor,
        outRgba);

    return !outRgba.empty();
}

bool CompositeModule::ExportCurrentPng(const std::string& path) const {
    if (path.empty()) {
        return false;
    }

    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    if (!BuildExportRaster(rgba, width, height, true) || rgba.empty()) {
        return false;
    }

    return stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4) != 0;
}

bool CompositeModule::ApplyLibraryProject(const StackBinaryFormat::ProjectDocument& document) {
    if (document.metadata.projectKind != StackBinaryFormat::kCompositeProjectKind) {
        return false;
    }

    ClearLayersGpu();
    ClearStagePreviewGpu();
    m_Layers.clear();
    m_SelectedId.clear();

    const json& root = document.pipelineData;
    if (!root.is_object()) {
        return false;
    }

    const json& view = root.value("view", json::object());
    m_ViewZoom = std::max(0.05f, view.value("zoom", 1.0f));
    m_ViewPanX = view.value("panX", 0.0f);
    m_ViewPanY = view.value("panY", 0.0f);
    m_ShowChecker = view.value("showChecker", true);
    m_LimitProjectResolution = view.value("limitProjectResolution", true);
    m_SnapEnabled = view.value("snapEnabled", false);
    m_SnapToObjects = view.value("snapToObjects", true);
    m_SnapToCenters = view.value("snapToCenters", true);
    m_SnapToCanvasCenter = view.value("snapToCanvasCenter", true);
    m_SnapToSpacing = view.value("snapToSpacing", true);
    m_LastNonZeroGridSize = 24.0f;
    m_LastNonZeroRotateSnapStep = 15.0f;
    m_LastNonZeroScaleSnapStep = 0.1f;
    m_GridSize = std::max(0.0f, view.value("gridSize", 24.0f));
    m_RotateSnapStep = std::clamp(view.value("rotateSnapStep", 15.0f), 0.0f, 180.0f);
    m_ScaleSnapStep = std::clamp(view.value("scaleSnapStep", 0.1f), 0.0f, 1.0f);
    RememberSnapStepDefaults();
    m_SelectedId = root.value("selectionLayerId", std::string());

    const json& exportJson = root.value("export", json::object());
    m_ExportSettings.boundsMode = ExportBoundsModeFromToken(exportJson.value("boundsMode", std::string("auto")));
    m_ExportSettings.backgroundMode = ExportBackgroundModeFromToken(exportJson.value("backgroundMode", std::string("transparent")));
    m_ExportSettings.customX = exportJson.value("x", m_ExportSettings.customX);
    m_ExportSettings.customY = exportJson.value("y", m_ExportSettings.customY);
    m_ExportSettings.customWidth = exportJson.value("width", m_ExportSettings.customWidth);
    m_ExportSettings.customHeight = exportJson.value("height", m_ExportSettings.customHeight);
    if (exportJson.contains("aspectPreset")) {
        m_ExportSettings.aspectPreset = ExportAspectPresetFromToken(exportJson.value("aspectPreset", std::string("1:1")));
    } else {
        m_ExportSettings.aspectPreset = CompositeExportAspectPreset::Custom;
    }
    m_ExportSettings.customAspectRatio = exportJson.value("customAspectRatio", m_ExportSettings.customAspectRatio);
    m_ExportSettings.outputWidth = std::max(1, exportJson.value("outputWidth", m_ExportSettings.outputWidth));
    m_ExportSettings.outputHeight = std::max(1, exportJson.value("outputHeight", m_ExportSettings.outputHeight));
    if (!std::isfinite(m_ExportSettings.customAspectRatio) || m_ExportSettings.customAspectRatio <= 0.0001f) {
        m_ExportSettings.customAspectRatio =
            static_cast<float>(m_ExportSettings.outputWidth) / static_cast<float>(std::max(1, m_ExportSettings.outputHeight));
    }
    if (exportJson.contains("backgroundColor") && exportJson["backgroundColor"].is_array() && exportJson["backgroundColor"].size() >= 3) {
        for (int channel = 0; channel < 3; ++channel) {
            m_ExportSettings.backgroundColor[channel] = Clamp01(exportJson["backgroundColor"][channel].get<float>());
        }
        if (exportJson["backgroundColor"].size() >= 4) {
            m_ExportSettings.backgroundColor[3] = Clamp01(exportJson["backgroundColor"][3].get<float>());
        }
    }

    const json& workspaceJson = root.value("workspace", json::object());
    const json& panelsJson = workspaceJson.value("panels", json::object());
    m_ShowLayersWindow = panelsJson.value("layers", true);
    m_ShowSelectedWindow = panelsJson.value("selected", true);
    m_ShowViewWindow = panelsJson.value("view", true);
    m_ShowExportWindow = panelsJson.value("export", true);
    m_WorkspaceLayoutIni = workspaceJson.value("layoutIni", std::string());
    m_PendingWorkspaceLayoutLoad = !m_WorkspaceLayoutIni.empty();
    m_PendingWorkspaceLayoutReset = m_WorkspaceLayoutIni.empty();
    m_SuspendWorkspaceLayoutDirtyTracking = true;
    m_RightMousePressedOnCanvas = false;
    m_ActiveExportHandle = ExportHandleType::None;
    m_ExportPanelActive = false;

    const json& layersJson = root.value("layers", json::array());
    for (const auto& item : layersJson) {
        if (!item.is_object()) {
            continue;
        }

        CompositeLayer layer;
        layer.id = item.value("id", NewLayerId());
        layer.name = item.value("name", std::string("Layer"));
        layer.kind = LayerKindFromToken(item.value("kind", std::string("image")));
        layer.x = item.value("x", 0.0f);
        layer.y = item.value("y", 0.0f);
        const float legacyScale = std::max(0.0001f, item.value("scale", 1.0f));
        layer.scaleX = std::max(0.0001f, item.value("scaleX", legacyScale));
        layer.scaleY = std::max(0.0001f, item.value("scaleY", legacyScale));
        layer.preserveAspectRatio = item.value("preserveAspectRatio", layer.kind == LayerKind::Text);
        layer.rotation = item.value("rotation", 0.0f);
        layer.opacity = std::clamp(item.value("opacity", 1.0f), 0.0f, 1.0f);
        layer.visible = item.value("visible", true);
        layer.locked = item.value("locked", false);
        layer.flipX = item.value("flipX", false);
        layer.flipY = item.value("flipY", false);
        layer.blendMode = BlendModeFromToken(item.value("blendMode", std::string("normal")));
        layer.z = item.value("z", 0);
        layer.logicalW = item.value("logicalW", 0);
        layer.logicalH = item.value("logicalH", 0);
        if (item.contains("fillColor") && item["fillColor"].is_array() && item["fillColor"].size() >= 3) {
            for (int channel = 0; channel < 3; ++channel) {
                layer.fillColor[channel] = Clamp01(item["fillColor"][channel].get<float>());
            }
            if (item["fillColor"].size() >= 4) {
                layer.fillColor[3] = Clamp01(item["fillColor"][3].get<float>());
            }
        }
        layer.textContent = item.value("textContent", std::string("Text"));
        layer.textFontSize = std::max(1.0f, item.value("textFontSize", 72.0f));
        layer.embeddedProjectJson = item.value("projectJson", json::array().dump());
        layer.linkedProjectFileName = item.value("linkedProjectFileName", std::string());
        layer.linkedProjectName = item.value("linkedProjectName", std::string());
        layer.generatedFromImage = item.value("generatedFromImage", false);

        if (item.contains("sourcePng") && item["sourcePng"].is_binary()) {
            layer.originalSourcePng = item["sourcePng"].get_binary();
        }

        if (item.contains("imagePng") && item["imagePng"].is_binary()) {
            const auto& binary = item["imagePng"].get_binary();
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_set_flip_vertically_on_load_thread(1);
            unsigned char* pixels = stbi_load_from_memory(binary.data(), static_cast<int>(binary.size()), &width, &height, &channels, 4);
            if (pixels && width > 0 && height > 0) {
                layer.imgW = width;
                layer.imgH = height;
                if (layer.logicalW <= 0) layer.logicalW = width;
                if (layer.logicalH <= 0) layer.logicalH = height;
                layer.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
                stbi_image_free(pixels);
                layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);

                if (layer.originalSourcePng.empty()) {
                    BuildTopLeftPngFromBottomLeftRgba(layer.rgba, layer.imgW, layer.imgH, layer.originalSourcePng);
                }
            } else if (pixels) {
                stbi_image_free(pixels);
            }
        }

        if ((layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) && IsGeneratedLayer(layer)) {
            RegenerateGeneratedLayerTexture(layer);
        }

        m_Layers.push_back(std::move(layer));
    }

    if (m_ExportSettings.aspectPreset == CompositeExportAspectPreset::Custom) {
        UpdateCustomExportAspectFromBounds();
    }
    SyncExportResolutionFromWidth();

    m_ProjectName = document.metadata.projectName;
    m_PendingOpenInEditorRequest = false;
    m_Dirty = false;
    m_StagePreviewDirty = true;
    m_LastStagePreviewZoom = -1.0f;
    return true;
}

bool CompositeModule::BuildProjectDocumentForSave(const std::string& displayName, StackBinaryFormat::ProjectDocument& outDocument) const {
    std::vector<uint8_t> rgba;
    int rasterW = 0;
    int rasterH = 0;
    if (!BuildExportRaster(rgba, rasterW, rasterH, false) || rgba.empty()) {
        return false;
    }

    std::vector<uint8_t> fullPng;
    EncodePng(rgba, rasterW, rasterH, fullPng);
    if (fullPng.empty()) {
        return false;
    }

    std::vector<uint8_t> thumbRgba;
    int thumbW = rasterW;
    int thumbH = rasterH;
    const int maxEdge = 320;
    if (rasterW > maxEdge || rasterH > maxEdge) {
        const float scale = static_cast<float>(maxEdge) / static_cast<float>(std::max(rasterW, rasterH));
        thumbW = std::max(1, static_cast<int>(std::floor(static_cast<float>(rasterW) * scale)));
        thumbH = std::max(1, static_cast<int>(std::floor(static_cast<float>(rasterH) * scale)));
        ResizeNearest(rgba, rasterW, rasterH, thumbW, thumbH, thumbRgba);
    } else {
        thumbRgba = rgba;
    }

    std::vector<uint8_t> thumbPng;
    EncodePng(thumbRgba, thumbW, thumbH, thumbPng);

    json layersJson = json::array();
    for (const CompositeLayer& layer : m_Layers) {
        if (layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
            continue;
        }

        std::vector<uint8_t> layerPreviewPng;
        if (!BuildTopLeftPngFromBottomLeftRgba(layer.rgba, layer.imgW, layer.imgH, layerPreviewPng)) {
            continue;
        }

        json layerJson = json::object();
        layerJson["id"] = layer.id;
        layerJson["kind"] = LayerKindToToken(layer.kind);
        layerJson["name"] = layer.name;
        layerJson["x"] = layer.x;
        layerJson["y"] = layer.y;
        layerJson["z"] = layer.z;
        layerJson["scaleX"] = layer.scaleX;
        layerJson["scaleY"] = layer.scaleY;
        layerJson["preserveAspectRatio"] = layer.preserveAspectRatio;
        layerJson["rotation"] = layer.rotation;
        layerJson["opacity"] = layer.opacity;
        layerJson["visible"] = layer.visible;
        layerJson["locked"] = layer.locked;
        layerJson["flipX"] = layer.flipX;
        layerJson["flipY"] = layer.flipY;
        layerJson["blendMode"] = BlendModeToToken(layer.blendMode);
        layerJson["fillColor"] = json::array({
            layer.fillColor[0],
            layer.fillColor[1],
            layer.fillColor[2],
            layer.fillColor[3]
        });
        layerJson["textContent"] = layer.textContent;
        layerJson["textFontSize"] = layer.textFontSize;
        layerJson["logicalW"] = layer.logicalW;
        layerJson["logicalH"] = layer.logicalH;
        layerJson["imagePng"] = json::binary(std::move(layerPreviewPng));
        layerJson["linkedProjectFileName"] = layer.linkedProjectFileName;
        layerJson["linkedProjectName"] = layer.linkedProjectName;
        layerJson["generatedFromImage"] = layer.generatedFromImage;
        if (!layer.originalSourcePng.empty()) {
            layerJson["sourcePng"] = json::binary(layer.originalSourcePng);
        }
        if (layer.kind == LayerKind::EditorProject) {
            layerJson["projectJson"] = layer.embeddedProjectJson;
        }

        layersJson.push_back(std::move(layerJson));
    }

    json root = json::object();
    root["compositeVersion"] = kCompositeFormatVersion;
    root["layers"] = std::move(layersJson);
    root["selectionLayerId"] = m_SelectedId;

    json viewJson = json::object();
    viewJson["zoom"] = m_ViewZoom;
    viewJson["panX"] = m_ViewPanX;
    viewJson["panY"] = m_ViewPanY;
    viewJson["showChecker"] = m_ShowChecker;
    viewJson["limitProjectResolution"] = m_LimitProjectResolution;
    viewJson["snapEnabled"] = m_SnapEnabled;
    viewJson["snapToObjects"] = m_SnapToObjects;
    viewJson["snapToCenters"] = m_SnapToCenters;
    viewJson["snapToCanvasCenter"] = m_SnapToCanvasCenter;
    viewJson["snapToSpacing"] = m_SnapToSpacing;
    viewJson["gridSize"] = m_GridSize;
    viewJson["rotateSnapStep"] = m_RotateSnapStep;
    viewJson["scaleSnapStep"] = m_ScaleSnapStep;
    root["view"] = std::move(viewJson);

    json exportJson = json::object();
    exportJson["boundsMode"] = ExportBoundsModeToToken(m_ExportSettings.boundsMode);
    exportJson["backgroundMode"] = ExportBackgroundModeToToken(m_ExportSettings.backgroundMode);
    exportJson["backgroundColor"] = json::array({
        m_ExportSettings.backgroundColor[0],
        m_ExportSettings.backgroundColor[1],
        m_ExportSettings.backgroundColor[2],
        m_ExportSettings.backgroundColor[3]
    });
    exportJson["x"] = m_ExportSettings.customX;
    exportJson["y"] = m_ExportSettings.customY;
    exportJson["width"] = m_ExportSettings.customWidth;
    exportJson["height"] = m_ExportSettings.customHeight;
    exportJson["aspectPreset"] = ExportAspectPresetToToken(m_ExportSettings.aspectPreset);
    exportJson["customAspectRatio"] = m_ExportSettings.customAspectRatio;
    exportJson["outputWidth"] = m_ExportSettings.outputWidth;
    exportJson["outputHeight"] = m_ExportSettings.outputHeight;
    root["export"] = std::move(exportJson);

    json workspaceJson = json::object();
    json panelsJson = json::object();
    panelsJson["layers"] = m_ShowLayersWindow;
    panelsJson["selected"] = m_ShowSelectedWindow;
    panelsJson["view"] = m_ShowViewWindow;
    panelsJson["export"] = m_ShowExportWindow;
    workspaceJson["panels"] = std::move(panelsJson);
    workspaceJson["layoutIni"] = m_WorkspaceLayoutIni;
    root["workspace"] = std::move(workspaceJson);

    outDocument.metadata.projectKind = StackBinaryFormat::kCompositeProjectKind;
    outDocument.metadata.projectName = displayName.empty() ? m_ProjectName : displayName;
    outDocument.metadata.timestamp.clear();
    outDocument.metadata.sourceWidth = rasterW;
    outDocument.metadata.sourceHeight = rasterH;
    outDocument.thumbnailBytes = std::move(thumbPng);
    outDocument.sourceImageBytes = std::move(fullPng);
    outDocument.pipelineData = std::move(root);
    return true;
}

void CompositeModule::ResetWorkspaceLayout(const bool markDirty) {
    m_ShowLayersWindow = true;
    m_ShowSelectedWindow = true;
    m_ShowViewWindow = true;
    m_ShowExportWindow = true;
    m_ActiveExportHandle = ExportHandleType::None;
    m_ExportPanelActive = false;
    m_WorkspaceLayoutIni.clear();
    m_PendingWorkspaceLayoutLoad = false;
    m_PendingWorkspaceLayoutReset = true;
    m_SuspendWorkspaceLayoutDirtyTracking = true;
    if (markDirty) {
        MarkDocumentDirty();
    }
}

void CompositeModule::BuildDefaultWorkspaceLayout(const unsigned int dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetWindowSize());

    ImGuiID mainDockId = dockspaceId;
    ImGuiID leftDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.28f, nullptr, &mainDockId);

    ImGui::DockBuilderDockWindow(kCompositeLayersWindowName, leftDockId);
    ImGui::DockBuilderDockWindow(kCompositeCanvasWindowName, mainDockId);
    ImGui::DockBuilderDockWindow(kCompositeSelectedWindowName, leftDockId);
    ImGui::DockBuilderDockWindow(kCompositeViewWindowName, leftDockId);
    ImGui::DockBuilderDockWindow(kCompositeExportWindowName, leftDockId);
    ImGui::DockBuilderFinish(dockspaceId);
}

void CompositeModule::CaptureWorkspaceLayout() {
    if (m_WorkspaceDockId == 0) {
        return;
    }

    const std::string currentLayout = CaptureCompositeWorkspaceIni(m_WorkspaceDockId);
    if (currentLayout.empty()) {
        return;
    }

    if (currentLayout != m_WorkspaceLayoutIni) {
        m_WorkspaceLayoutIni = currentLayout;
        if (!m_SuspendWorkspaceLayoutDirtyTracking) {
            MarkDocumentDirty();
        }
    }

    m_SuspendWorkspaceLayoutDirtyTracking = false;
}

bool CompositeModule::ShouldShowExportBoundsOverlay() const {
    return HasLayers() &&
           m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom &&
           (m_ExportPanelActive || m_ActiveExportHandle != ExportHandleType::None);
}

float CompositeModule::GetCurrentExportOutputAspectRatio() const {
    if (m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        const FloatRect customBounds {
            m_ExportSettings.customX,
            m_ExportSettings.customY,
            m_ExportSettings.customWidth,
            m_ExportSettings.customHeight
        };
        if (IsRectValid(customBounds)) {
            return RectAspectRatio(customBounds);
        }
    } else {
        std::vector<const CompositeLayer*> layers;
        layers.reserve(m_Layers.size());
        for (const CompositeLayer& layer : m_Layers) {
            if (!layer.visible || layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
                continue;
            }
            layers.push_back(&layer);
        }

        FloatRect autoBounds;
        if (ComputeAutoBounds(layers, autoBounds)) {
            return RectAspectRatio(autoBounds);
        }
    }

    return ExportAspectRatioValue(m_ExportSettings);
}

void CompositeModule::UpdateCustomExportAspectFromBounds() {
    m_ExportSettings.customWidth = std::max(1.0f, m_ExportSettings.customWidth);
    m_ExportSettings.customHeight = std::max(1.0f, m_ExportSettings.customHeight);
    m_ExportSettings.customAspectRatio =
        std::max(1.0f, m_ExportSettings.customWidth) / std::max(1.0f, m_ExportSettings.customHeight);
}

void CompositeModule::SyncExportResolutionFromWidth() {
    m_ExportSettings.outputWidth = std::max(1, m_ExportSettings.outputWidth);
    const float ratio = GetCurrentExportOutputAspectRatio();
    m_ExportSettings.outputHeight = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(m_ExportSettings.outputWidth) / std::max(0.0001f, ratio))));
}

void CompositeModule::SyncExportResolutionFromHeight() {
    m_ExportSettings.outputHeight = std::max(1, m_ExportSettings.outputHeight);
    const float ratio = GetCurrentExportOutputAspectRatio();
    m_ExportSettings.outputWidth = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(m_ExportSettings.outputHeight) * std::max(0.0001f, ratio))));
}

void CompositeModule::RenderToolbar() {
    if (ImGui::BeginTable("CompositeToolbarTable", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Panels", ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button("New")) {
            NewProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Image")) {
            TriggerAddImage();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Project")) {
            TriggerAddProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add From Library")) {
            TriggerAddFromLibrary();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!HasLayers());
        if (ImGui::Button("Save To Library")) {
            TriggerSaveToLibrary();
        }
        ImGui::SameLine();
        if (ImGui::Button("Export PNG")) {
            TriggerExportPng();
        }
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(1);
        auto renderPanelButton = [&](const char* label, bool& open) {
            if (ImGui::SmallButton(label) && !open) {
                open = true;
                MarkDocumentDirty();
            }
        };

        renderPanelButton("Layers", m_ShowLayersWindow);
        ImGui::SameLine();
        renderPanelButton("Selected", m_ShowSelectedWindow);
        ImGui::SameLine();
        renderPanelButton("View", m_ShowViewWindow);
        ImGui::SameLine();
        renderPanelButton("Export", m_ShowExportWindow);

        ImGui::EndTable();
    }

    const std::string& saveStatus = LibraryManager::Get().GetSaveStatusText();
    if (!saveStatus.empty()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", saveStatus.c_str());
        ImGui::PopTextWrapPos();
    }
}

void CompositeModule::RenderLayerPane() {
    ImGui::Text("Project: %s", m_ProjectName.c_str());
    ImGui::TextDisabled("Right click a layer for actions. Drag rows to reorder.");
    ImGui::Separator();

    if (ImGui::BeginTable("CompositeLayerTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(-1, 0))) {
        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("Vis", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Lock", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableHeadersRow();

        std::vector<CompositeLayer*> sorted;
        sorted.reserve(m_Layers.size());
        for (CompositeLayer& layer : m_Layers) {
            sorted.push_back(&layer);
        }
        std::sort(sorted.begin(), sorted.end(), [](const CompositeLayer* a, const CompositeLayer* b) {
            return a->z > b->z;
        });

        for (int displayIndex = 0; displayIndex < static_cast<int>(sorted.size()); ++displayIndex) {
            CompositeLayer* layer = sorted[displayIndex];
            ImGui::PushID(layer->id.c_str());
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const bool selected = (layer->id == m_SelectedId);
            if (ImGui::Selectable(layer->name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                m_SelectedId = layer->id;
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("COMPOSITE_LAYER_MOVE", &displayIndex, sizeof(int));
                ImGui::Text("Move %s", layer->name.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMPOSITE_LAYER_MOVE")) {
                    const int sourceIndex = *static_cast<const int*>(payload->Data);
                    if (sourceIndex != displayIndex &&
                        sourceIndex >= 0 &&
                        sourceIndex < static_cast<int>(sorted.size())) {
                        CompositeLayer* movingLayer = sorted[sourceIndex];
                        sorted.erase(sorted.begin() + sourceIndex);
                        sorted.insert(sorted.begin() + displayIndex, movingLayer);
                        ReassignZFromTopOrder(sorted);
                        MarkDocumentDirty();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem("CompositeLayerRowContext")) {
                m_SelectedId = layer->id;
                RenderLayerContextMenu(layer);
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", LayerKindBadge(layer->kind));

            ImGui::TableSetColumnIndex(2);
            bool visible = layer->visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                layer->visible = visible;
                MarkDocumentDirty();
            }

            ImGui::TableSetColumnIndex(3);
            bool locked = layer->locked;
            if (ImGui::Checkbox("##locked", &locked)) {
                layer->locked = locked;
                MarkDocumentDirty();
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void CompositeModule::RenderSelectedInspector(CompositeLayer* selectedLayer) {
    if (!selectedLayer) {
        ImGui::TextDisabled("Select a layer to inspect it.");
        return;
    }

    const bool layerLocked = selectedLayer->locked;
    const int displayWidth = std::max(1, static_cast<int>(std::round(LayerWorldWidth(*selectedLayer))));
    const int displayHeight = std::max(1, static_cast<int>(std::round(LayerWorldHeight(*selectedLayer))));

    ImGui::Text("Name: %s", selectedLayer->name.c_str());
    ImGui::TextDisabled("Kind: %s", LayerKindBadge(selectedLayer->kind));
    ImGui::TextDisabled("Display Size: %d x %d", displayWidth, displayHeight);
    if (selectedLayer->kind == LayerKind::Text) {
        ImGui::TextDisabled("Render Size: %d x %d", selectedLayer->imgW, selectedLayer->imgH);
    }
    ImGui::TextDisabled("Visible: %s", selectedLayer->visible ? "Yes" : "No");
    ImGui::TextDisabled("Locked: %s", selectedLayer->locked ? "Yes" : "No");
    if (selectedLayer->kind == LayerKind::EditorProject) {
        if (!selectedLayer->linkedProjectName.empty()) {
            ImGui::TextDisabled("Linked Project: %s", selectedLayer->linkedProjectName.c_str());
        } else if (selectedLayer->generatedFromImage) {
            ImGui::TextDisabled("Generated from an image layer inside Composite.");
        } else {
            ImGui::TextDisabled("Embedded editor project layer.");
        }
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Transform");
    ImGui::BeginDisabled(layerLocked);
    bool changed = false;
    bool scaleChanged = false;
    changed |= ImGui::DragFloat2("Position", &selectedLayer->x, 1.0f);
    changed |= ImGui::SliderAngle("Rotation", &selectedLayer->rotation, -180.0f, 180.0f);
    if (ImGui::Checkbox("Preserve Aspect Ratio", &selectedLayer->preserveAspectRatio)) {
        changed = true;
        scaleChanged = true;
        if (selectedLayer->preserveAspectRatio) {
            const float unifiedScale = std::max(selectedLayer->scaleX, selectedLayer->scaleY);
            selectedLayer->scaleX = unifiedScale;
            selectedLayer->scaleY = unifiedScale;
        }
    }
    if (selectedLayer->preserveAspectRatio) {
        float uniformScale = selectedLayer->scaleX;
        if (ImGui::DragFloat("Scale", &uniformScale, 0.01f, 0.01f, 32.0f, "%.2f")) {
            selectedLayer->scaleX = uniformScale;
            selectedLayer->scaleY = uniformScale;
            changed = true;
            scaleChanged = true;
        }
    } else {
        scaleChanged |= ImGui::DragFloat("Scale X", &selectedLayer->scaleX, 0.01f, 0.01f, 32.0f, "%.2f");
        scaleChanged |= ImGui::DragFloat("Scale Y", &selectedLayer->scaleY, 0.01f, 0.01f, 32.0f, "%.2f");
        changed |= scaleChanged;
    }
    if (changed) {
        selectedLayer->scaleX = std::max(0.01f, selectedLayer->scaleX);
        selectedLayer->scaleY = std::max(0.01f, selectedLayer->scaleY);
        if (scaleChanged && selectedLayer->kind == LayerKind::Text) {
            RegenerateGeneratedLayerTexture(*selectedLayer);
        }
        MarkDocumentDirty();
    }
    if (ImGui::Button("Reset Transform", ImVec2(-1, 0))) {
        selectedLayer->rotation = 0.0f;
        selectedLayer->scaleX = 1.0f;
        selectedLayer->scaleY = 1.0f;
        if (selectedLayer->kind == LayerKind::Text) {
            RegenerateGeneratedLayerTexture(*selectedLayer);
        }
        MarkDocumentDirty();
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextUnformatted("Appearance");
    ImGui::BeginDisabled(layerLocked);
    bool appearanceChanged = false;
    appearanceChanged |= ImGui::SliderFloat("Opacity", &selectedLayer->opacity, 0.0f, 1.0f);
    int blendModeIndex = static_cast<int>(selectedLayer->blendMode);
    const char* blendLabels[] = {
        "Normal",
        "Multiply",
        "Screen",
        "Add",
        "Overlay",
        "Soft Light",
        "Hard Light",
        "Hue",
        "Color"
    };
    if (ImGui::Combo("Blend Mode", &blendModeIndex, blendLabels, IM_ARRAYSIZE(blendLabels))) {
        selectedLayer->blendMode = static_cast<CompositeBlendMode>(blendModeIndex);
        appearanceChanged = true;
    }
    appearanceChanged |= ImGui::Checkbox("Flip X", &selectedLayer->flipX);
    appearanceChanged |= ImGui::Checkbox("Flip Y", &selectedLayer->flipY);
    if (selectedLayer->kind == LayerKind::ShapeRect ||
        selectedLayer->kind == LayerKind::ShapeCircle ||
        selectedLayer->kind == LayerKind::Text) {
        float fillColor[4] = {
            selectedLayer->fillColor[0],
            selectedLayer->fillColor[1],
            selectedLayer->fillColor[2],
            selectedLayer->fillColor[3]
        };
        if (ImGui::ColorEdit4("Fill Color", fillColor)) {
            for (int channel = 0; channel < 4; ++channel) {
                selectedLayer->fillColor[channel] = Clamp01(fillColor[channel]);
            }
            if (RegenerateGeneratedLayerTexture(*selectedLayer)) {
                appearanceChanged = true;
            }
        }
    }
    if (appearanceChanged) {
        MarkDocumentDirty();
    }
    ImGui::EndDisabled();

    if (selectedLayer->kind == LayerKind::Text) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Text");
        ImGui::BeginDisabled(layerLocked);
        char textBuffer[4096];
        std::snprintf(textBuffer, sizeof(textBuffer), "%s", selectedLayer->textContent.c_str());
        bool textChanged = false;
        if (ImGui::InputTextMultiline("Content", textBuffer, sizeof(textBuffer), ImVec2(-1.0f, 120.0f))) {
            selectedLayer->textContent = textBuffer;
            textChanged = true;
        }
        textChanged |= ImGui::DragFloat("Font Size", &selectedLayer->textFontSize, 1.0f, 1.0f, 0.0f, "%.0f px");
        if (textChanged) {
            selectedLayer->textFontSize = std::max(1.0f, selectedLayer->textFontSize);
            if (RegenerateGeneratedLayerTexture(*selectedLayer)) {
                MarkDocumentDirty();
            }
        }
        ImGui::EndDisabled();
    }
}

void CompositeModule::RenderViewInspector() {
    if (ImGui::SliderFloat("Zoom", &m_ViewZoom, 0.05f, 16.0f, "%.2f")) {
        MarkStageDirty();
    }
    if (ImGui::Button("Reset View", ImVec2(-1, 0))) {
        m_ViewZoom = 1.0f;
        m_ViewPanX = 0.0f;
        m_ViewPanY = 0.0f;
        MarkStageDirty();
    }

    if (ImGui::Checkbox("Show Checker", &m_ShowChecker)) {
        MarkDocumentDirty();
    }
    if (ImGui::Checkbox("Limit Project Preview Resolution To 4K", &m_LimitProjectResolution)) {
        MarkDocumentDirty();
    }

    ImGui::Separator();
    if (ImGui::Checkbox("Enable Snapping", &m_SnapEnabled)) {
        MarkDocumentDirty();
    }
    ImGui::BeginDisabled(!m_SnapEnabled);
    bool snapChanged = false;
    int snapModeIndex = static_cast<int>(GetSnapModePreset());
    const char* snapModeLabels[] = { "Full", "Object-Only", "Off", "Custom" };
    if (ImGui::Combo("Snap Mode", &snapModeIndex, snapModeLabels, IM_ARRAYSIZE(snapModeLabels))) {
        ApplySnapModePreset(static_cast<CompositeSnapModePreset>(snapModeIndex));
        snapChanged = true;
    }
    snapChanged |= ImGui::Checkbox("Snap To Objects", &m_SnapToObjects);
    snapChanged |= ImGui::Checkbox("Snap To Centers", &m_SnapToCenters);
    snapChanged |= ImGui::Checkbox("Snap To Canvas Center", &m_SnapToCanvasCenter);
    snapChanged |= ImGui::Checkbox("Snap To Spacing", &m_SnapToSpacing);
    ImGui::Separator();
    snapChanged |= ImGui::DragFloat("Grid Size", &m_GridSize, 1.0f, 0.0f, 512.0f, "%.0f px");
    snapChanged |= ImGui::DragFloat("Rotate Step", &m_RotateSnapStep, 1.0f, 0.0f, 180.0f, "%.0f deg");
    snapChanged |= ImGui::DragFloat("Scale Step", &m_ScaleSnapStep, 0.01f, 0.0f, 1.0f, "%.2f");
    if (snapChanged) {
        m_GridSize = std::max(0.0f, m_GridSize);
        m_RotateSnapStep = std::clamp(m_RotateSnapStep, 0.0f, 180.0f);
        m_ScaleSnapStep = std::clamp(m_ScaleSnapStep, 0.0f, 1.0f);
        RememberSnapStepDefaults();
        MarkDocumentDirty();
    }
    ImGui::EndDisabled();
}

void CompositeModule::RenderExportInspector() {
    ImGui::BeginDisabled(!HasLayers());
    const bool autoBounds = m_ExportSettings.boundsMode == CompositeExportBoundsMode::Auto;
    if (ImGui::RadioButton("Auto Bounds", autoBounds)) {
        m_ExportSettings.boundsMode = CompositeExportBoundsMode::Auto;
        MarkDocumentDirty();
    }
    if (ImGui::RadioButton("Custom Bounds", !autoBounds)) {
        m_ExportSettings.boundsMode = CompositeExportBoundsMode::Custom;
        MarkDocumentDirty();
    }

    static const CompositeExportAspectPreset aspectPresets[] = {
        CompositeExportAspectPreset::Ratio1x1,
        CompositeExportAspectPreset::Ratio4x3,
        CompositeExportAspectPreset::Ratio3x2,
        CompositeExportAspectPreset::Ratio16x9,
        CompositeExportAspectPreset::Ratio9x16,
        CompositeExportAspectPreset::Ratio2x3,
        CompositeExportAspectPreset::Ratio5x4,
        CompositeExportAspectPreset::Ratio21x9,
        CompositeExportAspectPreset::Custom
    };

    if (ImGui::BeginCombo("Aspect Ratio", ExportAspectPresetToLabel(m_ExportSettings.aspectPreset))) {
        for (int index = 0; index < IM_ARRAYSIZE(aspectPresets); ++index) {
            const bool selected = aspectPresets[index] == m_ExportSettings.aspectPreset;
            if (ImGui::Selectable(ExportAspectPresetToLabel(aspectPresets[index]), selected)) {
                m_ExportSettings.aspectPreset = aspectPresets[index];
                if (m_ExportSettings.aspectPreset == CompositeExportAspectPreset::Custom) {
                    UpdateCustomExportAspectFromBounds();
                } else if (m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
                    const float ratio = ExportAspectRatioValue(m_ExportSettings);
                    const float centerY = m_ExportSettings.customY + m_ExportSettings.customHeight * 0.5f;
                    m_ExportSettings.customHeight = std::max(1.0f, m_ExportSettings.customWidth / std::max(0.0001f, ratio));
                    m_ExportSettings.customY = centerY - m_ExportSettings.customHeight * 0.5f;
                }
                SyncExportResolutionFromWidth();
                MarkDocumentDirty();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        ImGui::TextDisabled("Adjust the export bounds directly on the canvas while the Export panel or tab is active.");
        ImGui::Text("Bounds: %.1f x %.1f", m_ExportSettings.customWidth, m_ExportSettings.customHeight);
        ImGui::TextDisabled("Origin: %.1f, %.1f", m_ExportSettings.customX, m_ExportSettings.customY);

        if (ImGui::Button("Use View As Export", ImVec2(-1, 0))) {
            const FloatRect viewRect = ComputeViewWorldRect(m_CanvasW, m_CanvasH, m_ViewZoom, m_ViewPanX, m_ViewPanY);
            m_ExportSettings.boundsMode = CompositeExportBoundsMode::Custom;
            m_ExportSettings.customX = viewRect.x;
            m_ExportSettings.customY = viewRect.y;
            m_ExportSettings.customWidth = viewRect.width;
            m_ExportSettings.customHeight = viewRect.height;
            m_ExportSettings.aspectPreset = CompositeExportAspectPreset::Custom;
            UpdateCustomExportAspectFromBounds();
            m_ExportSettings.outputWidth = std::max(1, static_cast<int>(std::round(std::max(1.0f, m_CanvasW))));
            SyncExportResolutionFromWidth();
            MarkDocumentDirty();
        }
    }

    ImGui::Separator();
    if (ImGui::RadioButton("Transparent Background", m_ExportSettings.backgroundMode == CompositeExportBackgroundMode::Transparent)) {
        m_ExportSettings.backgroundMode = CompositeExportBackgroundMode::Transparent;
        MarkDocumentDirty();
    }
    if (ImGui::RadioButton("Solid Background", m_ExportSettings.backgroundMode == CompositeExportBackgroundMode::Solid)) {
        m_ExportSettings.backgroundMode = CompositeExportBackgroundMode::Solid;
        MarkDocumentDirty();
    }
    if (m_ExportSettings.backgroundMode == CompositeExportBackgroundMode::Solid) {
        float color[3] = {
            m_ExportSettings.backgroundColor[0],
            m_ExportSettings.backgroundColor[1],
            m_ExportSettings.backgroundColor[2]
        };
        if (ImGui::ColorEdit3("Background Color", color)) {
            m_ExportSettings.backgroundColor[0] = Clamp01(color[0]);
            m_ExportSettings.backgroundColor[1] = Clamp01(color[1]);
            m_ExportSettings.backgroundColor[2] = Clamp01(color[2]);
            m_ExportSettings.backgroundColor[3] = 1.0f;
            MarkDocumentDirty();
        }
    }

    ImGui::Separator();
    bool resolutionChanged = false;
    const bool widthChanged = ImGui::InputInt("Output Width", &m_ExportSettings.outputWidth);
    const bool heightChanged = ImGui::InputInt("Output Height", &m_ExportSettings.outputHeight);
    if (widthChanged) {
        SyncExportResolutionFromWidth();
        resolutionChanged = true;
    } else if (heightChanged) {
        SyncExportResolutionFromHeight();
        resolutionChanged = true;
    }
    if (resolutionChanged) {
        MarkDocumentDirty();
    }

    if (ImGui::Button("Export PNG", ImVec2(-1, 0))) {
        TriggerExportPng();
    }
    ImGui::EndDisabled();

    if (!HasLayers()) {
        ImGui::TextDisabled("Add at least one layer to export the composite.");
    }
}

void CompositeModule::RenderLayerContextMenu(CompositeLayer* targetLayer) {
    if (targetLayer == nullptr) {
        return;
    }

    m_SelectedId = targetLayer->id;

    ImGui::TextDisabled("Layer");
    if (ImGui::MenuItem("Rename")) {
        BeginRenameSelectedLayer();
    }
    if (ImGui::MenuItem("Duplicate")) {
        DuplicateSelectedLayer();
    }
    const bool visible = targetLayer->visible;
    if (ImGui::MenuItem("Visible", nullptr, visible)) {
        targetLayer->visible = !visible;
        MarkDocumentDirty();
    }
    const bool locked = targetLayer->locked;
    if (ImGui::MenuItem("Locked", nullptr, locked)) {
        targetLayer->locked = !locked;
        MarkDocumentDirty();
    }
    ImGui::BeginDisabled(targetLayer->locked);
    if (ImGui::MenuItem("Delete")) {
        RemoveSelectedLayers();
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    if (IsEditorBridgeLayer(*targetLayer)) {
        ImGui::TextDisabled("Replace");
        if (ImGui::MenuItem("Replace With Image")) {
            const std::string path = FileDialogs::OpenImageFileDialog("Replace composite layer with image");
            if (!path.empty()) {
                ReplaceSelectedLayerWithImageFile(path);
            }
        }
        if (ImGui::MenuItem("Replace With Library Project")) {
            m_LibraryPickerMode = LibraryPickerMode::ReplaceSelectedWithProject;
            m_LibraryPickerTargetLayerId = targetLayer->id;
            m_ShowLibraryPicker = true;
        }

        ImGui::Separator();
        ImGui::TextDisabled("Bridge");
        if (ImGui::MenuItem("Open in Editor")) {
            OpenSelectedLayerInEditor();
        }
        if (targetLayer->kind == LayerKind::EditorProject && !targetLayer->linkedProjectFileName.empty()) {
            if (ImGui::MenuItem("Update Linked Project")) {
                UpdateLinkedProjectFromSelectedLayer();
            }
        }
    }
}

void CompositeModule::RenderCanvasContextMenu() {
    if (ImGui::MenuItem("Add Image")) {
        TriggerAddImage();
    }
    if (ImGui::MenuItem("Add Project")) {
        TriggerAddProject();
    }
    if (ImGui::MenuItem("Add From Library")) {
        TriggerAddFromLibrary();
    }
    if (ImGui::MenuItem("Add Square")) {
        AddShapeLayer(LayerKind::ShapeRect);
    }
    if (ImGui::MenuItem("Add Circle")) {
        AddShapeLayer(LayerKind::ShapeCircle);
    }
    if (ImGui::MenuItem("Add Text")) {
        AddTextLayer();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reset Composite Layout")) {
        ResetWorkspaceLayout(true);
    }
}

void CompositeModule::RenderStage() {
    struct SnapGuideLine {
        ImVec2 a;
        ImVec2 b;
        ImU32 color;
    };

    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    m_CanvasW = std::max(1.0f, canvasSize.x);
    m_CanvasH = std::max(1.0f, canvasSize.y);
    const ImVec2 canvasMax(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (m_ShowChecker) {
        const float checkerSize = 16.0f;
        for (float y = canvasPos.y; y < canvasMax.y; y += checkerSize) {
            for (float x = canvasPos.x; x < canvasMax.x; x += checkerSize) {
                const int cellX = static_cast<int>(std::floor((x - canvasPos.x) / checkerSize));
                const int cellY = static_cast<int>(std::floor((y - canvasPos.y) / checkerSize));
                const ImU32 color = ((cellX + cellY) & 1) ? IM_COL32(40, 42, 48, 255) : IM_COL32(28, 30, 34, 255);
                drawList->AddRectFilled(
                    ImVec2(x, y),
                    ImVec2(std::min(x + checkerSize, canvasMax.x), std::min(y + checkerSize, canvasMax.y)),
                    color);
            }
        }
    } else {
        drawList->AddRectFilled(canvasPos, canvasMax, IM_COL32(18, 19, 22, 255));
    }

    SyncLayerTextures();

    const ImVec2 canvasCenter(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
    const int canvasPixelW = std::max(1, static_cast<int>(std::round(canvasSize.x)));
    const int canvasPixelH = std::max(1, static_cast<int>(std::round(canvasSize.y)));
    const bool showExportBoundsOverlay = ShouldShowExportBoundsOverlay();
    std::vector<const CompositeLayer*> visibleLayers;
    visibleLayers.reserve(m_Layers.size());
    for (const CompositeLayer& layer : m_Layers) {
        if (!layer.visible || layer.rgba.empty() || layer.tex == 0) {
            continue;
        }
        visibleLayers.push_back(&layer);
    }

    std::sort(visibleLayers.begin(), visibleLayers.end(), [](const CompositeLayer* a, const CompositeLayer* b) {
        return a->z < b->z;
    });

    RefreshVisibleTextRasterQuality();

    const bool viewChanged =
        m_LastStagePreviewCanvasW != canvasPixelW ||
        m_LastStagePreviewCanvasH != canvasPixelH ||
        std::abs(m_LastStagePreviewZoom - m_ViewZoom) > 0.0001f ||
        std::abs(m_LastStagePreviewPanX - m_ViewPanX) > 0.25f ||
        std::abs(m_LastStagePreviewPanY - m_ViewPanY) > 0.25f;

    if (m_StagePreviewDirty || viewChanged) {
        if (!visibleLayers.empty()) {
            const FloatRect viewRect = ComputeViewWorldRect(canvasSize.x, canvasSize.y, m_ViewZoom, m_ViewPanX, m_ViewPanY);
            RenderStagePreviewTexture(
                visibleLayers,
                viewRect.x,
                viewRect.y,
                viewRect.width,
                viewRect.height,
                canvasPixelW,
                canvasPixelH);
        } else {
            ClearStagePreviewGpu();
        }

        m_StagePreviewDirty = false;
        m_LastStagePreviewCanvasW = canvasPixelW;
        m_LastStagePreviewCanvasH = canvasPixelH;
        m_LastStagePreviewZoom = m_ViewZoom;
        m_LastStagePreviewPanX = m_ViewPanX;
        m_LastStagePreviewPanY = m_ViewPanY;
    }

    if (m_StagePreviewTex[m_StagePreviewDisplayIndex] != 0) {
        drawList->AddImage(
            (ImTextureID)(intptr_t)m_StagePreviewTex[m_StagePreviewDisplayIndex],
            canvasPos,
            canvasMax,
            ImVec2(0, 1),
            ImVec2(1, 0));
    } else {
        drawList->AddText(ImVec2(canvasPos.x + 20.0f, canvasPos.y + 20.0f), IM_COL32(180, 180, 185, 255), "Add layers to start compositing.");
    }

    if (showExportBoundsOverlay) {
        const ImVec2 exportTopLeft = WorldToScreen(
            canvasCenter,
            m_ViewZoom,
            m_ViewPanX,
            m_ViewPanY,
            ImVec2(m_ExportSettings.customX, m_ExportSettings.customY));
        const ImVec2 exportBottomRight = WorldToScreen(
            canvasCenter,
            m_ViewZoom,
            m_ViewPanX,
            m_ViewPanY,
            ImVec2(
                m_ExportSettings.customX + m_ExportSettings.customWidth,
                m_ExportSettings.customY + m_ExportSettings.customHeight));
        drawList->AddRect(exportTopLeft, exportBottomRight, IM_COL32(90, 190, 255, 255), 0.0f, 0, 2.0f);

        const bool lockedExportAspect = m_ExportSettings.aspectPreset != CompositeExportAspectPreset::Custom;
        const ImVec2 topCenter((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportTopLeft.y);
        const ImVec2 bottomCenter((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportBottomRight.y);
        const ImVec2 leftCenter(exportTopLeft.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
        const ImVec2 rightCenter(exportBottomRight.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
        std::array<ImVec2, 8> handlePoints = {
            exportTopLeft,
            topCenter,
            ImVec2(exportBottomRight.x, exportTopLeft.y),
            rightCenter,
            exportBottomRight,
            bottomCenter,
            ImVec2(exportTopLeft.x, exportBottomRight.y),
            leftCenter
        };

        const int handleCount = lockedExportAspect ? 4 : 8;
        const int handleIndices[] = { 0, 2, 4, 6, 1, 3, 5, 7 };
        for (int handleIndex = 0; handleIndex < handleCount; ++handleIndex) {
            const ImVec2& handlePoint = handlePoints[handleIndices[handleIndex]];
            drawList->AddRectFilled(
                ImVec2(handlePoint.x - kExportBoundsHandleScreenRadius, handlePoint.y - kExportBoundsHandleScreenRadius),
                ImVec2(handlePoint.x + kExportBoundsHandleScreenRadius, handlePoint.y + kExportBoundsHandleScreenRadius),
                IM_COL32(90, 190, 255, 255),
                2.0f);
            drawList->AddRect(
                ImVec2(handlePoint.x - kExportBoundsHandleScreenRadius, handlePoint.y - kExportBoundsHandleScreenRadius),
                ImVec2(handlePoint.x + kExportBoundsHandleScreenRadius, handlePoint.y + kExportBoundsHandleScreenRadius),
                IM_COL32(12, 16, 20, 255),
                2.0f);
        }
    }

    CompositeLayer* selectedLayer = GetSelectedLayer();
    std::array<ImVec2, 4> selectedWorldQuad {};
    std::array<ImVec2, 4> selectedScreenQuad {};
    ImVec2 selectedScreenTopCenter {};
    ImVec2 selectedScreenBottomCenter {};
    ImVec2 selectedScreenLeftCenter {};
    ImVec2 selectedScreenRightCenter {};
    ImVec2 selectedRotationHandle {};
    if (selectedLayer) {
        selectedWorldQuad = ComputeLayerQuadWorld(*selectedLayer);
        for (int index = 0; index < 4; ++index) {
            selectedScreenQuad[index] = WorldToScreen(canvasCenter, m_ViewZoom, m_ViewPanX, m_ViewPanY, selectedWorldQuad[index]);
        }

        selectedScreenTopCenter = ImVec2(
            (selectedScreenQuad[0].x + selectedScreenQuad[1].x) * 0.5f,
            (selectedScreenQuad[0].y + selectedScreenQuad[1].y) * 0.5f);
        selectedScreenBottomCenter = ImVec2(
            (selectedScreenQuad[2].x + selectedScreenQuad[3].x) * 0.5f,
            (selectedScreenQuad[2].y + selectedScreenQuad[3].y) * 0.5f);
        selectedScreenLeftCenter = ImVec2(
            (selectedScreenQuad[0].x + selectedScreenQuad[3].x) * 0.5f,
            (selectedScreenQuad[0].y + selectedScreenQuad[3].y) * 0.5f);
        selectedScreenRightCenter = ImVec2(
            (selectedScreenQuad[1].x + selectedScreenQuad[2].x) * 0.5f,
            (selectedScreenQuad[1].y + selectedScreenQuad[2].y) * 0.5f);
        const float upX = -std::sin(selectedLayer->rotation);
        const float upY = -std::cos(selectedLayer->rotation);
        selectedRotationHandle = ImVec2(selectedScreenTopCenter.x + upX * 26.0f, selectedScreenTopCenter.y + upY * 26.0f);

        const ImU32 outlineColor = selectedLayer->locked ? IM_COL32(180, 180, 190, 255) : IM_COL32(255, 170, 0, 255);
        drawList->AddPolyline(selectedScreenQuad.data(), 4, outlineColor, true, 2.0f);
        if (!selectedLayer->locked) {
            for (const ImVec2& point : selectedScreenQuad) {
                drawList->AddCircleFilled(point, kLayerHandleScreenRadius, IM_COL32(255, 255, 255, 255));
                drawList->AddCircle(point, kLayerHandleScreenRadius, IM_COL32(0, 0, 0, 255));
            }
            if (!selectedLayer->preserveAspectRatio) {
                const ImVec2 edgeHandles[] = {
                    selectedScreenTopCenter,
                    selectedScreenRightCenter,
                    selectedScreenBottomCenter,
                    selectedScreenLeftCenter
                };
                for (const ImVec2& point : edgeHandles) {
                    drawList->AddRectFilled(
                        ImVec2(point.x - kLayerHandleScreenRadius, point.y - kLayerHandleScreenRadius),
                        ImVec2(point.x + kLayerHandleScreenRadius, point.y + kLayerHandleScreenRadius),
                        IM_COL32(255, 255, 255, 255),
                        2.0f);
                    drawList->AddRect(
                        ImVec2(point.x - kLayerHandleScreenRadius, point.y - kLayerHandleScreenRadius),
                        ImVec2(point.x + kLayerHandleScreenRadius, point.y + kLayerHandleScreenRadius),
                        IM_COL32(0, 0, 0, 255),
                        2.0f);
                }
            }
            drawList->AddLine(selectedScreenTopCenter, selectedRotationHandle, outlineColor, 2.0f);
            drawList->AddCircleFilled(selectedRotationHandle, kLayerHandleScreenRadius + 1.0f, IM_COL32(240, 190, 110, 255));
            drawList->AddCircle(selectedRotationHandle, kLayerHandleScreenRadius + 1.0f, IM_COL32(0, 0, 0, 255));
        }
    }

    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("composite_stage_interact", canvasSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 mousePos = ImGui::GetMousePos();
    const ImVec2 mouseWorld = ScreenToWorld(canvasCenter, m_ViewZoom, m_ViewPanX, m_ViewPanY, mousePos);
    const bool middleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    std::vector<SnapGuideLine> snapGuides;

    auto worldToScreen = [&](const ImVec2& worldPoint) {
        return WorldToScreen(canvasCenter, m_ViewZoom, m_ViewPanX, m_ViewPanY, worldPoint);
    };

    auto addGuideWorld = [&](const ImVec2& a, const ImVec2& b, const ImU32 color) {
        snapGuides.push_back({ a, b, color });
    };

    auto beginHandleInteraction = [&](CompositeLayer& layer, const HandleType handleType) {
        m_ActiveHandle = handleType;
        m_StartScaleX = layer.scaleX;
        m_StartScaleY = layer.scaleY;
        m_StartRotation = layer.rotation;
        m_StartX = layer.x;
        m_StartY = layer.y;
        m_StartWidth = LayerWorldWidth(layer);
        m_StartHeight = LayerWorldHeight(layer);
        const ImVec2 center = LayerCenterWorld(layer);
        const ImVec2 axisX = LayerAxisX(layer);
        const ImVec2 axisY = LayerAxisY(layer);
        const float halfW = m_StartWidth * 0.5f;
        const float halfH = m_StartHeight * 0.5f;

        switch (handleType) {
        case HandleType::ResizeTopLeft:
            m_ResizeAnchorX = center.x + axisX.x * halfW + axisY.x * halfH;
            m_ResizeAnchorY = center.y + axisX.y * halfW + axisY.y * halfH;
            break;
        case HandleType::ResizeTopRight:
            m_ResizeAnchorX = center.x - axisX.x * halfW + axisY.x * halfH;
            m_ResizeAnchorY = center.y - axisX.y * halfW + axisY.y * halfH;
            break;
        case HandleType::ResizeBottomRight:
            m_ResizeAnchorX = center.x - axisX.x * halfW - axisY.x * halfH;
            m_ResizeAnchorY = center.y - axisX.y * halfW - axisY.y * halfH;
            break;
        case HandleType::ResizeBottomLeft:
            m_ResizeAnchorX = center.x + axisX.x * halfW - axisY.x * halfH;
            m_ResizeAnchorY = center.y + axisX.y * halfW - axisY.y * halfH;
            break;
        case HandleType::ResizeLeft:
            m_ResizeAnchorX = center.x + axisX.x * halfW;
            m_ResizeAnchorY = center.y + axisX.y * halfW;
            break;
        case HandleType::ResizeRight:
            m_ResizeAnchorX = center.x - axisX.x * halfW;
            m_ResizeAnchorY = center.y - axisX.y * halfW;
            break;
        case HandleType::ResizeTop:
            m_ResizeAnchorX = center.x + axisY.x * halfH;
            m_ResizeAnchorY = center.y + axisY.y * halfH;
            break;
        case HandleType::ResizeBottom:
            m_ResizeAnchorX = center.x - axisY.x * halfH;
            m_ResizeAnchorY = center.y - axisY.y * halfH;
            break;
        case HandleType::Rotate: {
            const ImVec2 screenCenter = worldToScreen(center);
            m_StartMouseAngle = std::atan2(mousePos.y - screenCenter.y, mousePos.x - screenCenter.x) - layer.rotation;
            break;
        }
        case HandleType::Move:
        case HandleType::None:
        default:
            break;
        }
    };

    auto addSpacingGuidesForMove = [&](const FloatRect& activeBounds, float& targetX, float& targetY) {
        if (!m_SnapToSpacing) {
            return;
        }

        FloatRect movedBounds = activeBounds;
        movedBounds.x = targetX;
        movedBounds.y = targetY;
        const float threshold = kSnapThresholdScreenPixels / std::max(0.001f, m_ViewZoom);

        for (const CompositeLayer& first : m_Layers) {
            if (!first.visible || first.id == m_SelectedId) {
                continue;
            }
            const FloatRect a = ComputeLayerBoundsWorld(first);
            for (const CompositeLayer& second : m_Layers) {
                if (!second.visible || second.id == m_SelectedId || second.id == first.id) {
                    continue;
                }
                const FloatRect b = ComputeLayerBoundsWorld(second);

                if (a.x + a.width <= movedBounds.x && movedBounds.x + movedBounds.width <= b.x) {
                    const float desiredX = (a.x + a.width + b.x - movedBounds.width) * 0.5f;
                    if (std::abs(desiredX - targetX) <= threshold) {
                        targetX = desiredX;
                        const float guideY = std::max(a.y, std::max(b.y, movedBounds.y));
                        addGuideWorld(ImVec2(a.x + a.width, guideY), ImVec2(desiredX, guideY), IM_COL32(160, 110, 255, 255));
                        addGuideWorld(ImVec2(desiredX + movedBounds.width, guideY), ImVec2(b.x, guideY), IM_COL32(160, 110, 255, 255));
                    }
                }

                if (a.y + a.height <= movedBounds.y && movedBounds.y + movedBounds.height <= b.y) {
                    const float desiredY = (a.y + a.height + b.y - movedBounds.height) * 0.5f;
                    if (std::abs(desiredY - targetY) <= threshold) {
                        targetY = desiredY;
                        const float guideX = std::max(a.x, std::max(b.x, movedBounds.x));
                        addGuideWorld(ImVec2(guideX, a.y + a.height), ImVec2(guideX, desiredY), IM_COL32(160, 110, 255, 255));
                        addGuideWorld(ImVec2(guideX, desiredY + movedBounds.height), ImVec2(guideX, b.y), IM_COL32(160, 110, 255, 255));
                    }
                }
            }
        }
    };

    auto applyMoveSnapping = [&](const CompositeLayer& referenceLayer, float& targetX, float& targetY) {
        if (!m_SnapEnabled) {
            return;
        }

        if (m_GridSize > 0.0f) {
            targetX = std::round(targetX / m_GridSize) * m_GridSize;
            targetY = std::round(targetY / m_GridSize) * m_GridSize;
        }

        CompositeLayer temp = referenceLayer;
        temp.x = targetX;
        temp.y = targetY;
        const FloatRect activeBounds = ComputeLayerBoundsWorld(temp);
        const float activeLeft = activeBounds.x;
        const float activeCenterX = activeBounds.x + activeBounds.width * 0.5f;
        const float activeRight = activeBounds.x + activeBounds.width;
        const float activeTop = activeBounds.y;
        const float activeCenterY = activeBounds.y + activeBounds.height * 0.5f;
        const float activeBottom = activeBounds.y + activeBounds.height;
        const float threshold = kSnapThresholdScreenPixels / std::max(0.001f, m_ViewZoom);

        float bestDeltaX = std::numeric_limits<float>::max();
        float bestDeltaY = std::numeric_limits<float>::max();
        bool snappedX = false;
        bool snappedY = false;
        ImVec2 bestXGuideA {};
        ImVec2 bestXGuideB {};
        ImVec2 bestYGuideA {};
        ImVec2 bestYGuideB {};

        auto considerX = [&](const float delta, const float guideX, const float y1, const float y2) {
            if (std::abs(delta) <= threshold && (!snappedX || std::abs(delta) < std::abs(bestDeltaX))) {
                snappedX = true;
                bestDeltaX = delta;
                bestXGuideA = ImVec2(guideX, y1);
                bestXGuideB = ImVec2(guideX, y2);
            }
        };
        auto considerY = [&](const float delta, const float guideY, const float x1, const float x2) {
            if (std::abs(delta) <= threshold && (!snappedY || std::abs(delta) < std::abs(bestDeltaY))) {
                snappedY = true;
                bestDeltaY = delta;
                bestYGuideA = ImVec2(x1, guideY);
                bestYGuideB = ImVec2(x2, guideY);
            }
        };

        for (const CompositeLayer& otherLayer : m_Layers) {
            if (!otherLayer.visible || otherLayer.id == referenceLayer.id) {
                continue;
            }

            const FloatRect otherBounds = ComputeLayerBoundsWorld(otherLayer);
            const float otherLeft = otherBounds.x;
            const float otherCenterX = otherBounds.x + otherBounds.width * 0.5f;
            const float otherRight = otherBounds.x + otherBounds.width;
            const float otherTop = otherBounds.y;
            const float otherCenterY = otherBounds.y + otherBounds.height * 0.5f;
            const float otherBottom = otherBounds.y + otherBounds.height;

            if (m_SnapToObjects) {
                considerX(otherLeft - activeLeft, otherLeft, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                considerX(otherRight - activeRight, otherRight, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                considerY(otherTop - activeTop, otherTop, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
                considerY(otherBottom - activeBottom, otherBottom, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
            }
            if (m_SnapToCenters) {
                considerX(otherCenterX - activeCenterX, otherCenterX, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                considerY(otherCenterY - activeCenterY, otherCenterY, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
            }
        }

        if (m_SnapToCanvasCenter) {
            considerX(-activeCenterX, 0.0f, activeBounds.y, activeBottom);
            considerY(-activeCenterY, 0.0f, activeBounds.x, activeRight);
        }

        if (snappedX) {
            targetX += bestDeltaX;
            addGuideWorld(bestXGuideA, bestXGuideB, IM_COL32(80, 200, 255, 255));
        }
        if (snappedY) {
            targetY += bestDeltaY;
            addGuideWorld(bestYGuideA, bestYGuideB, IM_COL32(80, 200, 255, 255));
        }

        addSpacingGuidesForMove(activeBounds, targetX, targetY);
    };

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        m_MiddleMousePanActive = true;
        m_ActiveHandle = HandleType::None;
        m_ActiveExportHandle = ExportHandleType::None;
    }
    if (m_MiddleMousePanActive && middleMouseDown) {
        m_ViewPanX += ImGui::GetIO().MouseDelta.x;
        m_ViewPanY += ImGui::GetIO().MouseDelta.y;
        MarkStageDirty();
    }
    if (m_MiddleMousePanActive && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
        m_MiddleMousePanActive = false;
    }

    if (!m_MiddleMousePanActive && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_RightMousePressedOnCanvas = true;
        m_RightMousePressX = mousePos.x;
        m_RightMousePressY = mousePos.y;
    }

    if (!m_MiddleMousePanActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered) {
        m_ActiveHandle = HandleType::None;
        m_ActiveExportHandle = ExportHandleType::None;

        if (showExportBoundsOverlay) {
            const ImVec2 exportTopLeft = WorldToScreen(
                canvasCenter,
                m_ViewZoom,
                m_ViewPanX,
                m_ViewPanY,
                ImVec2(m_ExportSettings.customX, m_ExportSettings.customY));
            const ImVec2 exportBottomRight = WorldToScreen(
                canvasCenter,
                m_ViewZoom,
                m_ViewPanX,
                m_ViewPanY,
                ImVec2(
                    m_ExportSettings.customX + m_ExportSettings.customWidth,
                    m_ExportSettings.customY + m_ExportSettings.customHeight));
            const ImVec2 topRight(exportBottomRight.x, exportTopLeft.y);
            const ImVec2 bottomLeft(exportTopLeft.x, exportBottomRight.y);
            const ImVec2 topCenter((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportTopLeft.y);
            const ImVec2 bottomCenter((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportBottomRight.y);
            const ImVec2 leftCenter(exportTopLeft.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
            const ImVec2 rightCenter(exportBottomRight.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
            const bool lockedExportAspect = m_ExportSettings.aspectPreset != CompositeExportAspectPreset::Custom;

            auto distanceTo = [&](const ImVec2& point) {
                return std::hypot(mousePos.x - point.x, mousePos.y - point.y);
            };

            const float threshold = 12.0f;
            const float minX = std::min(exportTopLeft.x, exportBottomRight.x);
            const float maxX = std::max(exportTopLeft.x, exportBottomRight.x);
            const float minY = std::min(exportTopLeft.y, exportBottomRight.y);
            const float maxY = std::max(exportTopLeft.y, exportBottomRight.y);
            const bool insideExportRect =
                mousePos.x >= minX &&
                mousePos.x <= maxX &&
                mousePos.y >= minY &&
                mousePos.y <= maxY;

            if (distanceTo(exportTopLeft) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::TopLeft;
            } else if (distanceTo(topRight) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::TopRight;
            } else if (distanceTo(exportBottomRight) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::BottomRight;
            } else if (distanceTo(bottomLeft) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::BottomLeft;
            } else if (!lockedExportAspect && distanceTo(topCenter) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::Top;
            } else if (!lockedExportAspect && distanceTo(bottomCenter) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::Bottom;
            } else if (!lockedExportAspect && distanceTo(leftCenter) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::Left;
            } else if (!lockedExportAspect && distanceTo(rightCenter) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::Right;
            } else if (insideExportRect) {
                m_ActiveExportHandle = ExportHandleType::Move;
            }

            if (m_ActiveExportHandle != ExportHandleType::None) {
                m_ExportDragStartX = m_ExportSettings.customX;
                m_ExportDragStartY = m_ExportSettings.customY;
                m_ExportDragStartWidth = m_ExportSettings.customWidth;
                m_ExportDragStartHeight = m_ExportSettings.customHeight;
                m_ExportDragStartMouseWorldX = mouseWorld.x;
                m_ExportDragStartMouseWorldY = mouseWorld.y;
            }
        }

        selectedLayer = GetSelectedLayer();
        if (m_ActiveExportHandle == ExportHandleType::None && selectedLayer && !selectedLayer->locked) {
            auto distanceTo = [&](const ImVec2& point) {
                return std::hypot(mousePos.x - point.x, mousePos.y - point.y);
            };

            const float threshold = 12.0f;
            if (distanceTo(selectedRotationHandle) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::Rotate);
            } else if (distanceTo(selectedScreenQuad[0]) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeTopLeft);
            } else if (distanceTo(selectedScreenQuad[1]) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeTopRight);
            } else if (distanceTo(selectedScreenQuad[2]) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeBottomRight);
            } else if (distanceTo(selectedScreenQuad[3]) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeBottomLeft);
            } else if (!selectedLayer->preserveAspectRatio && distanceTo(selectedScreenTopCenter) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeTop);
            } else if (!selectedLayer->preserveAspectRatio && distanceTo(selectedScreenRightCenter) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeRight);
            } else if (!selectedLayer->preserveAspectRatio && distanceTo(selectedScreenBottomCenter) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeBottom);
            } else if (!selectedLayer->preserveAspectRatio && distanceTo(selectedScreenLeftCenter) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeLeft);
            } else if (MapWorldToLayerUv(*selectedLayer, mouseWorld.x, mouseWorld.y, m_StartMouseAngle, m_StartMouseDist)) {
                beginHandleInteraction(*selectedLayer, HandleType::Move);
            }
        }

        if (m_ActiveExportHandle == ExportHandleType::None && m_ActiveHandle == HandleType::None) {
            CompositeLayer* hitLayer = FindTopMostVisibleLayerAtWorldPoint(m_Layers, mouseWorld.x, mouseWorld.y);
            if (hitLayer != nullptr) {
                m_SelectedId = hitLayer->id;
                if (!hitLayer->locked) {
                    beginHandleInteraction(*hitLayer, HandleType::Move);
                }
            } else {
                m_SelectedId.clear();
            }
        }
    }

    selectedLayer = GetSelectedLayer();
    if (!m_MiddleMousePanActive && m_ActiveExportHandle != ExportHandleType::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float deltaX = mouseWorld.x - m_ExportDragStartMouseWorldX;
        const float deltaY = mouseWorld.y - m_ExportDragStartMouseWorldY;
        const bool lockedAspect = m_ExportSettings.aspectPreset != CompositeExportAspectPreset::Custom;
        const float ratio = ExportAspectRatioValue(m_ExportSettings);

        FloatRect newBounds {
            m_ExportDragStartX,
            m_ExportDragStartY,
            m_ExportDragStartWidth,
            m_ExportDragStartHeight
        };

        auto applyCornerWithAspect = [&](const float anchorX, const float anchorY, const bool dragLeft, const bool dragTop) {
            const float widthByX = std::max(1.0f, std::abs(mouseWorld.x - anchorX));
            const float heightByX = std::max(1.0f, widthByX / std::max(0.0001f, ratio));
            const float heightByY = std::max(1.0f, std::abs(mouseWorld.y - anchorY));
            const float widthByY = std::max(1.0f, heightByY * std::max(0.0001f, ratio));

            const float xCornerByX = dragLeft ? (anchorX - widthByX) : (anchorX + widthByX);
            const float yCornerByX = dragTop ? (anchorY - heightByX) : (anchorY + heightByX);
            const float xCornerByY = dragLeft ? (anchorX - widthByY) : (anchorX + widthByY);
            const float yCornerByY = dragTop ? (anchorY - heightByY) : (anchorY + heightByY);

            const float errorByX =
                std::pow(mouseWorld.x - xCornerByX, 2.0f) +
                std::pow(mouseWorld.y - yCornerByX, 2.0f);
            const float errorByY =
                std::pow(mouseWorld.x - xCornerByY, 2.0f) +
                std::pow(mouseWorld.y - yCornerByY, 2.0f);

            const float chosenWidth = (errorByX <= errorByY) ? widthByX : widthByY;
            const float chosenHeight = std::max(1.0f, chosenWidth / std::max(0.0001f, ratio));
            const float left = dragLeft ? (anchorX - chosenWidth) : anchorX;
            const float top = dragTop ? (anchorY - chosenHeight) : anchorY;
            newBounds = { left, top, chosenWidth, chosenHeight };
        };

        if (m_ActiveExportHandle == ExportHandleType::Move) {
            newBounds.x = m_ExportDragStartX + deltaX;
            newBounds.y = m_ExportDragStartY + deltaY;
        } else if (!lockedAspect) {
            float x1 = m_ExportDragStartX;
            float y1 = m_ExportDragStartY;
            float x2 = m_ExportDragStartX + m_ExportDragStartWidth;
            float y2 = m_ExportDragStartY + m_ExportDragStartHeight;

            if (m_ActiveExportHandle == ExportHandleType::Left ||
                m_ActiveExportHandle == ExportHandleType::TopLeft ||
                m_ActiveExportHandle == ExportHandleType::BottomLeft) {
                x1 += deltaX;
            }
            if (m_ActiveExportHandle == ExportHandleType::Right ||
                m_ActiveExportHandle == ExportHandleType::TopRight ||
                m_ActiveExportHandle == ExportHandleType::BottomRight) {
                x2 += deltaX;
            }
            if (m_ActiveExportHandle == ExportHandleType::Top ||
                m_ActiveExportHandle == ExportHandleType::TopLeft ||
                m_ActiveExportHandle == ExportHandleType::TopRight) {
                y1 += deltaY;
            }
            if (m_ActiveExportHandle == ExportHandleType::Bottom ||
                m_ActiveExportHandle == ExportHandleType::BottomLeft ||
                m_ActiveExportHandle == ExportHandleType::BottomRight) {
                y2 += deltaY;
            }

            newBounds = MakeNormalizedRect(x1, y1, x2, y2);
        } else {
            const float startRight = m_ExportDragStartX + m_ExportDragStartWidth;
            const float startBottom = m_ExportDragStartY + m_ExportDragStartHeight;
            switch (m_ActiveExportHandle) {
            case ExportHandleType::TopLeft:
                applyCornerWithAspect(startRight, startBottom, true, true);
                break;
            case ExportHandleType::TopRight:
                applyCornerWithAspect(m_ExportDragStartX, startBottom, false, true);
                break;
            case ExportHandleType::BottomRight:
                applyCornerWithAspect(m_ExportDragStartX, m_ExportDragStartY, false, false);
                break;
            case ExportHandleType::BottomLeft:
                applyCornerWithAspect(startRight, m_ExportDragStartY, true, false);
                break;
            default:
                break;
            }
        }

        const bool changed =
            std::abs(newBounds.x - m_ExportSettings.customX) > 0.0001f ||
            std::abs(newBounds.y - m_ExportSettings.customY) > 0.0001f ||
            std::abs(newBounds.width - m_ExportSettings.customWidth) > 0.0001f ||
            std::abs(newBounds.height - m_ExportSettings.customHeight) > 0.0001f;
        if (changed) {
            m_ExportSettings.customX = newBounds.x;
            m_ExportSettings.customY = newBounds.y;
            m_ExportSettings.customWidth = std::max(1.0f, newBounds.width);
            m_ExportSettings.customHeight = std::max(1.0f, newBounds.height);
            if (m_ExportSettings.aspectPreset == CompositeExportAspectPreset::Custom) {
                UpdateCustomExportAspectFromBounds();
            }
            MarkDocumentDirty();
        }
    } else if (!m_MiddleMousePanActive && active && m_ActiveHandle != HandleType::None && selectedLayer && !selectedLayer->locked) {
        const float baseWidth = std::max(1.0f, LayerBaseWidth(*selectedLayer));
        const float baseHeight = std::max(1.0f, LayerBaseHeight(*selectedLayer));
        const ImVec2 center = LayerCenterWorld(*selectedLayer);
        const ImVec2 screenCenter = worldToScreen(center);
        bool changed = false;

        if (m_ActiveHandle == HandleType::Move) {
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            float targetX = m_StartX + delta.x / std::max(0.001f, m_ViewZoom);
            float targetY = m_StartY + delta.y / std::max(0.001f, m_ViewZoom);
            applyMoveSnapping(*selectedLayer, targetX, targetY);
            if (std::abs(targetX - selectedLayer->x) > 0.0001f || std::abs(targetY - selectedLayer->y) > 0.0001f) {
                selectedLayer->x = targetX;
                selectedLayer->y = targetY;
                changed = true;
            }
        } else if (m_ActiveHandle == HandleType::Rotate) {
            float targetRotation = std::atan2(mousePos.y - screenCenter.y, mousePos.x - screenCenter.x) - m_StartMouseAngle;
            if (m_SnapEnabled && m_RotateSnapStep > 0.0f) {
                float degrees = RadiansToDegrees(targetRotation);
                degrees = std::round(degrees / m_RotateSnapStep) * m_RotateSnapStep;
                targetRotation = DegreesToRadians(degrees);
            }
            if (std::abs(targetRotation - selectedLayer->rotation) > 0.0001f) {
                selectedLayer->rotation = targetRotation;
                changed = true;
            }
        } else {
            const ImVec2 anchor(m_ResizeAnchorX, m_ResizeAnchorY);
            const ImVec2 axisX(std::cos(m_StartRotation), std::sin(m_StartRotation));
            const ImVec2 axisY(-std::sin(m_StartRotation), std::cos(m_StartRotation));
            const ImVec2 delta(mouseWorld.x - anchor.x, mouseWorld.y - anchor.y);
            const float alongX = delta.x * axisX.x + delta.y * axisX.y;
            const float alongY = delta.x * axisY.x + delta.y * axisY.y;
            const bool preserveAspect = selectedLayer->preserveAspectRatio;
            const float aspectRatio = std::max(0.0001f, m_StartWidth / std::max(1.0f, m_StartHeight));

            float newWidth = m_StartWidth;
            float newHeight = m_StartHeight;
            float signX = 0.0f;
            float signY = 0.0f;

            if (preserveAspect) {
                auto applyCornerWithAspect = [&](const bool dragLeft, const bool dragTop) {
                    signX = dragLeft ? -1.0f : 1.0f;
                    signY = dragTop ? -1.0f : 1.0f;

                    const float widthByX = std::max(1.0f, std::abs(alongX));
                    const float heightByX = std::max(1.0f, widthByX / aspectRatio);
                    const float heightByY = std::max(1.0f, std::abs(alongY));
                    const float widthByY = std::max(1.0f, heightByY * aspectRatio);

                    const float errorByX = std::abs(std::abs(alongY) - heightByX);
                    const float errorByY = std::abs(std::abs(alongX) - widthByY);
                    if (errorByX <= errorByY) {
                        newWidth = widthByX;
                        newHeight = heightByX;
                    } else {
                        newWidth = widthByY;
                        newHeight = heightByY;
                    }
                };

                switch (m_ActiveHandle) {
                case HandleType::ResizeTopLeft:
                    applyCornerWithAspect(true, true);
                    break;
                case HandleType::ResizeTopRight:
                    applyCornerWithAspect(false, true);
                    break;
                case HandleType::ResizeBottomRight:
                    applyCornerWithAspect(false, false);
                    break;
                case HandleType::ResizeBottomLeft:
                    applyCornerWithAspect(true, false);
                    break;
                case HandleType::ResizeLeft:
                    signX = -1.0f;
                    signY = 0.0f;
                    newWidth = std::max(1.0f, -alongX);
                    newHeight = std::max(1.0f, newWidth / aspectRatio);
                    break;
                case HandleType::ResizeRight:
                    signX = 1.0f;
                    signY = 0.0f;
                    newWidth = std::max(1.0f, alongX);
                    newHeight = std::max(1.0f, newWidth / aspectRatio);
                    break;
                case HandleType::ResizeTop:
                    signX = 0.0f;
                    signY = -1.0f;
                    newHeight = std::max(1.0f, -alongY);
                    newWidth = std::max(1.0f, newHeight * aspectRatio);
                    break;
                case HandleType::ResizeBottom:
                    signX = 0.0f;
                    signY = 1.0f;
                    newHeight = std::max(1.0f, alongY);
                    newWidth = std::max(1.0f, newHeight * aspectRatio);
                    break;
                default:
                    break;
                }
            } else {
                switch (m_ActiveHandle) {
                case HandleType::ResizeTopLeft:
                    signX = -1.0f;
                    signY = -1.0f;
                    newWidth = std::max(1.0f, -alongX);
                    newHeight = std::max(1.0f, -alongY);
                    break;
                case HandleType::ResizeTopRight:
                    signX = 1.0f;
                    signY = -1.0f;
                    newWidth = std::max(1.0f, alongX);
                    newHeight = std::max(1.0f, -alongY);
                    break;
                case HandleType::ResizeBottomRight:
                    signX = 1.0f;
                    signY = 1.0f;
                    newWidth = std::max(1.0f, alongX);
                    newHeight = std::max(1.0f, alongY);
                    break;
                case HandleType::ResizeBottomLeft:
                    signX = -1.0f;
                    signY = 1.0f;
                    newWidth = std::max(1.0f, -alongX);
                    newHeight = std::max(1.0f, alongY);
                    break;
                case HandleType::ResizeLeft:
                    signX = -1.0f;
                    newWidth = std::max(1.0f, -alongX);
                    break;
                case HandleType::ResizeRight:
                    signX = 1.0f;
                    newWidth = std::max(1.0f, alongX);
                    break;
                case HandleType::ResizeTop:
                    signY = -1.0f;
                    newHeight = std::max(1.0f, -alongY);
                    break;
                case HandleType::ResizeBottom:
                    signY = 1.0f;
                    newHeight = std::max(1.0f, alongY);
                    break;
                default:
                    break;
                }
            }

            if (m_SnapEnabled && m_ScaleSnapStep > 0.0f) {
                newWidth = std::max(1.0f, std::round((newWidth / baseWidth) / m_ScaleSnapStep) * m_ScaleSnapStep * baseWidth);
                newHeight = std::max(1.0f, std::round((newHeight / baseHeight) / m_ScaleSnapStep) * m_ScaleSnapStep * baseHeight);
            }

            const ImVec2 newCenter(
                anchor.x + axisX.x * signX * newWidth * 0.5f + axisY.x * signY * newHeight * 0.5f,
                anchor.y + axisX.y * signX * newWidth * 0.5f + axisY.y * signY * newHeight * 0.5f);
            const float targetScaleX = std::max(0.01f, newWidth / baseWidth);
            const float targetScaleY = std::max(0.01f, newHeight / baseHeight);
            const float targetX = newCenter.x - newWidth * 0.5f;
            const float targetY = newCenter.y - newHeight * 0.5f;

            if (std::abs(targetScaleX - selectedLayer->scaleX) > 0.0001f ||
                std::abs(targetScaleY - selectedLayer->scaleY) > 0.0001f ||
                std::abs(targetX - selectedLayer->x) > 0.0001f ||
                std::abs(targetY - selectedLayer->y) > 0.0001f) {
                selectedLayer->scaleX = targetScaleX;
                selectedLayer->scaleY = targetScaleY;
                selectedLayer->x = targetX;
                selectedLayer->y = targetY;
                if (selectedLayer->kind == LayerKind::Text) {
                    RegenerateGeneratedLayerTexture(*selectedLayer);
                }
                changed = true;
            }
        }

        if (changed) {
            MarkDocumentDirty();
        }
    }

    for (const SnapGuideLine& guide : snapGuides) {
        drawList->AddLine(worldToScreen(guide.a), worldToScreen(guide.b), guide.color, 1.5f);
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_ActiveHandle = HandleType::None;
        m_ActiveExportHandle = ExportHandleType::None;
    }

    if (!m_MiddleMousePanActive && hovered && m_RightMousePressedOnCanvas && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const float dragDistance = std::hypot(mousePos.x - m_RightMousePressX, mousePos.y - m_RightMousePressY);
        if (dragDistance > kCanvasContextDragThreshold) {
            m_ViewPanX += ImGui::GetIO().MouseDelta.x;
            m_ViewPanY += ImGui::GetIO().MouseDelta.y;
            MarkStageDirty();
        }
    }

    if (!m_MiddleMousePanActive && m_RightMousePressedOnCanvas && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const float dragDistance = std::hypot(mousePos.x - m_RightMousePressX, mousePos.y - m_RightMousePressY);
        if (hovered && dragDistance <= kCanvasContextDragThreshold) {
            CompositeLayer* hitLayer = FindTopMostVisibleLayerAtWorldPoint(m_Layers, mouseWorld.x, mouseWorld.y);
            CompositeLayer* currentSelected = GetSelectedLayer();
            if (currentSelected != nullptr && hitLayer != nullptr && hitLayer->id == currentSelected->id) {
                ImGui::OpenPopup("CompositeCanvasLayerContext");
            } else {
                ImGui::OpenPopup("CompositeCanvasContext");
            }
        }
        m_RightMousePressedOnCanvas = false;
    } else if (m_MiddleMousePanActive) {
        m_RightMousePressedOnCanvas = false;
    }

    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        m_ViewZoom = std::clamp(m_ViewZoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), 0.05f, 32.0f);
        MarkStageDirty();
    }

    selectedLayer = GetSelectedLayer();
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedLayer && !selectedLayer->locked) {
        RemoveSelectedLayers();
    }

    if (ImGui::BeginPopup("CompositeCanvasLayerContext")) {
        RenderLayerContextMenu(GetSelectedLayer());
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("CompositeCanvasContext")) {
        RenderCanvasContextMenu();
        ImGui::EndPopup();
    }
}

void CompositeModule::RenderSavePopup() {
    if (m_OpenSavePopup) {
        ImGui::OpenPopup("SaveCompositeToLibrary");
        m_OpenSavePopup = false;
    }

    if (ImGui::BeginPopupModal("SaveCompositeToLibrary", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char nameBuffer[256] = "Composite Project";
        if (ImGui::IsWindowAppearing()) {
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", m_ProjectName.c_str());
        }

        ImGui::InputText("Project name", nameBuffer, sizeof(nameBuffer));
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            LibraryManager::Get().RequestSaveCompositeProject(nameBuffer, this, "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void CompositeModule::RenderRenamePopup() {
    if (m_OpenRenamePopup) {
        ImGui::OpenPopup("RenameCompositeLayer");
        m_OpenRenamePopup = false;
    }

    if (ImGui::BeginPopupModal("RenameCompositeLayer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Layer name", m_RenameBuffer, sizeof(m_RenameBuffer));
        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            CompositeLayer* layer = FindLayerById(m_Layers, m_RenameLayerId);
            if (layer) {
                const std::string trimmedName = TrimWhitespace(m_RenameBuffer);
                if (!trimmedName.empty()) {
                    layer->name = trimmedName;
                    MarkDocumentDirty();
                }
            }
            m_RenameLayerId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_RenameLayerId.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void CompositeModule::RenderLibraryPicker() {
    if (!m_ShowLibraryPicker) {
        return;
    }

    const char* windowTitle = (m_LibraryPickerMode == LibraryPickerMode::ReplaceSelectedWithProject)
        ? "Replace Layer With Library Project"
        : "Select Library Project";

    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(windowTitle, &m_ShowLibraryPicker, ImGuiWindowFlags_NoCollapse)) {
        const auto& projects = LibraryManager::Get().GetProjects();
        if (projects.empty()) {
            ImGui::TextDisabled("No editor projects were found in the Library.");
        } else {
            ImGui::TextUnformatted(
                m_LibraryPickerMode == LibraryPickerMode::ReplaceSelectedWithProject
                    ? "Choose a project to replace the selected layer."
                    : "Choose a project to add it as a new layer.");
            ImGui::Separator();

            if (ImGui::BeginChild("CompositeLibraryPickerList")) {
                const float thumbSize = 84.0f;
                const float padding = 12.0f;

                for (const auto& project : projects) {
                    if (!project || project->projectKind != StackBinaryFormat::kEditorProjectKind) {
                        continue;
                    }

                    ImGui::PushID(project->fileName.c_str());
                    ImGui::BeginGroup();

                    if (project->thumbnailTex != 0) {
                        float aspect = 1.0f;
                        if (project->sourceWidth > 0 && project->sourceHeight > 0) {
                            aspect = static_cast<float>(project->sourceWidth) / static_cast<float>(project->sourceHeight);
                        }
                        float thumbW = thumbSize;
                        float thumbH = thumbSize;
                        if (aspect > 1.0f) {
                            thumbH = thumbSize / aspect;
                        } else {
                            thumbW = thumbSize * aspect;
                        }
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (thumbSize - thumbH) * 0.5f);
                        ImGui::Image(
                            (ImTextureID)(intptr_t)project->thumbnailTex,
                            ImVec2(thumbW, thumbH),
                            ImVec2(0, 1),
                            ImVec2(1, 0));
                    } else {
                        ImGui::Dummy(ImVec2(thumbSize, thumbSize));
                        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32_WHITE);
                    }

                    ImGui::SameLine(thumbSize + padding);
                    ImGui::BeginGroup();
                    ImGui::Text("%s", project->projectName.c_str());
                    ImGui::TextDisabled("%s", project->timestamp.c_str());
                    const char* actionLabel = (m_LibraryPickerMode == LibraryPickerMode::ReplaceSelectedWithProject)
                        ? "Replace Layer"
                        : "Add To Composite";
                    if (ImGui::Button(actionLabel)) {
                        const std::filesystem::path fullPath = LibraryManager::Get().GetLibraryPath() / project->fileName;
                        if (m_LibraryPickerMode == LibraryPickerMode::ReplaceSelectedWithProject) {
                            ReplaceSelectedLayerWithProjectFile(fullPath.string());
                        } else {
                            AddProjectLayerFromFile(fullPath.string());
                        }
                        m_ShowLibraryPicker = false;
                    }
                    ImGui::EndGroup();

                    ImGui::EndGroup();
                    ImGui::Separator();
                    ImGui::PopID();
                }

                ImGui::EndChild();
            }
        }

        ImGui::End();
    }
}

void CompositeModule::RenderUI() {
    if (!m_Initialized) {
        Initialize();
    }

    ImGui::BeginChild("CompositeToolbarRegion", ImVec2(0.0f, kCompositeToolbarHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    RenderToolbar();
    ImGui::EndChild();

    const ImGuiWindowFlags workspaceFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("CompositeWorkspaceRegion", ImVec2(0.0f, 0.0f), false, workspaceFlags);
    ImGui::PopStyleVar();

    m_WorkspaceDockId = ImGui::GetID(kCompositeDockSpaceName);
    if (m_PendingWorkspaceLayoutLoad && !m_WorkspaceLayoutIni.empty()) {
        ImGui::DockBuilderRemoveNode(m_WorkspaceDockId);
        ImGui::LoadIniSettingsFromMemory(m_WorkspaceLayoutIni.c_str(), m_WorkspaceLayoutIni.size());
        m_PendingWorkspaceLayoutLoad = false;
    }

    ImGui::DockSpace(m_WorkspaceDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (m_PendingWorkspaceLayoutReset) {
        BuildDefaultWorkspaceLayout(m_WorkspaceDockId);
        m_PendingWorkspaceLayoutReset = false;
    }

    bool panelVisibilityChanged = false;
    m_ExportPanelActive = false;

    const bool layersOpenBefore = m_ShowLayersWindow;
    if (m_ShowLayersWindow) {
        if (ImGui::Begin(kCompositeLayersWindowName, &m_ShowLayersWindow)) {
            RenderLayerPane();
        }
        ImGui::End();
    }
    panelVisibilityChanged |= (layersOpenBefore != m_ShowLayersWindow);

    const bool selectedOpenBefore = m_ShowSelectedWindow;
    if (m_ShowSelectedWindow) {
        if (ImGui::Begin(kCompositeSelectedWindowName, &m_ShowSelectedWindow)) {
            RenderSelectedInspector(GetSelectedLayer());
        }
        ImGui::End();
    }
    panelVisibilityChanged |= (selectedOpenBefore != m_ShowSelectedWindow);

    const bool viewOpenBefore = m_ShowViewWindow;
    if (m_ShowViewWindow) {
        if (ImGui::Begin(kCompositeViewWindowName, &m_ShowViewWindow)) {
            RenderViewInspector();
        }
        ImGui::End();
    }
    panelVisibilityChanged |= (viewOpenBefore != m_ShowViewWindow);

    const bool exportOpenBefore = m_ShowExportWindow;
    if (m_ShowExportWindow) {
        if (ImGui::Begin(kCompositeExportWindowName, &m_ShowExportWindow)) {
            m_ExportPanelActive = true;
            RenderExportInspector();
        }
        ImGui::End();
    }
    panelVisibilityChanged |= (exportOpenBefore != m_ShowExportWindow);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(kCompositeCanvasWindowName, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        RenderStage();
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (panelVisibilityChanged) {
        MarkDocumentDirty();
    }

    CaptureWorkspaceLayout();

    ImGui::EndChild();

    RenderLibraryPicker();
    RenderSavePopup();
    RenderRenamePopup();

    if (Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetSaveStatusText().c_str());
    }
}
