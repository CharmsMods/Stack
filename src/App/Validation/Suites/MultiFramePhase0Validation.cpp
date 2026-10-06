#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrame/PublicationFence.h"
#include "Raw/MultiFrame/ReferenceValidation.h"

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
        std::cerr << "Unified MultiFrame Phase 0 validation failed: "
                  << message << std::endl;
    }
    return condition;
}

std::size_t PixelIndex(
    std::uint64_t x,
    std::uint64_t y,
    std::uint64_t width) {
    return static_cast<std::size_t>(y * width + x);
}

std::size_t RgbIndex(std::size_t pixel, std::size_t channel) {
    return pixel * 3u + channel;
}

Raw::MultiFrame::CfaDomainIdentity MakeDomain(
    Raw::CfaPattern pattern,
    std::uint64_t width,
    std::uint64_t height) {
    Raw::MultiFrame::CfaDomainIdentity domain;
    std::string error;
    if (!Raw::MultiFrame::TryCreateCfaDomainIdentity(
            pattern,
            {
                0,
                0,
                static_cast<int>(height),
                static_cast<int>(width)
            },
            { width, height },
            domain,
            &error)) {
        std::cerr << "Could not create Phase 0 domain: " << error << std::endl;
    }
    return domain;
}

std::vector<Raw::MultiFrame::SampleEvidence> ExactEvidence(std::size_t count) {
    return std::vector<Raw::MultiFrame::SampleEvidence>(
        count,
        Raw::MultiFrame::SampleEvidence {});
}

float PhaseBase(Raw::MultiFrame::CfaPhase phase) {
    switch (phase) {
        case Raw::MultiFrame::CfaPhase::R: return -0.25f;
        case Raw::MultiFrame::CfaPhase::G0: return 0.25f;
        case Raw::MultiFrame::CfaPhase::G1: return 0.75f;
        case Raw::MultiFrame::CfaPhase::B: return 1.25f;
        default: return 0.0f;
    }
}

std::vector<float> MakeSignedOverrangeFixture(
    const Raw::MultiFrame::CfaDomainIdentity& domain) {
    const std::uint64_t width = domain.activeExtent.width;
    const std::uint64_t height = domain.activeExtent.height;
    std::vector<float> result(static_cast<std::size_t>(width * height));
    for (std::uint64_t y = 0; y < height; ++y) {
        for (std::uint64_t x = 0; x < width; ++x) {
            result[PixelIndex(x, y, width)] = PhaseBase(
                Raw::MultiFrame::PhaseAt(domain, x, y)) +
                0.01f * static_cast<float>(x) +
                0.02f * static_cast<float>(y);
        }
    }
    return result;
}

std::uint64_t HashBytes(
    std::uint64_t hash,
    const void* data,
    std::size_t byteCount) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < byteCount; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t HashReferenceImage(
    const Raw::MultiFrame::ReferenceRgbImage& image) {
    std::uint64_t hash = 14695981039346656037ull;
    hash = HashBytes(
        hash,
        image.linearRgb.data(),
        image.linearRgb.size() * sizeof(float));
    hash = HashBytes(
        hash,
        image.validChannelMask.data(),
        image.validChannelMask.size() * sizeof(std::uint8_t));
    hash = HashBytes(
        hash,
        image.supportCounts.data(),
        image.supportCounts.size() * sizeof(image.supportCounts.front()));
    return hash;
}

bool EqualImageBits(
    const Raw::MultiFrame::ReferenceRgbImage& left,
    const Raw::MultiFrame::ReferenceRgbImage& right) {
    return left.extent.width == right.extent.width &&
        left.extent.height == right.extent.height &&
        left.linearRgb.size() == right.linearRgb.size() &&
        left.validChannelMask == right.validChannelMask &&
        left.supportCounts == right.supportCounts &&
        (left.linearRgb.empty() || std::memcmp(
            left.linearRgb.data(),
            right.linearRgb.data(),
            left.linearRgb.size() * sizeof(float)) == 0);
}

bool ValidateFixedReferenceDemosaic() {
    const std::array<Raw::CfaPattern, 4> patterns {
        Raw::CfaPattern::RGGB,
        Raw::CfaPattern::BGGR,
        Raw::CfaPattern::GBRG,
        Raw::CfaPattern::GRBG
    };
    // These hashes freeze the v1 demosaic. A deliberate numerical change must
    // bump kReferenceDemosaicVersion and replace the reviewed hash set.
    const std::array<std::uint64_t, 4> expectedHashes {
        16662902206303912070ull,
        6337448957128988233ull,
        7774237363504209056ull,
        13804424047458900388ull
    };

    bool ok = true;
    for (std::size_t patternIndex = 0;
         patternIndex < patterns.size();
         ++patternIndex) {
        const Raw::MultiFrame::CfaDomainIdentity domain =
            MakeDomain(patterns[patternIndex], 8u, 6u);
        const std::vector<float> mosaic = MakeSignedOverrangeFixture(domain);
        const std::vector<Raw::MultiFrame::SampleEvidence> evidence =
            ExactEvidence(mosaic.size());
        Raw::MultiFrame::ReferenceRgbImage first;
        Raw::MultiFrame::ReferenceRgbImage second;
        std::string error;
        ok &= Check(
            Raw::MultiFrame::FixedReferenceDemosaic(
                mosaic, evidence, domain, first, &error),
            "fixed demosaic failed: " + error);
        error.clear();
        ok &= Check(
            Raw::MultiFrame::FixedReferenceDemosaic(
                mosaic, evidence, domain, second, &error),
            "fixed demosaic repeat failed: " + error);
        ok &= Check(EqualImageBits(first, second), "fixed demosaic is not deterministic");

        bool foundSignedNative = false;
        bool foundOverrangeNative = false;
        for (std::uint64_t y = 0; y < domain.activeExtent.height; ++y) {
            for (std::uint64_t x = 0; x < domain.activeExtent.width; ++x) {
                const std::size_t pixel = PixelIndex(x, y, domain.activeExtent.width);
                const Raw::MultiFrame::CfaPhase phase =
                    Raw::MultiFrame::PhaseAt(domain, x, y);
                std::size_t channel = 1u;
                if (phase == Raw::MultiFrame::CfaPhase::R) channel = 0u;
                if (phase == Raw::MultiFrame::CfaPhase::B) channel = 2u;
                ok &= Check(
                    first.linearRgb[RgbIndex(pixel, channel)] == mosaic[pixel],
                    "fixed demosaic changed a native CFA sample");
                foundSignedNative = foundSignedNative ||
                    first.linearRgb[RgbIndex(pixel, channel)] < 0.0f;
                foundOverrangeNative = foundOverrangeNative ||
                    first.linearRgb[RgbIndex(pixel, channel)] > 1.0f;
            }
        }
        ok &= Check(foundSignedNative, "fixed demosaic clamped signed samples");
        ok &= Check(foundOverrangeNative, "fixed demosaic clamped overrange samples");

        const std::uint64_t actualHash = HashReferenceImage(first);
        if (actualHash != expectedHashes[patternIndex]) {
            std::cerr << "Reference demosaic hash for pattern "
                      << Raw::CfaPatternName(patterns[patternIndex])
                      << " is " << actualHash << std::endl;
        }
        ok &= Check(
            actualHash == expectedHashes[patternIndex],
            "fixed demosaic v1 hash changed");

        Raw::MultiFrame::StageComparisonRequest request;
        request.domain = domain;
        request.latentMosaic = mosaic;
        request.expectedEvidence = evidence;
        request.candidateMosaic = mosaic;
        request.candidateEvidence = evidence;
        Raw::MultiFrame::StageComparisonMetrics metrics;
        error.clear();
        ok &= Check(
            Raw::MultiFrame::CompareReferenceStages(request, metrics, &error),
            "identity stage comparison failed: " + error);
        ok &= Check(
            metrics.mosaicAll.maximumAbsoluteError == 0.0 &&
                metrics.categoricalEvidenceMismatchCount == 0u &&
                metrics.falseEqualitySupportCount == 0u &&
                metrics.invalidOrClippedZeroCount == 0u &&
                metrics.darkArtifacts.connectedRegionCount == 0u,
            "identity fixture produced a false regression");
    }
    return ok;
}

bool ValidateInvalidZeroAndClippedLightFixture() {
    const Raw::MultiFrame::CfaDomainIdentity domain =
        MakeDomain(Raw::CfaPattern::RGGB, 9u, 9u);
    const std::size_t count = 81u;
    std::vector<float> latent(count, 0.05f);
    std::vector<Raw::MultiFrame::SampleEvidence> expected = ExactEvidence(count);
    std::vector<float> candidate = latent;
    std::vector<Raw::MultiFrame::SampleEvidence> candidateEvidence = expected;
    for (std::uint64_t y = 3u; y <= 5u; ++y) {
        for (std::uint64_t x = 3u; x <= 5u; ++x) {
            const std::size_t pixel = PixelIndex(x, y, 9u);
            const Raw::MultiFrame::CfaPhase phase =
                Raw::MultiFrame::PhaseAt(domain, x, y);
            latent[pixel] = phase == Raw::MultiFrame::CfaPhase::R
                ? 4.0f
                : (phase == Raw::MultiFrame::CfaPhase::B ? 1.5f : 2.5f);
            expected[pixel].state =
                Raw::MultiFrame::SampleEvidenceState::UpperCensored;
            expected[pixel].causes = Raw::MultiFrame::SampleEvidenceCauseMask(
                Raw::MultiFrame::SampleEvidenceCause::SensorSaturation);
            expected[pixel].bounds.lowerInclusive = 1.0;
            candidate[pixel] = 0.0f;
            candidateEvidence[pixel] = {};
        }
    }

    Raw::MultiFrame::StageComparisonRequest request;
    request.domain = domain;
    request.latentMosaic = latent;
    request.expectedEvidence = expected;
    request.candidateMosaic = candidate;
    request.candidateEvidence = candidateEvidence;
    request.darkArtifactRatio = 0.35;
    Raw::MultiFrame::StageComparisonMetrics metrics;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::CompareReferenceStages(request, metrics, &error),
        "clipped-light fixture comparison failed: " + error);
    ok &= Check(
        metrics.categoricalEvidenceMismatchCount == 9u &&
            metrics.falseEqualitySupportCount == 9u &&
            metrics.invalidOrClippedZeroCount == 9u,
        "invalid-to-zero failure was not detected exactly");
    ok &= Check(
        metrics.mosaicAll.rootMeanSquareError > 0.0 &&
            metrics.darkArtifacts.connectedRegionCount >= 1u &&
            metrics.darkArtifacts.affectedPixelCount >= 1u &&
            metrics.darkArtifacts.minimumCandidateToReferenceRatio < 0.35,
        "clipped-light black region was not detected after fixed demosaic");

    request.candidateEvidence = expected;
    error.clear();
    Raw::MultiFrame::StageComparisonMetrics honestMissing;
    ok &= Check(
        Raw::MultiFrame::CompareReferenceStages(request, honestMissing, &error),
        "honest missing-support comparison failed: " + error);
    ok &= Check(
        honestMissing.categoricalEvidenceMismatchCount == 0u &&
            honestMissing.falseEqualitySupportCount == 0u &&
            honestMissing.invalidOrClippedZeroCount == 0u &&
            honestMissing.unresolvedCandidateCount == 9u,
        "honest clipped evidence was mislabeled as measured support");
    return ok;
}

bool ValidateMissingSupportFixture() {
    const Raw::MultiFrame::CfaDomainIdentity domain =
        MakeDomain(Raw::CfaPattern::GBRG, 7u, 5u);
    const std::size_t count = 35u;
    std::vector<float> latent(count, 0.4f);
    std::vector<Raw::MultiFrame::SampleEvidence> expected = ExactEvidence(count);
    std::vector<float> candidate = latent;
    std::vector<Raw::MultiFrame::SampleEvidence> candidateEvidence = expected;
    for (std::uint64_t y = 0u; y < 5u; ++y) {
        const std::size_t pixel = PixelIndex(0u, y, 7u);
        expected[pixel].state = Raw::MultiFrame::SampleEvidenceState::OutOfBounds;
        candidate[pixel] = 0.0f;
        candidateEvidence[pixel] = {};
    }

    Raw::MultiFrame::StageComparisonRequest request;
    request.domain = domain;
    request.latentMosaic = latent;
    request.expectedEvidence = expected;
    request.candidateMosaic = candidate;
    request.candidateEvidence = candidateEvidence;
    Raw::MultiFrame::StageComparisonMetrics metrics;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::CompareReferenceStages(request, metrics, &error),
        "missing-support fixture comparison failed: " + error);
    ok &= Check(
        metrics.falseEqualitySupportCount == 5u &&
            metrics.invalidOrClippedZeroCount == 5u,
        "out-of-bounds support became an undetected numeric zero");
    return ok;
}

bool ValidateCfaPhaseEdgeFixture() {
    const Raw::MultiFrame::CfaDomainIdentity domain =
        MakeDomain(Raw::CfaPattern::GRBG, 8u, 6u);
    const std::size_t count = 48u;
    std::vector<float> latent(count, 0.0f);
    std::vector<float> candidate(count, 0.0f);
    const std::vector<Raw::MultiFrame::SampleEvidence> evidence = ExactEvidence(count);
    for (std::uint64_t y = 0u; y < 6u; ++y) {
        for (std::uint64_t x = 0u; x < 8u; ++x) {
            const std::size_t pixel = PixelIndex(x, y, 8u);
            const Raw::MultiFrame::CfaPhase phase =
                Raw::MultiFrame::PhaseAt(domain, x, y);
            const bool right = x >= 4u;
            if (phase == Raw::MultiFrame::CfaPhase::R) {
                latent[pixel] = right ? 0.9f : 0.2f;
                candidate[pixel] = latent[pixel];
            } else if (phase == Raw::MultiFrame::CfaPhase::B) {
                latent[pixel] = right ? 0.1f : 0.8f;
                candidate[pixel] = latent[pixel];
            } else {
                const float g0 = right ? 0.7f : 0.3f;
                const float g1 = right ? 0.5f : 0.4f;
                latent[pixel] = phase == Raw::MultiFrame::CfaPhase::G0 ? g0 : g1;
                candidate[pixel] = phase == Raw::MultiFrame::CfaPhase::G0 ? g1 : g0;
            }
        }
    }

    Raw::MultiFrame::StageComparisonRequest request;
    request.domain = domain;
    request.latentMosaic = latent;
    request.expectedEvidence = evidence;
    request.candidateMosaic = candidate;
    request.candidateEvidence = evidence;
    Raw::MultiFrame::StageComparisonMetrics metrics;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::CompareReferenceStages(request, metrics, &error),
        "CFA phase-edge fixture comparison failed: " + error);
    ok &= Check(
        metrics.mosaicByPhase[static_cast<std::size_t>(
            Raw::MultiFrame::CfaPhase::R)].maximumAbsoluteError == 0.0 &&
        metrics.mosaicByPhase[static_cast<std::size_t>(
            Raw::MultiFrame::CfaPhase::B)].maximumAbsoluteError == 0.0 &&
        metrics.mosaicByPhase[static_cast<std::size_t>(
            Raw::MultiFrame::CfaPhase::G0)].rootMeanSquareError > 0.0 &&
        metrics.mosaicByPhase[static_cast<std::size_t>(
            Raw::MultiFrame::CfaPhase::G1)].rootMeanSquareError > 0.0,
        "CFA phase metrics did not isolate the injected green-phase swap");
    ok &= Check(
        metrics.referenceRgbByChannel[1].rootMeanSquareError > 0.0,
        "fixed demosaic did not expose the green-phase edge error");
    return ok;
}

bool ValidatePublicationFenceContract() {
    using Raw::MultiFrame::ActivePublicationContext;
    using Raw::MultiFrame::EvaluatePublicationFence;
    using Raw::MultiFrame::PublicationAttemptStamp;
    using Raw::MultiFrame::PublicationFenceResult;

    const PublicationAttemptStamp attempt { 17u, "project-a", 9u };
    const ActivePublicationContext current {
        17u, true, "project-a", 9u, true };
    bool ok = Check(
        EvaluatePublicationFence(attempt, current) ==
            PublicationFenceResult::Current,
        "a current publication candidate was rejected");

    ActivePublicationContext changed = current;
    changed.generation = 18u;
    ok &= Check(
        EvaluatePublicationFence(attempt, changed) ==
            PublicationFenceResult::SupersededGeneration,
        "a superseded worker generation remained adoptable");
    changed = current;
    changed.hasProject = false;
    ok &= Check(
        EvaluatePublicationFence(attempt, changed) ==
            PublicationFenceResult::NoActiveProject,
        "a completion remained adoptable without an active project");
    changed = current;
    changed.projectId = "project-b";
    ok &= Check(
        EvaluatePublicationFence(attempt, changed) ==
            PublicationFenceResult::DifferentProject,
        "a completion crossed project identity");
    changed = current;
    changed.inputRevision = 10u;
    ok &= Check(
        EvaluatePublicationFence(attempt, changed) ==
            PublicationFenceResult::DifferentInputRevision,
        "a stale input revision remained adoptable");
    changed = current;
    changed.hasSourceSet = false;
    ok &= Check(
        EvaluatePublicationFence(attempt, changed) ==
            PublicationFenceResult::MissingSourceSet,
        "a completion remained adoptable after its source set disappeared");
    return ok;
}

} // namespace

bool ValidateUnifiedMultiFramePhase0() {
    bool ok = true;
    ok &= Check(
        Raw::MultiFrame::kReferenceDemosaicVersion == 1u &&
            std::string(Raw::MultiFrame::kReferenceDemosaicId) ==
                "stack-multiframe-fixed-linear-demosaic-v1",
        "reference demosaic identity changed without review");
    ok &= ValidateFixedReferenceDemosaic();
    ok &= ValidateInvalidZeroAndClippedLightFixture();
    ok &= ValidateMissingSupportFixture();
    ok &= ValidateCfaPhaseEdgeFixture();
    ok &= ValidatePublicationFenceContract();
    if (ok) {
        std::cout
            << "Unified MultiFrame Phase 0 fixtures and reference metrics passed."
            << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
