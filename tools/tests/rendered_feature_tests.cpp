#include "Raw/RenderedFeatureEvidence.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using Stack::RenderedFeatures::FeatureContext;
using Stack::RenderedFeatures::FeatureRecord;
using Stack::RenderedFeatures::FeatureValue;
using Stack::RenderedFeatures::LinearRgbImage;
using Stack::RenderedFeatures::ScalarMask;

int g_Failures = 0;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_Failures;
    }
}

void Near(double actual, double expected, double tolerance, const std::string& message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (actual=" << actual
                  << ", expected=" << expected << ", tolerance=" << tolerance << ")\n";
        ++g_Failures;
    }
}

FeatureContext Context(const std::string& source = "fixture-source") {
    FeatureContext context;
    context.sourceIdentity = source;
    context.recipeIdentity = "fixture-recipe-v1";
    context.stage = "working-linear-scene";
    context.colorSpace = "fixture-rgb";
    context.colorTransformIdentity = "fixture-working-to-xyz-v1";
    context.transferFunction = "linear";
    context.workingToXyz = {
        1.0, 0.0, 0.0,
        0.1, 0.6, 0.3,
        0.0, 0.0, 1.0
    };
    context.referenceGrey = 0.18;
    context.cropIdentity = "full-image";
    context.orientationNormalized = true;
    return context;
}

LinearRgbImage ConstantImage(int width, int height, float r, float g, float b) {
    LinearRgbImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    for (std::size_t i = 0; i < image.pixels.size(); i += 3) {
        image.pixels[i] = r;
        image.pixels[i + 1] = g;
        image.pixels[i + 2] = b;
    }
    return image;
}

LinearRgbImage StepImage(int width, int height, float left, float right) {
    LinearRgbImage image = ConstantImage(width, height, left, left, left);
    for (int y = 0; y < height; ++y) {
        for (int x = width / 2; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
            image.pixels[i] = right;
            image.pixels[i + 1] = right;
            image.pixels[i + 2] = right;
        }
    }
    return image;
}

const FeatureValue& RequireFeature(const FeatureRecord& record, const std::string& id) {
    const FeatureValue* value = Stack::RenderedFeatures::FindFeature(record, id);
    if (value == nullptr) {
        std::cerr << "FAIL: missing feature " << id << '\n';
        ++g_Failures;
        static const FeatureValue missing;
        return missing;
    }
    return *value;
}

void TestLuminanceDefinitionsAndMetadata() {
    FeatureContext context = Context();
    const LinearRgbImage image = ConstantImage(64, 64, 0.2f, 0.4f, 0.6f);
    const std::vector<float> original = image.pixels;
    const FeatureRecord record = Stack::RenderedFeatures::AnalyzeSceneLinear(image, context);
    Check(record.valid, "P02-LUMA-01 record is valid");
    const double expectedY = 0.1 * 0.2 + 0.6 * 0.4 + 0.3 * 0.6;
    Near(RequireFeature(record, "scene.ev_p50").value,
        std::log2(expectedY / 0.18), 1.0e-5,
        "P02-LUMA-01 declared working-to-XYZ Y row controls EV");
    Check(image.pixels == original, "analysis never mutates source pixels");
    const FeatureValue& p50 = RequireFeature(record, "scene.ev_p50");
    Check(p50.version == Stack::RenderedFeatures::kRenderedFeatureVersion,
        "feature carries stable version");
    Check(p50.units == "EV" && p50.stage == context.stage &&
          p50.colorSpace == context.colorSpace && p50.transferFunction == "linear",
        "feature carries units, stage, color space, and transfer function");
    Check(p50.minimumWidth > 0 && p50.minimumHeight > 0 &&
          p50.validFraction > 0.99 && p50.uncertainty01 >= 0.0,
        "feature carries resolution, validity, and uncertainty");
    Check(record.runtimeMs >= 0.0 && p50.runtimeMs >= 0.0,
        "record and feature carry measured cost");

    LinearRgbImage signedImage;
    signedImage.width = 2;
    signedImage.height = 1;
    signedImage.pixels = { -0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };
    const FeatureRecord signedRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(signedImage, context);
    Near(RequireFeature(signedRecord, "scene.negative_channel_fraction").value,
        1.0 / 6.0, 1.0e-8, "P02-LUMA-02 negative channel fraction is per channel");

    FeatureContext wrongTransfer = context;
    wrongTransfer.transferFunction = "sRGB";
    Check(!Stack::RenderedFeatures::AnalyzeSceneLinear(image, wrongTransfer).valid,
        "scene analyzer rejects nonlinear input instead of guessing");
}

void TestOrientationCropAndRegionalConflict() {
    LinearRgbImage marker;
    marker.width = 2;
    marker.height = 3;
    marker.pixels.resize(18);
    for (int p = 0; p < 6; ++p) {
        marker.pixels[static_cast<std::size_t>(p) * 3] = static_cast<float>(p + 1);
        marker.pixels[static_cast<std::size_t>(p) * 3 + 1] = static_cast<float>(p + 1);
        marker.pixels[static_cast<std::size_t>(p) * 3 + 2] = static_cast<float>(p + 1);
    }
    const LinearRgbImage oriented = Stack::RenderedFeatures::OrientLinearRgb(marker, 6);
    Check(oriented.width == 3 && oriented.height == 2, "orientation 6 swaps dimensions");
    Near(oriented.pixels[0], 5.0, 0.0, "orientation 6 maps top-left exactly");

    LinearRgbImage regions = ConstantImage(128, 128, 1.0f, 1.0f, 1.0f);
    for (int y = 32; y < 96; ++y) {
        for (int x = 32; x < 96; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)) * 3;
            regions.pixels[i] = regions.pixels[i + 1] = regions.pixels[i + 2] = 0.125f;
        }
    }
    FeatureContext context = Context();
    context.cropIdentity = "active-crop-sha-fixture";
    const FeatureRecord record = Stack::RenderedFeatures::AnalyzeSceneLinear(regions, context);
    Check(RequireFeature(record, "region.bright_border_center_conflict_ev").value > 2.0,
        "orientation-normalized regional feature detects bright-border/dark-center conflict");
    Check(record.cropIdentity == context.cropIdentity,
        "regional record preserves crop identity");

    FeatureContext notOriented = context;
    notOriented.orientationNormalized = false;
    const FeatureRecord rejectedRegions = Stack::RenderedFeatures::AnalyzeSceneLinear(regions, notOriented);
    Check(!RequireFeature(rejectedRegions, "region.center_median_ev").valid,
        "regional measurements are unavailable before orientation normalization");
}

void TestMaskReliability() {
    const int width = 128;
    const int height = 128;
    LinearRgbImage image = StepImage(width, height, 0.1f, 0.8f);
    ScalarMask aligned;
    aligned.width = width;
    aligned.height = height;
    aligned.values.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            aligned.values[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = x < width / 2 ? 1.0f : 0.0f;
        }
    }
    const FeatureRecord alignedRecord = Stack::RenderedFeatures::AnalyzeRegionMask(image, aligned, Context());
    Check(alignedRecord.valid, "aligned region mask produces a valid diagnostic record");
    Check(RequireFeature(alignedRecord, "region.mask_background_foreground_conflict_ev").value > 2.0,
        "mask regions retain signed foreground/background EV conflict");
    Near(RequireFeature(alignedRecord, "mask.uncertainty_mean").value, 0.0, 1.0e-8,
        "binary mask has zero continuous ambiguity");
    const double alignedSupport = RequireFeature(alignedRecord, "mask.boundary_support").value;

    ScalarMask unsupported = aligned;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsupported.values[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = x < width / 4 ? 1.0f : 0.0f;
        }
    }
    const FeatureRecord unsupportedRecord = Stack::RenderedFeatures::AnalyzeRegionMask(image, unsupported, Context());
    const double unsupportedSupport = RequireFeature(unsupportedRecord, "mask.boundary_support").value;
    Check(alignedSupport > unsupportedSupport,
        "mask boundary support distinguishes image-aligned and unsupported boundaries");
    Check(RequireFeature(unsupportedRecord, "mask.boundary_leakage_risk").value >
          RequireFeature(alignedRecord, "mask.boundary_leakage_risk").value,
        "unsupported mask boundary raises leakage uncertainty");

    ScalarMask soft = aligned;
    std::fill(soft.values.begin(), soft.values.end(), 0.5f);
    const FeatureRecord softRecord = Stack::RenderedFeatures::AnalyzeRegionMask(image, soft, Context());
    Near(RequireFeature(softRecord, "mask.uncertainty_mean").value, 1.0, 1.0e-8,
        "0.5 mask has maximum continuous ambiguity");
}

void TestMultiscaleAndProxyAgreement() {
    LinearRgbImage flat = ConstantImage(256, 256, 0.25f, 0.25f, 0.25f);
    LinearRgbImage texture = flat;
    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const float value = ((x / 2 + y / 2) & 1) ? 0.35f : 0.15f;
            const std::size_t i = (static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x)) * 3;
            texture.pixels[i] = texture.pixels[i + 1] = texture.pixels[i + 2] = value;
        }
    }
    const FeatureRecord flatRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(flat, Context());
    const FeatureRecord textureRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(texture, Context());
    Check(RequireFeature(textureRecord, "multiscale.detail_rms_r1").value >
          RequireFeature(flatRecord, "multiscale.detail_rms_r1").value,
        "P02-PROXY-01 fine texture increases small-scale detail energy");

    const LinearRgbImage proxy128 = Stack::RenderedFeatures::ResizeLinearRgb(texture, 128, 128);
    const LinearRgbImage proxy32 = Stack::RenderedFeatures::ResizeLinearRgb(texture, 32, 32);
    const FeatureRecord record128 = Stack::RenderedFeatures::AnalyzeSceneLinear(proxy128, Context());
    const FeatureRecord record32 = Stack::RenderedFeatures::AnalyzeSceneLinear(proxy32, Context());
    const auto agreement128 = Stack::RenderedFeatures::CompareFeatureRecords(textureRecord, record128);
    const auto agreement32 = Stack::RenderedFeatures::CompareFeatureRecords(textureRecord, record32);
    Check(agreement128.valid && agreement128.comparableFeatureCount > 10,
        "128px proxy exposes a measurable per-feature agreement record");
    Check(agreement32.missingOrMismatched.size() > agreement128.missingOrMismatched.size(),
        "minimum-resolution declarations reject more features at 32px");

    FeatureContext stale = Context();
    stale.stage = "different-stage";
    const FeatureRecord staleRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(proxy128, stale);
    Check(!Stack::RenderedFeatures::CompareFeatureRecords(textureRecord, staleRecord).valid,
        "proxy comparison rejects stale stage identity");
    Check(Stack::RenderedFeatures::FeatureRecordMatches(textureRecord,
        "fixture-source", "fixture-recipe-v1", "working-linear-scene"),
        "exact feature identity matches");
    Check(!Stack::RenderedFeatures::FeatureRecordMatches(textureRecord,
        "fixture-source", "changed-recipe", "working-linear-scene"),
        "changed recipe identity invalidates feature record");
}

void TestHaloProfilesAndRenderedComparison() {
    std::vector<double> reference(33, 0.0);
    std::vector<double> candidate(33, 0.0);
    for (int i = 16; i < 33; ++i) reference[static_cast<std::size_t>(i)] = candidate[static_cast<std::size_t>(i)] = 1.0;
    candidate[14] = -0.10;
    candidate[15] = -0.20;
    candidate[16] = 1.25;
    candidate[17] = 1.10;
    const auto metrics = Stack::RenderedFeatures::EvaluateEdgeProfile(reference, candidate, 16, 3);
    Check(metrics.valid, "P02-HALO-01 oriented edge profile is valid");
    Check(metrics.overshoot > 0.20 && metrics.undershoot > 0.10,
        "edge profile detects signed overshoot and undershoot");
    Check(metrics.adjacentBandEnergy > 0.0 && metrics.newExtremaCount > 0.0,
        "edge profile detects adjacent bands and new extrema");
    std::vector<double> reversed = reference;
    for (int i = 0; i < 33; ++i) reversed[static_cast<std::size_t>(i)] = i < 16 ? 1.0 : 0.0;
    const auto reversalMetrics = Stack::RenderedFeatures::EvaluateEdgeProfile(reference, reversed, 16, 3);
    Check(reversalMetrics.valid && reversalMetrics.reversalEnergy > 0.0,
        "edge profile detects a candidate gradient reversal");
    std::vector<double> shifted(33, 0.0);
    for (int i = 18; i < 33; ++i) shifted[static_cast<std::size_t>(i)] = 1.0;
    const auto shiftMetrics = Stack::RenderedFeatures::EvaluateEdgeProfile(reference, shifted, 16, 3);
    Check(shiftMetrics.valid && shiftMetrics.edgeShiftPixels >= 2.0,
        "edge profile measures a known two-pixel edge shift");

    const LinearRgbImage clean = StepImage(128, 128, 0.1f, 0.8f);
    LinearRgbImage halo = clean;
    for (int y = 0; y < 128; ++y) {
        for (int x : { 61, 62, 63 }) {
            const std::size_t i = (static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)) * 3;
            halo.pixels[i] = halo.pixels[i + 1] = halo.pixels[i + 2] = 0.05f;
        }
        for (int x : { 64, 65, 66 }) {
            const std::size_t i = (static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)) * 3;
            halo.pixels[i] = halo.pixels[i + 1] = halo.pixels[i + 2] = 1.1f;
        }
    }
    const FeatureRecord cleanComparison = Stack::RenderedFeatures::CompareRenderedImages(clean, clean, Context());
    const FeatureRecord haloComparison = Stack::RenderedFeatures::CompareRenderedImages(clean, halo, Context());
    Check(RequireFeature(haloComparison, "halo.overshoot_log_luma").value >
          RequireFeature(cleanComparison, "halo.overshoot_log_luma").value,
        "2D halo perturbation raises overshoot feature");
    Check(RequireFeature(haloComparison, "halo.adjacent_band_energy").value >
          RequireFeature(cleanComparison, "halo.adjacent_band_energy").value,
        "2D halo perturbation raises adjacent-band feature");
}

void TestNoiseAndLift() {
    LinearRgbImage clean = ConstantImage(128, 128, 0.05f, 0.05f, 0.05f);
    LinearRgbImage noisy = clean;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const float delta = ((x * 17 + y * 29) & 1) ? 0.008f : -0.008f;
            const std::size_t i = (static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)) * 3;
            noisy.pixels[i] += delta;
            noisy.pixels[i + 1] -= delta * 0.5f;
            noisy.pixels[i + 2] += delta * 0.25f;
        }
    }
    FeatureContext context = Context();
    context.globalLiftEv = 1.0;
    context.maximumLocalLiftEv = 3.0;
    const FeatureRecord cleanRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(clean, context);
    const FeatureRecord noisyRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(noisy, context);
    Check(RequireFeature(noisyRecord, "noise.rendered_luma_residual_rms").value >
          RequireFeature(cleanRecord, "noise.rendered_luma_residual_rms").value,
        "P02-NOISE-01 rendered luma residual responds to injected noise");
    Check(RequireFeature(noisyRecord, "noise.rendered_chroma_residual_rms").value >
          RequireFeature(cleanRecord, "noise.rendered_chroma_residual_rms").value,
        "rendered chroma residual responds to chromatic noise");
    Near(RequireFeature(noisyRecord, "noise.predicted_visible_amplification").value,
        8.0, 1.0e-12, "maximum declared +3EV lift predicts 8x visible amplification");
}

void TestWhiteBalanceColorAndGamut() {
    const LinearRgbImage neutral = ConstantImage(128, 128, 0.3f, 0.3f, 0.3f);
    const FeatureRecord neutralRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(neutral, Context());
    Near(RequireFeature(neutralRecord, "wb.estimator_disagreement_ev").value,
        0.0, 1.0e-8, "neutral field produces estimator agreement");
    const FeatureRecord neutralComparison = Stack::RenderedFeatures::CompareRenderedImages(neutral, neutral, Context());
    Check(!RequireFeature(neutralComparison, "color.hue_shift_degrees").valid,
        "near-neutral hue is unavailable instead of numerically unstable");

    LinearRgbImage mixed = neutral;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)) * 3;
            if (x < 64) {
                mixed.pixels[i] = 0.60f; mixed.pixels[i + 1] = 0.30f; mixed.pixels[i + 2] = 0.15f;
            } else {
                mixed.pixels[i] = 0.15f; mixed.pixels[i + 1] = 0.30f; mixed.pixels[i + 2] = 0.60f;
            }
        }
    }
    const FeatureRecord mixedRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(mixed, Context());
    Check(RequireFeature(mixedRecord, "wb.spatial_disagreement_ev").value > 1.0,
        "P02-WB-01 mixed illuminants raise spatial estimator disagreement");

    LinearRgbImage shifted = mixed;
    for (std::size_t i = 0; i < shifted.pixels.size(); i += 3) {
        std::swap(shifted.pixels[i], shifted.pixels[i + 1]);
    }
    const FeatureRecord colorComparison = Stack::RenderedFeatures::CompareRenderedImages(mixed, shifted, Context());
    Check(RequireFeature(colorComparison, "color.hue_shift_degrees").valid &&
          RequireFeature(colorComparison, "color.hue_shift_degrees").value > 10.0,
        "P02-COLOR-01 chroma-weighted hue feature responds to a channel transform");

    LinearRgbImage gamut = ConstantImage(64, 64, 1.2f, 0.5f, -0.1f);
    const FeatureRecord gamutRecord = Stack::RenderedFeatures::AnalyzeSceneLinear(gamut, Context());
    Near(RequireFeature(gamutRecord, "color.gamut_pressure_fraction").value,
        1.0, 1.0e-12, "out-of-range working RGB registers gamut pressure");
}

void TestDisplaySeparationAndModel() {
    FeatureContext displayContext = Context();
    displayContext.stage = "post-view-transform-linear-display";
    displayContext.colorSpace = "linear-sRGB";
    displayContext.workingToXyz = {
        0.4124564, 0.3575761, 0.1804375,
        0.2126729, 0.7151522, 0.0721750,
        0.0193339, 0.1191920, 0.9503041
    };
    const LinearRgbImage grey = ConstantImage(64, 64, 0.18f, 0.18f, 0.18f);
    const FeatureRecord relative = Stack::RenderedFeatures::AnalyzeDisplayMapped(grey, displayContext);
    Near(RequireFeature(relative, "display.linear_p50").value, 0.18, 1.0e-5,
        "P02-DISPLAY-01 linear display percentile remains linear");
    Check(RequireFeature(relative, "display.encoded_p50").value > 0.40,
        "encoded display percentile is labeled and numerically distinct");
    Check(!RequireFeature(relative, "display.absolute_dynamic_range").valid,
        "P02-DISPLAY-02 absolute metric is unavailable without a display model");

    Stack::RenderedFeatures::DisplayModel model;
    model.absoluteLuminanceKnown = true;
    model.peakLuminanceCdM2 = 100.0;
    model.blackLuminanceCdM2 = 0.1;
    const FeatureRecord calibrated = Stack::RenderedFeatures::AnalyzeDisplayMapped(grey, displayContext, model);
    Near(RequireFeature(calibrated, "display.absolute_dynamic_range").value,
        1000.0, 1.0e-8, "calibrated display dynamic range uses declared luminance model");

    LinearRgbImage clipped = grey;
    for (std::size_t i = 0; i < clipped.pixels.size() / 2; ++i) clipped.pixels[i] = 1.2f;
    const FeatureRecord clipRecord = Stack::RenderedFeatures::AnalyzeDisplayMapped(clipped, displayContext);
    Check(RequireFeature(clipRecord, "display.linear_clip_high_fraction").value > 0.0,
        "display high clipping fraction responds to out-of-range channels");
}

void TestPhaseOneCarryForwardAndIdentity() {
    Stack::RawEvidence::RawTechnicalEvidenceRecord raw;
    raw.valid = true;
    raw.sourceIdentity.valid = true;
    raw.sourceIdentity.sha256 = "fixture-source";
    raw.evidenceIdentitySha256 = "fixture-raw-evidence-identity";
    auto measurement = [](double value) {
        Stack::RawEvidence::EvidenceMeasurement m;
        m.valid = true;
        m.value = value;
        m.units = "fraction";
        m.uncertainty01 = 0.1;
        return m;
    };
    raw.clipping.singleChannelClippedFraction = measurement(0.20);
    raw.clipping.multiChannelClippedFraction = measurement(0.10);
    raw.clipping.allChannelClippedFraction = measurement(0.05);
    Stack::RawEvidence::NoisePlaneEvidence plane;
    Stack::RawEvidence::EvidenceMeasurement snr = measurement(12.0);
    snr.units = "linear-SNR";
    plane.snrBySignal.push_back(snr);
    raw.noise.push_back(plane);

    const LinearRgbImage image = ConstantImage(64, 64, 0.2f, 0.2f, 0.2f);
    FeatureContext matching = Context();
    matching.rawEvidenceIdentity = raw.evidenceIdentitySha256;
    const FeatureRecord record = Stack::RenderedFeatures::AnalyzeSceneLinear(image, matching, &raw);
    Near(RequireFeature(record, "highlight.raw_partial_clip_fraction").value, 0.20, 1.0e-12,
        "matching Phase 01 partial-clipping evidence is carried forward");
    Near(RequireFeature(record, "noise.raw_predicted_snr_min").value, 12.0, 1.0e-12,
        "matching Phase 01 SNR prior is carried forward");

    FeatureContext mismatch = Context("different-source");
    mismatch.rawEvidenceIdentity = raw.evidenceIdentitySha256;
    const FeatureRecord rejected = Stack::RenderedFeatures::AnalyzeSceneLinear(image, mismatch, &raw);
    Check(!RequireFeature(rejected, "highlight.raw_partial_clip_fraction").valid,
        "stale Phase 01 source identity is rejected");
    Check(!rejected.warnings.empty(), "stale Phase 01 rejection is visible in warnings");
}

void TestFilterPrototypesAndSerialization() {
    const int width = 64;
    const int height = 64;
    std::vector<float> signal(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            signal[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] =
                (x < width / 2 ? 0.1f : 0.8f) + (((x + y) & 1) ? 0.01f : -0.01f);
        }
    }
    const std::vector<float> original = signal;
    const auto bilateral = Stack::RenderedFeatures::BilateralBasePrototype(signal, width, height, 3, 2.0, 0.1);
    const auto guided = Stack::RenderedFeatures::GuidedBasePrototype(signal, width, height, 3, 1.0e-3);
    const auto local = Stack::RenderedFeatures::LocalLaplacianPrototype(signal, width, height, 4, 0.9, 1.0);
    Check(bilateral.size() == signal.size() && guided.size() == signal.size() && local.size() == signal.size(),
        "P02-FILTER-01 all three research prototypes produce declared-size outputs");
    Check(signal == original, "filter prototypes do not mutate their source fixture");
    Check(std::all_of(bilateral.begin(), bilateral.end(), [](float v) { return std::isfinite(v); }) &&
          std::all_of(guided.begin(), guided.end(), [](float v) { return std::isfinite(v); }) &&
          std::all_of(local.begin(), local.end(), [](float v) { return std::isfinite(v); }),
        "filter prototype outputs remain finite");
    Check(bilateral[static_cast<std::size_t>(height / 2) * width + width / 2 - 1] < 0.3f &&
          bilateral[static_cast<std::size_t>(height / 2) * width + width / 2] > 0.6f,
        "bilateral prototype preserves the major step while smoothing texture");

    const FeatureRecord record = Stack::RenderedFeatures::AnalyzeSceneLinear(
        ConstantImage(64, 64, 0.2f, 0.2f, 0.2f), Context());
    const nlohmann::json serialized = Stack::RenderedFeatures::SerializeFeatureRecord(record);
    Check(serialized.at("featureVersion").get<std::string>() ==
          Stack::RenderedFeatures::kRenderedFeatureVersion,
        "serialized record freezes feature version");
    Check(serialized.at("values").is_array() && !serialized.at("values").empty(),
        "serialized record retains individual feature values");
    Check(serialized.at("values")[0].contains("runtimeMs") &&
          serialized.at("values")[0].contains("minimumWidth") &&
          serialized.at("values")[0].contains("uncertainty01"),
        "serialized feature retains cost, minimum resolution, and uncertainty");
}

} // namespace

int main() {
    TestLuminanceDefinitionsAndMetadata();
    TestOrientationCropAndRegionalConflict();
    TestMaskReliability();
    TestMultiscaleAndProxyAgreement();
    TestHaloProfilesAndRenderedComparison();
    TestNoiseAndLift();
    TestWhiteBalanceColorAndGamut();
    TestDisplaySeparationAndModel();
    TestPhaseOneCarryForwardAndIdentity();
    TestFilterPrototypesAndSerialization();

    if (g_Failures != 0) {
        std::cerr << g_Failures << " rendered feature fixture assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Phase 02 rendered feature fixtures passed.\n";
    return 0;
}
