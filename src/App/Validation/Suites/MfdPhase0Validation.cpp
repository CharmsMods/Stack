#include "App/Validation/ValidationSuites.h"

#include "Persistence/RawProjectModel.h"
#include "Raw/MultiFrameDenoise/Contracts.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 0 validation failed: " << message << std::endl;
    }
    return condition;
}

bool NearlyEqual(double a, double b, double tolerance = 1.0e-12) {
    return std::abs(a - b) <= tolerance;
}

std::uint64_t StableFloatHash(const std::vector<float>& samples) {
    std::uint64_t hash = 14695981039346656037ull;
    for (float sample : samples) {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(sample), "Unexpected float width");
        std::memcpy(&bits, &sample, sizeof(bits));
        for (unsigned int byte = 0; byte < 4; ++byte) {
            hash ^= static_cast<std::uint8_t>((bits >> (byte * 8u)) & 0xffu);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

bool EqualBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() &&
        (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

using Raw::Mfd::CfaSite;

struct GoldenPlaneFixture {
    Raw::CfaPattern pattern = Raw::CfaPattern::Unknown;
    std::array<std::vector<float>, 4> planes;
};

std::size_t SiteIndex(CfaSite site) {
    return static_cast<std::size_t>(site);
}

bool ValidateGoldenPlanes() {
    const Raw::Mfd::PixelExtent extent { 5, 3 };
    const std::vector<float> mosaic {
        0.0f, 1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f, 9.0f,
        10.0f, 11.0f, 12.0f, 13.0f, 14.0f
    };
    const std::vector<GoldenPlaneFixture> fixtures {
        {
            Raw::CfaPattern::RGGB,
            {{
                { 0.0f, 2.0f, 4.0f, 10.0f, 12.0f, 14.0f },
                { 1.0f, 3.0f, 11.0f, 13.0f },
                { 5.0f, 7.0f, 9.0f },
                { 6.0f, 8.0f }
            }}
        },
        {
            Raw::CfaPattern::BGGR,
            {{
                { 6.0f, 8.0f },
                { 1.0f, 3.0f, 11.0f, 13.0f },
                { 5.0f, 7.0f, 9.0f },
                { 0.0f, 2.0f, 4.0f, 10.0f, 12.0f, 14.0f }
            }}
        },
        {
            Raw::CfaPattern::GBRG,
            {{
                { 5.0f, 7.0f, 9.0f },
                { 0.0f, 2.0f, 4.0f, 10.0f, 12.0f, 14.0f },
                { 6.0f, 8.0f },
                { 1.0f, 3.0f, 11.0f, 13.0f }
            }}
        },
        {
            Raw::CfaPattern::GRBG,
            {{
                { 1.0f, 3.0f, 11.0f, 13.0f },
                { 0.0f, 2.0f, 4.0f, 10.0f, 12.0f, 14.0f },
                { 6.0f, 8.0f },
                { 5.0f, 7.0f, 9.0f }
            }}
        }
    };

    bool ok = true;
    for (const GoldenPlaneFixture& fixture : fixtures) {
        Raw::Mfd::CfaLayout layout;
        ok &= Check(
            Raw::Mfd::CfaLayout::TryCreate(fixture.pattern, layout),
            "golden fixture CFA layout could not be created");
        for (CfaSite site : {
                 CfaSite::Red,
                 CfaSite::Green0,
                 CfaSite::Green1,
                 CfaSite::Blue }) {
            std::vector<float> plane;
            std::string error;
            ok &= Check(
                Raw::Mfd::ExtractCfaPlane(
                    mosaic, extent, layout, site, plane, &error),
                "golden plane extraction failed: " + error);
            ok &= Check(
                plane == fixture.planes[SiteIndex(site)],
                std::string("hand-computed plane mismatch for ") +
                    Raw::CfaPatternName(fixture.pattern) + "/" +
                    Raw::Mfd::CfaSiteName(site));
        }
    }
    return ok;
}

bool ValidateCfaParityAndCoordinates() {
    const std::array<Raw::CfaPattern, 4> patterns {
        Raw::CfaPattern::RGGB,
        Raw::CfaPattern::BGGR,
        Raw::CfaPattern::GBRG,
        Raw::CfaPattern::GRBG
    };
    bool ok = true;
    Raw::Mfd::CfaLayout invalid;
    ok &= Check(
        !Raw::Mfd::CfaLayout::TryCreate(Raw::CfaPattern::Unknown, invalid),
        "unknown CFA pattern must not create a valid MFD layout");

    for (Raw::CfaPattern pattern : patterns) {
        Raw::Mfd::CfaLayout sensorLayout;
        ok &= Check(
            Raw::Mfd::CfaLayout::TryCreate(pattern, sensorLayout),
            "supported CFA pattern could not create a layout");

        for (std::int64_t activeTop = 0; activeTop < 4; ++activeTop) {
            for (std::int64_t activeLeft = 0; activeLeft < 4; ++activeLeft) {
                const Raw::Mfd::CfaLayout active =
                    sensorLayout.ShiftedToActiveArea(activeLeft, activeTop);
                ok &= Check(active.IsValid(), "active-area CFA phase became invalid");
                for (std::int64_t y = 0; y < 6; ++y) {
                    for (std::int64_t x = 0; x < 6; ++x) {
                        ok &= Check(
                            active.SiteAt(x, y) ==
                                sensorLayout.SiteAt(activeLeft + x, activeTop + y),
                            "active-area shift changed CFA identity");
                    }
                }
            }
        }

        for (std::uint64_t height = 1; height <= 9; ++height) {
            for (std::uint64_t width = 1; width <= 9; ++width) {
                const Raw::Mfd::PixelExtent extent { width, height };
                std::vector<float> mosaic(static_cast<std::size_t>(width * height));
                for (std::uint64_t y = 0; y < height; ++y) {
                    for (std::uint64_t x = 0; x < width; ++x) {
                        mosaic[static_cast<std::size_t>(y * width + x)] =
                            static_cast<float>(100u * y + x);
                        const CfaSite site = sensorLayout.SiteAt(
                            static_cast<std::int64_t>(x),
                            static_cast<std::int64_t>(y));
                        const Raw::Mfd::CfaOffset offset = sensorLayout.OffsetFor(site);
                        ok &= Check(
                            (x % 2u) == static_cast<std::uint64_t>(offset.x) &&
                                (y % 2u) == static_cast<std::uint64_t>(offset.y),
                            "CFA site offset disagrees with raw parity");

                        const Raw::Mfd::CfaPlaneCoordinate plane =
                            sensorLayout.RawToPlane(
                                { static_cast<double>(x), static_cast<double>(y) },
                                site);
                        ok &= Check(
                            NearlyEqual(plane.x, std::floor(plane.x)) &&
                                NearlyEqual(plane.y, std::floor(plane.y)),
                            "exact CFA raw sample did not map to an integer plane sample");
                        const Raw::Mfd::RawCoordinate raw = sensorLayout.PlaneToRaw(plane);
                        ok &= Check(
                            NearlyEqual(raw.x, static_cast<double>(x)) &&
                                NearlyEqual(raw.y, static_cast<double>(y)),
                            "raw-plane-raw coordinate round trip changed a sample");

                        for (std::uint32_t level = 0; level < 6; ++level) {
                            const Raw::Mfd::CfaPyramidCoordinate pyramid =
                                sensorLayout.PlaneToPyramid(plane, level);
                            const Raw::Mfd::CfaPlaneCoordinate restored =
                                sensorLayout.PyramidToPlane(pyramid);
                            ok &= Check(
                                restored.site == site &&
                                    NearlyEqual(restored.x, plane.x) &&
                                    NearlyEqual(restored.y, plane.y),
                                "plane-pyramid-plane coordinate round trip changed a sample");
                        }
                    }
                }

                std::uint64_t totalPlaneSamples = 0;
                for (CfaSite site : {
                         CfaSite::Red,
                         CfaSite::Green0,
                         CfaSite::Green1,
                         CfaSite::Blue }) {
                    const Raw::Mfd::PixelExtent planeExtent =
                        sensorLayout.PlaneExtent(site, extent);
                    totalPlaneSamples += planeExtent.width * planeExtent.height;
                    for (std::uint64_t planeY = 0; planeY < planeExtent.height; ++planeY) {
                        for (std::uint64_t planeX = 0; planeX < planeExtent.width; ++planeX) {
                            float sample = -1.0f;
                            const Raw::Mfd::CfaPlanePixel coordinate {
                                static_cast<std::int64_t>(planeX),
                                static_cast<std::int64_t>(planeY),
                                site
                            };
                            ok &= Check(
                                Raw::Mfd::TryReadExactSameCfaSample(
                                    mosaic, extent, sensorLayout, coordinate, sample),
                                "identity same-CFA sample failed");
                            const Raw::Mfd::RawCoordinate raw =
                                sensorLayout.PlanePixelToRaw(coordinate);
                            ok &= Check(
                                sensorLayout.SiteAt(
                                    static_cast<std::int64_t>(raw.x),
                                    static_cast<std::int64_t>(raw.y)) == site,
                                "same-CFA sample crossed into a different CFA site");
                            ok &= Check(
                                sample == mosaic[static_cast<std::size_t>(
                                    static_cast<std::uint64_t>(raw.y) * width +
                                    static_cast<std::uint64_t>(raw.x))],
                                "identity same-CFA sample changed its value");
                        }
                    }
                }
                ok &= Check(
                    totalPlaneSamples == width * height,
                    "four CFA plane extents did not partition the packed mosaic");
            }
        }

        for (CfaSite site : {
                 CfaSite::Red,
                 CfaSite::Green0,
                 CfaSite::Green1,
                 CfaSite::Blue }) {
            const Raw::Mfd::RawCoordinate base = sensorLayout.PlaneToRaw({ 2.0, 3.0, site });
            const Raw::Mfd::CfaPlaneCoordinate shifted = sensorLayout.RawToPlane(
                { base.x + 1.0, base.y + 1.0 }, site);
            ok &= Check(
                NearlyEqual(shifted.x, 2.5) && NearlyEqual(shifted.y, 3.5),
                "one raw-pixel translation must equal one-half plane pixel");
        }
    }
    return ok;
}

bool ValidateContractsAndParameters() {
    bool ok = true;
    ok &= Check(
        Raw::Mfd::IsCanonical(Raw::Mfd::CanonicalNormalizedMosaicContract()),
        "canonical normalized-mosaic contract is internally inconsistent");
    ok &= Check(
        Raw::Mfd::IsCanonical(Raw::Mfd::CanonicalOutputContract()),
        "canonical MFD output contract is internally inconsistent");

    Raw::Mfd::Parameters defaults;
    std::string error;
    ok &= Check(
        Raw::Mfd::ValidateParameters(defaults, &error),
        "default RA-CFA V1 parameters are invalid: " + error);
    const nlohmann::json serialized = Raw::Mfd::SerializeParameters(defaults);
    Raw::Mfd::Parameters restored;
    error.clear();
    ok &= Check(
        Raw::Mfd::DeserializeParameters(serialized, restored, &error),
        "serialized RA-CFA V1 parameters did not deserialize: " + error);
    ok &= Check(
        Raw::Mfd::SerializeParameters(restored).dump() == serialized.dump(),
        "RA-CFA V1 parameter serialization is not a stable logical round trip");

    nlohmann::json legacy = serialized;
    legacy["schemaVersion"] = Raw::Mfd::kLegacyParameterSchemaVersion;
    for (const char* key : {
            "minimumTileValidFraction",
            "minimumStructuredSamples",
            "localHessianConditionLimit",
            "localHessianAbsoluteDamping",
            "localCovarianceRegularization",
            "localNumericalVarianceFloor",
            "subpixelConvergencePlanePixels",
            "maximumSubpixelCostIncreases",
            "cappedResidualSquared",
            "flatSafeUnobservableSigmaRawPixels",
            "covarianceResidualScaleFloor",
            "candidateTieTolerance",
            "interpolationWeightEpsilon" }) {
        legacy["registration"].erase(key);
    }
    for (const char* key : {
            "storageTileCells",
            "frameUsableReliabilityThreshold",
            "frameUsableMaximumFraction",
            "frameUsableMinimumCells",
            "frameUsableMinimumFraction" }) {
        legacy["reliability"].erase(key);
    }
    error.clear();
    ok &= Check(
        Raw::Mfd::DeserializeParameters(legacy, restored, &error) &&
            restored.schemaVersion == Raw::Mfd::kParameterSchemaVersion &&
            restored.registration.minimumTileValidFraction == 0.70,
        "schema-1 MFD parameters did not migrate to the current MFD schema: " + error);

    nlohmann::json phase5 = serialized;
    phase5["schemaVersion"] = Raw::Mfd::kPhase5ParameterSchemaVersion;
    for (const char* key : {
            "storageTileCells",
            "frameUsableReliabilityThreshold",
            "frameUsableMaximumFraction",
            "frameUsableMinimumCells",
            "frameUsableMinimumFraction" }) {
        phase5["reliability"].erase(key);
    }
    error.clear();
    ok &= Check(
        Raw::Mfd::DeserializeParameters(phase5, restored, &error) &&
            restored.schemaVersion == Raw::Mfd::kParameterSchemaVersion &&
            restored.reliability.storageTileCells == 256u,
        "schema-2 MFD parameters did not migrate to the Phase 6 schema: " + error);

    nlohmann::json missing = serialized;
    missing["registration"].erase("keysBicubicParameter");
    error.clear();
    ok &= Check(
        !Raw::Mfd::DeserializeParameters(missing, restored, &error),
        "missing semantic MFD parameters must not be silently defaulted");

    nlohmann::json invalid = serialized;
    invalid["reliability"]["patchFullWeightSigma"] = 6.0;
    error.clear();
    ok &= Check(
        !Raw::Mfd::DeserializeParameters(invalid, restored, &error),
        "reversed reliability thresholds must be rejected");

    const nlohmann::json operation = Project::MakeDefaultMfdOperationSettings();
    ok &= Check(
        operation.value("schemaVersion", 0u) == Project::kMfdOperationSchemaVersion &&
            operation.value("algorithmId", std::string()) == Raw::Mfd::kAlgorithmId &&
            operation.value("algorithmVersion", 0u) == Raw::Mfd::kAlgorithmVersion &&
            operation.value("experimentalAlignmentMode", std::string()) ==
                "full" &&
            operation.value("experimentalMemoryBudgetGiB", -1.0) == 0.0 &&
            !operation.value("processingImplemented", true),
        "new MFD project settings do not pin RA-CFA V1 with full alignment, automatic memory budgeting, and unavailable graph output");
    return ok;
}

bool ValidateReasonsAndExactFallback() {
    bool ok = true;
    const std::array<Raw::Mfd::DecisionReason, 7> fallbackReasons {
        Raw::Mfd::DecisionReason::NoiseModelUnavailable,
        Raw::Mfd::DecisionReason::ReferenceClipped,
        Raw::Mfd::DecisionReason::InvalidReferenceGain,
        Raw::Mfd::DecisionReason::NoValidCandidate,
        Raw::Mfd::DecisionReason::AlternateWeightInsufficient,
        Raw::Mfd::DecisionReason::AllAlternatesRejected,
        Raw::Mfd::DecisionReason::NumericalFallback
    };
    for (Raw::Mfd::DecisionReason reason : fallbackReasons) {
        ok &= Check(
            Raw::Mfd::IsExactReferenceFallbackReason(reason),
            std::string("reason must be classified as exact reference fallback: ") +
                Raw::Mfd::DecisionReasonName(reason));
    }

    const std::vector<float> reference {
        -0.0f,
        -0.125f,
        0.0f,
        0.25f,
        1.0f,
        1.25f
    };
    const Raw::Mfd::ExactReferenceFallback fallback =
        Raw::Mfd::MakeExactReferenceFallback(
            reference,
            Raw::Mfd::DecisionReason::AllAlternatesRejected);
    ok &= Check(fallback.exactReferenceCopy, "fallback did not record an exact reference copy");
    ok &= Check(EqualBits(reference, fallback.normalizedMosaic),
        "exact reference fallback changed one or more float bits");
    ok &= Check(
        StableFloatHash(fallback.normalizedMosaic) == 0x252211ffb05fd495ull,
        "exact reference fallback golden hash changed");
    return ok;
}

} // namespace

bool ValidateMfdPhase0Contracts() {
    bool ok = true;
    ok &= ValidateGoldenPlanes();
    ok &= ValidateCfaParityAndCoordinates();
    ok &= ValidateContractsAndParameters();
    ok &= ValidateReasonsAndExactFallback();
    if (ok) {
        std::cout << "MFD Phase 0 contract validation passed." << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
