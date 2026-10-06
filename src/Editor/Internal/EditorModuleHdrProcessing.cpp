#include "Utils/UiBusyState.h"
#include "Editor/EditorModule.h"

#include "App/AppPaths.h"
#include "Async/TaskSystem.h"
#include "Notifications/AsyncActivity.h"
#include "Editor/MultiFrameResultCache.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Raw/MultiFrame/GraphExecution.h"
#include "Raw/MultiFrame/PublicationFence.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/MultiFrameHdr/Processor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>

namespace {

bool FinishHdr(std::string* output, const std::string& message, bool value) {
    if (output) *output = message;
    return value;
}

std::filesystem::path MaterializedHdrAssetPath(
    const std::filesystem::path& sourceDirectory,
    const Stack::Project::EmbeddedAssetRecord& asset) {
    std::string extension =
        std::filesystem::path(asset.originalFilename).extension().string();
    if (!extension.empty() && extension.front() != '.') extension.insert(extension.begin(), '.');
    return sourceDirectory /
        (asset.sha256 + "-" + std::to_string(asset.byteLength) + extension);
}

bool CachedHdrAssetMatchesSize(
    const std::filesystem::path& path,
    std::uint64_t expectedBytes) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error &&
        static_cast<std::uint64_t>(std::filesystem::file_size(path, error)) == expectedBytes &&
        !error;
}

std::uint64_t CacheKeyHash64(const std::string& value) {
    if (value.size() < 16u) return std::hash<std::string> {}(value);
    try { return std::stoull(value.substr(0u, 16u), nullptr, 16); }
    catch (...) { return std::hash<std::string> {}(value); }
}

std::shared_ptr<Raw::RawImageData> BuildHdrRawData(
    const std::shared_ptr<const Raw::Hdr::Result>& published,
    Raw::RawMetadata metadata,
    const std::string& projectId,
    const std::string& sourceSetId) {
    if (!published || published->virtualAnchorMosaic.empty()) return {};
    auto raw = std::make_shared<Raw::RawImageData>();
    raw->metadata = std::move(metadata);
    raw->metadata.sourcePath = "hdr://" + projectId + "/" + sourceSetId;
    raw->metadata.sourceContentSha256 = published->diagnostics.cacheKey;
    raw->metadata.sourceByteSize =
        published->virtualAnchorMosaic.size() * sizeof(float);
    raw->metadata.rawWidth = static_cast<int>(published->width);
    raw->metadata.rawHeight = static_cast<int>(published->height);
    raw->metadata.visibleWidth = raw->metadata.rawWidth;
    raw->metadata.visibleHeight = raw->metadata.rawHeight;
    raw->metadata.leftMargin = 0;
    raw->metadata.topMargin = 0;
    raw->metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw->metadata.mosaiced = true;
    raw->metadata.cfaPattern = published->outputCfaPattern;
    raw->metadata.uploadFormat = "R32F";
    raw->metadata.dngActiveArea = {
        0, 0, raw->metadata.rawHeight, raw->metadata.rawWidth };
    raw->metadata.hasDngActiveArea = true;
    // BaselineExposure belongs to a single DNG capture. Carrying the
    // geometric reference's value onto a synthetic radiometric anchor would
    // create a second, invisible exposure adjustment.
    raw->metadata.hasDngBaselineExposure = false;
    raw->metadata.dngBaselineExposure = 0.0f;
    raw->metadata.dngMaskedAreas.clear();
    raw->normalizedMosaicInputContract =
        Raw::NormalizedMosaicInputContract::HdrVirtualAnchorPreGain;
    raw->normalizedMosaicBuffer =
        std::shared_ptr<const std::vector<float>>(
            published, &published->virtualAnchorMosaic);
    raw->normalizedMosaicContentHash = CacheKeyHash64(
        published->diagnostics.cacheKey);
    auto sidecars = std::make_shared<Raw::RawImageData::HdrSidecars>();
    sidecars->varianceProxy = std::shared_ptr<const std::vector<float>>(
        published, &published->varianceProxy);
    sidecars->mergeConfidence = std::shared_ptr<const std::vector<float>>(
        published, &published->mergeConfidence);
    sidecars->effectiveSampleCount = std::shared_ptr<const std::vector<float>>(
        published, &published->effectiveSampleCount);
    sidecars->recoveredHeadroomStops = std::shared_ptr<const std::vector<float>>(
        published, &published->recoveredHeadroomStops);
    sidecars->validityMask = std::shared_ptr<const std::vector<std::uint8_t>>(
        published, &published->validityMask);
    sidecars->ownerFrame = std::shared_ptr<const std::vector<std::uint8_t>>(
        published, &published->ownerFrame);
    sidecars->flags = std::shared_ptr<const std::vector<std::uint8_t>>(
        published, &published->flags);
    sidecars->geometricReferenceFrameId =
        published->geometricReferenceFrameId;
    sidecars->radiometricAnchorFrameId =
        published->radiometricAnchorFrameId;
    raw->hdrSidecars = std::move(sidecars);
    return raw;
}

double CaptureExposureMetric(
    const Stack::Project::RawCaptureCompatibilitySummary& capture) {
    if (capture.exposureTimeSeconds <= 0.0) return 0.0;
    const double aperture = capture.apertureFNumber > 0.0
        ? capture.apertureFNumber : 1.0;
    const double iso = capture.isoSpeed > 0.0 ? capture.isoSpeed : 100.0;
    return capture.exposureTimeSeconds * iso / (aperture * aperture);
}

std::string ShutterLabel(double seconds) {
    if (!(seconds > 0.0)) return "shutter ?";
    std::ostringstream stream;
    if (seconds < 0.5) {
        stream << "1/" << static_cast<int>(std::llround(1.0 / seconds)) << " s";
    } else {
        stream << std::fixed << std::setprecision(seconds < 10.0 ? 2 : 1)
               << seconds << " s";
    }
    return stream.str();
}

} // namespace

bool EditorModule::SetHdrProcessingConfiguration(
    const std::string& sourceSetId,
    Raw::Hdr::AlignmentMode alignmentMode,
    bool automaticGeometricReference,
    const std::string& geometricReferenceFrameId,
    bool automaticRadiometricAnchor,
    const std::string& radiometricAnchorFrameId,
    std::string* outError) {
    if (!IsMultiFrameRawProjectActive())
        return FinishHdr(outError, "No multi-frame RAW project is active.", false);
    if (IsHdrProcessingBusy())
        return FinishHdr(outError, "Cancel the current HDR run before changing its inputs.", false);

    Stack::Project::RawProjectSnapshot snapshot = *m_Project->snapshot;
    Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet || sourceSet->operationIntent !=
            Stack::Project::MultiFrameOperationIntent::RawBurstHdr) {
        return FinishHdr(outError, "The selected source set is not a RAW HDR bracket.", false);
    }
    const auto hasFrame = [&](const std::string& frameId) {
        return std::any_of(sourceSet->frames.begin(), sourceSet->frames.end(),
            [&](const Stack::Project::SourceSetFrame& frame) {
                return frame.frameId == frameId;
            });
    };
    if (!automaticGeometricReference && !hasFrame(geometricReferenceFrameId))
        return FinishHdr(outError, "Choose a valid geometric reference frame.", false);
    if (!automaticRadiometricAnchor && !hasFrame(radiometricAnchorFrameId))
        return FinishHdr(outError, "Choose a valid radiometric anchor frame.", false);

    Raw::Hdr::Parameters parameters;
    std::string parameterError;
    if (!Raw::Hdr::DeserializeParameters(
            sourceSet->settings.value("parameters", nlohmann::json::object()),
            parameters, &parameterError)) {
        return FinishHdr(outError, parameterError, false);
    }
    parameters.alignmentMode = alignmentMode;
    const nlohmann::json serializedParameters = Raw::Hdr::SerializeParameters(parameters);
    const std::string requestedReference = automaticGeometricReference
        ? sourceSet->referenceFrameId : geometricReferenceFrameId;
    const std::string requestedAnchor = automaticRadiometricAnchor
        ? std::string() : radiometricAnchorFrameId;
    const nlohmann::json currentAnchor = sourceSet->settings.value(
        "radiometricAnchorFrameId", nlohmann::json(nullptr));
    const std::string currentAnchorId = currentAnchor.is_string()
        ? currentAnchor.get<std::string>() : std::string();
    const bool changed =
        sourceSet->settings.value("parameters", nlohmann::json::object()) !=
            serializedParameters ||
        sourceSet->settings.value("automaticGeometricReference", true) !=
            automaticGeometricReference ||
        sourceSet->settings.value("automaticRadiometricAnchor", true) !=
            automaticRadiometricAnchor ||
        (!automaticGeometricReference &&
         sourceSet->referenceFrameId != requestedReference) ||
        currentAnchorId != requestedAnchor;
    if (!changed) return FinishHdr(outError, std::string(), true);

    sourceSet->settings["parameters"] = serializedParameters;
    sourceSet->settings["automaticGeometricReference"] =
        automaticGeometricReference;
    sourceSet->settings["automaticRadiometricAnchor"] =
        automaticRadiometricAnchor;
    sourceSet->settings["radiometricAnchorFrameId"] = automaticRadiometricAnchor
        ? nlohmann::json(nullptr) : nlohmann::json(requestedAnchor);
    if (!automaticGeometricReference) {
        sourceSet->referenceFrameId = requestedReference;
        snapshot.activeFrameId = requestedReference;
    }
    nlohmann::json previousResult = sourceSet->settings.value(
        "result", nlohmann::json::object());
    if (!previousResult.is_object()) previousResult = nlohmann::json::object();
    previousResult["state"] = "stale";
    sourceSet->settings["result"] = std::move(previousResult);
    ++snapshot.hdrInputRevision;

    EditorNodeGraph::Graph graph = m_Project->graph;
    for (EditorNodeGraph::Node& node : graph.EditNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr &&
            node.multiFrameHdr.sourceSetId == sourceSetId) {
            node.multiFrameHdr.presentationStatus =
                EditorNodeGraph::kHdrAwaitingProcessingStatus;
            node.multiFrameHdr.resultState = "stale";
            node.multiFrameHdr.radiometricAnchorFrameId = requestedAnchor;
        } else if (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
                   node.rawProjectFrame.sourceSetId == sourceSetId) {
            node.rawProjectFrame.reference =
                node.rawProjectFrame.frameId == sourceSet->referenceFrameId;
        }
    }
    const auto transaction = m_Project->store->BeginTransaction(
        snapshot.persistedStorageRevision);
    if (!transaction)
        return FinishHdr(outError, "Could not begin the HDR settings transaction.", false);
    return CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, outError);
}

bool EditorModule::PublishHdrResultToRawWorkspace(
    HdrAdoptedRawResult adopted,
    const std::filesystem::path& cacheDirectory,
    std::string* outError) {
    if (!m_Project->snapshot || !adopted.rawData || !adopted.result ||
        adopted.result->status != Raw::Hdr::ProcessingStatus::Published ||
        adopted.rawData->normalizedMosaicBuffer == nullptr ||
        adopted.rawData->normalizedMosaicBuffer->empty()) {
        return FinishHdr(
            outError, "The HDR processor did not provide a publishable RAW result.", false);
    }
    if (m_Project->snapshot->projectId != adopted.projectId ||
        m_Project->snapshot->hdrInputRevision != adopted.inputRevision) {
        return FinishHdr(
            outError, "The HDR result no longer matches the active project inputs.", false);
    }

    auto snapshotCopy = std::make_shared<Stack::Project::RawProjectSnapshot>(
        *m_Project->snapshot);
    Stack::Project::MultiFrameSourceSet* set =
        Stack::Project::FindSourceSet(*snapshotCopy, adopted.sourceSetId);
    if (!set) {
        return FinishHdr(
            outError, "The HDR result source set no longer exists.", false);
    }

    nlohmann::json resultState = {
        { "state", "ready" },
        { "algorithmVersion", Raw::Hdr::kAlgorithmVersion },
        { "cacheKey", adopted.result->diagnostics.cacheKey },
        { "inputRevision", adopted.inputRevision },
        { "geometricReferenceFrameId",
            adopted.result->geometricReferenceFrameId },
        { "radiometricAnchorFrameId",
            adopted.result->radiometricAnchorFrameId },
        { "referenceMetadata",
            EditorNodeGraph::SerializeRawMetadata(
                adopted.result->referenceMetadata) },
        { "provenance", Raw::Hdr::SerializeDiagnostics(*adopted.result) }
    };
    if (!cacheDirectory.empty()) {
        resultState["cacheDirectory"] = cacheDirectory.generic_string();
    }
    set->settings["result"] = std::move(resultState);
    set->settings["processingImplemented"] = true;
    m_Project->snapshot = std::move(snapshotCopy);

    HdrProcessingReport report;
    report.projectId = adopted.projectId;
    report.sourceSetId = adopted.sourceSetId;
    report.inputRevision = adopted.inputRevision;
    report.statusName = Raw::Hdr::ProcessingStatusName(adopted.result->status);
    report.message = adopted.result->message;
    report.cacheKey = adopted.result->diagnostics.cacheKey;
    report.executionBackend = adopted.result->diagnostics.executionBackend;
    report.gpuDeviceIdentity = adopted.result->diagnostics.gpuDeviceIdentity;
    report.gpuFallbackReason = adopted.result->diagnostics.gpuFallbackReason;
    report.gpuDispatchedTileCount =
        adopted.result->diagnostics.gpuDispatchedTileCount;
    report.exposureSpanEv = adopted.result->diagnostics.exposureSpanEv;
    report.meanEffectiveSamples =
        adopted.result->diagnostics.meanEffectiveSamples;
    report.colorCoherentRepairPixelCount =
        adopted.result->diagnostics.colorCoherentRepairPixelCount;
    report.warnings = adopted.result->diagnostics.warnings;
    report.frames = adopted.result->diagnostics.frames;
    report.exposureFitEdges = adopted.result->diagnostics.exposureFitEdges;

    const std::string sourceSetId = adopted.sourceSetId;
    const std::string radiometricAnchorFrameId =
        adopted.result->radiometricAnchorFrameId;
    m_HdrProcessingReport = std::move(report);
    m_HdrAdoptedRawResult = std::move(adopted);
    for (EditorNodeGraph::Node& node : m_Project->graph.EditNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr &&
            node.multiFrameHdr.sourceSetId == sourceSetId &&
            node.multiFrameHdr.managed && !node.multiFrameHdr.quarantined) {
            node.multiFrameHdr.presentationStatus =
                "Scene-linear HDR result ready.";
            node.multiFrameHdr.resultState = "ready";
            node.multiFrameHdr.radiometricAnchorFrameId =
                radiometricAnchorFrameId;
        }
    }
    MarkDirty();
    m_MultiFrameProjectCoverRefreshPending = true;
    MarkRenderDirty();
    return FinishHdr(outError, std::string(), true);
}

void EditorModule::RestoreHdrResultCacheAfterProjectLoad() {
    if (IsBracketingActive()) return;
    m_HdrAdoptedRawResult.reset();
    m_HdrProcessingReport.reset();
    m_HdrProcessingTaskState = Async::TaskState::Idle;
    m_MfdAdoptedRawResult.reset();
    if (!m_Project->snapshot) return;

    const Stack::Project::RawProjectSnapshot& snapshot =
        *m_Project->snapshot;
    Stack::Project::RawProjectEditRecipeBinding binding;
    if (!Stack::Project::ResolveRawProjectEditRecipe(
            snapshot,
            snapshot.activeSourceSetId,
            binding,
            nullptr) ||
        !binding.multiFrameResult) {
        return;
    }
    const Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, binding.sourceSetId);
    if (!sourceSet) return;
    const nlohmann::json resultState = sourceSet->settings.value(
        "result", nlohmann::json::object());
    if (!resultState.is_object() ||
        resultState.value("state", std::string()) != "ready") {
        return;
    }

    const Raw::MultiFrame::GraphExecutionPlan plan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(snapshot);
    if (!plan.valid || plan.contentIdentitySha256.empty()) return;
    const std::string savedGraphIdentity = resultState.value(
        "graphIdentity", std::string());
    if (!savedGraphIdentity.empty() &&
        savedGraphIdentity != plan.contentIdentitySha256) {
        return;
    }
    const std::uint64_t inputRevision = binding.hdrResult
        ? snapshot.hdrInputRevision
        : snapshot.mfdInputRevision;
    if (resultState.value("inputRevision", std::uint64_t { 0 }) !=
        inputRevision) {
        return;
    }

    const std::string projectId = snapshot.projectId;
    const std::string sourceSetId = sourceSet->sourceSetId;
    const std::string graphIdentity = plan.contentIdentitySha256;
    const bool hdrResult = binding.hdrResult;
    const std::uint64_t generation =
        m_MultiFrameGraphProcessingGeneration.fetch_add(
            1u, std::memory_order_relaxed) + 1u;
    m_MultiFrameGraphProcessingTaskState = Async::TaskState::Queued;
    m_MultiFrameGraphProcessingProjectId = projectId;
    m_MultiFrameGraphProcessingSourceSetId = sourceSetId;
    m_MultiFrameGraphProcessingIdentity = graphIdentity;
    m_MultiFrameGraphProcessingStatusText =
        "Restoring the saved MultiFrame RAW result...";
    const auto notifier = GetNotifier();
    const auto activity = notifier.BeginActivity("Processing frames", false);
    m_MultiFrameProcessingActivity = activity;
    notifier.UpdateActivity(activity, "Restoring saved result...");
    const auto completionLease = Stack::Notifications::RetainAsyncActivity(notifier, activity);
    const auto activityMetadata = Stack::Notifications::ForAsyncActivity(notifier, activity, "Processing frames");

    struct RestoreOutcome {
        std::string error;
        std::shared_ptr<Raw::RawImageData> raw;
        std::shared_ptr<const Raw::Hdr::Result> hdr;
    };
    const std::string cacheKey = resultState.value(
        "cacheKey", std::string());
    const nlohmann::json metadataJson = resultState.value(
        "referenceMetadata", nlohmann::json::object());
    const std::string persistedCacheDirectory = resultState.value(
        "cacheDirectory", std::string());
    const std::filesystem::path hdrCacheDirectory =
        persistedCacheDirectory.empty()
        ? AppPaths::GetCacheDirectory() / "MultiFrameHdr" /
            projectId / sourceSetId / "processor" / "results"
        : std::filesystem::path(persistedCacheDirectory);
    const std::filesystem::path burstCachePath =
        std::filesystem::path(resultState.value(
            "cachePath", std::string()));
    const bool submitted = ProjectTasks().Submit(activityMetadata, [
        this,
        notifier, activity, completionLease,
        generation,
        projectId,
        sourceSetId,
        graphIdentity,
        inputRevision,
        hdrResult,
        cacheKey,
        metadataJson,
        hdrCacheDirectory,
        burstCachePath
    ]() mutable {
        RestoreOutcome outcome;
        if (hdrResult) {
            Raw::Hdr::Result cached;
            if (!metadataJson.is_object() || metadataJson.empty()) {
                outcome.error = "HDR cache metadata is unavailable.";
            } else if (!Raw::Hdr::ReadResultCache(
                    hdrCacheDirectory,
                    cacheKey,
                    cached,
                    &outcome.error)) {
                // ReadResultCache supplies the concrete verification failure.
            } else {
                cached.referenceMetadata =
                    EditorNodeGraph::DeserializeRawMetadata(metadataJson);
                outcome.hdr =
                    std::make_shared<const Raw::Hdr::Result>(
                        std::move(cached));
                outcome.raw = BuildHdrRawData(
                    outcome.hdr,
                    outcome.hdr->referenceMetadata,
                    projectId,
                    sourceSetId);
                if (!outcome.raw) {
                    outcome.error =
                        "The verified HDR cache could not be rebuilt as RAW data.";
                }
            }
        } else if (burstCachePath.empty()) {
            outcome.error = "The saved Burst result has no cache path.";
        } else {
            Stack::EditorMultiFrameCache::ReadBurstResult(
                burstCachePath,
                graphIdentity,
                inputRevision,
                outcome.raw,
                &outcome.error);
        }

        ProjectTasks().PostToMain([
            this,
            notifier, activity, completionLease,
            generation,
            projectId,
            sourceSetId,
            graphIdentity,
            inputRevision,
            hdrResult,
            outcome = std::move(outcome)
        ]() mutable {
            if (generation !=
                m_MultiFrameGraphProcessingGeneration.load(
                    std::memory_order_relaxed) ||
                !m_Project->snapshot ||
                m_Project->snapshot->projectId != projectId) {
                notifier.CancelActivity(activity, "Saved result restore superseded.");
                return;
            }
            const Raw::MultiFrame::GraphExecutionPlan activePlan =
                Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(
                    *m_Project->snapshot);
            const std::uint64_t activeRevision = hdrResult
                ? m_Project->snapshot->hdrInputRevision
                : m_Project->snapshot->mfdInputRevision;
            if (!activePlan.valid ||
                activePlan.contentIdentitySha256 != graphIdentity ||
                activeRevision != inputRevision) {
                m_MultiFrameGraphProcessingTaskState =
                    Async::TaskState::Idle;
                m_MultiFrameGraphProcessingStatusText =
                    "The saved result no longer matches the active graph.";
                notifier.CancelActivity(activity, m_MultiFrameGraphProcessingStatusText);
                return;
            }
            if (!outcome.raw || !outcome.error.empty()) {
                m_MultiFrameGraphProcessingTaskState =
                    Async::TaskState::Failed;
                m_MultiFrameGraphProcessingStatusText =
                    outcome.error.empty()
                    ? "The saved MultiFrame result cache is unavailable."
                    : outcome.error;
                m_RawWorkspaceStaleRenderStatusText =
                    m_MultiFrameGraphProcessingStatusText +
                    " Reprocess the connected Output.";
                if (Stack::Project::MultiFrameSourceSet* activeSet =
                        Stack::Project::FindSourceSet(
                            *m_Project->snapshot, sourceSetId)) {
                    activeSet->settings["result"]["state"] = "stale";
                }
                for (EditorNodeGraph::Node& node :
                     m_Project->graph.EditNodes()) {
                    if (node.kind ==
                            EditorNodeGraph::NodeKind::MultiFrameHdr &&
                        node.multiFrameHdr.sourceSetId == sourceSetId) {
                        node.multiFrameHdr.resultState = "stale";
                        node.multiFrameHdr.presentationStatus =
                            "Saved result cache is missing or corrupt. Reprocess the connected Output.";
                    }
                }
                notifier.FailActivity(activity, "The saved result could not be opened.",
                    m_MultiFrameGraphProcessingStatusText);
                return;
            }

            if (hdrResult && outcome.hdr) {
                HdrAdoptedRawResult adopted;
                adopted.projectId = projectId;
                adopted.sourceSetId = sourceSetId;
                adopted.inputRevision = inputRevision;
                adopted.contentHash =
                    outcome.raw->normalizedMosaicContentHash;
                adopted.rawData = outcome.raw;
                adopted.result = outcome.hdr;
                m_HdrAdoptedRawResult = std::move(adopted);

                HdrProcessingReport report;
                report.projectId = projectId;
                report.sourceSetId = sourceSetId;
                report.inputRevision = inputRevision;
                report.statusName =
                    Raw::Hdr::ProcessingStatusName(outcome.hdr->status);
                report.message = outcome.hdr->message;
                report.cacheKey = outcome.hdr->diagnostics.cacheKey;
                report.executionBackend =
                    outcome.hdr->diagnostics.executionBackend;
                report.gpuDeviceIdentity =
                    outcome.hdr->diagnostics.gpuDeviceIdentity;
                report.gpuFallbackReason =
                    outcome.hdr->diagnostics.gpuFallbackReason;
                report.gpuDispatchedTileCount =
                    outcome.hdr->diagnostics.gpuDispatchedTileCount;
                report.exposureSpanEv =
                    outcome.hdr->diagnostics.exposureSpanEv;
                report.meanEffectiveSamples =
                    outcome.hdr->diagnostics.meanEffectiveSamples;
                report.colorCoherentRepairPixelCount =
                    outcome.hdr->diagnostics.colorCoherentRepairPixelCount;
                report.warnings = outcome.hdr->diagnostics.warnings;
                report.frames = outcome.hdr->diagnostics.frames;
                report.exposureFitEdges =
                    outcome.hdr->diagnostics.exposureFitEdges;
                m_HdrProcessingReport = std::move(report);
                m_HdrProcessingTaskState = Async::TaskState::Ready;
                m_HdrProcessingStatusText =
                    "Verified cached HDR result restored.";
            } else {
                MfdAdoptedRawResult adopted;
                adopted.projectId = projectId;
                adopted.sourceSetId = sourceSetId;
                adopted.inputRevision = inputRevision;
                adopted.contentHash = CacheKeyHash64(graphIdentity);
                adopted.rawData = std::move(outcome.raw);
                m_MfdAdoptedRawResult = std::move(adopted);
                m_MfdExperimentalProcessingReport.reset();
            }
            for (EditorNodeGraph::Node& node : m_Project->graph.EditNodes()) {
                if (hdrResult &&
                    node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr &&
                    node.multiFrameHdr.sourceSetId == sourceSetId) {
                    node.multiFrameHdr.resultState = "ready";
                    node.multiFrameHdr.presentationStatus =
                        "Verified cached HDR result ready.";
                } else if (!hdrResult &&
                    node.kind ==
                        EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                    node.multiFrameDenoise.sourceSetId == sourceSetId) {
                    node.multiFrameDenoise.resultState = "ready";
                    node.multiFrameDenoise.presentationStatus =
                        "Verified cached Burst result ready.";
                }
            }
            m_MultiFrameGraphProcessingTaskState = Async::TaskState::Ready;
            m_MultiFrameGraphProcessingStatusText =
                "Verified cached MultiFrame RAW result restored.";
            m_RawWorkspaceStaleRenderStatusText.clear();
            m_MultiFrameProjectCoverRefreshPending = true;
            MarkRenderDirty();
            notifier.CompleteActivity(activity, "Saved RAW result opened.", false);
        });
    });
    if (!submitted) {
        m_MultiFrameGraphProcessingTaskState = Async::TaskState::Failed;
        m_MultiFrameGraphProcessingStatusText =
            "Stack could not queue the saved MultiFrame result restore.";
        notifier.FailActivity(activity, m_MultiFrameGraphProcessingStatusText);
    } else {
        m_MultiFrameGraphProcessingTaskState = Async::TaskState::Running;
    }
}

bool EditorModule::StartHdrProcessing(
    const std::string& sourceSetId,
    std::string* outError) {
    if(RequestAutoBracketForeground("start processing",[this,sourceSetId]{StartHdrProcessing(sourceSetId,nullptr);}))return true;
    if (IsBracketingActive()) return StartBracketingProcessing(true, outError);
    if (!IsMultiFrameRawProjectActive())
        return FinishHdr(outError, "No multi-frame RAW project is active.", false);
    if (IsHdrProcessingBusy() || IsMfdExperimentalProcessingBusy())
        return FinishHdr(outError, "A multi-frame processor is already running.", false);
    if (IsDirty() && m_DocumentPersistenceEnabled) {
        std::string saveError;
        if (!SaveActiveMultiFrameRawProject(&saveError)) {
            return FinishHdr(
                outError,
                saveError.empty()
                    ? "Stack could not save the project before the memory-intensive run."
                    : "Stack could not save the project before processing: " + saveError,
                false);
        }
    }

    Stack::Project::RawProjectSnapshot snapshot = *m_Project->snapshot;
    const Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet ||
        sourceSet->operationIntent != Stack::Project::MultiFrameOperationIntent::RawBurstHdr)
        return FinishHdr(outError, "The selected source set is not a RAW HDR bracket.", false);
    const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [](const Stack::Project::SourceSetFrame& frame) { return frame.enabled; }));
    if (enabledCount < Raw::Hdr::kMinimumFrameCount ||
        enabledCount > Raw::Hdr::kMaximumFrameCount)
        return FinishHdr(outError, "Enable between two and twenty HDR frames.", false);

    Raw::Hdr::Parameters parameters;
    std::string parameterError;
    if (!Raw::Hdr::DeserializeParameters(
            sourceSet->settings.value("parameters", nlohmann::json::object()),
            parameters, &parameterError)) {
        return FinishHdr(outError, parameterError, false);
    }
    const auto memory = Raw::Mfd::ResolveMfdProcessingMemoryBudget(
        0.0, Raw::Mfd::QueryPhysicalMemorySnapshot());
    if (!memory.valid)
        return FinishHdr(outError, memory.message, false);

    const Stack::Project::ProjectStoreHandle store = m_Project->store;
    const std::uint64_t generation = m_HdrProcessingGeneration.fetch_add(
        1u, std::memory_order_relaxed) + 1u;
    const std::string projectId = snapshot.projectId;
    const std::uint64_t inputRevision = snapshot.hdrInputRevision;
    const std::filesystem::path workingRoot =
        AppPaths::GetCacheDirectory() / "MultiFrameHdr" / projectId / sourceSetId;
    auto progressState = std::make_shared<MfdExperimentalProcessingProgressState>();
    m_HdrProcessingProgress = progressState;
    m_HdrProcessingTaskState = Async::TaskState::Queued;
    m_HdrProcessingProjectId = projectId;
    m_HdrProcessingSourceSetId = sourceSetId;
    m_HdrProcessingInputRevision = inputRevision;
    m_HdrProcessingStatusText = "Queuing scene-linear Bayer HDR processing...";
    const auto notifier = GetNotifier();
    const auto activity = notifier.BeginActivity("Merging HDR");
    m_HdrProcessingActivity = activity;
    const auto completionLease = Stack::Notifications::RetainAsyncActivity(notifier, activity);
    const auto activityMetadata = Stack::Notifications::ForAsyncActivity(notifier, activity, "Merging HDR");

    struct Outcome {
        bool publish = false;
        bool canceled = false;
        std::string message;
        std::optional<HdrAdoptedRawResult> adopted;
    };

    bool submitted = false;
    try {
        submitted = ProjectTasks().SubmitHighPriority(activityMetadata, [
            this, generation, projectId, sourceSetId, inputRevision, notifier, activity, completionLease,
            workingRoot, store, parameters, memory, progressState,
            snapshot = std::move(snapshot)
        ]() mutable {
            const auto canceled = [this, generation]() {
                return generation != m_HdrProcessingGeneration.load(
                    std::memory_order_relaxed);
            };
            const auto recordProgress = [progressState, notifier, activity](const Raw::Hdr::Progress& progress) {
                std::lock_guard<std::mutex> lock(progressState->mutex);
                progressState->stageLabel = Raw::Hdr::ProcessingStageName(progress.stage);
                progressState->message = progress.message;
                progressState->overallFraction = std::clamp(
                    progress.overallFraction, progressState->overallFraction, 1.0);
                progressState->stageFraction = std::clamp(progress.stageFraction, 0.0, 1.0);
                progressState->frameOrdinal = progress.frameOrdinal;
                progressState->frameCount = progress.frameCount;
                progressState->lastAdvancedAt = std::chrono::steady_clock::now();
                std::optional<Stack::Notifications::Progress> measured;
                if (progress.frameCount > 0) measured = Stack::Notifications::Progress{
                    static_cast<double>(progress.frameOrdinal), static_cast<double>(progress.frameCount), "Captures"};
                notifier.UpdateActivity(activity, progressState->stageLabel, measured);
            };
            ProjectTasks().PostToMain([this, generation, notifier, activity, completionLease]() {
                if (generation != m_HdrProcessingGeneration.load(std::memory_order_relaxed)) return;
                m_HdrProcessingTaskState = Async::TaskState::Running;
                m_HdrProcessingStatusText = "Preparing and aligning the HDR bracket...";
                notifier.UpdateActivity(activity, "Preparing HDR captures...");
            });

            Outcome outcome;
            try {
                const Stack::Project::MultiFrameSourceSet* workerSet =
                    Stack::Project::FindSourceSet(snapshot, sourceSetId);
                if (!workerSet) {
                    outcome.message = "The HDR source set disappeared before processing.";
                } else {
                    Raw::Hdr::Request request;
                    request.parameters = parameters;
                    request.workingDirectory = workingRoot / "processor";
                    request.inputRevision = inputRevision;
                    request.memoryBudgetBytes = memory.budgetBytes;
                    request.workerCount = std::max(
                        1u, std::thread::hardware_concurrency());
                    request.enforceMemoryBudget = false;
                    request.shouldCancel = canceled;
                    request.reportProgress = recordProgress;
                    const bool automaticReference = workerSet->settings.value(
                        "automaticGeometricReference", true);
                    const bool automaticAnchor = workerSet->settings.value(
                        "automaticRadiometricAnchor", true);
                    const nlohmann::json anchorValue = workerSet->settings.value(
                        "radiometricAnchorFrameId", nlohmann::json(nullptr));
                    const std::string anchorFrameId = anchorValue.is_string()
                        ? anchorValue.get<std::string>() : std::string();
                    const std::filesystem::path sourceDirectory = workingRoot / "sources";
                    std::string requestError;
                    for (const Stack::Project::SourceSetFrame& frame : workerSet->frames) {
                        if (!frame.enabled) continue;
                        if (canceled()) {
                            outcome.canceled = true;
                            outcome.message = "HDR processing was canceled; the previous result remains active.";
                            break;
                        }
                        const Stack::Project::EmbeddedAssetRecord* asset =
                            Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
                        if (!asset) {
                            outcome.message = "An enabled HDR frame has no embedded original.";
                            break;
                        }
                        const std::filesystem::path materialized =
                            MaterializedHdrAssetPath(sourceDirectory, *asset);
                        if (!CachedHdrAssetMatchesSize(materialized, asset->byteLength) &&
                            !store->CopyAssetToFile(asset->assetId, materialized, &requestError)) {
                            outcome.message = requestError.empty()
                                ? "An embedded HDR original could not be materialized."
                                : requestError;
                            break;
                        }
                        Raw::Hdr::FrameInput input;
                        input.stableFrameId = frame.frameId;
                        input.sourcePath = materialized;
                        input.expectedSourceSha256 = asset->sha256;
                        input.expectedSourceByteLength = asset->byteLength;
                        if (!automaticReference && frame.frameId == workerSet->referenceFrameId)
                            request.geometricReferenceFrameIndex =
                                static_cast<std::int64_t>(request.frames.size());
                        if (!automaticAnchor && frame.frameId == anchorFrameId)
                            request.radiometricAnchorFrameIndex =
                                static_cast<std::int64_t>(request.frames.size());
                        request.frames.push_back(std::move(input));
                    }
                    if (!outcome.canceled && outcome.message.empty()) {
                        Raw::Hdr::Services services =
                            Raw::Hdr::MakeFilesystemServices();
                        if (m_RawRenderClientId != 0 ||
                            m_RenderWorkerAvailable) {
                            services.executeOpenGlTask = [this](
                                Raw::Hdr::OpenGlTask task,
                                std::string& error) {
                                return ExecuteRenderOwnerOpenGlTaskBlocking(
                                    std::move(task), error);
                            };
                        }
                        Raw::Hdr::Result processed = Raw::Hdr::ProcessBurst(
                            request, services);
                        if (processed.status == Raw::Hdr::ProcessingStatus::Canceled || canceled()) {
                            outcome.canceled = true;
                            outcome.message = "HDR processing was canceled; the previous result remains active.";
                        } else if (processed.status != Raw::Hdr::ProcessingStatus::Published) {
                            outcome.message = processed.message;
                        } else {
                            auto published = std::make_shared<const Raw::Hdr::Result>(
                                std::move(processed));
                            auto raw = std::make_shared<Raw::RawImageData>();
                            raw->metadata = published->referenceMetadata;
                            raw->metadata.sourcePath = "hdr://" + projectId + "/" + sourceSetId;
                            raw->metadata.sourceContentSha256 = published->diagnostics.cacheKey;
                            raw->metadata.sourceByteSize = published->virtualAnchorMosaic.size() * sizeof(float);
                            raw->metadata.rawWidth = static_cast<int>(published->width);
                            raw->metadata.rawHeight = static_cast<int>(published->height);
                            raw->metadata.visibleWidth = raw->metadata.rawWidth;
                            raw->metadata.visibleHeight = raw->metadata.rawHeight;
                            raw->metadata.leftMargin = 0;
                            raw->metadata.topMargin = 0;
                            raw->metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
                            raw->metadata.mosaiced = true;
                            raw->metadata.cfaPattern = published->outputCfaPattern;
                            raw->metadata.uploadFormat = "R32F";
                            raw->metadata.dngActiveArea = {
                                0, 0, raw->metadata.rawHeight, raw->metadata.rawWidth };
                            raw->metadata.hasDngActiveArea = true;
                            raw->metadata.hasDngBaselineExposure = false;
                            raw->metadata.dngBaselineExposure = 0.0f;
                            raw->metadata.dngMaskedAreas.clear();
                            raw->normalizedMosaicInputContract =
                                Raw::NormalizedMosaicInputContract::HdrVirtualAnchorPreGain;
                            raw->normalizedMosaicBuffer =
                                std::shared_ptr<const std::vector<float>>(
                                    published, &published->virtualAnchorMosaic);
                            raw->normalizedMosaicContentHash = CacheKeyHash64(
                                published->diagnostics.cacheKey);
                            auto sidecars = std::make_shared<Raw::RawImageData::HdrSidecars>();
                            sidecars->varianceProxy = std::shared_ptr<const std::vector<float>>(
                                published, &published->varianceProxy);
                            sidecars->mergeConfidence = std::shared_ptr<const std::vector<float>>(
                                published, &published->mergeConfidence);
                            sidecars->effectiveSampleCount = std::shared_ptr<const std::vector<float>>(
                                published, &published->effectiveSampleCount);
                            sidecars->recoveredHeadroomStops = std::shared_ptr<const std::vector<float>>(
                                published, &published->recoveredHeadroomStops);
                            sidecars->validityMask = std::shared_ptr<const std::vector<std::uint8_t>>(
                                published, &published->validityMask);
                            sidecars->ownerFrame = std::shared_ptr<const std::vector<std::uint8_t>>(
                                published, &published->ownerFrame);
                            sidecars->flags = std::shared_ptr<const std::vector<std::uint8_t>>(
                                published, &published->flags);
                            sidecars->geometricReferenceFrameId =
                                published->geometricReferenceFrameId;
                            sidecars->radiometricAnchorFrameId =
                                published->radiometricAnchorFrameId;
                            raw->hdrSidecars = std::move(sidecars);
                            HdrAdoptedRawResult adopted;
                            adopted.projectId = projectId;
                            adopted.sourceSetId = sourceSetId;
                            adopted.inputRevision = inputRevision;
                            adopted.contentHash = raw->normalizedMosaicContentHash;
                            adopted.rawData = std::move(raw);
                            adopted.result = published;
                            outcome.adopted = std::move(adopted);
                            outcome.message = published->message;
                            outcome.publish = true;
                        }
                    }
                }
            } catch (const std::bad_alloc&) {
                outcome.message = "The system could not satisfy an HDR memory allocation; the saved project and previous result remain active.";
            } catch (const std::exception& exception) {
                outcome.message = std::string("HDR processing failed: ") + exception.what();
            } catch (...) {
                outcome.message = "HDR processing failed; the previous result remains active.";
            }

            ProjectTasks().PostToMain([
                this, generation, projectId, sourceSetId, inputRevision, notifier, activity, completionLease,
                outcome = std::move(outcome)
            ]() mutable {
                const auto activeSnapshot = m_Project->snapshot;
                const Raw::MultiFrame::PublicationFenceResult publicationFence =
                    Raw::MultiFrame::EvaluatePublicationFence(
                        { generation, projectId, inputRevision },
                        {
                            m_HdrProcessingGeneration.load(
                                std::memory_order_relaxed),
                            static_cast<bool>(activeSnapshot),
                            activeSnapshot
                                ? std::string_view(activeSnapshot->projectId)
                                : std::string_view(),
                            activeSnapshot ? activeSnapshot->hdrInputRevision : 0u,
                            activeSnapshot && Stack::Project::FindSourceSet(
                                *activeSnapshot, sourceSetId)
                        });
                if (publicationFence ==
                    Raw::MultiFrame::PublicationFenceResult::SupersededGeneration) {
                    notifier.CancelActivity(activity, "HDR run superseded.");
                    return;
                }
                if (outcome.canceled) {
                    m_HdrProcessingTaskState = Async::TaskState::Idle;
                    m_HdrProcessingStatusText = outcome.message;
                    notifier.CancelActivity(activity, "HDR run cancelled.");
                    return;
                }
                if (publicationFence !=
                    Raw::MultiFrame::PublicationFenceResult::Current) {
                    m_HdrProcessingTaskState = Async::TaskState::Idle;
                    m_HdrProcessingStatusText =
                        "The HDR run finished after its inputs changed, so it was not adopted.";
                    notifier.CancelActivity(activity, "HDR inputs changed. The result was not adopted.");
                    return;
                }
                if (!outcome.publish || !outcome.adopted) {
                    m_HdrProcessingTaskState = Async::TaskState::Failed;
                    m_HdrProcessingStatusText = outcome.message.empty()
                        ? "HDR processing failed; the previous result remains active."
                        : outcome.message;
                    notifier.FailActivity(activity, "HDR processing failed.", m_HdrProcessingStatusText);
                    return;
                }
                std::string publicationError;
                if (!PublishHdrResultToRawWorkspace(
                        std::move(*outcome.adopted), {}, &publicationError)) {
                    m_HdrProcessingTaskState = Async::TaskState::Failed;
                    m_HdrProcessingStatusText = publicationError;
                    notifier.FailActivity(activity, "HDR result could not be adopted.", publicationError);
                    return;
                }
                m_HdrProcessingTaskState = Async::TaskState::Ready;
                m_HdrProcessingStatusText = outcome.message;
                notifier.CompleteActivity(activity, "HDR result ready.");
            });
        });
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        m_HdrProcessingTaskState = Async::TaskState::Failed;
        m_HdrProcessingProgress.reset();
        m_HdrProcessingStatusText = "The HDR run could not be queued.";
        notifier.FailActivity(activity, m_HdrProcessingStatusText);
        return FinishHdr(outError, m_HdrProcessingStatusText, false);
    }
    return FinishHdr(outError, std::string(), true);
}

void EditorModule::RenderHdrRawLabTool() {
    using Stack::Project::MultiFrameOperationIntent;
    if (!m_Project->snapshot) {
        ImGui::TextDisabled("No HDR project is active.");
        return;
    }
    const Stack::Project::RawProjectSnapshot& snapshot =
        *m_Project->snapshot;
    const Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, snapshot.activeSourceSetId);
    if (!sourceSet || sourceSet->operationIntent !=
            MultiFrameOperationIntent::RawBurstHdr) {
        ImGui::TextDisabled("The active source set is not an HDR bracket.");
        return;
    }
    const std::string sourceSetId = sourceSet->sourceSetId;
    const bool busy = IsHdrProcessingBusy();
    const bool currentResult = m_HdrAdoptedRawResult &&
        m_HdrAdoptedRawResult->projectId == snapshot.projectId &&
        m_HdrAdoptedRawResult->sourceSetId == sourceSetId &&
        m_HdrAdoptedRawResult->inputRevision == snapshot.hdrInputRevision;

    Raw::Hdr::Parameters parameters;
    std::string parameterError;
    if (!Raw::Hdr::DeserializeParameters(
            sourceSet->settings.value("parameters", nlohmann::json::object()),
            parameters, &parameterError)) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "%s",
            parameterError.c_str());
        return;
    }
    const bool automaticReference = sourceSet->settings.value(
        "automaticGeometricReference", true);
    const bool automaticAnchor = sourceSet->settings.value(
        "automaticRadiometricAnchor", true);
    const nlohmann::json anchorValue = sourceSet->settings.value(
        "radiometricAnchorFrameId", nlohmann::json(nullptr));
    const std::string anchorFrameId = anchorValue.is_string()
        ? anchorValue.get<std::string>() : std::string();

    ImGui::TextUnformatted(sourceSet->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%llu frames",
        static_cast<unsigned long long>(sourceSet->frames.size()));
    ImGui::TextWrapped(
        "Tripod-first scene-linear Bayer HDR. The merge preserves highlight "
        "values above nominal white and is developed once through the normal RAW pipeline.");

    std::vector<Stack::Project::RawCaptureCompatibilitySummary> captures(
        sourceSet->frames.size());
    std::vector<double> exposureMetrics(sourceSet->frames.size(), 0.0);
    std::vector<double> finiteMetrics;
    for (std::size_t i = 0; i < sourceSet->frames.size(); ++i) {
        const Stack::Project::EmbeddedAssetRecord* asset =
            Stack::Project::FindEmbeddedAsset(snapshot, sourceSet->frames[i].assetId);
        if (asset) {
            Stack::Project::DeserializeRawCaptureCompatibilitySummary(
                asset->captureMetadataSummary, captures[i], nullptr);
            exposureMetrics[i] = CaptureExposureMetric(captures[i]);
            if (exposureMetrics[i] > 0.0) finiteMetrics.push_back(exposureMetrics[i]);
        }
    }
    double medianExposure = 0.0;
    if (!finiteMetrics.empty()) {
        std::sort(finiteMetrics.begin(), finiteMetrics.end());
        medianExposure = finiteMetrics[finiteMetrics.size() / 2u];
    }

    ImGui::SeparatorText("Bracket frames");
    for (std::size_t i = 0; i < sourceSet->frames.size(); ++i) {
        const Stack::Project::SourceSetFrame& frame = sourceSet->frames[i];
        const Stack::Project::EmbeddedAssetRecord* asset =
            Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
        bool enabled = frame.enabled;
        ImGui::PushID(frame.frameId.c_str());
        Stack::UiActivity::BeginDisabledForWork(busy);
        if (ImGui::Checkbox("##enabled", &enabled)) {
            std::string error;
            SetMultiFrameFrameEnabled(sourceSetId, frame.frameId, enabled, &error);
            if (!error.empty()) m_HdrProcessingStatusText = error;
            ImGui::EndDisabled();
            ImGui::PopID();
            return;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const std::string fileLabel = asset && !asset->originalFilename.empty()
            ? asset->originalFilename
            : (frame.userLabel.empty() ? std::string("RAW frame") : frame.userLabel);
        ImGui::TextUnformatted(fileLabel.c_str());
        ImGui::SameLine();
        const auto& capture = captures[i];
        const double relativeEv = exposureMetrics[i] > 0.0 && medianExposure > 0.0
            ? std::log2(exposureMetrics[i] / medianExposure) : 0.0;
        const std::string shutter = ShutterLabel(capture.exposureTimeSeconds);
        if (exposureMetrics[i] > 0.0) {
            ImGui::TextDisabled("%s  f/%.1f  ISO %.0f  %+0.2f EV",
                shutter.c_str(),
                capture.apertureFNumber > 0.0 ? capture.apertureFNumber : 0.0,
                capture.isoSpeed > 0.0 ? capture.isoSpeed : 0.0,
                relativeEv);
        } else {
            ImGui::TextDisabled("Exposure metadata incomplete");
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Alignment and calibration");
    int alignment = parameters.alignmentMode == Raw::Hdr::AlignmentMode::Identity
        ? 1 : 0;
    Stack::UiActivity::BeginDisabledForWork(busy);
    if (ImGui::Combo("Alignment", &alignment,
            "Auto: identity or subpixel translation\0Identity: fixed tripod coordinates\0")) {
        std::string error;
        SetHdrProcessingConfiguration(
            sourceSetId,
            alignment == 1 ? Raw::Hdr::AlignmentMode::Identity
                           : Raw::Hdr::AlignmentMode::AutoTranslation,
            automaticReference,
            sourceSet->referenceFrameId,
            automaticAnchor,
            anchorFrameId,
            &error);
        if (!error.empty()) m_HdrProcessingStatusText = error;
        ImGui::EndDisabled();
        return;
    }

    bool autoReferenceDraft = automaticReference;
    if (ImGui::Checkbox("Choose geometric reference automatically", &autoReferenceDraft)) {
        std::string error;
        SetHdrProcessingConfiguration(sourceSetId, parameters.alignmentMode,
            autoReferenceDraft, sourceSet->referenceFrameId,
            automaticAnchor, anchorFrameId, &error);
        if (!error.empty()) m_HdrProcessingStatusText = error;
        ImGui::EndDisabled();
        return;
    }
    if (!automaticReference && !sourceSet->frames.empty()) {
        const auto current = std::find_if(sourceSet->frames.begin(), sourceSet->frames.end(),
            [&](const Stack::Project::SourceSetFrame& frame) {
                return frame.frameId == sourceSet->referenceFrameId;
            });
        const char* label = current != sourceSet->frames.end()
            ? (current->userLabel.empty() ? "Selected RAW frame" : current->userLabel.c_str())
            : "Choose frame";
        if (ImGui::BeginCombo("Geometric reference", label)) {
            for (const auto& frame : sourceSet->frames) {
                const auto* asset = Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
                const std::string frameLabel = asset ? asset->originalFilename : frame.userLabel;
                if (ImGui::Selectable(frameLabel.c_str(),
                        frame.frameId == sourceSet->referenceFrameId)) {
                    std::string error;
                    SetHdrProcessingConfiguration(sourceSetId, parameters.alignmentMode,
                        false, frame.frameId, automaticAnchor, anchorFrameId, &error);
                    if (!error.empty()) m_HdrProcessingStatusText = error;
                    ImGui::EndCombo();
                    ImGui::EndDisabled();
                    return;
                }
            }
            ImGui::EndCombo();
        }
    }

    bool autoAnchorDraft = automaticAnchor;
    if (ImGui::Checkbox("Choose radiometric anchor automatically", &autoAnchorDraft)) {
        const std::string fallbackAnchor = anchorFrameId.empty()
            ? sourceSet->referenceFrameId : anchorFrameId;
        std::string error;
        SetHdrProcessingConfiguration(sourceSetId, parameters.alignmentMode,
            automaticReference, sourceSet->referenceFrameId,
            autoAnchorDraft, fallbackAnchor, &error);
        if (!error.empty()) m_HdrProcessingStatusText = error;
        ImGui::EndDisabled();
        return;
    }
    if (!automaticAnchor && !sourceSet->frames.empty()) {
        const auto current = std::find_if(sourceSet->frames.begin(), sourceSet->frames.end(),
            [&](const Stack::Project::SourceSetFrame& frame) {
                return frame.frameId == anchorFrameId;
            });
        const char* label = current != sourceSet->frames.end()
            ? (current->userLabel.empty() ? "Selected RAW frame" : current->userLabel.c_str())
            : "Choose frame";
        if (ImGui::BeginCombo("Radiometric anchor", label)) {
            for (const auto& frame : sourceSet->frames) {
                const auto* asset = Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
                const std::string frameLabel = asset ? asset->originalFilename : frame.userLabel;
                if (ImGui::Selectable(frameLabel.c_str(), frame.frameId == anchorFrameId)) {
                    std::string error;
                    SetHdrProcessingConfiguration(sourceSetId, parameters.alignmentMode,
                        automaticReference, sourceSet->referenceFrameId,
                        false, frame.frameId, &error);
                    if (!error.empty()) m_HdrProcessingStatusText = error;
                    ImGui::EndCombo();
                    ImGui::EndDisabled();
                    return;
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "Reference controls geometry; the independent anchor controls the HDR signal scale.");

    ImGui::SeparatorText("Process");
    if (busy) {
        double fraction = 0.0;
        std::string stage = "Processing HDR";
        std::string detail;
        if (m_HdrProcessingProgress) {
            std::lock_guard<std::mutex> lock(m_HdrProcessingProgress->mutex);
            fraction = m_HdrProcessingProgress->overallFraction;
            stage = m_HdrProcessingProgress->stageLabel;
            detail = m_HdrProcessingProgress->message;
        }
        ImGui::ProgressBar(static_cast<float>(fraction), ImVec2(-1.0f, 0.0f), stage.c_str());
        if (!detail.empty()) ImGui::TextWrapped("%s", detail.c_str());
        if (ImGui::Button("Cancel processing")) CancelHdrProcessing();
    } else {
        const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(
            sourceSet->frames.begin(), sourceSet->frames.end(),
            [](const Stack::Project::SourceSetFrame& frame) { return frame.enabled; }));
        ImGui::BeginDisabled(enabledCount < Raw::Hdr::kMinimumFrameCount ||
            enabledCount > Raw::Hdr::kMaximumFrameCount);
        if (ImGui::Button(currentResult ? "Reprocess HDR" : "Process HDR")) {
            std::string error;
            if (!StartHdrProcessing(sourceSetId, &error) && !error.empty())
                m_HdrProcessingStatusText = error;
        }
        ImGui::EndDisabled();
    }
    if (!m_HdrProcessingStatusText.empty())
        ImGui::TextWrapped("%s", m_HdrProcessingStatusText.c_str());

    if (currentResult && m_HdrProcessingReport) {
        ImGui::SeparatorText("Result and diagnostics");
        static const char* views[] = {
            "Final result", "Merge confidence", "Effective samples",
            "Owner / reference fallback", "Recovered highlight headroom",
            "Noise / variance"
        };
        ImGui::Combo("Display", &m_HdrDiagnosticView, views,
            static_cast<int>(std::size(views)));
        ImGui::Text("Exposure span: %.2f EV", m_HdrProcessingReport->exposureSpanEv);
        ImGui::Text("Mean effective samples: %.2f",
            m_HdrProcessingReport->meanEffectiveSamples);
        ImGui::Text("Processing backend: %s",
            m_HdrProcessingReport->executionBackend.empty()
                ? "CPU reference"
                : m_HdrProcessingReport->executionBackend.c_str());
        if (!m_HdrProcessingReport->gpuDeviceIdentity.empty()) {
            ImGui::TextWrapped("GPU: %s",
                m_HdrProcessingReport->gpuDeviceIdentity.c_str());
        }
        if (m_HdrProcessingReport->gpuDispatchedTileCount > 0u) {
            ImGui::TextDisabled("GPU dispatch tiles: %u",
                m_HdrProcessingReport->gpuDispatchedTileCount);
        }
        if (m_HdrProcessingReport->colorCoherentRepairPixelCount > 0u) {
            ImGui::Text("Isolated color samples repaired: %llu",
                static_cast<unsigned long long>(
                    m_HdrProcessingReport->colorCoherentRepairPixelCount));
        }
        ImGui::TextDisabled(
            "The merge stays scene-linear. This preview uses the RAW tab's saved Exposure and single View Transform.");
        if (!m_HdrProcessingReport->warnings.empty() &&
            ImGui::TreeNode("Warnings")) {
            for (const std::string& warning : m_HdrProcessingReport->warnings)
                ImGui::BulletText("%s", warning.c_str());
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Frame contributions")) {
            for (const Raw::Hdr::FrameDiagnostic& frame :
                    m_HdrProcessingReport->frames) {
                ImGui::BulletText(
                    "%s  scale %.4f  shift (%+.2f, %+.2f)  total %.1f%%  shadows %.1f%%  mid %.1f%%  highlights %.1f%%%s",
                    frame.stableFrameId.c_str(),
                    frame.fittedExposureRelativeToAnchor,
                    frame.translationRawX,
                    frame.translationRawY,
                    frame.aggregateContribution * 100.0,
                    frame.shadowContribution * 100.0,
                    frame.midtoneContribution * 100.0,
                    frame.highlightContribution * 100.0,
                    frame.lowConfidence ? "  low confidence" : "");
                ImGui::TextDisabled("Noise: %s (%s), variance x%.2f",
                    frame.noiseModelQuality.c_str(),
                    frame.noiseModelSource.c_str(),
                    frame.noiseVarianceInflation);
                if (!frame.exposureFitPath.empty()) {
                    std::string path;
                    for (const std::string& step : frame.exposureFitPath) {
                        if (!path.empty()) path += " -> ";
                        path += step;
                    }
                    ImGui::TextDisabled("Exposure path: %s", path.c_str());
                }
            }
            ImGui::TreePop();
        }
        if (!m_HdrProcessingReport->exposureFitEdges.empty() &&
            ImGui::TreeNode("Exposure-fit edges")) {
            for (const Raw::Hdr::ExposureFitEdgeDiagnostic& edge :
                    m_HdrProcessingReport->exposureFitEdges) {
                ImGui::BulletText(
                    "%s -> %s  x%.4f  uncertainty %.3f EV  samples %llu%s",
                    edge.fromFrameId.c_str(),
                    edge.toFrameId.c_str(),
                    edge.fittedScale,
                    edge.uncertaintyEv,
                    static_cast<unsigned long long>(edge.sampleCount),
                    edge.verified ? "" : "  unverified");
            }
            ImGui::TreePop();
        }
    } else if (sourceSet->settings.value("result", nlohmann::json::object())
                   .value("state", std::string()) == "stale") {
        ImGui::TextColored(ImVec4(0.92f, 0.72f, 0.35f, 1.0f),
            "Inputs changed. Reprocess HDR to unlock post-fusion editing.");
    }
}

void EditorModule::CancelHdrProcessing(const std::string& reason) {
    const bool busy = IsHdrProcessingBusy();
    if (busy) GetNotifier().CancelActivity(m_HdrProcessingActivity,
        reason.empty() ? "HDR run cancelled." : reason);
    m_HdrProcessingGeneration.fetch_add(1u, std::memory_order_relaxed);
    m_HdrProcessingTaskState = Async::TaskState::Idle;
    if (busy || !reason.empty()) {
        m_HdrProcessingStatusText = reason.empty()
            ? "HDR processing canceled; the previous result remains active."
            : reason;
    }
    m_HdrProcessingProgress.reset();
}
