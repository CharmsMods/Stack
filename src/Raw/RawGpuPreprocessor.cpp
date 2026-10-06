#include "RawGpuPreprocessor.h"

#include "RawProcessingMath.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif

namespace Raw {
namespace {

constexpr int kWorkgroupSize = 16;
constexpr int kGainMapHeaderStride = 16;
constexpr std::size_t kLargestExactlyRepresentableFloatInteger = 16777216u;

constexpr const char* kPreprocessShader = R"GLSL(
#version 430 core
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0) uniform usampler2D uRaw;
layout(binding = 1) uniform sampler2D uMetadata;
layout(r32f, binding = 0) writeonly uniform image2D uCorrected;
layout(r32f, binding = 1) writeonly uniform image2D uVariance;

uniform ivec2 uRawSize;
uniform vec4 uActiveArea;
uniform int uFallbackCfaPattern;
uniform ivec2 uDngCfaRepeat;
uniform vec4 uDngCfaPattern;
uniform vec3 uDngCfaPlaneColor;
uniform float uBlackLevel;
uniform vec3 uPerChannelBlack;
uniform ivec2 uBlackRepeat;
uniform float uWhiteLevel;
uniform int uOverrideBlack;
uniform float uBlackOverride;
uniform int uOverrideWhite;
uniform float uWhiteOverride;
uniform int uLinearizationOffset;
uniform int uLinearizationCount;
uniform int uBlackValuesOffset;
uniform int uBlackValuesCount;
uniform int uBlackDeltaHOffset;
uniform int uBlackDeltaHCount;
uniform int uBlackDeltaVOffset;
uniform int uBlackDeltaVCount;
uniform int uWhiteValuesOffset;
uniform int uWhiteValuesCount;
uniform int uGainMapHeadersOffset;
uniform int uGainMapCount;
uniform int uMetadataWidth;
uniform int uWriteVariance;
uniform vec3 uNoiseShotScale;
uniform vec3 uNoiseReadVariance;

float metadataAt(int index) {
    int safeWidth = max(1, uMetadataWidth);
    return texelFetch(
        uMetadata,
        ivec2(index % safeWidth, index / safeWidth),
        0).r;
}

int positiveModulo(int value, int modulus) {
    if (modulus <= 0) return 0;
    int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

int fallbackColorAt(ivec2 localP) {
    int x = positiveModulo(localP.x, 2);
    int y = positiveModulo(localP.y, 2);
    int index = y * 2 + x;
    if (uFallbackCfaPattern == 1) {
        const int colors[4] = int[4](0, 1, 1, 2);
        return colors[index];
    }
    if (uFallbackCfaPattern == 2) {
        const int colors[4] = int[4](2, 1, 1, 0);
        return colors[index];
    }
    if (uFallbackCfaPattern == 3) {
        const int colors[4] = int[4](1, 2, 0, 1);
        return colors[index];
    }
    if (uFallbackCfaPattern == 4) {
        const int colors[4] = int[4](1, 0, 2, 1);
        return colors[index];
    }
    return 1;
}

int cfaColorAt(ivec2 sensorP, ivec4 activeRect) {
    if (uDngCfaRepeat.x == 2 && uDngCfaRepeat.y == 2) {
        int patternIndex =
            positiveModulo(sensorP.y - activeRect.y, 2) * 2 +
            positiveModulo(sensorP.x - activeRect.x, 2);
        int plane = int(round(uDngCfaPattern[patternIndex]));
        if (plane >= 0 && plane < 3) {
            int color = int(round(uDngCfaPlaneColor[plane]));
            if (color >= 0 && color < 3) return color;
        }
    }
    return fallbackColorAt(sensorP - activeRect.xy);
}

float blackLevelAt(ivec2 sensorP, ivec4 activeRect, int color) {
    if (uOverrideBlack != 0) return uBlackOverride;
    float channelBlack = color == 0
        ? uPerChannelBlack.r
        : (color == 2 ? uPerChannelBlack.b : uPerChannelBlack.g);
    float black = channelBlack > 0.0 ? channelBlack : uBlackLevel;
    if (uBlackRepeat.x > 0 && uBlackRepeat.y > 0 &&
        uBlackValuesCount > 0) {
        int index =
            positiveModulo(sensorP.y - activeRect.y, uBlackRepeat.x) *
                uBlackRepeat.y +
            positiveModulo(sensorP.x - activeRect.x, uBlackRepeat.y);
        if (index >= 0 && index < uBlackValuesCount) {
            black = metadataAt(uBlackValuesOffset + index);
        }
    }
    int localX = sensorP.x - activeRect.x;
    int localY = sensorP.y - activeRect.y;
    if (localX >= 0 && localX < uBlackDeltaHCount) {
        black += metadataAt(uBlackDeltaHOffset + localX);
    }
    if (localY >= 0 && localY < uBlackDeltaVCount) {
        black += metadataAt(uBlackDeltaVOffset + localY);
    }
    return black;
}

float whiteLevelAt(int color) {
    if (uOverrideWhite != 0) return uWhiteOverride;
    if (uWhiteValuesCount == 1) {
        return metadataAt(uWhiteValuesOffset);
    }
    if (color >= 0 && color < uWhiteValuesCount) {
        return metadataAt(uWhiteValuesOffset + color);
    }
    return uWhiteLevel;
}

float linearizedSample(uint storedSample) {
    if (uLinearizationCount <= 0) return float(storedSample);
    int index = min(int(storedSample), uLinearizationCount - 1);
    return metadataAt(uLinearizationOffset + index);
}

float sampleGainMap(int mapIndex, ivec2 visibleP, ivec2 activeSize) {
    int base = uGainMapHeadersOffset + mapIndex * 16;
    int top = int(round(metadataAt(base + 0)));
    int left = int(round(metadataAt(base + 1)));
    int bottom = int(round(metadataAt(base + 2)));
    int right = int(round(metadataAt(base + 3)));
    int rowPitch = max(1, int(round(metadataAt(base + 4))));
    int colPitch = max(1, int(round(metadataAt(base + 5))));
    int pointsV = int(round(metadataAt(base + 6)));
    int pointsH = int(round(metadataAt(base + 7)));
    int mapPlanes = int(round(metadataAt(base + 8)));
    int plane = clamp(int(round(metadataAt(base + 9))), 0, max(0, mapPlanes - 1));
    int gainsOffset = int(round(metadataAt(base + 10)));
    int gainsCount = int(round(metadataAt(base + 11)));
    float spacingV = metadataAt(base + 12);
    float spacingH = metadataAt(base + 13);
    float originV = metadataAt(base + 14);
    float originH = metadataAt(base + 15);

    if (visibleP.y < top || visibleP.y >= bottom ||
        visibleP.x < left || visibleP.x >= right ||
        positiveModulo(visibleP.y - top, rowPitch) != 0 ||
        positiveModulo(visibleP.x - left, colPitch) != 0) {
        return 1.0;
    }

    float normalizedX =
        (float(visibleP.x) + 0.5) / float(max(1, activeSize.x));
    float normalizedY =
        (float(visibleP.y) + 0.5) / float(max(1, activeSize.y));
    float gridX = (normalizedX - originH) / spacingH;
    float gridY = (normalizedY - originV) / spacingV;
    float clampedX = clamp(gridX, 0.0, float(pointsH - 1));
    float clampedY = clamp(gridY, 0.0, float(pointsV - 1));
    int x0 = int(floor(clampedX));
    int y0 = int(floor(clampedY));
    int x1 = min(x0 + 1, pointsH - 1);
    int y1 = min(y0 + 1, pointsV - 1);
    float tx = clampedX - float(x0);
    float ty = clampedY - float(y0);

    int index00 = (y0 * pointsH + x0) * mapPlanes + plane;
    int index10 = (y0 * pointsH + x1) * mapPlanes + plane;
    int index01 = (y1 * pointsH + x0) * mapPlanes + plane;
    int index11 = (y1 * pointsH + x1) * mapPlanes + plane;
    float g00 = index00 >= 0 && index00 < gainsCount
        ? metadataAt(gainsOffset + index00) : 1.0;
    float g10 = index10 >= 0 && index10 < gainsCount
        ? metadataAt(gainsOffset + index10) : 1.0;
    float g01 = index01 >= 0 && index01 < gainsCount
        ? metadataAt(gainsOffset + index01) : 1.0;
    float g11 = index11 >= 0 && index11 < gainsCount
        ? metadataAt(gainsOffset + index11) : 1.0;
    float a = mix(g00, g10, tx);
    float b = mix(g01, g11, tx);
    return max(0.0, mix(a, b, ty));
}

void main() {
    ivec2 sensorP = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(sensorP, uRawSize))) return;

    ivec4 activeRect = ivec4(round(uActiveArea));
    int color = cfaColorAt(sensorP, activeRect);
    float black = blackLevelAt(sensorP, activeRect, color);
    float white = max(black + 1.0, whiteLevelAt(color));
    uint storedSample = texelFetch(uRaw, sensorP, 0).r;
    float signal = min(
        (linearizedSample(storedSample) - black) / (white - black),
        1.0);
    float variance = max(
        0.0,
        uNoiseShotScale[color] * clamp(signal, 0.0, 1.0) +
            uNoiseReadVariance[color]);

    ivec2 visibleP = sensorP - activeRect.xy;
    ivec2 activeSize = activeRect.zw - activeRect.xy;
    if (visibleP.x >= 0 && visibleP.y >= 0 &&
        sensorP.x < activeRect.z && sensorP.y < activeRect.w) {
        for (int mapIndex = 0; mapIndex < uGainMapCount; ++mapIndex) {
            float gain = sampleGainMap(mapIndex, visibleP, activeSize);
            signal = clamp(signal * gain, 0.0, 1.0);
            variance = max(0.0, variance * gain * gain);
        }
    }

    imageStore(uCorrected, sensorP, vec4(signal, 0.0, 0.0, 1.0));
    if (uWriteVariance != 0) {
        imageStore(uVariance, sensorP, vec4(variance, 0.0, 0.0, 1.0));
    }
}
)GLSL";

double Milliseconds(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

int PatternUniform(CfaPattern pattern) {
    switch (pattern) {
        case CfaPattern::RGGB: return 1;
        case CfaPattern::BGGR: return 2;
        case CfaPattern::GBRG: return 3;
        case CfaPattern::GRBG: return 4;
        case CfaPattern::Unknown:
        default: return 0;
    }
}

template <typename T>
void AppendValues(
    const std::vector<T>& source,
    std::vector<float>& destination,
    int& offset,
    int& count) {
    offset = static_cast<int>(destination.size());
    count = static_cast<int>(source.size());
    destination.reserve(destination.size() + source.size());
    for (const T& value : source) {
        destination.push_back(static_cast<float>(value));
    }
}

bool IsGpuGainMap(const DngGainMapOpcode& map) {
    if (map.mapPointsH <= 0 || map.mapPointsV <= 0 || map.mapPlanes <= 0 ||
        !std::isfinite(map.mapSpacingH) || !std::isfinite(map.mapSpacingV) ||
        !std::isfinite(map.mapOriginH) || !std::isfinite(map.mapOriginV) ||
        map.mapSpacingH <= 0.0 || map.mapSpacingV <= 0.0 ||
        map.gains.empty()) {
        return false;
    }
    return map.plane <= 0 && map.plane + map.planes > 0;
}

bool HasNonFiniteGain(const DngGainMapOpcode& map) {
    return std::any_of(map.gains.begin(), map.gains.end(), [](float value) {
        return !std::isfinite(value);
    });
}

struct SavedImageBinding {
    GLint name = 0;
    GLint level = 0;
    GLint layered = GL_FALSE;
    GLint layer = 0;
    GLint access = GL_READ_ONLY;
    GLint format = GL_R32F;
};

SavedImageBinding SaveImageBinding(unsigned int unit) {
    SavedImageBinding saved;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, unit, &saved.name);
    glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, unit, &saved.level);
    glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, unit, &saved.layered);
    glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, unit, &saved.layer);
    glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, unit, &saved.access);
    glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, unit, &saved.format);
    return saved;
}

void RestoreImageBinding(unsigned int unit, const SavedImageBinding& saved) {
    glBindImageTexture(
        unit,
        static_cast<unsigned int>(std::max(0, saved.name)),
        saved.level,
        saved.layered != 0 ? GL_TRUE : GL_FALSE,
        saved.layer,
        static_cast<unsigned int>(saved.access),
        static_cast<unsigned int>(saved.format != 0 ? saved.format : GL_R32F));
}

} // namespace

RawGpuPreprocessor::~RawGpuPreprocessor() {
    Clear();
}

void RawGpuPreprocessor::Clear() {
    if (m_Program != 0) glDeleteProgram(m_Program);
    if (m_MetadataTexture != 0) glDeleteTextures(1, &m_MetadataTexture);
    if (m_VarianceSinkTexture != 0) glDeleteTextures(1, &m_VarianceSinkTexture);
    m_Program = 0;
    m_ProgramAttempted = false;
    m_ProgramFailure.clear();
    m_MetadataTexture = 0;
    m_VarianceSinkTexture = 0;
    m_Uniforms = {};
    m_MetadataLayout = {};
    m_MetadataFingerprint = 0;
    InvalidateOutputs();
}

void RawGpuPreprocessor::InvalidateOutputs() {
    m_PreprocessFingerprint = 0;
    m_OutputWidth = 0;
    m_OutputHeight = 0;
}

bool RawGpuPreprocessor::EnsureProgram(std::string& error) {
    if (m_Program != 0) return true;
    if (m_ProgramAttempted) {
        error = m_ProgramFailure;
        return false;
    }
    m_ProgramAttempted = true;

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3)) {
        m_ProgramFailure =
            "OpenGL 4.3 compute is unavailable for RAW cold preprocessing.";
        error = m_ProgramFailure;
        return false;
    }

    m_Program = GLHelpers::CreateComputeProgram(kPreprocessShader);
    if (m_Program == 0) {
        m_ProgramFailure =
            "RAW cold preprocessing compute shader did not compile.";
        error = m_ProgramFailure;
        return false;
    }

    m_Uniforms.raw = glGetUniformLocation(m_Program, "uRaw");
    m_Uniforms.metadata = glGetUniformLocation(m_Program, "uMetadata");
    m_Uniforms.rawSize = glGetUniformLocation(m_Program, "uRawSize");
    m_Uniforms.activeArea = glGetUniformLocation(m_Program, "uActiveArea");
    m_Uniforms.fallbackCfaPattern = glGetUniformLocation(m_Program, "uFallbackCfaPattern");
    m_Uniforms.dngCfaRepeat = glGetUniformLocation(m_Program, "uDngCfaRepeat");
    m_Uniforms.dngCfaPattern = glGetUniformLocation(m_Program, "uDngCfaPattern");
    m_Uniforms.dngCfaPlaneColor = glGetUniformLocation(m_Program, "uDngCfaPlaneColor");
    m_Uniforms.blackLevel = glGetUniformLocation(m_Program, "uBlackLevel");
    m_Uniforms.perChannelBlack = glGetUniformLocation(m_Program, "uPerChannelBlack");
    m_Uniforms.blackRepeat = glGetUniformLocation(m_Program, "uBlackRepeat");
    m_Uniforms.whiteLevel = glGetUniformLocation(m_Program, "uWhiteLevel");
    m_Uniforms.overrideBlack = glGetUniformLocation(m_Program, "uOverrideBlack");
    m_Uniforms.blackOverride = glGetUniformLocation(m_Program, "uBlackOverride");
    m_Uniforms.overrideWhite = glGetUniformLocation(m_Program, "uOverrideWhite");
    m_Uniforms.whiteOverride = glGetUniformLocation(m_Program, "uWhiteOverride");
    m_Uniforms.linearizationOffset = glGetUniformLocation(m_Program, "uLinearizationOffset");
    m_Uniforms.linearizationCount = glGetUniformLocation(m_Program, "uLinearizationCount");
    m_Uniforms.blackValuesOffset = glGetUniformLocation(m_Program, "uBlackValuesOffset");
    m_Uniforms.blackValuesCount = glGetUniformLocation(m_Program, "uBlackValuesCount");
    m_Uniforms.blackDeltaHOffset = glGetUniformLocation(m_Program, "uBlackDeltaHOffset");
    m_Uniforms.blackDeltaHCount = glGetUniformLocation(m_Program, "uBlackDeltaHCount");
    m_Uniforms.blackDeltaVOffset = glGetUniformLocation(m_Program, "uBlackDeltaVOffset");
    m_Uniforms.blackDeltaVCount = glGetUniformLocation(m_Program, "uBlackDeltaVCount");
    m_Uniforms.whiteValuesOffset = glGetUniformLocation(m_Program, "uWhiteValuesOffset");
    m_Uniforms.whiteValuesCount = glGetUniformLocation(m_Program, "uWhiteValuesCount");
    m_Uniforms.gainMapHeadersOffset = glGetUniformLocation(m_Program, "uGainMapHeadersOffset");
    m_Uniforms.gainMapCount = glGetUniformLocation(m_Program, "uGainMapCount");
    m_Uniforms.metadataWidth = glGetUniformLocation(m_Program, "uMetadataWidth");
    m_Uniforms.writeVariance = glGetUniformLocation(m_Program, "uWriteVariance");
    m_Uniforms.noiseShotScale = glGetUniformLocation(m_Program, "uNoiseShotScale");
    m_Uniforms.noiseReadVariance = glGetUniformLocation(m_Program, "uNoiseReadVariance");
    return true;
}

bool RawGpuPreprocessor::EnsureVarianceSink(std::string& error) {
    if (m_VarianceSinkTexture != 0) return true;
    m_VarianceSinkTexture = GLHelpers::CreateStorageTexture(1, 1, GL_R32F);
    if (m_VarianceSinkTexture == 0) {
        error = "RAW cold preprocessing could not allocate its variance sink.";
        return false;
    }
    return true;
}

bool RawGpuPreprocessor::EnsureMetadataTexture(
    const RawMetadata& metadata,
    std::size_t metadataFingerprint,
    RawGpuPreprocessTelemetry& telemetry,
    std::string& error) {
    if (m_MetadataTexture != 0 &&
        m_MetadataFingerprint == metadataFingerprint &&
        m_MetadataLayout.atlasWidth > 0 &&
        m_MetadataLayout.atlasHeight > 0) {
        return true;
    }

    const auto buildBegin = std::chrono::steady_clock::now();
    MetadataLayout layout;
    std::vector<float> values;
    AppendValues(metadata.dngLinearizationTable, values,
        layout.linearizationOffset, layout.linearizationCount);
    AppendValues(metadata.dngBlackLevelValues, values,
        layout.blackValuesOffset, layout.blackValuesCount);
    AppendValues(metadata.dngBlackLevelDeltaH, values,
        layout.blackDeltaHOffset, layout.blackDeltaHCount);
    AppendValues(metadata.dngBlackLevelDeltaV, values,
        layout.blackDeltaVOffset, layout.blackDeltaVCount);
    AppendValues(metadata.dngWhiteLevelValues, values,
        layout.whiteValuesOffset, layout.whiteValuesCount);

    std::vector<const DngGainMapOpcode*> gainMaps;
    gainMaps.reserve(metadata.dngGainMaps.size());
    for (const DngGainMapOpcode& map : metadata.dngGainMaps) {
        if (!IsGpuGainMap(map)) continue;
        if (HasNonFiniteGain(map)) {
            error = "DNG GainMap contains a non-finite value; using the CPU parity fallback.";
            return false;
        }
        gainMaps.push_back(&map);
    }
    layout.gainMapHeadersOffset = static_cast<int>(values.size());
    layout.gainMapCount = static_cast<int>(gainMaps.size());
    values.resize(
        values.size() + gainMaps.size() *
            static_cast<std::size_t>(kGainMapHeaderStride),
        0.0f);
    for (std::size_t mapIndex = 0; mapIndex < gainMaps.size(); ++mapIndex) {
        const DngGainMapOpcode& map = *gainMaps[mapIndex];
        const std::size_t header =
            static_cast<std::size_t>(layout.gainMapHeadersOffset) +
            mapIndex * static_cast<std::size_t>(kGainMapHeaderStride);
        const std::size_t gainsOffset = values.size();
        values.insert(values.end(), map.gains.begin(), map.gains.end());
        const std::array<float, kGainMapHeaderStride> packed {
            static_cast<float>(map.top),
            static_cast<float>(map.left),
            static_cast<float>(map.bottom),
            static_cast<float>(map.right),
            static_cast<float>(std::max(1, map.rowPitch)),
            static_cast<float>(std::max(1, map.colPitch)),
            static_cast<float>(map.mapPointsV),
            static_cast<float>(map.mapPointsH),
            static_cast<float>(map.mapPlanes),
            static_cast<float>(std::clamp(map.plane, 0, map.mapPlanes - 1)),
            static_cast<float>(gainsOffset),
            static_cast<float>(map.gains.size()),
            static_cast<float>(map.mapSpacingV),
            static_cast<float>(map.mapSpacingH),
            static_cast<float>(map.mapOriginV),
            static_cast<float>(map.mapOriginH)
        };
        std::copy(packed.begin(), packed.end(), values.begin() +
            static_cast<std::ptrdiff_t>(header));
    }
    if (values.empty()) values.push_back(0.0f);
    if (values.size() > kLargestExactlyRepresentableFloatInteger) {
        error = "DNG preprocessing metadata exceeds the exact compact-atlas index range.";
        return false;
    }

    GLint maximumTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    const int atlasWidth = std::min(1024, maximumTextureSize);
    if (atlasWidth <= 0) {
        error = "OpenGL did not report a usable metadata texture size.";
        return false;
    }
    const std::size_t atlasHeight =
        (values.size() + static_cast<std::size_t>(atlasWidth) - 1u) /
        static_cast<std::size_t>(atlasWidth);
    if (atlasHeight == 0u ||
        atlasHeight > static_cast<std::size_t>(maximumTextureSize)) {
        error = "DNG preprocessing metadata does not fit in a compact GPU atlas.";
        return false;
    }
    layout.atlasWidth = atlasWidth;
    layout.atlasHeight = static_cast<int>(atlasHeight);
    values.resize(
        static_cast<std::size_t>(layout.atlasWidth) *
            static_cast<std::size_t>(layout.atlasHeight),
        0.0f);
    telemetry.metadataBuildMs = Milliseconds(
        buildBegin, std::chrono::steady_clock::now());

    const auto uploadBegin = std::chrono::steady_clock::now();
    if (m_MetadataTexture != 0) {
        glDeleteTextures(1, &m_MetadataTexture);
        m_MetadataTexture = 0;
    }
    m_MetadataTexture = GLHelpers::CreateTextureFromData(
        values.data(), layout.atlasWidth, layout.atlasHeight,
        GL_R32F, GL_RED, GL_FLOAT);
    telemetry.metadataUploadMs = Milliseconds(
        uploadBegin, std::chrono::steady_clock::now());
    telemetry.metadataUploadBytes = values.size() * sizeof(float);
    if (m_MetadataTexture == 0) {
        error = "DNG compact metadata texture upload failed.";
        return false;
    }

    m_MetadataLayout = layout;
    m_MetadataFingerprint = metadataFingerprint;
    return true;
}

bool RawGpuPreprocessor::Process(
    const RawImageData& raw,
    const RawDevelopSettings& settings,
    unsigned int rawTexture,
    std::size_t preprocessFingerprint,
    std::size_t metadataFingerprint,
    bool generateNoiseVariance,
    const std::array<DngNoiseProfilePlane, 3>& noiseProfiles,
    unsigned int& correctedTexture,
    unsigned int& varianceTexture,
    RawGpuPreprocessTelemetry& telemetry,
    std::string& error) {
    error.clear();
    const RawMetadata& metadata = raw.metadata;
    const int width = metadata.rawWidth;
    const int height = metadata.rawHeight;
    if (rawTexture == 0 || width <= 0 || height <= 0) {
        error = "RAW cold preprocessing requires a valid R16UI sensor texture.";
        return false;
    }

    if (correctedTexture != 0 &&
        m_OutputWidth == width &&
        m_OutputHeight == height &&
        m_PreprocessFingerprint == preprocessFingerprint &&
        (!generateNoiseVariance || varianceTexture != 0)) {
        telemetry.correctedCacheHit = true;
        telemetry.varianceGenerated = generateNoiseVariance;
        return true;
    }

    if (!EnsureProgram(error) ||
        !EnsureVarianceSink(error) ||
        !EnsureMetadataTexture(
            metadata,
            metadataFingerprint,
            telemetry,
            error)) {
        return false;
    }

    if (m_OutputWidth != width || m_OutputHeight != height) {
        if (correctedTexture != 0) {
            glDeleteTextures(1, &correctedTexture);
            correctedTexture = 0;
        }
        if (varianceTexture != 0) {
            glDeleteTextures(1, &varianceTexture);
            varianceTexture = 0;
        }
    }
    if (correctedTexture == 0) {
        correctedTexture = GLHelpers::CreateStorageTexture(width, height, GL_R32F);
        if (correctedTexture != 0) {
            GLint previousTextureBinding = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
            glBindTexture(GL_TEXTURE_2D, correctedTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(
                GL_TEXTURE_2D,
                static_cast<unsigned int>(
                    std::max(0, previousTextureBinding)));
        }
    }
    if (generateNoiseVariance && varianceTexture == 0) {
        varianceTexture = GLHelpers::CreateStorageTexture(width, height, GL_R32F);
        if (varianceTexture != 0) {
            GLint previousTextureBinding = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
            glBindTexture(GL_TEXTURE_2D, varianceTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(
                GL_TEXTURE_2D,
                static_cast<unsigned int>(
                    std::max(0, previousTextureBinding)));
        }
    } else if (!generateNoiseVariance && varianceTexture != 0) {
        glDeleteTextures(1, &varianceTexture);
        varianceTexture = 0;
    }
    if (correctedTexture == 0 ||
        (generateNoiseVariance && varianceTexture == 0)) {
        error = "RAW cold preprocessing could not allocate its R32F output textures.";
        return false;
    }

    GLint previousProgram = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousRawBinding = 0;
    GLint previousMetadataBinding = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousRawBinding);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousMetadataBinding);
    const SavedImageBinding previousCorrectedImage = SaveImageBinding(0);
    const SavedImageBinding previousVarianceImage = SaveImageBinding(1);
    const auto restoreState = [&]() {
        RestoreImageBinding(0, previousCorrectedImage);
        RestoreImageBinding(1, previousVarianceImage);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,
            static_cast<unsigned int>(std::max(0, previousRawBinding)));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,
            static_cast<unsigned int>(std::max(0, previousMetadataBinding)));
        glActiveTexture(static_cast<unsigned int>(previousActiveTexture));
        glUseProgram(static_cast<unsigned int>(std::max(0, previousProgram)));
    };

    const RawSensorRect active = Processing::ResolveActiveArea(metadata);
    glUseProgram(m_Program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rawTexture);
    glUniform1i(m_Uniforms.raw, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_MetadataTexture);
    glUniform1i(m_Uniforms.metadata, 1);
    glBindImageTexture(0, correctedTexture, 0, GL_FALSE, 0,
        GL_WRITE_ONLY, GL_R32F);
    glBindImageTexture(
        1,
        generateNoiseVariance ? varianceTexture : m_VarianceSinkTexture,
        0,
        GL_FALSE,
        0,
        GL_WRITE_ONLY,
        GL_R32F);
    glUniform2i(m_Uniforms.rawSize, width, height);
    glUniform4f(m_Uniforms.activeArea,
        static_cast<float>(active.left),
        static_cast<float>(active.top),
        static_cast<float>(active.right),
        static_cast<float>(active.bottom));
    glUniform1i(m_Uniforms.fallbackCfaPattern,
        PatternUniform(metadata.cfaPattern));
    glUniform2i(m_Uniforms.dngCfaRepeat,
        metadata.dngCfaRepeatPatternDim[0],
        metadata.dngCfaRepeatPatternDim[1]);
    glUniform4f(m_Uniforms.dngCfaPattern,
        static_cast<float>(metadata.dngCfaPattern[0]),
        static_cast<float>(metadata.dngCfaPattern[1]),
        static_cast<float>(metadata.dngCfaPattern[2]),
        static_cast<float>(metadata.dngCfaPattern[3]));
    glUniform3f(m_Uniforms.dngCfaPlaneColor,
        static_cast<float>(metadata.dngCfaPlaneColor[0]),
        static_cast<float>(metadata.dngCfaPlaneColor[1]),
        static_cast<float>(metadata.dngCfaPlaneColor[2]));
    glUniform1f(m_Uniforms.blackLevel, metadata.blackLevel);
    glUniform3f(m_Uniforms.perChannelBlack,
        metadata.perChannelBlack[0],
        metadata.perChannelBlack[1],
        metadata.perChannelBlack[2]);
    glUniform2i(m_Uniforms.blackRepeat,
        metadata.dngBlackLevelRepeatDim[0],
        metadata.dngBlackLevelRepeatDim[1]);
    glUniform1f(m_Uniforms.whiteLevel, metadata.whiteLevel);
    glUniform1i(m_Uniforms.overrideBlack,
        settings.overrideBlackLevel ? 1 : 0);
    glUniform1f(m_Uniforms.blackOverride, settings.blackLevelOverride);
    glUniform1i(m_Uniforms.overrideWhite,
        settings.overrideWhiteLevel ? 1 : 0);
    glUniform1f(m_Uniforms.whiteOverride, settings.whiteLevelOverride);
    glUniform1i(m_Uniforms.linearizationOffset,
        m_MetadataLayout.linearizationOffset);
    glUniform1i(m_Uniforms.linearizationCount,
        m_MetadataLayout.linearizationCount);
    glUniform1i(m_Uniforms.blackValuesOffset,
        m_MetadataLayout.blackValuesOffset);
    glUniform1i(m_Uniforms.blackValuesCount,
        m_MetadataLayout.blackValuesCount);
    glUniform1i(m_Uniforms.blackDeltaHOffset,
        m_MetadataLayout.blackDeltaHOffset);
    glUniform1i(m_Uniforms.blackDeltaHCount,
        m_MetadataLayout.blackDeltaHCount);
    glUniform1i(m_Uniforms.blackDeltaVOffset,
        m_MetadataLayout.blackDeltaVOffset);
    glUniform1i(m_Uniforms.blackDeltaVCount,
        m_MetadataLayout.blackDeltaVCount);
    glUniform1i(m_Uniforms.whiteValuesOffset,
        m_MetadataLayout.whiteValuesOffset);
    glUniform1i(m_Uniforms.whiteValuesCount,
        m_MetadataLayout.whiteValuesCount);
    glUniform1i(m_Uniforms.gainMapHeadersOffset,
        m_MetadataLayout.gainMapHeadersOffset);
    glUniform1i(m_Uniforms.gainMapCount,
        m_MetadataLayout.gainMapCount);
    glUniform1i(m_Uniforms.metadataWidth,
        m_MetadataLayout.atlasWidth);
    glUniform1i(m_Uniforms.writeVariance,
        generateNoiseVariance ? 1 : 0);
    glUniform3f(m_Uniforms.noiseShotScale,
        static_cast<float>(noiseProfiles[0].shotScale),
        static_cast<float>(noiseProfiles[1].shotScale),
        static_cast<float>(noiseProfiles[2].shotScale));
    glUniform3f(m_Uniforms.noiseReadVariance,
        static_cast<float>(noiseProfiles[0].readNoiseVariance),
        static_cast<float>(noiseProfiles[1].readNoiseVariance),
        static_cast<float>(noiseProfiles[2].readNoiseVariance));

    while (glGetError() != GL_NO_ERROR) {
    }
    const auto dispatchBegin = std::chrono::steady_clock::now();
    glDispatchCompute(
        static_cast<unsigned int>((width + kWorkgroupSize - 1) /
            kWorkgroupSize),
        static_cast<unsigned int>((height + kWorkgroupSize - 1) /
            kWorkgroupSize),
        1u);
    glMemoryBarrier(
        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
        GL_TEXTURE_FETCH_BARRIER_BIT);
    telemetry.gpuDispatchSubmitMs = Milliseconds(
        dispatchBegin, std::chrono::steady_clock::now());
    const unsigned int dispatchError = glGetError();
    restoreState();
    if (dispatchError != GL_NO_ERROR) {
        error = "RAW cold preprocessing compute dispatch failed with OpenGL error " +
            std::to_string(dispatchError) + ".";
        return false;
    }

    m_PreprocessFingerprint = preprocessFingerprint;
    m_OutputWidth = width;
    m_OutputHeight = height;
    telemetry.gpuDispatched = true;
    telemetry.varianceGenerated = generateNoiseVariance;
    return true;
}

} // namespace Raw
