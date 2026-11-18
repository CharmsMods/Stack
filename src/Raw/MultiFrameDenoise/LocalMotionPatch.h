#pragma once

#include "LocalMotion.h"

#include <cstddef>
#include <limits>

// Private patch scoring/refinement machinery shared by the discrete search
// and its native subpixel verifier. No editor or host owns these decisions.
namespace Raw::Mfd::Detail {

bool ValidateLevel(const CfaPyramidLevel& level);

struct PatchReferenceGeometry {
    struct Sample {
        RawCoordinate mappedSourceRaw;
        double signal = 0.0;
        double variance = -1.0;
    };
    struct Site {
        const CfaPyramidLevel* reference = nullptr;
        const CfaPyramidLevel* source = nullptr;
        CfaOffset sourceOffset;
    };
    std::array<Site, 4> sites;
    std::vector<Sample> samples;
    std::size_t validSampleCount = 0u;
    const LocalMotionDirectionRequest* request = nullptr;
    RawCoordinate centerRaw;
    std::uint32_t level = 0u;
    std::uint32_t patch = 0u;

    bool Prepare(const LocalMotionDirectionRequest& input, RawCoordinate center,
        std::uint32_t levelIndex, std::uint32_t patchLevelPixels);
    bool Matches(const LocalMotionDirectionRequest& input, RawCoordinate center,
        std::uint32_t levelIndex, std::uint32_t patchLevelPixels) const;
};

struct PatchStatistics {
    bool valid = false;
    std::uint64_t validCount = 0u;
    std::uint64_t nominalCount = 0u;
    double validFraction = 0.0;
    double robustCost = std::numeric_limits<double>::infinity();
    double cappedChiSquared = std::numeric_limits<double>::infinity();
    double flatPhotometricCost = std::numeric_limits<double>::infinity();
    double hessianXX = 0.0;
    double hessianXY = 0.0;
    double hessianYY = 0.0;
    double gradientX = 0.0;
    double gradientY = 0.0;
    std::vector<double> residuals;
    std::array<double,4> siteSquared {};
    std::array<std::uint64_t,4> siteCount {};
};

struct RefinedTile {
    bool valid = false;
    bool observable = false;
    RawCoordinate residualRaw;
    PatchStatistics statistics;
    SymmetricRawCovariance localCovarianceRaw;
    double hessianCondition = std::numeric_limits<double>::infinity();
    double positionSigmaRaw = std::numeric_limits<double>::infinity();
    // The ordinary grid search supplies its distinct runner-up. Verify that
    // its ambiguity evidence survives the common support used by this fit.
    bool ambiguityVerified = false;
    double seedCost = std::numeric_limits<double>::infinity();
    double alternativeCost = std::numeric_limits<double>::infinity();
};

// Nonpositive entries identify samples unavailable somewhere in the allowed
// seed neighborhood. Positive entries freeze the sensor-noise denominator for
// one bounded refinement on the resulting common measured domain.
struct FixedPatchSupport {
    std::vector<double> denominator;
    std::uint64_t validCount = 0u;
};

enum class PatchEvaluation {
    Native,
    CaptureSupport,
    FixedObjective,
    PhysicalOnFixedSupport
};

bool EvaluatePatch(
    const LocalMotionDirectionRequest& request,
    RawCoordinate centerRaw,
    std::uint32_t levelIndex,
    RawCoordinate residualRaw,
    std::uint32_t patchLevelPixels,
    bool calculateNormal,
    bool calculateFlatPhotometricCost,
    PatchStatistics& statistics,
    const PatchReferenceGeometry* geometry = nullptr,
    PatchEvaluation evaluation = PatchEvaluation::Native,
    FixedPatchSupport* support = nullptr);

bool RefineTileSubpixel(
    const LocalMotionDirectionRequest& request,
    RawCoordinate centerRaw,
    RawCoordinate discreteResidualRaw,
    RefinedTile& refined,
    const PatchStatistics* initialStatistics = nullptr,
    FixedPatchSupport* initialSupport = nullptr,
    const RawCoordinate* alternativeResidualRaw = nullptr);

} // namespace Raw::Mfd::Detail
