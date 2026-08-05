#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Reliability.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 6 validation failed: " << message << std::endl;
    }
    return condition;
}

Raw::Mfd::LocalMotionGrid MakeIdentityMotionGrid(
    Raw::Mfd::PixelExtent extent) {
    Raw::Mfd::LocalMotionGrid grid;
    grid.valid = true;
    grid.referenceRawExtent = extent;
    grid.sourceRawExtent = extent;
    grid.globalWarp.centerRaw = {
        0.5 * static_cast<double>(extent.width - 1u),
        0.5 * static_cast<double>(extent.height - 1u)
    };
    grid.width = 2u;
    grid.height = 2u;
    grid.originRawX = 0.0;
    grid.originRawY = 0.0;
    grid.spacingRawX = static_cast<double>(extent.width - 1u);
    grid.spacingRawY = static_cast<double>(extent.height - 1u);
    grid.nodes.resize(4u);
    for (std::uint32_t y = 0u; y < 2u; ++y) {
        for (std::uint32_t x = 0u; x < 2u; ++x) {
            Raw::Mfd::MotionNode& node = grid.nodes[y * 2u + x];
            node.gridX = x;
            node.gridY = y;
            node.centerRaw = {
                grid.spacingRawX * x,
                grid.spacingRawY * y
            };
            node.covarianceRaw = { 0.01, 0.0, 0.01 };
            node.confidence = 1.0;
            node.state = Raw::Mfd::MotionNodeState::Structured;
        }
    }
    grid.structuredCount = 4u;
    return grid;
}

struct EvidencePattern {
    bool movingRegion = false;
    bool hardInvalidSample = false;
};

Raw::Mfd::ReliabilityBuildRequest MakeRequest(
    Raw::Mfd::NoiseModelQuality quality,
    const EvidencePattern& pattern,
    Raw::Mfd::LocalMotionGrid& motion) {
    Raw::Mfd::ReliabilityBuildRequest request;
    request.rawExtent = { 64u, 64u };
    Raw::Mfd::CfaLayout::TryCreate(Raw::CfaPattern::RGGB, request.layout);
    request.motionGrid = &motion;
    request.noiseQuality = quality;
    request.residualEvaluator = [pattern](
        Raw::Mfd::RawCoordinate referenceRaw,
        Raw::Mfd::CfaSite site,
        const Raw::Mfd::LocalMotionFieldSample&,
        Raw::Mfd::ReliabilityResidualSample& sample) {
        const std::uint32_t cellX = static_cast<std::uint32_t>(
            std::floor(referenceRaw.x / 2.0));
        const std::uint32_t cellY = static_cast<std::uint32_t>(
            std::floor(referenceRaw.y / 2.0));
        const bool moving = pattern.movingRegion &&
            cellX >= 12u && cellX <= 19u &&
            cellY >= 12u && cellY <= 19u;
        const bool invalid = pattern.hardInvalidSample &&
            cellX == 8u && cellY == 8u &&
            site == Raw::Mfd::CfaSite::Blue;
        const double sign = ((cellX + 3u * cellY +
            static_cast<std::uint32_t>(site)) & 1u) == 0u ? 1.0 : -1.0;
        sample.valid = true;
        sample.hardValid = !invalid;
        sample.referenceValue = 0.0;
        sample.alternateValue = moving
            ? 4.0
            : sign * Raw::Mfd::kStandardNormalMedianAbsoluteValue;
        sample.referenceGateVariance = 0.5;
        sample.alternateGateVariance = 0.5;
        return true;
    };
    return request;
}

const Raw::Mfd::ReliabilityCell& Cell(
    const Raw::Mfd::ReliabilityMap& map,
    std::uint32_t x,
    std::uint32_t y) {
    return map.cells[static_cast<std::size_t>(
        static_cast<std::uint64_t>(y) * map.cellExtent.width + x)];
}

bool ValidatePatchMapAndNoisePolicy() {
    bool ok = true;
    const Raw::Mfd::Parameters defaults;
    Raw::Mfd::LocalMotionGrid motion = MakeIdentityMotionGrid({ 64u, 64u });
    Raw::Mfd::ReliabilityMap trusted;
    std::string error;
    ok &= Check(Raw::Mfd::BuildReliabilityMap(
            MakeRequest(
                Raw::Mfd::NoiseModelQuality::TrustedMetadata,
                {},
                motion),
            trusted,
            &error),
        "static trusted reliability map failed: " + error);
    ok &= Check(trusted.valid && trusted.frameUsable &&
            trusted.minimumUsableCellCount == 256u &&
            trusted.usableCellCount >= 600u,
        "flat static content did not retain efficient usable coverage");
    const Raw::Mfd::ReliabilityCell& center = Cell(trusted, 16u, 16u);
    ok &= Check(std::abs(center.patchScale - 1.0) < 1.0e-6 &&
            center.patchGate == 1.0 && center.reliability > 0.999,
        "5-by-5 standardized patch statistic is not normalized to unit Gaussian noise");

    Raw::Mfd::ReliabilityMap estimated;
    error.clear();
    ok &= Check(Raw::Mfd::BuildReliabilityMap(
            MakeRequest(
                Raw::Mfd::NoiseModelQuality::EstimatedBurst,
                {},
                motion),
            estimated,
            &error),
        "estimated-noise reliability map failed: " + error);
    Raw::Mfd::ReliabilityMap generic;
    error.clear();
    ok &= Check(Raw::Mfd::BuildReliabilityMap(
            MakeRequest(
                Raw::Mfd::NoiseModelQuality::GenericLowConfidence,
                {},
                motion),
            generic,
            &error),
        "generic-noise reliability map failed: " + error);
    ok &= Check(center.reliability > Cell(estimated, 16u, 16u).reliability &&
            Cell(estimated, 16u, 16u).reliability >
                Cell(generic, 16u, 16u).reliability &&
            generic.noiseConfidence == 0.35,
        "noise-quality confidence policy is not conservative and ordered");
    ok &= Check(generic.valid && generic.frameUsable &&
            generic.usableCellCount == trusted.usableCellCount &&
            Cell(generic, 16u, 16u).reliability > 0.0 &&
            Cell(generic, 16u, 16u).reliability <
                defaults.reliability.frameUsableReliabilityThreshold,
        "generic noise confidence incorrectly prevented spatially safe frame admission");

    Raw::Mfd::ReliabilityMap unavailable;
    error.clear();
    ok &= Check(Raw::Mfd::BuildReliabilityMap(
            MakeRequest(
                Raw::Mfd::NoiseModelQuality::Unavailable,
                {},
                motion),
            unavailable,
            &error) &&
            unavailable.usableCellCount == 0u && !unavailable.frameUsable &&
            (Cell(unavailable, 16u, 16u).rejectionBits &
                Raw::Mfd::ReliabilityRejectMask(
                    Raw::Mfd::ReliabilityRejectBit::NoiseUnavailable)) != 0u,
        "unavailable noise model did not produce a safe reference-only map");
    return ok;
}

bool ValidateMovingMaskErosionAndHardSupport() {
    bool ok = true;
    Raw::Mfd::LocalMotionGrid motion = MakeIdentityMotionGrid({ 64u, 64u });
    Raw::Mfd::ReliabilityMap moving;
    std::string error;
    EvidencePattern movingPattern;
    movingPattern.movingRegion = true;
    ok &= Check(Raw::Mfd::BuildReliabilityMap(
            MakeRequest(
                Raw::Mfd::NoiseModelQuality::TrustedMetadata,
                movingPattern,
                motion),
            moving,
            &error),
        "moving-mask reliability map failed: " + error);
    ok &= Check(Cell(moving, 16u, 16u).rawConfidence == 0.0 &&
            Cell(moving, 16u, 16u).reliability == 0.0,
        "known moving region survived the patch gate");
    ok &= Check(Cell(moving, 11u, 16u).rawConfidence > 0.9 &&
            Cell(moving, 11u, 16u).erodedConfidence == 0.0 &&
            Cell(moving, 10u, 16u).erodedConfidence > 0.9,
        "3-by-3 minimum filter did not produce the documented one-cell boundary protection");

    EvidencePattern invalidPattern;
    invalidPattern.hardInvalidSample = true;
    Raw::Mfd::ReliabilityMap invalid;
    error.clear();
    ok &= Check(Raw::Mfd::BuildReliabilityMap(
            MakeRequest(
                Raw::Mfd::NoiseModelQuality::TrustedMetadata,
                invalidPattern,
                motion),
            invalid,
            &error),
        "hard-invalid sample reliability map failed: " + error);
    const Raw::Mfd::ReliabilityCell& invalidCell = Cell(invalid, 8u, 8u);
    ok &= Check(invalidCell.rawConfidence == 0.0 &&
            (invalidCell.rejectionBits & Raw::Mfd::ReliabilityRejectMask(
                Raw::Mfd::ReliabilityRejectBit::SampleInvalid)) != 0u,
        "clipped/defective source support did not hard-invalidate its Bayer cell");
    return ok;
}

bool ValidatePixelAndAbsoluteGates() {
    bool ok = true;
    Raw::Mfd::Parameters parameters;
    ok &= Check(Raw::Mfd::FlatTopQuinticGate(2.5, 2.5, 5.0) == 1.0 &&
            Raw::Mfd::FlatTopQuinticGate(5.0, 2.5, 5.0) == 0.0 &&
            Raw::Mfd::FlatTopQuinticGate(3.75, 2.5, 5.0) > 0.0,
        "pixel flat-top gate endpoints or transition are invalid");

    Raw::Mfd::CandidateGateInput thin;
    thin.hardValid = true;
    thin.noiseQuality = Raw::Mfd::NoiseModelQuality::TrustedMetadata;
    thin.reliability = 1.0;
    thin.referenceValue = 0.20;
    thin.alternateValue = 0.26;
    thin.referenceGateVariance = 0.00005;
    thin.alternateGateVariance = 0.00005;
    thin.referenceDarkVariance = 0.00001;
    thin.alternateDarkVariance = 0.00001;
    thin.referenceDnStep = 1.0 / 4095.0;
    thin.alternateDnStep = 1.0 / 4095.0;
    Raw::Mfd::CandidateGateResult gate;
    std::string error;
    ok &= Check(!Raw::Mfd::EvaluateCandidateGate(
            thin, parameters, gate, &error) &&
            gate.failure == Raw::Mfd::CandidateGateFailure::PixelOutlier,
        "one-pixel/thin-feature outlier survived the redescending gate");

    Raw::Mfd::CandidateGateInput wrongNoise = thin;
    wrongNoise.noiseQuality =
        Raw::Mfd::NoiseModelQuality::GenericLowConfidence;
    wrongNoise.referenceGateVariance = 0.10;
    wrongNoise.alternateGateVariance = 0.10;
    wrongNoise.referenceDarkVariance = 1.0e-8;
    wrongNoise.alternateDarkVariance = 1.0e-8;
    error.clear();
    ok &= Check(!Raw::Mfd::EvaluateCandidateGate(
            wrongNoise, parameters, gate, &error) &&
            gate.failure ==
                Raw::Mfd::CandidateGateFailure::AbsoluteSafetyFailure &&
            gate.pixelGate == 1.0 && gate.absoluteSafetyApplied,
        "overestimated low-confidence noise bypassed the absolute safety gate");

    wrongNoise.alternateValue = 0.21;
    error.clear();
    ok &= Check(Raw::Mfd::EvaluateCandidateGate(
            wrongNoise, parameters, gate, &error) && gate.valid &&
            gate.absoluteSafetyPassed && gate.gate > 0.99,
        "safe low-confidence candidate was rejected: " + error);
    Raw::Mfd::CandidateGateInput overflow = wrongNoise;
    overflow.referenceDarkVariance = std::numeric_limits<double>::max();
    overflow.alternateDarkVariance = std::numeric_limits<double>::max();
    Raw::Mfd::CandidateGateResult overflowResult;
    error.clear();
    ok &= Check(!Raw::Mfd::EvaluateCandidateGate(
            overflow,
            parameters,
            overflowResult,
            &error) &&
            overflowResult.failure ==
                Raw::Mfd::CandidateGateFailure::InvalidNumericInput,
        "overflowing absolute-safety arithmetic did not fail closed");
    return ok;
}

bool ValidateTiledStorageDecisionIdentity() {
    bool ok = true;
    Raw::Mfd::LocalMotionGrid motion = MakeIdentityMotionGrid({ 64u, 64u });
    EvidencePattern pattern;
    pattern.movingRegion = true;
    pattern.hardInvalidSample = true;
    Raw::Mfd::ReliabilityMap map;
    std::string error;
    ok &= Check(Raw::Mfd::BuildReliabilityMap(
            MakeRequest(
                Raw::Mfd::NoiseModelQuality::EstimatedBurst,
                pattern,
                motion),
            map,
            &error),
        "storage test reliability map failed: " + error);
    Raw::Mfd::ReliabilityStore uint16Store;
    Raw::Mfd::ReliabilityStore floatStore;
    ok &= Check(Raw::Mfd::BuildReliabilityStore(
            map,
            Raw::Mfd::ReliabilityStorageFormat::Uint16Unorm,
            7u,
            uint16Store,
            &error) &&
            Raw::Mfd::BuildReliabilityStore(
                map,
                Raw::Mfd::ReliabilityStorageFormat::Float32,
                7u,
                floatStore,
                &error),
        "tiled uint16/float reliability stores failed: " + error);
    ok &= Check(uint16Store.tilesX == 5u && uint16Store.tilesY == 5u &&
            uint16Store.tiles.size() == 25u,
        "row-major reliability tiling or partial edge tiles are wrong");
    for (std::uint32_t y = 0u; y < map.cellExtent.height; ++y) {
        for (std::uint32_t x = 0u; x < map.cellExtent.width; ++x) {
            double quantized = 0.0;
            double floating = 0.0;
            std::uint16_t quantizedBits = 0u;
            std::uint16_t floatBits = 0u;
            error.clear();
            const bool read = Raw::Mfd::ReadReliabilityStoreCell(
                    uint16Store, x, y, quantized, &quantizedBits, &error) &&
                Raw::Mfd::ReadReliabilityStoreCell(
                    floatStore, x, y, floating, &floatBits, &error);
            ok &= Check(read,
                "reliability tile read failed: " + error);
            if (!read) continue;
            ok &= Check(std::abs(quantized - floating) <=
                    (0.5 / 65535.0 + 1.0e-7) &&
                    (quantized > 0.20) == (floating > 0.20) &&
                    quantizedBits == floatBits,
                "uint16 reliability changed a float decision or rejection bit");
        }
    }
    ok &= Check(Raw::Mfd::QuantizeReliabilityUnorm16(0.0) == 0u &&
            Raw::Mfd::QuantizeReliabilityUnorm16(1.0) == 65535u &&
            std::abs(Raw::Mfd::DecodeReliabilityUnorm16(
                Raw::Mfd::QuantizeReliabilityUnorm16(0.5)) - 0.5) < 1.0e-5,
        "deterministic uint16 reliability quantization endpoints are wrong");
    Raw::Mfd::ReliabilityStore malformedStore = uint16Store;
    malformedStore.contractId = "unexpected-contract";
    double ignoredReliability = 0.0;
    error.clear();
    ok &= Check(!Raw::Mfd::ReadReliabilityStoreCell(
            malformedStore,
            0u,
            0u,
            ignoredReliability,
            nullptr,
            &error),
        "malformed reliability-store contracts must fail closed");
    error.clear();
    ok &= Check(!Raw::Mfd::BuildReliabilityStore(
            map,
            static_cast<Raw::Mfd::ReliabilityStorageFormat>(255u),
            7u,
            malformedStore,
            &error),
        "unknown reliability storage formats must fail closed");
    return ok;
}

} // namespace

bool ValidateMfdPhase6Reliability() {
    bool ok = true;
    ok &= ValidatePatchMapAndNoisePolicy();
    ok &= ValidateMovingMaskErosionAndHardSupport();
    ok &= ValidatePixelAndAbsoluteGates();
    ok &= ValidateTiledStorageDecisionIdentity();
    if (ok) {
        std::cout << "MFD Phase 6 reliability validation passed." << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
