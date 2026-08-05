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
    std::function<bool()> shouldCancel;
    std::function<void(double)> reportProgress;
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
    std::function<bool()> shouldCancel;
    std::function<void(double)> reportProgress;
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

bool EstimateBidirectionalLocalMotion(
    const BidirectionalLocalMotionRequest& request,
    BidirectionalLocalMotionResult& result,
    std::string* error = nullptr);

} // namespace Raw::Mfd
