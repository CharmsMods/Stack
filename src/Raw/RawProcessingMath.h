#pragma once

#include "RawImageData.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::Processing {

RawSensorRect ResolveActiveArea(const RawMetadata& metadata);
int CfaColorAt(const RawMetadata& metadata, const RawSensorRect& activeArea, int sensorX, int sensorY);
float BlackLevelAt(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings,
    const RawSensorRect& activeArea,
    int sensorX,
    int sensorY,
    int color);
float WhiteLevelForColor(const RawMetadata& metadata, const RawDevelopSettings& settings, int color);
float LinearizeStoredSample(const RawMetadata& metadata, std::uint16_t storedSample);
float NormalizeStoredSample(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings,
    const RawSensorRect& activeArea,
    int sensorX,
    int sensorY,
    std::uint16_t storedSample);
float SampleDngGainMap(
    const DngGainMapOpcode& map,
    int imageWidth,
    int imageHeight,
    int imageX,
    int imageY);

bool ResolveDngNoiseProfile(
    const RawMetadata& metadata,
    std::array<DngNoiseProfilePlane, 3>& colorPlanes);
float DngNoiseVariance(
    const DngNoiseProfilePlane& profile,
    float normalizedSignal);
float NoiseAwareRangeWeight(
    float center,
    float neighbor,
    float centerVariance,
    float neighborVariance,
    float edgeProtection);

bool BuildTruthfulNormalizedMosaic(
    const RawImageData& raw,
    const RawDevelopSettings& settings,
    std::vector<float>& normalized,
    std::string* error = nullptr);

bool BuildTruthfulNoiseVarianceMosaic(
    const RawImageData& raw,
    const RawDevelopSettings& settings,
    std::vector<float>& noiseVariance,
    std::string* error = nullptr);

bool ApplyWhiteBalanceToCfaMosaic(
    const RawMetadata& metadata,
    const std::array<float, 3>& multipliers,
    std::vector<float>& normalizedMosaic,
    std::string* error = nullptr);

std::array<float, 3> DemosaicMalvarHeCutlerAt(
    const std::vector<float>& mosaic,
    int width,
    int height,
    CfaPattern pattern,
    int x,
    int y);
std::array<float, 3> DemosaicNearestNeighborAt(
    const std::vector<float>& mosaic,
    int width,
    int height,
    CfaPattern pattern,
    int x,
    int y);
std::array<float, 3> DemosaicHamiltonAdamsAt(
    const std::vector<float>& mosaic,
    int width,
    int height,
    CfaPattern pattern,
    int x,
    int y);

float EncodeSrgb(float linearValue);
std::array<float, 3> EncodeSrgb(const std::array<float, 3>& linearRgb);

} // namespace Raw::Processing
