#pragma once

#include "Raw/MultiFrame/Contracts.h"
#include "Raw/MultiFrameDenoise/NoiseModel.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Raw::MultiFrame {

enum class LegacyProcessorKind : std::uint8_t {
    MultiFrameDenoise = 0,
    RawBurstHdr
};

const char* LegacyProcessorKindName(LegacyProcessorKind kind);

struct LegacyProcessorIdentity {
    LegacyProcessorKind kind = LegacyProcessorKind::MultiFrameDenoise;
    std::string algorithmId;
    std::uint32_t algorithmVersion = 0;
    std::string processorContractId;
    std::uint32_t processorContractVersion = 0;
    std::string preparationContractId;
    std::uint32_t preparationContractVersion = 0;
    std::string noiseModelContractId;
    std::uint32_t noiseModelContractVersion = 0;
};

LegacyProcessorIdentity CaptureLegacyProcessorIdentity(
    LegacyProcessorKind kind);
bool ValidateLegacyProcessorIdentity(
    const LegacyProcessorIdentity& identity,
    std::string* error = nullptr);

SampleEvidence AdaptLegacySampleEvidence(std::uint8_t preparedFlags);

bool AdaptLegacyPreparedFrame(
    LegacyProcessorKind producer,
    const std::string& stableFrameId,
    const Mfd::PreparedRawFrame& frame,
    std::shared_ptr<Mfd::NormalizedTileCache> tileCache,
    PreparedMeasurementSource& result,
    std::string* error = nullptr);

bool AdaptLegacyNoiseModel(
    const Mfd::NoiseModel& model,
    NoiseModelReference& result,
    std::string* error = nullptr);

} // namespace Raw::MultiFrame
