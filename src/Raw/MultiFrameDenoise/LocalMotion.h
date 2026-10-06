#pragma once

#include "Raw/MultiFrameDenoise/GlobalRegistration.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kLocalMotionContractVersion = 1;
inline constexpr const char* kLocalMotionContractId =
    "ra-cfa-local-motion-covariance-v1";

struct CfaPyramidBasePlane {
    CfaSite site = CfaSite::Red;
    PixelExtent extent;
    std::vector<double> signal;
    std::vector<double> variance;
    std::vector<std::uint8_t> validMask;
};

struct CfaPyramidLevel {
    CfaSite site = CfaSite::Red;
    std::uint32_t level = 0u;
    PixelExtent extent;
    double rawPixelsPerLevelPixel = 2.0;
    std::vector<double> signal;
    std::vector<double> variance;
    std::vector<std::uint8_t> validMask;
};

struct CfaPlanePyramid {
    std::uint32_t contractVersion = kLocalMotionContractVersion;
    std::string contractId = kLocalMotionContractId;
    CfaLayout layout;
    PixelExtent rawExtent;
    std::uint32_t levelCount = 0u;
    std::array<std::vector<CfaPyramidLevel>, 4> planes;
};

bool BuildCfaPlanePyramid(
    const CfaLayout& layout,
    PixelExtent rawExtent,
    const std::array<CfaPyramidBasePlane, 4>& basePlanes,
    const RegistrationParameters& parameters,
    CfaPlanePyramid& result,
    std::string* error = nullptr);

const CfaPyramidLevel* FindCfaPyramidLevel(
    const CfaPlanePyramid& pyramid,
    CfaSite site,
    std::uint32_t level);

enum class MotionNodeState : std::uint8_t {
    Structured = 0,
    FlatSafe,
    Rejected
};

const char* MotionNodeStateName(MotionNodeState state);

enum class MotionNodeRejectReason : std::uint8_t {
    None = 0,
    InsufficientCoverage,
    NoDiscreteCandidate,
    AmbiguousMatch,
    SubpixelFailure,
    UnobservableUnsafe,
    InvalidCovariance,
    ForwardBackwardUnavailable,
    ForwardBackwardFailure,
    SourceBorder,
    MotionFieldAmbiguous,
    NumericalFailure
};

const char* MotionNodeRejectReasonName(MotionNodeRejectReason reason);

struct MotionNode {
    std::uint32_t gridX = 0u;
    std::uint32_t gridY = 0u;
    RawCoordinate centerRaw;
    RawCoordinate residualRaw;
    RawCoordinate discreteResidualRaw;
    SymmetricRawCovariance covarianceRaw;
    MotionNodeState state = MotionNodeState::Rejected;
    MotionNodeRejectReason rejectReason = MotionNodeRejectReason::None;
    double confidence = 0.0;
    double robustCost = 0.0;
    double secondBestCost = 0.0;
    double uniqueness = 0.0;
    double validFraction = 0.0;
    double cappedChiSquared = 0.0;
    double positionalSigmaRaw = 0.0;
    double hessianCondition = 0.0;
    double forwardBackwardMahalanobis = 0.0;
    double forwardBackwardEuclideanRaw = 0.0;
    RawCoordinate forwardBackwardErrorRaw;
};

struct LocalMotionOptions {
    RegistrationParameters registration;
};

// Discrete coarse-to-fine search is the large, embarrassingly parallel part
// of local registration.  An optional accelerator may score a batch in FP32;
// the CPU still verifies a conservative shortlist in FP64 before accepting a
// best/second-best pair, and every later covariance/safety decision remains
// on the reference path.
struct LocalMotionDiscreteCandidate {
    RawCoordinate centerRaw;
    RawCoordinate residualRaw;
    std::uint32_t level = 0u;
    std::uint32_t patchLevelPixels = 0u;
};

struct LocalMotionDiscreteScore {
    bool valid = false;
    std::uint64_t validCount = 0u;
    double validFraction = 0.0;
    double robustCost = 0.0;
    double cappedChiSquared = 0.0;
};

struct LocalMotionDirectionRequest;
struct LocalMotionGrid;
using LocalMotionDiscreteBatchEvaluator = std::function<bool(
    const LocalMotionDirectionRequest& request,
    const std::vector<LocalMotionDiscreteCandidate>& candidates,
    std::vector<LocalMotionDiscreteScore>& scores,
    std::string& error)>;

using GlobalWarpCovarianceEvaluator = std::function<bool(
    RawCoordinate referenceRaw,
    SymmetricRawCovariance& covariance)>;

struct LocalMotionDirectionRequest {
    const CfaPlanePyramid* reference = nullptr;
    const CfaPlanePyramid* source = nullptr;
    AffineModel globalWarp;
    double exposureScale = 1.0;
    GlobalWarpCovarianceEvaluator globalCovariance;
    LocalMotionOptions options;
    // Independent nodes inside one wavefront/refinement pass may use these
    // workers. Search dependencies between wavefronts remain ordered.
    std::uint32_t workerCount = 1u;
    LocalMotionDiscreteBatchEvaluator evaluateDiscreteCandidates;
    std::function<bool()> shouldCancel;
    std::function<void(double)> reportProgress;
    std::function<void(const LocalMotionGrid&,bool)> reportObservation;
};

enum class LocalMotionFailure : std::uint8_t {
    None = 0,
    InvalidInput,
    PyramidMismatch,
    NoGrid,
    NoUsableNodes,
    NumericalFailure
};

const char* LocalMotionFailureName(LocalMotionFailure failure);

struct LocalMotionGrid {
    bool valid = false;
    LocalMotionFailure failure = LocalMotionFailure::None;
    std::string message;
    PixelExtent referenceRawExtent;
    PixelExtent sourceRawExtent;
    AffineModel globalWarp;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    double originRawX = 0.0;
    double originRawY = 0.0;
    double spacingRawX = 0.0;
    double spacingRawY = 0.0;
    std::uint64_t structuredCount = 0u;
    std::uint64_t flatSafeCount = 0u;
    std::uint64_t rejectedCount = 0u;
    std::vector<MotionNode> nodes;
};

bool EstimateLocalMotionDirection(
    const LocalMotionDirectionRequest& request,
    LocalMotionGrid& result,
    std::string* error = nullptr);

struct LocalMotionFieldSample {
    bool valid = false;
    MotionNodeRejectReason rejectReason = MotionNodeRejectReason::None;
    std::string message;
    RawCoordinate referenceRaw;
    RawCoordinate residualRaw;
    RawCoordinate sourceRaw;
    SymmetricRawCovariance covarianceRaw;
    SymmetricRawCovariance disagreementCovarianceRaw;
    double gridConfidence = 0.0;
    double disagreementSigmaRaw = 0.0;
    double alignmentConfidence = 0.0;
};

bool EvaluateLocalMotionField(
    const LocalMotionGrid& grid,
    RawCoordinate referenceRaw,
    const LocalMotionOptions& options,
    LocalMotionFieldSample& result,
    std::string* error = nullptr);

bool InvertAffineModel(
    const AffineModel& model,
    AffineModel& inverse,
    std::string* error = nullptr);

struct BidirectionalLocalMotionRequest {
    const CfaPlanePyramid* reference = nullptr;
    const CfaPlanePyramid* alternate = nullptr;
    AffineModel referenceToAlternate;
    double exposureScale = 1.0;
    GlobalWarpCovarianceEvaluator forwardGlobalCovariance;
    GlobalWarpCovarianceEvaluator reverseGlobalCovariance;
    LocalMotionOptions options;
    LocalMotionDiscreteBatchEvaluator evaluateDiscreteCandidates;
    // Forward and reverse grids are independent until closure evaluation.
    // A value of two or more permits those two exact computations to run in
    // parallel without changing either grid's deterministic math.
    std::uint32_t workerCount = 1u;
    std::function<bool()> shouldCancel;
    std::function<void(double)> reportProgress;
    std::function<void(const LocalMotionGrid&,bool)> reportObservation;
};

struct BidirectionalLocalMotionResult {
    bool valid = false;
    LocalMotionFailure failure = LocalMotionFailure::None;
    std::string message;
    LocalMotionGrid forward;
    LocalMotionGrid reverse;
    std::uint64_t closureAcceptedCount = 0u;
    std::uint64_t closureRejectedCount = 0u;
};

// One local refinement from an already accepted warp, without another global
// or discrete search. Failure leaves the caller's original field authoritative.
bool RefineBidirectionalMotionPoint(const BidirectionalLocalMotionRequest&,
    RawCoordinate referenceRaw,RawCoordinate sourceRaw,MotionNode& result);

bool EstimateBidirectionalLocalMotion(
    const BidirectionalLocalMotionRequest& request,
    BidirectionalLocalMotionResult& result,
    std::string* error = nullptr);

} // namespace Raw::Mfd
