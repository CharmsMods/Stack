#include "Editor/Bracketing/BracketingSession.h"
#include "Editor/EditorModule.h"

#include "App/AppPaths.h"
#include "Async/TaskSystem.h"
#include "Notifications/AsyncActivity.h"
#include "Editor/MultiFrameResultCache.h"
#include "Raw/MultiFrame/GraphProcessor.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"

#include <algorithm>
#include <filesystem>
#include <thread>

namespace {

bool FinishGraphProcessing(
    std::string* output,
    const std::string& message,
    bool value) {
    if (output) *output = message;
    return value;
}

std::filesystem::path MaterializedGraphAssetPath(
    const std::filesystem::path& sourceDirectory,
    const Stack::Project::EmbeddedAssetRecord& asset) {
    std::string extension =
        std::filesystem::path(asset.originalFilename).extension().string();
    if (!extension.empty() && extension.front() != '.')
        extension.insert(extension.begin(), '.');
    return sourceDirectory /
        (asset.sha256 + "-" + std::to_string(asset.byteLength) + extension);
}

bool CachedGraphAssetMatchesSize(
    const std::filesystem::path& path,
    std::uint64_t expectedBytes) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error &&
        static_cast<std::uint64_t>(std::filesystem::file_size(path, error)) ==
            expectedBytes &&
        !error;
}

std::uint64_t GraphIdentityHash64(const std::string& value) {
    if (value.size() < 16u) return std::hash<std::string> {}(value);
    try { return std::stoull(value.substr(0u, 16u), nullptr, 16); }
    catch (...) { return std::hash<std::string> {}(value); }
}

std::string CompactGraphCacheIdentity(
    const std::string& prefix,
    const std::string& identity,
    std::size_t maximumIdentityCharacters) {
    const std::size_t count = std::min(
        maximumIdentityCharacters, identity.size());
    return prefix + identity.substr(0u, count);
}

} // namespace

bool EditorModule::StartMultiFrameGraphProcessing(
    const std::string& sourceSetId,
    std::string* outError) {
    if(RequestAutoBracketForeground("start processing",[this,sourceSetId]{StartMultiFrameGraphProcessing(sourceSetId,nullptr);}))return true;
    if (!IsMultiFrameRawProjectActive() || !m_Project->snapshot ||
        !m_Project->store) {
        return FinishGraphProcessing(
            outError, "No MultiFrame RAW project is active.", false);
    }
    if (IsMultiFrameGraphProcessingBusy() || IsHdrProcessingBusy() ||
        IsMfdExperimentalProcessingBusy()) {
        return FinishGraphProcessing(
            outError, "A MultiFrame processor is already running.", false);
    }

    if (IsDirty() && m_DocumentPersistenceEnabled) {
        std::string saveError;
        if (!SaveActiveMultiFrameRawProject(&saveError)) {
            return FinishGraphProcessing(
                outError,
                saveError.empty()
                    ? "Stack could not save the project before the memory-intensive run."
                    : "Stack could not save the project before processing: " + saveError,
                false);
        }
    }

    Stack::Project::RawProjectSnapshot snapshot =
        *m_Project->snapshot;
    const Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) {
        return FinishGraphProcessing(
            outError, "The selected capture set no longer exists.", false);
    }
    const Raw::MultiFrame::GraphExecutionPlan plan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(snapshot);
    if (!plan.valid) {
        return FinishGraphProcessing(
            outError,
            plan.errors.empty() ? "The MultiFrame graph is invalid."
                                : plan.errors.front(),
            false);
    }
    if (!plan.executableWithCurrentAdapters) {
        return FinishGraphProcessing(
            outError,
            plan.warnings.empty()
                ? "The graph requires covariance-aware fusion."
                : plan.warnings.back(),
            false);
    }
    const Raw::MultiFrame::GraphExecutionStep* terminal =
        Raw::MultiFrame::FindGraphExecutionStep(
            plan, plan.outputProducerNodeId);
    const bool finalHdr = terminal && terminal->adapter ==
        Raw::MultiFrame::GraphExecutionAdapter::HdrV4;
    const bool finalBurst = terminal && terminal->adapter ==
        Raw::MultiFrame::GraphExecutionAdapter::SharedBurstV1;
    if (!finalHdr && !finalBurst) {
        return FinishGraphProcessing(
            outError,
            "Connect a Burst Denoise or HDR Merge result to Output.",
            false);
    }

    const auto memory = Raw::Mfd::ResolveMfdProcessingMemoryBudget(
        0.0, Raw::Mfd::QueryPhysicalMemorySnapshot());
    if (!memory.valid)
        return FinishGraphProcessing(outError, memory.message, false);

    const Stack::Project::ProjectStoreHandle store =
        m_Project->store;
    const std::uint64_t generation =
        m_MultiFrameGraphProcessingGeneration.fetch_add(
            1u, std::memory_order_relaxed) + 1u;
    const std::string projectId = snapshot.projectId;
    const std::string graphIdentity = plan.contentIdentitySha256;
    const std::uint64_t inputRevision = finalHdr
        ? snapshot.hdrInputRevision : snapshot.mfdInputRevision;
    // Keep enough identity for deterministic cache isolation while leaving
    // headroom for processor-owned SHA-256 tile/result directories and their
    // atomic temporary suffixes on Windows.
    const std::filesystem::path workingRoot =
        AppPaths::GetCacheDirectory() / "MFG" /
        CompactGraphCacheIdentity("p-", projectId, 12u) /
        CompactGraphCacheIdentity("g-", graphIdentity, 24u);
    auto progressState =
        std::make_shared<MfdExperimentalProcessingProgressState>();
    m_MultiFrameGraphProcessingProgress = progressState;
    m_MultiFrameGraphProcessingTaskState = Async::TaskState::Queued;
    m_MultiFrameGraphProcessingProjectId = projectId;
    m_MultiFrameGraphProcessingSourceSetId = sourceSetId;
    m_MultiFrameGraphProcessingIdentity = graphIdentity;
    m_MultiFrameGraphProcessingStatusText =
        "Queuing the connected MultiFrame Output plan; memory estimates are advisory and the project is saved...";
    const auto notifier = GetNotifier();
    const auto activity = notifier.BeginActivity("Processing frames");
    m_MultiFrameProcessingActivity = activity;
    const auto completionLease = Stack::Notifications::RetainAsyncActivity(notifier, activity);
    const auto activityMetadata = Stack::Notifications::ForAsyncActivity(notifier, activity, "Processing frames");

    struct Outcome {
        bool canceled = false;
        bool finalHdr = false;
        std::string message;
        std::filesystem::path burstCachePath;
        std::string burstCacheError;
        Raw::MultiFrame::GraphProcessingResult processed;
    };

    const auto cachedMeasurements = m_MultiFrameCacheProjectId == projectId
        ? m_MultiFrameProcessingCache : nullptr;
    bool submitted = false;
    try {
        submitted = ProjectTasks().SubmitHighPriority(activityMetadata, [
            this, generation, projectId, sourceSetId, graphIdentity, notifier, activity, completionLease,
            inputRevision, finalHdr, workingRoot, store, memory,
            progressState, plan, cachedMeasurements, snapshot = std::move(snapshot)
        ]() mutable {
            const auto canceled = [this, generation]() {
                return generation !=
                    m_MultiFrameGraphProcessingGeneration.load(
                        std::memory_order_relaxed);
            };
            ProjectTasks().PostToMain([this, generation, notifier, activity, completionLease]() {
                if (generation !=
                    m_MultiFrameGraphProcessingGeneration.load(
                        std::memory_order_relaxed)) return;
                m_MultiFrameGraphProcessingTaskState = Async::TaskState::Running;
                m_MultiFrameGraphProcessingStatusText =
                    "Materializing graph source evidence...";
                notifier.UpdateActivity(activity, "Preparing source captures...");
            });

            Outcome outcome;
            outcome.finalHdr = finalHdr;
            try {
                Raw::MultiFrame::GraphProcessingRequest request;
                request.snapshot = snapshot;
                request.cache = cachedMeasurements;
                request.workingDirectory = workingRoot / "processor";
                request.memoryBudgetBytes = memory.budgetBytes;
                request.enforceMemoryBudget = false;
                const unsigned int hardwareThreads =
                    std::max(1u, std::thread::hardware_concurrency());
                request.workerCount = std::clamp(
                    hardwareThreads / 2u, 1u, 4u);
                if (m_RawRenderClientId != 0 ||
                    m_RenderWorkerAvailable) {
                    request.executeOpenGlTask = [this](
                        Raw::Hdr::OpenGlTask task,
                        std::string& error) {
                        return ExecuteRenderOwnerOpenGlTaskBlocking(
                            std::move(task), error);
                    };
                }
                request.shouldCancel = canceled;
                request.reportProgress = [progressState, notifier, activity](
                    const Raw::MultiFrame::GraphProcessingProgress& progress) {
                    std::lock_guard<std::mutex> lock(progressState->mutex);
                    progressState->stageLabel =
                        Raw::MultiFrame::GraphExecutionAdapterName(
                            progress.adapter);
                    progressState->message = progress.message;
                    progressState->overallFraction = std::clamp(
                        progress.overallFraction,
                        progressState->overallFraction,
                        1.0);
                    progressState->stageFraction = progress.nodeFraction;
                    progressState->completedUnits =
                        progress.completedNodeCount;
                    progressState->totalUnits = progress.totalNodeCount;
                    progressState->lastAdvancedAt =
                        std::chrono::steady_clock::now();
                    std::optional<Stack::Notifications::Progress> measured;
                    if (progress.totalNodeCount > 0) measured = Stack::Notifications::Progress{
                        static_cast<double>(progress.completedNodeCount), static_cast<double>(progress.totalNodeCount), "Nodes"};
                    notifier.UpdateActivity(activity, progressState->stageLabel, measured);
                };

                const std::filesystem::path sourceDirectory =
                    workingRoot / "sources";
                std::string materializationError;
                for (const Raw::MultiFrame::GraphExecutionStep& step : plan.steps) {
                    if (step.adapter !=
                            Raw::MultiFrame::GraphExecutionAdapter::CaptureSource &&
                        step.adapter !=
                            Raw::MultiFrame::GraphExecutionAdapter::CaptureSubset) {
                        continue;
                    }
                    const Stack::Project::MultiFrameGraphNode* node =
                        Stack::Project::FindMultiFrameGraphNode(
                            snapshot.multiFrameGraph, step.nodeId);
                    const Stack::Project::MultiFrameSourceSet* workerSet = node
                        ? Stack::Project::FindSourceSet(snapshot, node->sourceSetId)
                        : nullptr;
                    if (!node || !workerSet) {
                        outcome.message =
                            "A reachable Capture node lost its source set.";
                        break;
                    }
                    for (const std::string& frameId : node->frameIds) {
                        if (request.materializedSourcePathsByFrameId.count(frameId))
                            continue;
                        if (canceled()) {
                            outcome.canceled = true;
                            outcome.message =
                                "Graph processing was canceled; the previous Output remains active.";
                            break;
                        }
                        const auto frame = std::find_if(
                            workerSet->frames.begin(), workerSet->frames.end(),
                            [&](const Stack::Project::SourceSetFrame& candidate) {
                                return candidate.frameId == frameId;
                            });
                        const Stack::Project::EmbeddedAssetRecord* asset =
                            frame == workerSet->frames.end()
                                ? nullptr
                                : Stack::Project::FindEmbeddedAsset(
                                    snapshot, frame->assetId);
                        if (!asset) {
                            outcome.message =
                                "A reachable graph frame has no embedded original.";
                            break;
                        }
                        const std::filesystem::path materialized =
                            MaterializedGraphAssetPath(sourceDirectory, *asset);
                        if (!CachedGraphAssetMatchesSize(
                                materialized, asset->byteLength) &&
                            !store->CopyAssetToFile(
                                asset->assetId, materialized,
                                &materializationError)) {
                            outcome.message = materializationError.empty()
                                ? "A graph original could not be materialized."
                                : materializationError;
                            break;
                        }
                        request.materializedSourcePathsByFrameId[frameId] =
                            materialized;
                    }
                    if (outcome.canceled || !outcome.message.empty()) break;
                }
                if (!outcome.canceled && outcome.message.empty()) {
                    outcome.processed =
                        Raw::MultiFrame::ProcessMultiFrameGraph(request);
                    outcome.canceled = outcome.processed.status ==
                        Raw::MultiFrame::GraphProcessingStatus::Canceled ||
                        canceled();
                    outcome.message = outcome.processed.message;
                    if (!outcome.canceled && !finalHdr &&
                        outcome.processed.status ==
                            Raw::MultiFrame::GraphProcessingStatus::Completed &&
                        outcome.processed.outputRawData) {
                        outcome.burstCachePath =
                            workingRoot / "published" / "result.mfg-cache";
                        if (!Stack::EditorMultiFrameCache::WriteBurstResult(
                                outcome.burstCachePath,
                                graphIdentity,
                                inputRevision,
                                *outcome.processed.outputRawData,
                                &outcome.burstCacheError)) {
                            outcome.burstCachePath.clear();
                        }
                    }
                }
            } catch (const std::bad_alloc&) {
                outcome.message =
                    "The system could not satisfy a MultiFrame memory allocation; the saved project and previous Output remain active.";
            } catch (const std::exception& exception) {
                outcome.message = std::string(
                    "MultiFrame graph processing failed: ") + exception.what();
            } catch (...) {
                outcome.message =
                    "MultiFrame graph processing failed; the previous Output remains active.";
            }

            ProjectTasks().PostToMain([
                this, generation, projectId, sourceSetId, graphIdentity, notifier, activity, completionLease,
                inputRevision, outcome = std::move(outcome)
            ]() mutable {
                if (generation !=
                    m_MultiFrameGraphProcessingGeneration.load(
                        std::memory_order_relaxed)) {
                    notifier.CancelActivity(activity, "Frame processing superseded.");
                    return;
                }
                if (outcome.canceled) {
                    m_MultiFrameGraphProcessingTaskState = Async::TaskState::Idle;
                    m_MultiFrameGraphProcessingStatusText = outcome.message;
                    notifier.CancelActivity(activity, "Frame processing cancelled.");
                    return;
                }
                if (!m_Project->snapshot ||
                    m_Project->snapshot->projectId != projectId ||
                    !Stack::Project::FindSourceSet(
                        *m_Project->snapshot, sourceSetId)) {
                    m_MultiFrameGraphProcessingTaskState = Async::TaskState::Idle;
                    m_MultiFrameGraphProcessingStatusText =
                        "The graph finished after its project changed, so it was not adopted.";
                    notifier.CancelActivity(activity, "The project changed. The result was not adopted.");
                    return;
                }
                const Raw::MultiFrame::GraphExecutionPlan activePlan =
                    Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(
                        *m_Project->snapshot);
                const std::uint64_t activeRevision = outcome.finalHdr
                    ? m_Project->snapshot->hdrInputRevision
                    : m_Project->snapshot->mfdInputRevision;
                if (!activePlan.valid ||
                    activePlan.contentIdentitySha256 != graphIdentity ||
                    activeRevision != inputRevision) {
                    m_MultiFrameGraphProcessingTaskState = Async::TaskState::Idle;
                    m_MultiFrameGraphProcessingStatusText =
                        "The graph finished after its inputs changed, so it was not adopted.";
                    notifier.CancelActivity(activity, "Frame inputs changed. The result was not adopted.");
                    return;
                }
                if (outcome.processed.status !=
                        Raw::MultiFrame::GraphProcessingStatus::Completed ||
                    !outcome.processed.outputRawData ||
                    (outcome.finalHdr && !outcome.processed.outputHdrResult)) {
                    m_MultiFrameGraphProcessingTaskState = Async::TaskState::Failed;
                    m_MultiFrameGraphProcessingStatusText = outcome.message.empty()
                        ? "MultiFrame graph processing failed; the previous Output remains active."
                        : outcome.message;
                    notifier.FailActivity(activity, "Frame processing failed.", m_MultiFrameGraphProcessingStatusText);
                    return;
                }

                m_MultiFrameProcessingCache = outcome.processed.cache;
                m_MultiFrameFusionResults = outcome.processed.hdrNodeResults;
                m_MultiFrameCacheProjectId = projectId;
                if (outcome.finalHdr) {
                    HdrAdoptedRawResult adopted;
                    adopted.projectId = projectId;
                    adopted.sourceSetId = sourceSetId;
                    adopted.inputRevision = inputRevision;
                    adopted.contentHash = outcome.processed.outputRawData->
                        normalizedMosaicContentHash;
                    adopted.rawData = outcome.processed.outputRawData;
                    adopted.result = outcome.processed.outputHdrResult;
                    std::string publicationError;
                    if (!PublishHdrResultToRawWorkspace(
                            std::move(adopted),
                            outcome.processed.outputHdrCacheDirectory,
                            &publicationError)) {
                        m_MultiFrameGraphProcessingTaskState =
                            Async::TaskState::Failed;
                        m_MultiFrameGraphProcessingStatusText =
                            publicationError;
                        notifier.FailActivity(activity, "The frame result could not be adopted.", publicationError);
                        return;
                    }
                    m_HdrProcessingTaskState = Async::TaskState::Ready;
                    m_HdrProcessingStatusText =
                        "Connected MultiFrame HDR Output published to RAW.";
                    if (Stack::Project::MultiFrameSourceSet* activeSet =
                            Stack::Project::FindSourceSet(
                                *m_Project->snapshot, sourceSetId)) {
                        activeSet->settings["result"]["graphIdentity"] =
                            graphIdentity;
                    }
                } else {
                    const std::uint64_t contentHash =
                        GraphIdentityHash64(graphIdentity);
                    MfdAdoptedRawResult adopted;
                    adopted.projectId = projectId;
                    adopted.sourceSetId = sourceSetId;
                    adopted.inputRevision = inputRevision;
                    adopted.contentHash = contentHash;
                    adopted.rawData = outcome.processed.outputRawData;
                    m_MfdAdoptedRawResult = std::move(adopted);
                    m_MfdExperimentalProcessingReport.reset();
                    for (EditorNodeGraph::Node& node :
                         m_Project->graph.EditNodes()) {
                        if (node.kind ==
                                EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                            node.multiFrameDenoise.sourceSetId ==
                                sourceSetId) {
                            node.multiFrameDenoise.resultState = "ready";
                            node.multiFrameDenoise.presentationStatus =
                                "Connected Burst result ready.";
                        }
                    }
                    Stack::Project::MultiFrameSourceSet* activeSet =
                        Stack::Project::FindSourceSet(
                            *m_Project->snapshot, sourceSetId);
                    if (activeSet) {
                        nlohmann::json resultState = {
                            { "state", "ready" },
                            { "resultKind", "burst" },
                            { "graphIdentity", graphIdentity },
                            { "inputRevision", inputRevision },
                            { "normalizedMosaicContentHash",
                                outcome.processed.outputRawData->
                                    normalizedMosaicContentHash },
                            { "sourceContentSha256",
                                outcome.processed.outputRawData->metadata.
                                    sourceContentSha256 }
                        };
                        if (!outcome.burstCachePath.empty()) {
                            resultState["cachePath"] =
                                outcome.burstCachePath.generic_string();
                        }
                        if (!outcome.burstCacheError.empty()) {
                            resultState["cacheWarning"] =
                                outcome.burstCacheError;
                        }
                        activeSet->settings["result"] =
                            std::move(resultState);
                        activeSet->settings["processingImplemented"] = true;
                        MarkDirty();
                    }
                }
                m_MultiFrameGraphProcessingTaskState = Async::TaskState::Ready;
                m_MultiFrameGraphProcessingStatusText = outcome.message;
                if (!outcome.processed.executionBackend.empty()) {
                    m_MultiFrameGraphProcessingStatusText +=
                        " Backend: " + outcome.processed.executionBackend + ".";
                }
                if (!outcome.processed.registrationBackend.empty()) {
                    m_MultiFrameGraphProcessingStatusText +=
                        " Registration: " +
                        outcome.processed.registrationBackend + ".";
                }
                if (!outcome.processed.registrationGpuFallbackReason.empty()) {
                    m_MultiFrameGraphProcessingStatusText +=
                        " Registration GPU fallback: " +
                        outcome.processed.registrationGpuFallbackReason + ".";
                }
                if (!outcome.burstCacheError.empty()) {
                    m_MultiFrameGraphProcessingStatusText +=
                        " Result cache warning: " +
                        outcome.burstCacheError;
                }
                if (!outcome.finalHdr) {
                    m_RawWorkspaceStaleRenderStatusText.clear();
                    m_MultiFrameProjectCoverRefreshPending = true;
                    MarkRenderDirty();
                }
                if (outcome.burstCacheError.empty()) notifier.CompleteActivity(activity, "RAW result ready.");
                else notifier.FinishActivity(activity, Stack::Notifications::Outcome::Partial,
                    "RAW result ready. Its cache could not be saved.", outcome.burstCacheError);
            });
        });
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        m_MultiFrameGraphProcessingTaskState = Async::TaskState::Failed;
        m_MultiFrameGraphProcessingProgress.reset();
        m_MultiFrameGraphProcessingStatusText =
            "The MultiFrame graph run could not be queued.";
        notifier.FailActivity(activity, m_MultiFrameGraphProcessingStatusText);
        return FinishGraphProcessing(
            outError, m_MultiFrameGraphProcessingStatusText, false);
    }
    // Submission succeeded on the UI thread. Do not leave the workspace
    // labeled "queued" while a long registration callback is already active;
    // the worker's main-thread checkpoint remains as a redundant state fence.
    m_MultiFrameGraphProcessingTaskState = Async::TaskState::Running;
    m_MultiFrameGraphProcessingStatusText =
        "Processing the connected MultiFrame Output plan...";
    return FinishGraphProcessing(outError, std::string(), true);
}

void EditorModule::CancelMultiFrameGraphProcessing(
    const std::string& reason) {
    if (m_Bracketing) {
        if (m_Bracketing->job) m_Bracketing->job->canceled = true;
        if (m_Bracketing->detailJob) m_Bracketing->detailJob->canceled = true;
        m_Bracketing->publishRequested = false;
    }
    const bool busy = IsMultiFrameGraphProcessingBusy();
    if (busy) GetNotifier().CancelActivity(m_MultiFrameProcessingActivity,
        reason.empty() ? "Frame processing cancelled." : reason);
    m_MultiFrameGraphProcessingGeneration.fetch_add(
        1u, std::memory_order_relaxed);
    m_MultiFrameGraphProcessingTaskState = Async::TaskState::Idle;
    if (busy) {
        m_MultiFrameGraphProcessingStatusText = reason.empty()
            ? "Graph processing canceled; the previous Output remains active."
            : reason;
    }
    m_MultiFrameGraphProcessingProgress.reset();
}
