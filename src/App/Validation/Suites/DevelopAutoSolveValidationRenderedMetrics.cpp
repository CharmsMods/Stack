#include "App/Validation/Suites/DevelopAutoSolveValidationRenderedMetrics.h"

#include "Editor/EditorModule.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Stack::Validation::Detail {

nlohmann::json DevelopAutoSolveRenderedMetricsToJson(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics) {
    return nlohmann::json{
        { "meanLuma", metrics.meanLuma },
        { "medianLuma", metrics.medianLuma },
        { "p10Luma", metrics.p10Luma },
        { "p90Luma", metrics.p90Luma },
        { "shadowFraction", metrics.shadowFraction },
        { "highlightFraction", metrics.highlightFraction },
        { "clippedFraction", metrics.clippedFraction },
        { "contrastSpan", metrics.contrastSpan },
        { "meanRed", metrics.meanRed },
        { "meanGreen", metrics.meanGreen },
        { "meanBlue", metrics.meanBlue },
        { "warmCoolBias", metrics.warmCoolBias },
        { "magentaGreenBias", metrics.magentaGreenBias },
        { "channelImbalance", metrics.channelImbalance },
        { "colorCastRisk", metrics.colorCastRisk },
        { "meanSaturation", metrics.meanSaturation },
        { "lowSaturationFraction", metrics.lowSaturationFraction },
        { "highlightBandFraction", metrics.highlightBandFraction },
        { "highlightMeanLuma", metrics.highlightMeanLuma },
        { "highlightLowSaturationFraction", metrics.highlightLowSaturationFraction },
        { "highlightGrayRisk", metrics.highlightGrayRisk },
        { "highlightTileCoverage", metrics.highlightTileCoverage },
        { "highlightStructureScore", metrics.highlightStructureScore },
        { "meaningfulHighlightPressure", metrics.meaningfulHighlightPressure },
        { "edgeContrast", metrics.edgeContrast },
        { "haloRiskFraction", metrics.haloRiskFraction },
        { "shadowTextureRisk", metrics.shadowTextureRisk },
        { "localMeanLuma3x3", metrics.localMeanLuma },
        { "localContrastSpan3x3", metrics.localContrastSpan },
        { "localDamageRiskScore3x3", metrics.localDamageRiskScore },
        { "localLumaSpread", metrics.localLumaSpread },
        { "localEvSpreadStops", metrics.localEvSpreadStops },
        { "localEvConflict", metrics.localEvConflict },
        { "localContrastPeak", metrics.localContrastPeak },
        { "localShadowPressure", metrics.localShadowPressure },
        { "localHighlightPressure", metrics.localHighlightPressure },
        { "localDamageRiskMean", metrics.localDamageRiskMean },
        { "localDamageRiskPeak", metrics.localDamageRiskPeak },
        { "localDamageRiskPeakTile", metrics.localDamageRiskPeakTile },
        { "localExposureHighlightCrowding", metrics.localExposureHighlightCrowding },
        { "localExposureShadowCrowding", metrics.localExposureShadowCrowding },
        { "localExposureHaloStress", metrics.localExposureHaloStress },
        { "localExposureFlatnessRisk", metrics.localExposureFlatnessRisk },
        { "localExposureDamageRisk", metrics.localExposureDamageRisk },
        { "centerMeanLuma", metrics.centerMeanLuma },
        { "centerShadowFraction", metrics.centerShadowFraction },
        { "centerHighlightFraction", metrics.centerHighlightFraction },
        { "subjectCenterPrior", metrics.subjectCenterPrior },
        { "subjectReadabilityPressure", metrics.subjectReadabilityPressure },
        { "subjectProtectionPressure", metrics.subjectProtectionPressure },
        { "subjectMoodPreservationPressure", metrics.subjectMoodPreservationPressure },
        { "subjectImportanceConfidence", metrics.subjectImportanceConfidence }
    };
}

DevelopAutoSolveRenderedMetricFixtures BuildDevelopAutoSolveRenderedMetricFixtures() {
    using Metrics = EditorRenderWorker::DevelopCandidateRenderMetrics;

    DevelopAutoSolveRenderedMetricFixtures fixtures;

    constexpr int visualRiskW = 6;
    constexpr int visualRiskH = 6;
    std::vector<unsigned char> visualRiskPixels(
        static_cast<std::size_t>(visualRiskW * visualRiskH * 4),
        255);
    auto setVisualRiskPixel = [&](int x, int y, unsigned char value) {
        const std::size_t offset = static_cast<std::size_t>((y * visualRiskW + x) * 4);
        visualRiskPixels[offset + 0] = value;
        visualRiskPixels[offset + 1] = value;
        visualRiskPixels[offset + 2] = value;
        visualRiskPixels[offset + 3] = 255;
    };
    for (int y = 0; y < visualRiskH; ++y) {
        for (int x = 0; x < visualRiskW; ++x) {
            const unsigned char value = x < 3
                ? static_cast<unsigned char>(((x + y) & 1) ? 55 : 18)
                : static_cast<unsigned char>(((x + y) & 1) ? 224 : 184);
            setVisualRiskPixel(x, y, value);
        }
    }
    setVisualRiskPixel(2, 2, 255);
    fixtures.visualRiskMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            visualRiskPixels,
            visualRiskW,
            visualRiskH);

    std::vector<unsigned char> spatialRiskPixels(
        static_cast<std::size_t>(visualRiskW * visualRiskH * 4),
        96);
    for (std::size_t offset = 0; offset < spatialRiskPixels.size(); offset += 4u) {
        spatialRiskPixels[offset + 0] = 96;
        spatialRiskPixels[offset + 1] = 96;
        spatialRiskPixels[offset + 2] = 96;
        spatialRiskPixels[offset + 3] = 255;
    }
    auto setSpatialRiskPixel = [&](int x, int y, unsigned char value) {
        const std::size_t offset = static_cast<std::size_t>((y * visualRiskW + x) * 4);
        spatialRiskPixels[offset + 0] = value;
        spatialRiskPixels[offset + 1] = value;
        spatialRiskPixels[offset + 2] = value;
        spatialRiskPixels[offset + 3] = 255;
    };
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            setSpatialRiskPixel(x, y, 245);
        }
    }
    fixtures.spatialRiskMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            spatialRiskPixels,
            visualRiskW,
            visualRiskH);

    fixtures.renderedVisualRiskMetricsPopulated =
        fixtures.visualRiskMetrics.contrastSpan > 0.45f &&
        fixtures.visualRiskMetrics.lowSaturationFraction > 0.45f &&
        fixtures.visualRiskMetrics.edgeContrast > 0.20f &&
        fixtures.visualRiskMetrics.haloRiskFraction > 0.0f &&
        fixtures.visualRiskMetrics.shadowTextureRisk > 0.05f &&
        fixtures.visualRiskMetrics.localExposureHighlightCrowding > 0.10f &&
        fixtures.visualRiskMetrics.localExposureShadowCrowding > 0.10f &&
        fixtures.visualRiskMetrics.localExposureHaloStress > 0.05f &&
        fixtures.visualRiskMetrics.localExposureDamageRisk > 0.05f;
    fixtures.renderedHighlightGrayMetricsPopulated =
        fixtures.visualRiskMetrics.highlightBandFraction > 0.25f &&
        fixtures.visualRiskMetrics.highlightMeanLuma > 0.55f &&
        fixtures.visualRiskMetrics.highlightLowSaturationFraction > 0.70f &&
        fixtures.visualRiskMetrics.highlightGrayRisk > 0.10f;
    fixtures.renderedMeaningfulHighlightMetricsPopulated =
        fixtures.visualRiskMetrics.highlightTileCoverage > 0.25f &&
        fixtures.visualRiskMetrics.highlightStructureScore > 0.10f &&
        fixtures.visualRiskMetrics.meaningfulHighlightPressure > 0.22f;
    fixtures.renderedLocalMetricsPopulated =
        fixtures.visualRiskMetrics.localLumaSpread > 0.35f &&
        fixtures.visualRiskMetrics.localEvSpreadStops > 1.0f &&
        fixtures.visualRiskMetrics.localEvConflict > 0.20f &&
        fixtures.visualRiskMetrics.localContrastPeak > 0.40f &&
        fixtures.visualRiskMetrics.localShadowPressure > 0.30f &&
        fixtures.visualRiskMetrics.centerMeanLuma > 0.10f;

    std::vector<unsigned char> subjectRiskPixels(
        static_cast<std::size_t>(visualRiskW * visualRiskH * 4),
        128);
    for (std::size_t offset = 0; offset < subjectRiskPixels.size(); offset += 4u) {
        subjectRiskPixels[offset + 0] = 122;
        subjectRiskPixels[offset + 1] = 122;
        subjectRiskPixels[offset + 2] = 122;
        subjectRiskPixels[offset + 3] = 255;
    }
    auto setSubjectRiskPixel = [&](int x, int y, unsigned char value) {
        const std::size_t offset = static_cast<std::size_t>((y * visualRiskW + x) * 4);
        subjectRiskPixels[offset + 0] = value;
        subjectRiskPixels[offset + 1] = value;
        subjectRiskPixels[offset + 2] = value;
        subjectRiskPixels[offset + 3] = 255;
    };
    for (int y = 0; y < visualRiskH; ++y) {
        for (int x = 4; x < visualRiskW; ++x) {
            setSubjectRiskPixel(x, y, 208);
        }
    }
    setSubjectRiskPixel(2, 2, 34);
    setSubjectRiskPixel(3, 2, 78);
    setSubjectRiskPixel(2, 3, 46);
    setSubjectRiskPixel(3, 3, 92);
    fixtures.subjectRiskMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            subjectRiskPixels,
            visualRiskW,
            visualRiskH);
    fixtures.renderedSubjectMetricsPopulated =
        fixtures.subjectRiskMetrics.subjectCenterPrior > 0.45f &&
        fixtures.subjectRiskMetrics.subjectImportanceConfidence > 0.35f &&
        std::max({
            fixtures.subjectRiskMetrics.subjectReadabilityPressure,
            fixtures.subjectRiskMetrics.subjectProtectionPressure,
            fixtures.subjectRiskMetrics.subjectMoodPreservationPressure }) > 0.10f;

    EditorRenderWorker::DevelopSubjectMetricSampling markedSubjectSampling;
    markedSubjectSampling.enabled = true;
    EditorRenderWorker::DevelopSubjectMetricRegion markedSubjectRegion;
    markedSubjectRegion.mode =
        static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Reveal);
    markedSubjectRegion.centerX = 0.50f;
    markedSubjectRegion.centerY = 0.50f;
    markedSubjectRegion.radiusX = 0.30f;
    markedSubjectRegion.radiusY = 0.30f;
    markedSubjectRegion.feather = 0.20f;
    markedSubjectRegion.strength = 0.95f;
    markedSubjectSampling.regions.push_back(markedSubjectRegion);
    fixtures.markedSubjectMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            subjectRiskPixels,
            visualRiskW,
            visualRiskH,
            markedSubjectSampling);

    EditorRenderWorker::DevelopSubjectMetricSampling lowPrioritySubjectSampling;
    lowPrioritySubjectSampling.enabled = true;
    EditorRenderWorker::DevelopSubjectMetricRegion lowPrioritySubjectRegion;
    lowPrioritySubjectRegion.mode =
        static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Ignore);
    lowPrioritySubjectRegion.lowPriority = true;
    lowPrioritySubjectRegion.centerX = 0.84f;
    lowPrioritySubjectRegion.centerY = 0.50f;
    lowPrioritySubjectRegion.radiusX = 0.24f;
    lowPrioritySubjectRegion.radiusY = 0.55f;
    lowPrioritySubjectRegion.feather = 0.15f;
    lowPrioritySubjectRegion.strength = 0.90f;
    lowPrioritySubjectSampling.regions.push_back(lowPrioritySubjectRegion);
    fixtures.lowPrioritySubjectMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            subjectRiskPixels,
            visualRiskW,
            visualRiskH,
            lowPrioritySubjectSampling);

    fixtures.renderedMarkedSubjectMetricsPopulated =
        fixtures.markedSubjectMetrics.subjectMarkedSampleCount > 0 &&
        fixtures.markedSubjectMetrics.subjectMarkedCoverage > 0.04f &&
        fixtures.markedSubjectMetrics.subjectMarkedPositiveCoverage > 0.04f &&
        fixtures.markedSubjectMetrics.subjectMarkedRevealCoverage > 0.01f &&
        fixtures.markedSubjectMetrics.subjectMarkedMeanLuma > 0.10f &&
        fixtures.markedSubjectMetrics.subjectMarkedMeanLuma < 0.55f &&
        fixtures.markedSubjectMetrics.subjectMarkedReadabilityScore > 0.20f &&
        fixtures.subjectRiskMetrics.subjectMarkedCoverage == 0.0f;
    fixtures.renderedMarkedLowPriorityMetricsPopulated =
        fixtures.lowPrioritySubjectMetrics.subjectMarkedLowPriorityCoverage > 0.04f &&
        fixtures.lowPrioritySubjectMetrics.subjectMarkedLowPriorityMeanLuma > 0.60f &&
        fixtures.lowPrioritySubjectMetrics.subjectMarkedLowPriorityBrightFraction > 0.70f &&
        fixtures.lowPrioritySubjectMetrics.subjectMarkedLowPriorityPressure > 0.03f;
    fixtures.renderedSpatialRiskMetricsPopulated =
        fixtures.spatialRiskMetrics.localDamageRiskPeak > 0.10f &&
        fixtures.spatialRiskMetrics.localDamageRiskMean > 0.03f &&
        fixtures.spatialRiskMetrics.localDamageRiskPeakTile >= 0 &&
        fixtures.spatialRiskMetrics.localDamageRiskPeakTile < 9 &&
        fixtures.spatialRiskMetrics.localDamageRiskScore[static_cast<std::size_t>(
            fixtures.spatialRiskMetrics.localDamageRiskPeakTile)] >=
            fixtures.spatialRiskMetrics.localDamageRiskPeak - 0.0001f;

    constexpr int colorCastW = 4;
    constexpr int colorCastH = 4;
    std::vector<unsigned char> colorCastPixels(
        static_cast<std::size_t>(colorCastW * colorCastH * 4),
        255);
    for (std::size_t offset = 0; offset < colorCastPixels.size(); offset += 4u) {
        colorCastPixels[offset + 0] = 24;
        colorCastPixels[offset + 1] = 212;
        colorCastPixels[offset + 2] = 42;
        colorCastPixels[offset + 3] = 255;
    }
    fixtures.colorCastMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            colorCastPixels,
            colorCastW,
            colorCastH);
    fixtures.renderedColorCastMetricsPopulated =
        fixtures.colorCastMetrics.meanGreen > fixtures.colorCastMetrics.meanRed + 0.50f &&
        fixtures.colorCastMetrics.meanGreen > fixtures.colorCastMetrics.meanBlue + 0.50f &&
        fixtures.colorCastMetrics.magentaGreenBias < -0.20f &&
        fixtures.colorCastMetrics.channelImbalance > 0.75f &&
        fixtures.colorCastMetrics.colorCastRisk > 0.75f;

    fixtures.similarRenderedMetrics = fixtures.visualRiskMetrics;
    fixtures.similarRenderedMetrics.meanLuma += 0.005f;
    fixtures.similarRenderedMetrics.medianLuma += 0.004f;
    fixtures.distinctRenderedMetrics = fixtures.visualRiskMetrics;
    fixtures.distinctRenderedMetrics.meanLuma = 0.72f;
    fixtures.distinctRenderedMetrics.medianLuma = 0.74f;
    fixtures.distinctRenderedMetrics.p10Luma = 0.58f;
    fixtures.distinctRenderedMetrics.p90Luma = 0.96f;
    fixtures.distinctRenderedMetrics.shadowFraction = 0.02f;
    fixtures.distinctRenderedMetrics.highlightFraction = 0.44f;
    fixtures.distinctRenderedMetrics.clippedFraction = 0.08f;

    fixtures.renderedDuplicateMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
            fixtures.visualRiskMetrics,
            fixtures.similarRenderedMetrics);
    fixtures.renderedDistinctMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
            fixtures.visualRiskMetrics,
            fixtures.distinctRenderedMetrics);

    Metrics localOnlyDistinctMetrics = fixtures.visualRiskMetrics;
    for (float& tileMean : localOnlyDistinctMetrics.localMeanLuma) {
        tileMean = 1.0f - tileMean;
    }
    localOnlyDistinctMetrics.localLumaSpread = 1.0f;
    localOnlyDistinctMetrics.centerMeanLuma = 1.0f - fixtures.visualRiskMetrics.centerMeanLuma;
    fixtures.renderedLocalMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
            fixtures.visualRiskMetrics,
            localOnlyDistinctMetrics);

    Metrics neutralColorMetrics = fixtures.visualRiskMetrics;
    neutralColorMetrics.meanRed = 0.42f;
    neutralColorMetrics.meanGreen = 0.42f;
    neutralColorMetrics.meanBlue = 0.42f;
    neutralColorMetrics.warmCoolBias = 0.0f;
    neutralColorMetrics.magentaGreenBias = 0.0f;
    neutralColorMetrics.channelImbalance = 0.0f;
    neutralColorMetrics.colorCastRisk = 0.0f;
    Metrics distinctColorMetrics = neutralColorMetrics;
    distinctColorMetrics.meanRed = 0.12f;
    distinctColorMetrics.meanGreen = 0.72f;
    distinctColorMetrics.meanBlue = 0.14f;
    distinctColorMetrics.magentaGreenBias = -0.44f;
    distinctColorMetrics.channelImbalance = 0.83f;
    distinctColorMetrics.colorCastRisk = 0.92f;
    fixtures.renderedColorMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
            neutralColorMetrics,
            distinctColorMetrics);

    Metrics distinctMarkedSubjectMetrics = fixtures.markedSubjectMetrics;
    distinctMarkedSubjectMetrics.subjectMarkedMeanLuma = 0.90f;
    distinctMarkedSubjectMetrics.subjectMarkedReadabilityScore = 0.04f;
    distinctMarkedSubjectMetrics.subjectMarkedProtectionRisk = 0.88f;
    fixtures.renderedMarkedSubjectMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
            fixtures.markedSubjectMetrics,
            distinctMarkedSubjectMetrics);
    fixtures.renderedDuplicateMetricDistanceWorks =
        fixtures.renderedDuplicateMetricDistance < 0.085f &&
        fixtures.renderedDistinctMetricDistance > 0.085f &&
        fixtures.renderedLocalMetricDistance > 0.085f &&
        fixtures.renderedColorMetricDistance > 0.085f &&
        fixtures.renderedMarkedSubjectMetricDistance > 0.085f;

    fixtures.finishOnlyStageBoundary =
        EditorModule::ClassifyDevelopRenderedStageBoundaryForValidation(
            fixtures.visualRiskMetrics,
            fixtures.distinctRenderedMetrics,
            true,
            fixtures.visualRiskMetrics,
            fixtures.similarRenderedMetrics,
            true,
            fixtures.finishOnlyFinalMetricDistance,
            fixtures.finishOnlyPreFinishMetricDistance);
    fixtures.preFinishChangedStageBoundary =
        EditorModule::ClassifyDevelopRenderedStageBoundaryForValidation(
            fixtures.visualRiskMetrics,
            fixtures.similarRenderedMetrics,
            true,
            fixtures.visualRiskMetrics,
            fixtures.distinctRenderedMetrics,
            true,
            fixtures.preFinishChangedFinalMetricDistance,
            fixtures.preFinishChangedPreFinishMetricDistance);
    fixtures.renderedStageBoundaryClassifierWorks =
        fixtures.finishOnlyStageBoundary == "finishToneOnly" &&
        fixtures.finishOnlyFinalMetricDistance >= 0.085f &&
        fixtures.finishOnlyPreFinishMetricDistance <= 0.055f &&
        fixtures.preFinishChangedStageBoundary == "preFinishChangedButFinalMasked" &&
        fixtures.preFinishChangedFinalMetricDistance < 0.085f &&
        fixtures.preFinishChangedPreFinishMetricDistance >= 0.085f;

    const bool stageAwareDuplicateFinalAndPreFinishClose =
        EditorModule::ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation(
            fixtures.similarRenderedMetrics,
            fixtures.visualRiskMetrics,
            true,
            fixtures.similarRenderedMetrics,
            true,
            fixtures.visualRiskMetrics,
            fixtures.stageAwareDuplicateFinalDistance,
            fixtures.stageAwareDuplicatePreFinishDistance,
            fixtures.stageAwareDuplicatePreFinishDistinct);
    const bool stageAwareDuplicatePreservesPreFinishDistinct =
        !EditorModule::ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation(
            fixtures.similarRenderedMetrics,
            fixtures.visualRiskMetrics,
            true,
            fixtures.distinctRenderedMetrics,
            true,
            fixtures.visualRiskMetrics,
            fixtures.stageAwareMaskedFinalDistance,
            fixtures.stageAwareMaskedPreFinishDistance,
            fixtures.stageAwareMaskedPreFinishDistinct);
    fixtures.renderedStageAwareDuplicateClusteringWorks =
        stageAwareDuplicateFinalAndPreFinishClose &&
        !fixtures.stageAwareDuplicatePreFinishDistinct &&
        fixtures.stageAwareDuplicateFinalDistance < 0.085f &&
        fixtures.stageAwareDuplicatePreFinishDistance < 0.085f &&
        stageAwareDuplicatePreservesPreFinishDistinct &&
        fixtures.stageAwareMaskedPreFinishDistinct &&
        fixtures.stageAwareMaskedFinalDistance < 0.085f &&
        fixtures.stageAwareMaskedPreFinishDistance >= 0.085f;

    Metrics damagedClipMetrics = fixtures.distinctRenderedMetrics;
    damagedClipMetrics.clippedFraction = 0.09f;
    damagedClipMetrics.highlightFraction = 0.82f;
    Metrics damagedHaloMetrics = fixtures.visualRiskMetrics;
    damagedHaloMetrics.haloRiskFraction = 0.26f;
    damagedHaloMetrics.edgeContrast = 0.48f;
    damagedHaloMetrics.localContrastPeak = 0.82f;
    Metrics damagedGrayMetrics = fixtures.visualRiskMetrics;
    damagedGrayMetrics.lowSaturationFraction = 0.96f;
    damagedGrayMetrics.meanSaturation = 0.03f;
    damagedGrayMetrics.contrastSpan = 0.18f;
    Metrics damagedShadowNoiseMetrics = fixtures.visualRiskMetrics;
    damagedShadowNoiseMetrics.shadowTextureRisk = 0.94f;
    damagedShadowNoiseMetrics.shadowFraction = 0.58f;
    damagedShadowNoiseMetrics.medianLuma = 0.24f;
    Metrics damagedSpatialHotspotMetrics = fixtures.visualRiskMetrics;
    damagedSpatialHotspotMetrics.localDamageRiskScore = {
        0.08f, 0.16f, 0.94f,
        0.12f, 0.18f, 0.88f,
        0.10f, 0.14f, 0.82f
    };
    damagedSpatialHotspotMetrics.localDamageRiskPeak = 0.94f;
    damagedSpatialHotspotMetrics.localDamageRiskMean = 0.36f;
    damagedSpatialHotspotMetrics.localDamageRiskPeakTile = 2;
    damagedSpatialHotspotMetrics.localHighlightPressure = 0.72f;
    damagedSpatialHotspotMetrics.localContrastPeak = 0.90f;
    Metrics safeRenderedMetrics = fixtures.visualRiskMetrics;
    safeRenderedMetrics.clippedFraction = 0.0f;
    safeRenderedMetrics.highlightFraction = 0.12f;
    safeRenderedMetrics.haloRiskFraction = 0.01f;
    safeRenderedMetrics.edgeContrast = 0.24f;
    safeRenderedMetrics.localContrastPeak = 0.32f;
    safeRenderedMetrics.lowSaturationFraction = 0.36f;
    safeRenderedMetrics.meanSaturation = 0.18f;
    safeRenderedMetrics.contrastSpan = 0.42f;
    safeRenderedMetrics.shadowTextureRisk = 0.16f;
    safeRenderedMetrics.shadowFraction = 0.20f;
    safeRenderedMetrics.medianLuma = 0.38f;
    Metrics damagedColorCastMetrics = safeRenderedMetrics;
    damagedColorCastMetrics.meanRed = 0.12f;
    damagedColorCastMetrics.meanGreen = 0.76f;
    damagedColorCastMetrics.meanBlue = 0.14f;
    damagedColorCastMetrics.magentaGreenBias = -0.46f;
    damagedColorCastMetrics.channelImbalance = 0.84f;
    damagedColorCastMetrics.colorCastRisk = 0.94f;
    damagedColorCastMetrics.meanSaturation = 0.64f;

    fixtures.damagedClipReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedClipMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    fixtures.damagedHaloReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedHaloMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    fixtures.damagedGrayReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedGrayMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    fixtures.damagedShadowNoiseReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedShadowNoiseMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    fixtures.damagedSpatialHotspotReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedSpatialHotspotMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    fixtures.damagedColorCastReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedColorCastMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    fixtures.safeRenderedDamageReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            safeRenderedMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    fixtures.renderedDamageClassifierWorks =
        !fixtures.damagedClipReason.empty() &&
        !fixtures.damagedHaloReason.empty() &&
        !fixtures.damagedGrayReason.empty() &&
        !fixtures.damagedShadowNoiseReason.empty() &&
        !fixtures.damagedSpatialHotspotReason.empty() &&
        !fixtures.damagedColorCastReason.empty() &&
        fixtures.safeRenderedDamageReason.empty();

    Metrics localCenterShadowMetrics = fixtures.visualRiskMetrics;
    localCenterShadowMetrics.meanLuma = 0.32f;
    localCenterShadowMetrics.medianLuma = 0.30f;
    localCenterShadowMetrics.shadowFraction = 0.22f;
    localCenterShadowMetrics.highlightFraction = 0.04f;
    localCenterShadowMetrics.clippedFraction = 0.0f;
    localCenterShadowMetrics.centerMeanLuma = 0.11f;
    localCenterShadowMetrics.centerShadowFraction = 0.68f;
    localCenterShadowMetrics.localShadowPressure = 0.68f;
    localCenterShadowMetrics.localHighlightPressure = 0.06f;
    localCenterShadowMetrics.shadowTextureRisk = 0.18f;
    std::string localCenterShadowReason;
    fixtures.localCenterShadowIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localCenterShadowMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localCenterShadowReason);

    Metrics localHighlightMetrics = fixtures.visualRiskMetrics;
    localHighlightMetrics.meanLuma = 0.38f;
    localHighlightMetrics.medianLuma = 0.36f;
    localHighlightMetrics.highlightFraction = 0.12f;
    localHighlightMetrics.clippedFraction = 0.0f;
    localHighlightMetrics.localHighlightPressure = 0.76f;
    localHighlightMetrics.centerHighlightFraction = 0.18f;
    std::string localHighlightReason;
    fixtures.localHighlightIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localHighlightMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localHighlightReason);

    Metrics structuredHighlightPressureMetrics = fixtures.visualRiskMetrics;
    structuredHighlightPressureMetrics.meanLuma = 0.40f;
    structuredHighlightPressureMetrics.medianLuma = 0.36f;
    structuredHighlightPressureMetrics.highlightFraction = 0.12f;
    structuredHighlightPressureMetrics.clippedFraction = 0.0f;
    structuredHighlightPressureMetrics.localHighlightPressure = 0.62f;
    structuredHighlightPressureMetrics.meaningfulHighlightPressure = 0.72f;
    fixtures.structuredHighlightPressureIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            structuredHighlightPressureMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            fixtures.structuredHighlightPressureReason);

    Metrics localSpatialHighlightRiskMetrics = fixtures.visualRiskMetrics;
    localSpatialHighlightRiskMetrics.meanLuma = 0.40f;
    localSpatialHighlightRiskMetrics.medianLuma = 0.36f;
    localSpatialHighlightRiskMetrics.highlightFraction = 0.12f;
    localSpatialHighlightRiskMetrics.clippedFraction = 0.0f;
    localSpatialHighlightRiskMetrics.haloRiskFraction = 0.0f;
    localSpatialHighlightRiskMetrics.edgeContrast = 0.24f;
    localSpatialHighlightRiskMetrics.localHighlightPressure = 0.62f;
    localSpatialHighlightRiskMetrics.localShadowPressure = 0.12f;
    localSpatialHighlightRiskMetrics.localContrastPeak = 0.48f;
    localSpatialHighlightRiskMetrics.localDamageRiskPeak = 0.78f;
    localSpatialHighlightRiskMetrics.localDamageRiskMean = 0.18f;
    localSpatialHighlightRiskMetrics.centerHighlightFraction = 0.12f;
    localSpatialHighlightRiskMetrics.highlightTileCoverage = 0.0f;
    localSpatialHighlightRiskMetrics.highlightStructureScore = 0.0f;
    localSpatialHighlightRiskMetrics.meaningfulHighlightPressure = 0.0f;
    fixtures.localSpatialHighlightRiskIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localSpatialHighlightRiskMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            fixtures.localSpatialHighlightRiskReason);

    Metrics localFlatMetrics = fixtures.visualRiskMetrics;
    localFlatMetrics.meanLuma = 0.36f;
    localFlatMetrics.medianLuma = 0.34f;
    localFlatMetrics.contrastSpan = 0.28f;
    localFlatMetrics.highlightFraction = 0.06f;
    localFlatMetrics.clippedFraction = 0.0f;
    localFlatMetrics.lowSaturationFraction = 0.30f;
    localFlatMetrics.localLumaSpread = 0.08f;
    localFlatMetrics.localContrastPeak = 0.20f;
    localFlatMetrics.localHighlightPressure = 0.04f;
    localFlatMetrics.localShadowPressure = 0.12f;
    std::string localFlatReason;
    fixtures.localFlatIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localFlatMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localFlatReason);

    fixtures.renderedCleanShadowMetrics = fixtures.visualRiskMetrics;
    fixtures.renderedCleanShadowMetrics.meanLuma = 0.31f;
    fixtures.renderedCleanShadowMetrics.medianLuma = 0.29f;
    fixtures.renderedCleanShadowMetrics.shadowFraction = 0.34f;
    fixtures.renderedCleanShadowMetrics.highlightFraction = 0.04f;
    fixtures.renderedCleanShadowMetrics.clippedFraction = 0.0f;
    fixtures.renderedCleanShadowMetrics.haloRiskFraction = 0.01f;
    fixtures.renderedCleanShadowMetrics.shadowTextureRisk = 0.84f;
    fixtures.renderedCleanShadowMetrics.localShadowPressure = 0.56f;
    fixtures.renderedCleanShadowMetrics.localHighlightPressure = 0.05f;
    fixtures.renderedCleanShadowMetrics.localContrastPeak = 0.34f;
    fixtures.renderedCleanShadowMetrics.centerMeanLuma = 0.20f;
    fixtures.renderedCleanShadowMetrics.centerShadowFraction = 0.42f;
    fixtures.renderedCleanShadowMetrics.centerHighlightFraction = 0.0f;
    std::string renderedCleanShadowReason;
    fixtures.renderedCleanShadowIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            fixtures.renderedCleanShadowMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            renderedCleanShadowReason);

    Metrics localSpatialShadowRiskMetrics = fixtures.visualRiskMetrics;
    localSpatialShadowRiskMetrics.meanLuma = 0.30f;
    localSpatialShadowRiskMetrics.medianLuma = 0.28f;
    localSpatialShadowRiskMetrics.shadowFraction = 0.22f;
    localSpatialShadowRiskMetrics.highlightFraction = 0.04f;
    localSpatialShadowRiskMetrics.clippedFraction = 0.0f;
    localSpatialShadowRiskMetrics.haloRiskFraction = 0.0f;
    localSpatialShadowRiskMetrics.shadowTextureRisk = 0.60f;
    localSpatialShadowRiskMetrics.localShadowPressure = 0.70f;
    localSpatialShadowRiskMetrics.localHighlightPressure = 0.05f;
    localSpatialShadowRiskMetrics.localContrastPeak = 0.36f;
    localSpatialShadowRiskMetrics.localDamageRiskPeak = 0.76f;
    localSpatialShadowRiskMetrics.localDamageRiskMean = 0.16f;
    localSpatialShadowRiskMetrics.centerShadowFraction = 0.24f;
    fixtures.localSpatialShadowRiskIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localSpatialShadowRiskMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            fixtures.localSpatialShadowRiskReason);

    Metrics localSpatialFlatRiskMetrics = fixtures.visualRiskMetrics;
    localSpatialFlatRiskMetrics.meanLuma = 0.36f;
    localSpatialFlatRiskMetrics.medianLuma = 0.34f;
    localSpatialFlatRiskMetrics.contrastSpan = 0.28f;
    localSpatialFlatRiskMetrics.lowSaturationFraction = 0.72f;
    localSpatialFlatRiskMetrics.highlightFraction = 0.06f;
    localSpatialFlatRiskMetrics.clippedFraction = 0.0f;
    localSpatialFlatRiskMetrics.localLumaSpread = 0.20f;
    localSpatialFlatRiskMetrics.localContrastPeak = 0.18f;
    localSpatialFlatRiskMetrics.localHighlightPressure = 0.04f;
    localSpatialFlatRiskMetrics.localShadowPressure = 0.12f;
    localSpatialFlatRiskMetrics.localDamageRiskPeak = 0.74f;
    localSpatialFlatRiskMetrics.localDamageRiskMean = 0.14f;
    fixtures.localSpatialFlatRiskIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localSpatialFlatRiskMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            fixtures.localSpatialFlatRiskReason);

    Metrics renderedPreserveTextureMetrics = fixtures.visualRiskMetrics;
    renderedPreserveTextureMetrics.meanLuma = 0.42f;
    renderedPreserveTextureMetrics.medianLuma = 0.36f;
    renderedPreserveTextureMetrics.shadowFraction = 0.10f;
    renderedPreserveTextureMetrics.highlightFraction = 0.06f;
    renderedPreserveTextureMetrics.clippedFraction = 0.0f;
    renderedPreserveTextureMetrics.contrastSpan = 0.40f;
    renderedPreserveTextureMetrics.lowSaturationFraction = 0.35f;
    renderedPreserveTextureMetrics.edgeContrast = 0.18f;
    renderedPreserveTextureMetrics.haloRiskFraction = 0.01f;
    renderedPreserveTextureMetrics.shadowTextureRisk = 0.12f;
    renderedPreserveTextureMetrics.localLumaSpread = 0.18f;
    renderedPreserveTextureMetrics.localContrastPeak = 0.34f;
    renderedPreserveTextureMetrics.localShadowPressure = 0.12f;
    renderedPreserveTextureMetrics.localHighlightPressure = 0.04f;
    renderedPreserveTextureMetrics.centerMeanLuma = 0.34f;
    renderedPreserveTextureMetrics.centerShadowFraction = 0.08f;
    renderedPreserveTextureMetrics.centerHighlightFraction = 0.0f;
    std::string renderedPreserveTextureReason;
    fixtures.renderedPreserveTextureIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            renderedPreserveTextureMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            renderedPreserveTextureReason);

    fixtures.renderedLocalRefineIntentWorks =
        fixtures.localCenterShadowIntent == "openShadows" &&
        fixtures.localHighlightIntent == "protectHighlights" &&
        fixtures.structuredHighlightPressureIntent == "protectHighlights" &&
        fixtures.localSpatialHighlightRiskIntent == "protectHighlights" &&
        fixtures.localFlatIntent == "addContrast" &&
        fixtures.renderedCleanShadowIntent == "cleanShadows" &&
        fixtures.localSpatialShadowRiskIntent == "cleanShadows" &&
        fixtures.localSpatialFlatRiskIntent == "addContrast" &&
        fixtures.renderedPreserveTextureIntent == "preserveTexture" &&
        !localCenterShadowReason.empty() &&
        !localHighlightReason.empty() &&
        fixtures.structuredHighlightPressureReason.find("structured") != std::string::npos &&
        fixtures.localSpatialHighlightRiskReason.find("spatial") != std::string::npos &&
        !localFlatReason.empty() &&
        !renderedCleanShadowReason.empty() &&
        fixtures.localSpatialShadowRiskReason.find("spatial") != std::string::npos &&
        fixtures.localSpatialFlatRiskReason.find("spatial") != std::string::npos &&
        !renderedPreserveTextureReason.empty();

    Metrics relativeSelectedMetrics = fixtures.visualRiskMetrics;
    relativeSelectedMetrics.meanLuma = 0.42f;
    relativeSelectedMetrics.medianLuma = 0.40f;
    relativeSelectedMetrics.shadowFraction = 0.16f;
    relativeSelectedMetrics.highlightFraction = 0.62f;
    relativeSelectedMetrics.clippedFraction = 0.028f;
    relativeSelectedMetrics.contrastSpan = 0.48f;
    relativeSelectedMetrics.edgeContrast = 0.22f;
    relativeSelectedMetrics.haloRiskFraction = 0.02f;
    relativeSelectedMetrics.shadowTextureRisk = 0.18f;
    relativeSelectedMetrics.localHighlightPressure = 0.66f;
    relativeSelectedMetrics.localShadowPressure = 0.16f;
    relativeSelectedMetrics.localDamageRiskPeak = 0.42f;
    relativeSelectedMetrics.localDamageRiskMean = 0.10f;
    relativeSelectedMetrics.centerHighlightFraction = 0.24f;
    relativeSelectedMetrics.centerShadowFraction = 0.08f;
    Metrics relativeRawScoreWinnerMetrics = relativeSelectedMetrics;
    relativeRawScoreWinnerMetrics.meanLuma = 0.48f;
    relativeRawScoreWinnerMetrics.medianLuma = 0.47f;
    relativeRawScoreWinnerMetrics.highlightFraction = 0.76f;
    relativeRawScoreWinnerMetrics.clippedFraction = 0.075f;
    relativeRawScoreWinnerMetrics.localHighlightPressure = 0.76f;
    relativeRawScoreWinnerMetrics.localDamageRiskPeak = 0.58f;
    relativeRawScoreWinnerMetrics.centerHighlightFraction = 0.36f;
    Metrics relativeIntentWinnerMetrics = relativeSelectedMetrics;
    relativeIntentWinnerMetrics.meanLuma = 0.40f;
    relativeIntentWinnerMetrics.medianLuma = 0.39f;
    relativeIntentWinnerMetrics.highlightFraction = 0.42f;
    relativeIntentWinnerMetrics.clippedFraction = 0.004f;
    relativeIntentWinnerMetrics.localHighlightPressure = 0.34f;
    relativeIntentWinnerMetrics.localDamageRiskPeak = 0.30f;
    relativeIntentWinnerMetrics.centerHighlightFraction = 0.10f;
    std::string relativeRawScoreMetric;
    float relativeRawScoreDistance = -1.0f;
    float relativeRawScoreRepairBonus = 0.0f;
    fixtures.relativeAdjustedRawScore =
        EditorModule::ScoreDevelopRenderedCandidateRelativeToSelectedForValidation(
            relativeRawScoreWinnerMetrics,
            0.74f,
            relativeSelectedMetrics,
            0.66f,
            "protectHighlights",
            fixtures.relativeRawScoreStatus,
            relativeRawScoreMetric,
            relativeRawScoreDistance,
            fixtures.relativeRawScoreRepairDelta,
            relativeRawScoreRepairBonus,
            fixtures.relativeRawScoreRegressionPenalty);
    std::string relativeIntentMetric;
    float relativeIntentDistance = -1.0f;
    float relativeIntentRepairBonus = 0.0f;
    float relativeIntentRegressionPenalty = 0.0f;
    fixtures.relativeAdjustedIntentScore =
        EditorModule::ScoreDevelopRenderedCandidateRelativeToSelectedForValidation(
            relativeIntentWinnerMetrics,
            0.70f,
            relativeSelectedMetrics,
            0.66f,
            "protectHighlights",
            fixtures.relativeIntentStatus,
            relativeIntentMetric,
            relativeIntentDistance,
            fixtures.relativeIntentRepairDelta,
            relativeIntentRepairBonus,
            relativeIntentRegressionPenalty);
    fixtures.renderedRelativeComparisonWorks =
        fixtures.relativeAdjustedIntentScore > fixtures.relativeAdjustedRawScore + 0.035f &&
        fixtures.relativeIntentStatus == "improvesActiveRepair" &&
        (fixtures.relativeRawScoreStatus == "missedActiveRepair" ||
         fixtures.relativeRawScoreStatus == "regressedAgainstSelected") &&
        relativeIntentMetric == "highlightPressure" &&
        fixtures.relativeIntentRepairDelta > 0.05f &&
        fixtures.relativeRawScoreRepairDelta < -0.05f &&
        fixtures.relativeRawScoreRegressionPenalty > relativeRawScoreRepairBonus;

    return fixtures;
}

} // namespace Stack::Validation::Detail
