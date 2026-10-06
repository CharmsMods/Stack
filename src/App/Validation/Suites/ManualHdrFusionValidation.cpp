#include "App/Validation/ValidationSuites.h"
#include "Raw/MultiFrameHdr/Processor.h"
#include "Raw/MultiFrame/GraphProcessor.h"
#include "Persistence/RawProjectModel.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <unordered_map>

namespace Stack::Validation {
namespace {
bool Check(bool ok, const char* text) {
    if (!ok) std::cerr << "Manual HDR Fusion: " << text << '\n';
    return ok;
}
Raw::RawImageData Frame(char id, float exposure) {
    Raw::RawImageData raw;
    auto& m = raw.metadata;
    m.sourceContentSha256 = std::string(64, id); m.sourcePath = std::string(1, id) + ".dng";
    m.rawWidth = m.visibleWidth = 96; m.rawHeight = m.visibleHeight = 80;
    m.sourceByteSize = 96 * 80 * 2;
    m.cameraMake = "Stack validation"; m.cameraModel = "Manual fusion";
    m.cfaPattern = Raw::CfaPattern::RGGB; m.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    m.mosaiced = true; m.isDng = true; m.orientation = 1; m.bitDepth = 16;
    m.blackLevel = 64; m.perChannelBlack.fill(64); m.whiteLevel = 65000;
    m.exposureTimeSeconds = exposure * 0.16f; m.isoSpeed = 100; m.apertureFNumber = 4;
    m.hasExposureTime = m.hasIsoSpeed = m.hasApertureFNumber = true;
    m.hasDngNoiseProfile = true;
    m.dngNoiseProfile = {{1.0e-6, 1.0e-8}, {1.0e-6, 1.0e-8}, {1.0e-6, 1.0e-8}};
    raw.rawBuffer.resize(96 * 80);
    for (int y = 0; y < 80; ++y) for (int x = 0; x < 96; ++x) {
        float scene = 0.08f + 0.025f * std::sin(x * 0.16f) + 0.02f * std::cos(y * 0.12f);
        if (x < 16 && y < 16) scene = 0.36f + 0.007f * x;
        if (x > 60 && y > 48) scene = 0.8f;
        if (x > 82 && y > 68) scene = 3.0f;
        // A colored highlight clips only red in the longer capture.
        if (x >= 30 && x < 46 && y >= 30 && y < 46) scene = (x % 2 == 0 && y % 2 == 0) ? 0.65f : 0.1f;
        raw.rawBuffer[y * 96 + x] = static_cast<std::uint16_t>(64 + 64936 * std::min(1.0f, exposure * scene));
    }
    return raw;
}
}

bool ValidateManualHdrFusion() {
    using namespace Raw::Hdr;
    bool ok = true;
    FusionSourceInfo sources[3] { {"short", -2}, {"middle", 0}, {"long", 2} };
    FusionObservation observations[3] { {0.2f, 0.03f, 1, 1, 1}, {0.2f, 0.02f, 1, 1, 1}, {0.2f, 0.01f, 1, 1, 1} };
    FusionControls controls;
    auto baseline = EvaluateFusionPixel(controls, sources, observations, 3, 0, 0, 0.5f, 0.5f);
    controls.sources = {{"long", true, 1}};
    auto biased = EvaluateFusionPixel(controls, sources, observations, 3, 0, 0, 0.5f, 0.5f);
    ok &= Check(std::abs(biased.contribution[2] / biased.contribution[0] - 2) < 1e-5f, "+1 bias stop doubles relative weight");
    ok &= Check(std::abs(biased.scene - baseline.scene) < 1e-6f, "bias does not change equal-radiance brightness");
    const float expectedVariance = 0.25f * 0.25f * 0.03f + 0.25f * 0.25f * 0.02f + 0.5f * 0.5f * 0.01f;
    ok &= Check(std::abs(biased.variance - expectedVariance) < 1e-6f, "manual variance uses squared normalized weights");
    controls.targetEv = 1;
    auto lifted = EvaluateFusionPixel(controls, sources, observations, 3, 0, 0, 0.5f, 0.5f);
    ok &= Check(std::abs(lifted.scene - biased.scene * 2) < 1e-6f && std::abs(lifted.variance - biased.variance * 4) < 1e-6f,
        "target exposure scales radiance and variance independently of trust");
    observations[2].automaticWeight = 0; controls.sources[0].biasStops = 12;
    lifted = EvaluateFusionPixel(controls, sources, observations, 3, 0, 0, 0.5f, 0.5f);
    ok &= Check(lifted.contribution[2] == 0, "manual bias cannot resurrect hard clipping or rejection");
    controls = {}; controls.sources = {{"short", false}, {"middle", false}, {"long", false}};
    lifted = EvaluateFusionPixel(controls, sources, observations, 3, 0, 0, 0.5f, 0.5f);
    ok &= Check(lifted.fallback && lifted.contribution[0] == 1 && std::abs(lifted.scene - 0.2f) < 1e-6f, "all excluded sources give explicit anchor fallback");
    controls = {}; FusionLocalMask mask; mask.targetEv = 1; mask.preferredEv = 0.63f; controls.masks.push_back(mask);
    ok &= Check(EvaluateFusionField(controls, 0, 0.5f, 0.5f).targetEv == 1 && EvaluateFusionField(controls, 0, 0, 0).targetEv == 0,
        "local masks act only inside their feathered region");
    FusionControls roundtrip;
    std::string error;
    ok &= Check(DeserializeFusionControls(SerializeFusionControls(controls), roundtrip, &error) &&
        SerializeFusionControls(roundtrip) == SerializeFusionControls(controls), "controls and masks survive serialization");
    auto malformed = SerializeFusionControls(controls); malformed["targetEv"] = "bad";
    ok &= Check(!DeserializeFusionControls(malformed, roundtrip, &error), "malformed persisted controls fail safely");
    controls = {}; controls.preferSources = true; controls.targetFollow = 1; controls.blendWidthEv = 0.25f; controls.targetEv = 12;
    observations[2].automaticWeight = 1;
    lifted = EvaluateFusionPixel(controls, sources, observations, 3, 0, 0, 0.5f, 0.5f);
    ok &= Check(std::isfinite(lifted.scene) && lifted.contribution[2] > 0.99f, "log weights remain normalized under extreme preference");

    const auto temp = std::filesystem::temp_directory_path() / ("stack-manual-fusion-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p, e); } } cleanup{temp};
    std::unordered_map<std::string, Raw::RawImageData> frames {{"a.dng", Frame('a', 0.5f)}, {"b.dng", Frame('b', 2)}};
    // Preserve the recorded exposure while varying aperture and shutter. The
    // production processor must accept this and retain its brightness checks.
    frames.at("b.dng").metadata.apertureFNumber = 8;
    frames.at("b.dng").metadata.exposureTimeSeconds *= 4;
    int decodes = 0;
    Services services;
    services.loadRawFrame = [&](const auto& path, auto& raw, const auto&, auto& problem) {
        const auto it = frames.find(path.generic_string());
        if (it == frames.end()) { problem = "missing test frame"; return false; }
        ++decodes; raw = it->second; return true;
    };
    Request request; request.workingDirectory = temp / "processor"; request.interactiveFusion = true;
    request.parameters.alignmentMode = AlignmentMode::Identity; request.workerCount = 2;
    request.geometricReferenceFrameIndex = request.radiometricAnchorFrameIndex = 0;
    request.frames = {{"short", "a.dng", std::string(64, 'a'), 96 * 80 * 2}, {"long", "b.dng", std::string(64, 'b'), 96 * 80 * 2}};
    const auto neutral = ProcessBurst(request, services);
    ok &= Check(neutral.status == ProcessingStatus::Published, neutral.message.c_str());
    if (neutral.status != ProcessingStatus::Published) return false;
    ok &= Check(neutral.preparedFusion && neutral.fusionPreview && decodes == 2, "neutral result retains prepared data and preview measurements");
    const auto& neutralPreview = *neutral.fusionPreview;
    const auto headroomAt = [&](int x) { return neutralPreview.observations[
        ((4 * neutralPreview.width + x / 2) * 4) * 2 + 1].headroom; };
    ok &= Check(headroomAt(4) > headroomAt(14) && headroomAt(14) > 0 && headroomAt(14) < 1,
        "near-clipped sources fade fractionally before hard clipping");
    request.preparedFusion = neutral.preparedFusion; request.inputRevision++;
    request.fusion.targetEv = 1;
    const auto active = ProcessBurst(request, services);
    ok &= Check(active.status == ProcessingStatus::Published && active.diagnostics.reusedPreparation && decodes == 2,
        "target edits reuse RAW decoding and registration across input revisions");
    if (active.status != ProcessingStatus::Published) return false;
    float maxError = 0;
    for (std::size_t i = 0; i < neutral.virtualAnchorMosaic.size(); ++i)
        maxError = std::max(maxError, std::abs(active.virtualAnchorMosaic[i] - 2 * neutral.virtualAnchorMosaic[i]));
    ok &= Check(maxError < 2e-5f, "full-resolution target applies exactly once, including boundary pixels");
    ok &= Check(active.fusionPreview->analysisEv == neutral.fusionPreview->analysisEv, "analysis luminance remains fixed during target edits");
    request.fusion.targetCurve = {2, 1, 0.5f, 0, -0.5f, -1, -1};
    request.fusion.sources = {{"long", true, 0.63f}};
    request.fusion.masks = {mask};
    const auto curved = ProcessBurst(request, services);
    ok &= Check(curved.status == ProcessingStatus::Published && decodes == 2, "curves, bias and local masks reuse prepared data");
    if (curved.status != ProcessingStatus::Published) return false;
    const auto& preview = *curved.fusionPreview;
    float previewError = 0, sumError = 0;
    for (std::size_t i = 0; i < preview.analysisEv.size(); ++i) for (std::size_t parity = 0; parity < 4; ++parity) {
        const auto x = (i % preview.width) * 2 * preview.stride + parity % 2;
        const auto y = (i / preview.width) * 2 * preview.stride + parity / 2;
        const auto value = EvaluateFusionPixel(request.fusion, preview.sources.data(),
            preview.observations.data() + (i * 4 + parity) * preview.sources.size(), preview.sources.size(), preview.anchor,
            preview.analysisEv[i], static_cast<float>((x + 0.5) / preview.rawWidth), static_cast<float>((y + 0.5) / preview.rawHeight));
        previewError = std::max(previewError, std::abs(value.scene - curved.virtualAnchorMosaic[y * preview.rawWidth + x]));
        sumError = std::max(sumError, std::abs(std::accumulate(value.contribution.begin(), value.contribution.end(), 0.0f) - 1));
        if (x >= 32 && x < 44 && y >= 32 && y < 44 && parity == 0)
            ok &= Check(value.contribution[1] == 0, "hard-clipped red receives zero weight despite positive bias");
    }
    ok &= Check(previewError < 2e-5f && sumError < 1e-5f, "interactive preview matches full-resolution estimator and sums to 100 percent");
    request.parameters.alignmentMode = AlignmentMode::VerifyOnly;
    auto verified = ProcessBurst(request, services);
    ok &= Check(verified.status == ProcessingStatus::Published && decodes > 2 && !verified.diagnostics.reusedPreparation,
        "registration edits invalidate preparation");
    for (const auto& f : verified.diagnostics.frames) ok &= Check(f.translationRawX == 0 && f.translationRawY == 0, "verify-only never applies a transform");
    request.shouldCancel = [] { return true; };
    const auto canceled = ProcessBurst(request, services);
    ok &= Check(canceled.status == ProcessingStatus::Canceled && canceled.virtualAnchorMosaic.empty(), "cancellation does not publish partial pixels");

    // Author, serialize, and execute two denoise groups into HDR Fusion.
    Stack::Project::RawProjectSnapshot snapshot; snapshot.projectId = "manual-fusion-test";
    Stack::Project::MultiFrameSourceSet set; set.sourceSetId = "set";
    set.operationIntent = Stack::Project::MultiFrameOperationIntent::RawBurstHdr;
    for (int i = 0; i < 4; ++i) {
        const char id = static_cast<char>('a' + i); const auto raw = Frame(id, i < 2 ? 0.5f : 2);
        frames[raw.metadata.sourcePath] = raw;
        Stack::Project::EmbeddedAssetRecord asset; asset.assetId = std::string("asset-") + id;
        asset.sha256 = raw.metadata.sourceContentSha256; asset.byteLength = raw.metadata.sourceByteSize;
        asset.originalFilename = raw.metadata.sourcePath;
        asset.captureMetadataSummary = Stack::Project::SerializeRawCaptureCompatibilitySummary(Stack::Project::BuildRawCaptureCompatibilitySummary(raw.metadata));
        snapshot.embeddedAssets.push_back(asset);
        Stack::Project::SourceSetFrame frame; frame.frameId = std::string("frame-") + id; frame.assetId = asset.assetId;
        set.frames.push_back(frame);
    }
    snapshot.sourceSets.push_back(set); snapshot.activeSourceSetId = set.sourceSetId;
    ok &= Check(Stack::Project::BuildExposureGroupedMultiFrameGraph(snapshot, set, snapshot.multiFrameGraph, &error), error.c_str());
    const auto graphJson = Stack::Project::SerializeMultiFrameGraph(snapshot.multiFrameGraph);
    Stack::Project::MultiFrameGraphDocument graphRoundtrip;
    ok &= Check(Stack::Project::DeserializeMultiFrameGraph(graphJson, graphRoundtrip, &error), "grouped graph round trips");
    Raw::MultiFrame::GraphProcessingRequest graphRequest; graphRequest.snapshot = snapshot; graphRequest.workingDirectory = temp / "graph";
    graphRequest.loadRawFrame = services.loadRawFrame;
    for (int i = 0; i < 4; ++i) graphRequest.materializedSourcePathsByFrameId[std::string("frame-") + static_cast<char>('a' + i)] = std::string(1, static_cast<char>('a' + i)) + ".dng";
    auto grouped = Raw::MultiFrame::ProcessMultiFrameGraph(graphRequest);
    ok &= Check(grouped.status == Raw::MultiFrame::GraphProcessingStatus::Completed, grouped.message.c_str());
    if (grouped.status == Raw::MultiFrame::GraphProcessingStatus::Completed) {
        ok &= Check(grouped.outputMeasurement->planes.variance && grouped.outputHdrResult && grouped.outputHdrResult->fusionPreview,
            "group uncertainty reaches the HDR result");
        auto wrongGeometry = std::make_shared<Raw::MultiFrame::RawMeasurementHandle>(*grouped.outputMeasurement);
        wrongGeometry->geometryId = "different-geometry";
        Raw::MultiFrame::RawMeasurementSetHandle incompatible {{grouped.outputMeasurement, wrongGeometry}};
        ok &= Check(!Raw::MultiFrame::ValidateFusionMeasurementCompatibility(incompatible, &error),
            "different geometry IDs require explicit registration");
        graphRequest.cache = grouped.cache; const int before = decodes;
        for (auto& node : graphRequest.snapshot.multiFrameGraph.nodes) if (node.kind == Stack::Project::MultiFrameGraphNodeKind::HdrMerge) {
            FusionControls f; f.targetEv = 0.18f; node.settings["fusion"] = SerializeFusionControls(f);
        }
        auto edited = Raw::MultiFrame::ProcessMultiFrameGraph(graphRequest);
        ok &= Check(edited.status == Raw::MultiFrame::GraphProcessingStatus::Completed && decodes == before,
            "HDR-only graph edits reuse both burst branches and registration");
        graphRequest.cache = edited.cache;
        graphRequest.snapshot.sourceSets.front().frames.front().enabled = false;
        const auto excluded = Raw::MultiFrame::ProcessMultiFrameGraph(graphRequest);
        ok &= Check(excluded.status == Raw::MultiFrame::GraphProcessingStatus::Completed,
            "a group with one included capture still feeds valid original evidence into HDR");
    }
    std::cout << "Manual HDR Fusion " << (ok ? "passed" : "FAILED") << "; preview error=" << previewError << "; sum error=" << sumError << '\n';
    return ok;
}
} // namespace Stack::Validation
