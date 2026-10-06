#pragma once

#include "RawGpuPreprocessTelemetry.h"
#include "RawImageData.h"

#include <array>
#include <cstddef>
#include <string>

namespace Raw {

// Cold-source preprocessing for ordinary Bayer mosaics. Large sensor data is
// kept in the immutable R16UI texture; only compact DNG tables and gain-map
// grids cross the bus before a compute pass produces the normalized R32F CFA
// texture and, when requested, its noise-variance companion.
class RawGpuPreprocessor {
public:
    RawGpuPreprocessor() = default;
    ~RawGpuPreprocessor();

    bool Process(
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
        std::string& error);

    void InvalidateOutputs();
    void Clear();

private:
    struct MetadataLayout {
        int linearizationOffset = 0;
        int linearizationCount = 0;
        int blackValuesOffset = 0;
        int blackValuesCount = 0;
        int blackDeltaHOffset = 0;
        int blackDeltaHCount = 0;
        int blackDeltaVOffset = 0;
        int blackDeltaVCount = 0;
        int whiteValuesOffset = 0;
        int whiteValuesCount = 0;
        int gainMapHeadersOffset = 0;
        int gainMapCount = 0;
        int atlasWidth = 0;
        int atlasHeight = 0;
    };

    struct UniformLocations {
        int raw = -1;
        int metadata = -1;
        int rawSize = -1;
        int activeArea = -1;
        int fallbackCfaPattern = -1;
        int dngCfaRepeat = -1;
        int dngCfaPattern = -1;
        int dngCfaPlaneColor = -1;
        int blackLevel = -1;
        int perChannelBlack = -1;
        int blackRepeat = -1;
        int whiteLevel = -1;
        int overrideBlack = -1;
        int blackOverride = -1;
        int overrideWhite = -1;
        int whiteOverride = -1;
        int linearizationOffset = -1;
        int linearizationCount = -1;
        int blackValuesOffset = -1;
        int blackValuesCount = -1;
        int blackDeltaHOffset = -1;
        int blackDeltaHCount = -1;
        int blackDeltaVOffset = -1;
        int blackDeltaVCount = -1;
        int whiteValuesOffset = -1;
        int whiteValuesCount = -1;
        int gainMapHeadersOffset = -1;
        int gainMapCount = -1;
        int metadataWidth = -1;
        int writeVariance = -1;
        int noiseShotScale = -1;
        int noiseReadVariance = -1;
    };

    unsigned int m_Program = 0;
    bool m_ProgramAttempted = false;
    std::string m_ProgramFailure;
    unsigned int m_MetadataTexture = 0;
    unsigned int m_VarianceSinkTexture = 0;
    UniformLocations m_Uniforms;
    MetadataLayout m_MetadataLayout;
    std::size_t m_MetadataFingerprint = 0;
    std::size_t m_PreprocessFingerprint = 0;
    int m_OutputWidth = 0;
    int m_OutputHeight = 0;

    bool EnsureProgram(std::string& error);
    bool EnsureVarianceSink(std::string& error);
    bool EnsureMetadataTexture(
        const RawMetadata& metadata,
        std::size_t metadataFingerprint,
        RawGpuPreprocessTelemetry& telemetry,
        std::string& error);
};

} // namespace Raw
