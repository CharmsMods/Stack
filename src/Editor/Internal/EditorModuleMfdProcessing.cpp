#include "Editor/EditorModule.h"

#include "App/AppPaths.h"
#include "Async/TaskSystem.h"
#include "Notifications/AsyncActivity.h"
#include "Raw/MultiFrame/PublicationFence.h"
#include "Raw/MultiFrameDenoise/Inspection.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/MultiFrameDenoise/Processor.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

bool FinishMfdUi(
    std::string* output,
    const std::string& message,
    bool result) {
    if (output) *output = message;
    return result;
}

Raw::CfaPattern ParseCfaPatternName(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    if (value == "RGGB") return Raw::CfaPattern::RGGB;
    if (value == "BGGR") return Raw::CfaPattern::BGGR;
    if (value == "GBRG") return Raw::CfaPattern::GBRG;
    if (value == "GRBG") return Raw::CfaPattern::GRBG;
    return Raw::CfaPattern::Unknown;
}

std::filesystem::path MaterializedAssetPath(
    const std::filesystem::path& sourceDirectory,
    const Stack::Project::EmbeddedAssetRecord& asset) {
    std::string extension =
        std::filesystem::path(asset.originalFilename).extension().string();
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    return sourceDirectory /
        (asset.sha256 + "-" + std::to_string(asset.byteLength) + extension);
}

bool CachedAssetMatchesSize(
    const std::filesystem::path& path,
    std::uint64_t expectedBytes) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return false;
    const std::uint64_t bytes = static_cast<std::uint64_t>(
        std::filesystem::file_size(path, error));
    return !error && bytes == expectedBytes;
}

} // namespace

bool EditorModule::SetMfdExperimentalParameters(
    const std::string& sourceSetId,
    double motionDisagreementHardLimitRawPixels,
    double trustedPixelZeroWeightSigma,
    double oneAlternateWeightCapRelativeToReference,
    double exactFallbackAlternateToReferenceRatio,
    int fusionMethod,
    double fusionSmoothing,
    std::string* outError) {
    if (!IsMultiFrameRawProjectActive()) {
        return FinishMfdUi(outError, "No multi-frame RAW project is active.", false);
    }
    if (IsMfdExperimentalProcessingBusy()) {
        return FinishMfdUi(
            outError,
            "Cancel the current MFD run before changing its parameters.",
            false);
    }
    if (motionDisagreementHardLimitRawPixels < 0.4 ||
        motionDisagreementHardLimitRawPixels > 1.2 ||
        trustedPixelZeroWeightSigma < 4.0 ||
        trustedPixelZeroWeightSigma > 7.0 ||
        oneAlternateWeightCapRelativeToReference < 2.0 ||
        oneAlternateWeightCapRelativeToReference > 8.0 ||
        exactFallbackAlternateToReferenceRatio < 0.01 ||
        exactFallbackAlternateToReferenceRatio > 0.10 ||
        fusionMethod < 0 || fusionMethod > 1 ||
        fusionSmoothing < 0.0 || fusionSmoothing > 1.0) {
        return FinishMfdUi(
            outError,
            "Use the documented RA-CFA V1 ranges. Fusion smoothing must remain between 0 and 1.",
            false);
    }

    Stack::Project::RawProjectSnapshot snapshot = *m_Project->snapshot;
    Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet ||
        sourceSet->operationIntent !=
            Stack::Project::MultiFrameOperationIntent::RawBurstDenoise) {
        return FinishMfdUi(outError, "The MFD source set no longer exists.", false);
    }

    Raw::Mfd::Parameters parameters;
    std::string parameterError;
    const auto serialized = sourceSet->settings.find("parameters");
    if (serialized == sourceSet->settings.end() ||
        !Raw::Mfd::DeserializeParameters(
            *serialized, parameters, &parameterError)) {
        return FinishMfdUi(
            outError,
            parameterError.empty()
                ? "The stored Burst preparation and safety parameters are invalid."
                : parameterError,
            false);
    }
    parameters.registration.motionDisagreementHardLimitRawPixels =
        motionDisagreementHardLimitRawPixels;
    parameters.reliability.trustedPixelZeroWeightSigma =
        trustedPixelZeroWeightSigma;
    parameters.fusion.oneAlternateWeightCapRelativeToReference =
        oneAlternateWeightCapRelativeToReference;
    parameters.fusion.exactFallbackAlternateToReferenceRatio =
        exactFallbackAlternateToReferenceRatio;
    parameters.fusion.method = fusionMethod == 0
        ? "robust"
        : "weighted-average";
    parameters.fusion.smoothing = fusionSmoothing;
    if (!Raw::Mfd::ValidateParameters(parameters, &parameterError)) {
        return FinishMfdUi(outError, parameterError, false);
    }

    sourceSet->settings["parameters"] =
        Raw::Mfd::SerializeParameters(parameters);
    sourceSet->settings["experimentalProcessingAvailable"] = true;
    ++snapshot.mfdInputRevision;
    const Stack::Project::ProjectStoreTransaction transaction =
        m_Project->store->BeginTransaction(
            snapshot.persistedStorageRevision);
    if (!transaction) {
        return FinishMfdUi(
            outError,
            "Could not begin the MFD parameter transaction.",
            false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot),
        m_Project->graph,
        transaction,
        false,
        outError);
}

bool EditorModule::SetMfdExperimentalMemoryBudgetGiB(
    const std::string& sourceSetId,
    double memoryBudgetGiB,
    std::string* outError) {
    if (!IsMultiFrameRawProjectActive()) {
        return FinishMfdUi(outError, "No multi-frame RAW project is active.", false);
    }
    if (IsMfdExperimentalProcessingBusy()) {
        return FinishMfdUi(
            outError,
            "Cancel the current MFD run before changing its memory budget.",
            false);
    }
    const Raw::Mfd::MfdProcessingMemoryBudgetDecision decision =
        Raw::Mfd::ResolveMfdProcessingMemoryBudget(
            memoryBudgetGiB,
            Raw::Mfd::QueryPhysicalMemorySnapshot());
    if (!decision.valid) {
        return FinishMfdUi(outError, decision.message, false);
    }

    Stack::Project::RawProjectSnapshot snapshot = *m_Project->snapshot;
    Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet ||
        sourceSet->operationIntent !=
            Stack::Project::MultiFrameOperationIntent::RawBurstDenoise) {
        return FinishMfdUi(outError, "The MFD source set no longer exists.", false);
    }
    sourceSet->settings["experimentalMemoryBudgetGiB"] = memoryBudgetGiB;
    sourceSet->settings["experimentalProcessingAvailable"] = true;
    const Stack::Project::ProjectStoreTransaction transaction =
        m_Project->store->BeginTransaction(
            snapshot.persistedStorageRevision);
    if (!transaction) {
        return FinishMfdUi(
            outError,
            "Could not begin the MFD memory-budget transaction.",
            false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot),
        m_Project->graph,
        transaction,
        false,
        outError);
}

bool EditorModule::SetMfdExperimentalAlignmentMode(
    const std::string& sourceSetId,
    Raw::Mfd::MfdAlignmentMode alignmentMode,
    std::string* outError) {
    if (!IsMultiFrameRawProjectActive()) {
        return FinishMfdUi(outError, "No multi-frame RAW project is active.", false);
    }
    if (IsMfdExperimentalProcessingBusy()) {
        return FinishMfdUi(
            outError,
            "Cancel the current MFD run before changing alignment mode.",
            false);
    }
    if (std::string(Raw::Mfd::MfdAlignmentModeId(alignmentMode)) ==
        "invalid") {
        return FinishMfdUi(outError, "The MFD alignment mode is invalid.", false);
    }

    Stack::Project::RawProjectSnapshot snapshot = *m_Project->snapshot;
    Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet ||
        sourceSet->operationIntent !=
            Stack::Project::MultiFrameOperationIntent::RawBurstDenoise) {
        return FinishMfdUi(outError, "The MFD source set no longer exists.", false);
    }
    const std::string serialized = Raw::Mfd::MfdAlignmentModeId(alignmentMode);
    if (sourceSet->settings.value(
            "experimentalAlignmentMode", std::string("full")) == serialized) {
        return FinishMfdUi(outError, std::string(), true);
    }
    sourceSet->settings["experimentalAlignmentMode"] = serialized;
    sourceSet->settings["experimentalProcessingAvailable"] = true;
    ++snapshot.mfdInputRevision;
    const Stack::Project::ProjectStoreTransaction transaction =
        m_Project->store->BeginTransaction(
            snapshot.persistedStorageRevision);
    if (!transaction) {
        return FinishMfdUi(
            outError,
            "Could not begin the MFD alignment-mode transaction.",
            false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot),
        m_Project->graph,
        transaction,
        false,
        outError);
}

bool EditorModule::SetMfdSharedBurstExposureTolerance(
    const std::string& sourceSetId,
    double toleranceEv,
    std::string* outError) {
    if (!IsMultiFrameRawProjectActive() ||
        !std::isfinite(toleranceEv) || toleranceEv <= 0.0 ||
        toleranceEv > 4.0 || IsMfdExperimentalProcessingBusy()) {
        return FinishMfdUi(
            outError,
            "Shared Burst exposure tolerance must be above 0 and no more than 4 EV, with processing idle.",
            false);
    }
    Stack::Project::RawProjectSnapshot snapshot =
        *m_Project->snapshot;
    auto* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet || sourceSet->operationIntent !=
            Stack::Project::MultiFrameOperationIntent::RawBurstDenoise) {
        return FinishMfdUi(outError, "The Burst source set no longer exists.", false);
    }
    if (sourceSet->operationSchemaVersion !=
            Stack::Project::kMfdOperationSchemaVersion ||
        sourceSet->settings.value("algorithmId", std::string()) !=
            Raw::Mfd::kSharedBurstAlgorithmId) {
        return FinishMfdUi(outError,
            "The Burst settings are not current.",
            false);
    }
    Raw::Mfd::SharedBurstSettings settings;
    std::string error;
    if (!Raw::Mfd::DeserializeSharedBurstSettings(
            sourceSet->settings.at("sharedBurstSettings"),
            settings,
            &error)) {
        return FinishMfdUi(outError, error, false);
    }
    if (std::abs(settings.exposureGroupToleranceEv - toleranceEv) < 1.0e-9) {
        return FinishMfdUi(outError, std::string(), true);
    }
    settings.exposureGroupToleranceEv = toleranceEv;
    sourceSet->settings["sharedBurstSettings"] =
        Raw::Mfd::SerializeSharedBurstSettings(settings);
    ++snapshot.mfdInputRevision;
    const auto transaction = m_Project->store->BeginTransaction(
        snapshot.persistedStorageRevision);
    if (!transaction) {
        return FinishMfdUi(outError,
            "Could not begin the Shared Burst settings transaction.", false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot), m_Project->graph, transaction, false, outError);
}

bool EditorModule::SetMfdSharedBurstFrameTrust(
    const std::string& sourceSetId,
    const std::string& frameId,
    double trustAttenuation,
    std::string* outError) {
    if (!IsMultiFrameRawProjectActive() || frameId.empty() ||
        !std::isfinite(trustAttenuation) || trustAttenuation < 0.0 ||
        trustAttenuation > 1.0 || IsMfdExperimentalProcessingBusy()) {
        return FinishMfdUi(outError,
            "Shared Burst frame trust must remain in [0,1] with processing idle.",
            false);
    }
    Stack::Project::RawProjectSnapshot snapshot =
        *m_Project->snapshot;
    auto* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet || sourceSet->operationIntent !=
            Stack::Project::MultiFrameOperationIntent::RawBurstDenoise ||
        std::none_of(sourceSet->frames.begin(), sourceSet->frames.end(),
            [&frameId](const auto& frame) {
                return frame.frameId == frameId;
            })) {
        return FinishMfdUi(outError,
            "The Burst frame no longer exists.", false);
    }
    if (sourceSet->operationSchemaVersion !=
            Stack::Project::kMfdOperationSchemaVersion ||
        sourceSet->settings.value("algorithmId", std::string()) !=
            Raw::Mfd::kSharedBurstAlgorithmId) {
        return FinishMfdUi(outError,
            "The Burst settings are not current.",
            false);
    }
    nlohmann::json& trust = sourceSet->settings["frameTrust"];
    if (!trust.is_object()) trust = nlohmann::json::object();
    const double existing = trust.value(frameId, 1.0);
    if (std::abs(existing - trustAttenuation) < 1.0e-9) {
        return FinishMfdUi(outError, std::string(), true);
    }
    if (trustAttenuation >= 1.0 - 1.0e-9) trust.erase(frameId);
    else trust[frameId] = trustAttenuation;
    ++snapshot.mfdInputRevision;
    const auto transaction = m_Project->store->BeginTransaction(
        snapshot.persistedStorageRevision);
    if (!transaction) {
        return FinishMfdUi(outError,
            "Could not begin the Shared Burst frame-trust transaction.", false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot), m_Project->graph, transaction, false, outError);
}

bool EditorModule::StartMfdExperimentalProcessing(
    const std::string& sourceSetId,
    std::string* outError) {
    if(RequestAutoBracketForeground("start processing",[this,sourceSetId]{StartMfdExperimentalProcessing(sourceSetId,nullptr);}))return true;
    using Stack::Project::EmbeddedAssetRecord;
    using Stack::Project::MultiFrameSourceSet;
    using Stack::Project::RawCaptureCompatibilitySummary;
    using Stack::Project::SourceSetFrame;

    if (!IsMultiFrameRawProjectActive()) {
        return FinishMfdUi(outError, "No multi-frame RAW project is active.", false);
    }
    if (IsMfdExperimentalProcessingBusy()) {
        return FinishMfdUi(outError, "An MFD run is already in progress.", false);
    }
    if (IsDirty() && m_DocumentPersistenceEnabled) {
        std::string saveError;
        if (!SaveActiveMultiFrameRawProject(&saveError)) {
            return FinishMfdUi(
                outError,
                saveError.empty()
                    ? "Stack could not save the project before the memory-intensive run."
                    : "Stack could not save the project before processing: " + saveError,
                false);
        }
    }

    Stack::Project::RawProjectSnapshot snapshot = *m_Project->snapshot;
    const MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet ||
        sourceSet->operationIntent !=
            Stack::Project::MultiFrameOperationIntent::RawBurstDenoise) {
        return FinishMfdUi(outError, "The MFD source set no longer exists.", false);
    }

    Raw::Mfd::Parameters parameters;
    Raw::Mfd::SharedBurstSettings sharedBurstSettings;
    std::string parameterError;
    const auto serialized = sourceSet->settings.find("parameters");
    if (serialized == sourceSet->settings.end() ||
        !Raw::Mfd::DeserializeParameters(
            *serialized, parameters, &parameterError)) {
        return FinishMfdUi(
            outError,
            parameterError.empty()
                ? "The stored Burst preparation and safety parameters are invalid."
                : parameterError,
            false);
    }
    const bool storedSharedBurst =
        sourceSet->operationSchemaVersion ==
            Stack::Project::kMfdOperationSchemaVersion &&
        sourceSet->settings.value("algorithmId", std::string()) ==
            Raw::Mfd::kSharedBurstAlgorithmId;
    if (storedSharedBurst) {
        const auto stored = sourceSet->settings.find("sharedBurstSettings");
        if (stored == sourceSet->settings.end() ||
            !Raw::Mfd::DeserializeSharedBurstSettings(
                *stored, sharedBurstSettings, &parameterError)) {
            return FinishMfdUi(
                outError,
                parameterError.empty()
                    ? "The stored Shared Burst settings are invalid."
                    : parameterError,
                false);
        }
    }
    const nlohmann::json frameTrust = storedSharedBurst
        ? sourceSet->settings.value(
            "frameTrust", nlohmann::json::object())
        : nlohmann::json::object();
    const double requestedMemoryBudgetGiB = sourceSet->settings.value(
        "experimentalMemoryBudgetGiB", 0.0);
    const Raw::Mfd::MfdProcessingMemoryBudgetDecision memoryBudget =
        Raw::Mfd::ResolveMfdProcessingMemoryBudget(
            requestedMemoryBudgetGiB,
            Raw::Mfd::QueryPhysicalMemorySnapshot());
    if (!memoryBudget.valid) {
        return FinishMfdUi(outError, memoryBudget.message, false);
    }
    Raw::Mfd::MfdAlignmentMode alignmentMode =
        Raw::Mfd::MfdAlignmentMode::Full;
    const std::string storedAlignmentMode = sourceSet->settings.value(
        "experimentalAlignmentMode", std::string("full"));
    if (!Raw::Mfd::ParseMfdAlignmentMode(
            storedAlignmentMode, alignmentMode)) {
        return FinishMfdUi(
            outError,
            "The saved MFD alignment mode is invalid.",
            false);
    }

    std::size_t enabledCount = 0;
    bool referenceEnabled = false;
    Raw::CfaPattern cfaPattern = Raw::CfaPattern::Unknown;
    for (const SourceSetFrame& frame : sourceSet->frames) {
        if (!frame.enabled) continue;
        ++enabledCount;
        if (frame.frameId == sourceSet->referenceFrameId) {
            referenceEnabled = true;
        }
        const int orientationOverride =
            frame.metadataOverrides.value("orientation", 0);
        if (orientationOverride != 0) {
            return FinishMfdUi(
                outError,
                "The experimental processor does not yet apply orientation overrides. Set every frame to Use capture metadata before processing.",
                false);
        }
        const EmbeddedAssetRecord* asset =
            Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
        RawCaptureCompatibilitySummary capture;
        if (!asset ||
            !Stack::Project::DeserializeRawCaptureCompatibilitySummary(
                asset->captureMetadataSummary, capture, &parameterError) ||
            !capture.supported) {
            return FinishMfdUi(
                outError,
                parameterError.empty()
                    ? "An enabled MFD frame has invalid RAW compatibility metadata."
                    : parameterError,
                false);
        }
        if (cfaPattern == Raw::CfaPattern::Unknown) {
            cfaPattern = ParseCfaPatternName(capture.cfaPattern);
        }
    }
    if (enabledCount < 2u) {
        return FinishMfdUi(
            outError,
            "Enable at least two compatible RAW frames before processing.",
            false);
    }
    if (enabledCount > Raw::Mfd::kSharedBurstMaximumEnabledCaptures) {
        return FinishMfdUi(
            outError,
            "Shared Burst supports at most 30 enabled captures. Disable additional frames without removing them from the project.",
            false);
    }
    if (!referenceEnabled) {
        return FinishMfdUi(
            outError,
            "The reference frame must be enabled before processing.",
            false);
    }
    if (cfaPattern == Raw::CfaPattern::Unknown) {
        return FinishMfdUi(outError, "The Bayer CFA pattern is unavailable.", false);
    }

    Raw::RawMosaicDenoiseSettings sharedCfaDenoise;
    const nlohmann::json sharedPreRecipe = sourceSet->settings.value(
        "sharedPreMfdRecipe", nlohmann::json::object());
    if (sharedPreRecipe.is_object() &&
        sharedPreRecipe.contains("rawRecipeVersion")) {
        sharedCfaDenoise = Stack::RawRecipe::DeserializeRecipe(
            sharedPreRecipe).technical.mosaicDenoise;
    }
    const std::string sharedPreIdentity = sharedPreRecipe.dump();

    const Stack::Project::ProjectStoreHandle store = m_Project->store;
    const std::uint64_t generation =
        m_MfdExperimentalProcessingGeneration.fetch_add(
            1u, std::memory_order_relaxed) + 1u;
    const std::string projectId = snapshot.projectId;
    const std::uint64_t inputRevision = snapshot.mfdInputRevision;
    const std::filesystem::path workingRoot =
        AppPaths::GetCacheDirectory() / "MFD" / projectId / sourceSetId;

    m_MfdExperimentalProcessingTaskState = Async::TaskState::Queued;
    m_MfdExperimentalProcessingStatusText =
        "Queuing Shared Burst V1 Static Maximum processing (" +
        std::string(Raw::Mfd::MfdAlignmentModeName(alignmentMode)) +
        ") with a " +
        std::to_string(
            static_cast<double>(memoryBudget.budgetBytes) /
            Raw::Mfd::kMemoryPolicyGibibyte) +
        " GiB advisory memory target...";
    m_MfdExperimentalProcessingProjectId = projectId;
    m_MfdExperimentalProcessingSourceSetId = sourceSetId;
    m_MfdExperimentalProcessingInputRevision = inputRevision;
    auto progressState =
        std::make_shared<MfdExperimentalProcessingProgressState>();
    progressState->startedAt = std::chrono::steady_clock::now();
    progressState->lastAdvancedAt = progressState->startedAt;
    m_MfdExperimentalProcessingProgress = progressState;
    const auto notifier = GetNotifier();
    const auto activity = notifier.BeginActivity("Denoising");
    m_MfdProcessingActivity = activity;
    const auto completionLease = Stack::Notifications::RetainAsyncActivity(notifier, activity);
    const auto activityMetadata = Stack::Notifications::ForAsyncActivity(notifier, activity, "Denoising");

    struct WorkerOutcome {
        bool publish = false;
        bool canceled = false;
        std::string message;
        MfdExperimentalProcessingReport report;
        std::optional<MfdAdoptedRawResult> adoptedRawResult;
    };

    bool submitted = false;
    try {
        submitted = ProjectTasks().SubmitHighPriority(activityMetadata, [
            this,
            notifier, activity, completionLease,
            generation,
            projectId,
            sourceSetId,
            inputRevision,
            cfaPattern,
            workingRoot,
            store,
            parameters,
            sharedBurstSettings,
            frameTrust,
            alignmentMode,
            memoryBudget,
            enabledCount,
            sharedCfaDenoise,
            sharedPreIdentity,
            progressState,
            snapshot = std::move(snapshot)
        ]() mutable {
            const auto recordProgress = [progressState, notifier, activity](
                const Raw::Mfd::MfdProcessingProgress& progress) {
                std::lock_guard<std::mutex> lock(progressState->mutex);
                progressState->stageLabel =
                    Raw::Mfd::MfdProcessingStageName(progress.stage);
                progressState->message = progress.message;
                progressState->overallFraction = std::clamp(
                    progress.overallFraction,
                    progressState->overallFraction,
                    1.0);
                progressState->stageFraction = std::clamp(
                    progress.stageFraction, 0.0, 1.0);
                progressState->completedUnits = progress.completedUnits;
                progressState->totalUnits = progress.totalUnits;
                progressState->frameOrdinal = progress.frameOrdinal;
                progressState->frameCount = progress.frameCount;
                progressState->lastAdvancedAt =
                    std::chrono::steady_clock::now();
                std::optional<Stack::Notifications::Progress> measured;
                if (progress.totalUnits > 0) measured = Stack::Notifications::Progress{
                    static_cast<double>(progress.completedUnits), static_cast<double>(progress.totalUnits), ""};
                notifier.UpdateActivity(activity, progressState->stageLabel, measured);
            };
            ProjectTasks().PostToMain([this, generation, notifier, activity, completionLease]() {
                if (generation != m_MfdExperimentalProcessingGeneration.load(
                        std::memory_order_relaxed)) {
                    return;
                }
                m_MfdExperimentalProcessingTaskState = Async::TaskState::Running;
                m_MfdExperimentalProcessingStatusText =
                    "Materializing embedded originals and running Shared Burst V1...";
                notifier.UpdateActivity(activity, "Preparing burst captures...");
            });

            const auto canceled = [this, generation]() {
                return generation !=
                    m_MfdExperimentalProcessingGeneration.load(
                        std::memory_order_relaxed);
            };
            WorkerOutcome outcome;
            try {
                const MultiFrameSourceSet* workerSet =
                    Stack::Project::FindSourceSet(snapshot, sourceSetId);
                if (!workerSet) {
                    outcome.message = "The MFD source set disappeared before processing.";
                } else {
                    const std::filesystem::path sourceDirectory =
                        workingRoot / "sources";
                    Raw::Mfd::MfdProcessingRequest request;
                    request.parameters = parameters;
                    request.sharedBurstSettings = sharedBurstSettings;
                    request.fusionBackend =
                        Raw::Mfd::MfdFusionBackend::SharedBurstV1;
                    request.alignmentMode = alignmentMode;
                    request.workingDirectory = workingRoot / "processor";
                    request.memoryBudgetBytes = memoryBudget.budgetBytes;
                    request.enforceMemoryBudget = false;
                    const unsigned int hardwareThreads =
                        std::max(1u, std::thread::hardware_concurrency());
                    request.workerCount = std::clamp(
                        hardwareThreads / 2u, 1u, 4u);
                    request.shouldCancel = canceled;
                    request.reportProgress = recordProgress;

                    std::unordered_map<std::string, std::string> frameLabels;
                    bool requestValid = true;
                    std::string requestError;
                    std::size_t materializedCount = 0u;
                    Raw::Mfd::MfdProcessingProgress materializationProgress;
                    materializationProgress.stage =
                        Raw::Mfd::MfdProcessingStage::MaterializingSources;
                    materializationProgress.totalUnits = enabledCount;
                    materializationProgress.frameCount =
                        static_cast<std::uint32_t>(enabledCount);
                    materializationProgress.message =
                        "Materializing embedded originals.";
                    recordProgress(materializationProgress);
                    for (const SourceSetFrame& frame : workerSet->frames) {
                        if (!frame.enabled) continue;
                        if (canceled()) {
                            outcome.canceled = true;
                            outcome.message =
                                "MFD processing was canceled; the previous valid inspection remains available.";
                            requestValid = false;
                            break;
                        }
                        const EmbeddedAssetRecord* asset =
                            Stack::Project::FindEmbeddedAsset(
                                snapshot, frame.assetId);
                        RawCaptureCompatibilitySummary capture;
                        if (!asset ||
                            !Stack::Project::DeserializeRawCaptureCompatibilitySummary(
                                asset->captureMetadataSummary,
                                capture,
                                &requestError)) {
                            outcome.message = requestError.empty()
                                ? "An enabled MFD frame has no embedded asset."
                                : requestError;
                            requestValid = false;
                            break;
                        }
                        const std::filesystem::path materialized =
                            MaterializedAssetPath(sourceDirectory, *asset);
                        if (!CachedAssetMatchesSize(
                                materialized, asset->byteLength) &&
                            !store->CopyAssetToFile(
                                asset->assetId,
                                materialized,
                                &requestError)) {
                            outcome.message = requestError.empty()
                                ? "An embedded MFD original could not be materialized."
                                : requestError;
                            requestValid = false;
                            break;
                        }
                        Raw::Mfd::MfdProcessingFrameInput input;
                        input.stableFrameId = frame.frameId;
                        input.sourcePath = materialized;
                        input.expectedSourceSha256 = asset->sha256;
                        input.expectedSourceByteLength = asset->byteLength;
                        input.expectedVisibleExtent = {
                            static_cast<std::uint64_t>(capture.visibleWidth),
                            static_cast<std::uint64_t>(capture.visibleHeight)
                        };
                        input.sharedCfaDenoise = sharedCfaDenoise;
                        const auto trustValue = frameTrust.find(frame.frameId);
                        if (trustValue != frameTrust.end() &&
                            trustValue->is_number()) {
                            input.trustAttenuation = std::clamp(
                                trustValue->get<double>(), 0.0, 1.0);
                        }
                        const std::string preprocessingIdentityMaterial =
                            asset->sha256 + "\n" + sharedPreIdentity;
                        const std::vector<std::uint8_t> preprocessingIdentityBytes(
                            preprocessingIdentityMaterial.begin(),
                            preprocessingIdentityMaterial.end());
                        input.preprocessingIdentitySha256 =
                            Stack::RawEvidence::ComputeSourceIdentity(
                                preprocessingIdentityBytes).sha256;
                        if (frame.frameId == workerSet->referenceFrameId) {
                            request.referenceFrameIndex = request.frames.size();
                        }
                        request.frames.push_back(std::move(input));
                        frameLabels[frame.frameId] = frame.userLabel.empty()
                            ? asset->originalFilename
                            : frame.userLabel;
                        ++materializedCount;
                        materializationProgress.completedUnits =
                            materializedCount;
                        materializationProgress.frameOrdinal =
                            static_cast<std::uint32_t>(materializedCount);
                        materializationProgress.stageFraction =
                            static_cast<double>(materializedCount) /
                            static_cast<double>(enabledCount);
                        materializationProgress.overallFraction =
                            0.05 * materializationProgress.stageFraction;
                        materializationProgress.message =
                            "Materialized original " +
                            std::to_string(materializedCount) + " of " +
                            std::to_string(enabledCount) + ".";
                        recordProgress(materializationProgress);
                    }

                    if (requestValid) {
                        Raw::Mfd::MfdProcessingServices processingServices =
                            Raw::Mfd::MakeFilesystemMfdProcessingServices();
                        processingServices.executeOpenGlTask = [this](
                            Raw::OpenGlTask task,
                            std::string& error) {
                            return ExecuteRenderOwnerOpenGlTaskBlocking(
                                std::move(task), error);
                        };
                        Raw::Mfd::MfdProcessingResult result =
                            Raw::Mfd::ProcessMfdBurst(
                                request,
                                processingServices);
                        // Preserve processor evidence even when no mosaic is
                        // published.  A failed run is often actionable (for
                        // example, one bad exposure relationship) and must not
                        // collapse into an unexplained "output not current"
                        // state in the MultiFrame inspector.
                        outcome.report.projectId = projectId;
                        outcome.report.sourceSetId = sourceSetId;
                        outcome.report.inputRevision = inputRevision;
                        outcome.report.statusName =
                            Raw::Mfd::MfdProcessingStatusName(result.status);
                        outcome.report.message = result.message;
                        outcome.report.executionBackend =
                            result.diagnostics.executionBackend;
                        outcome.report.gpuDeviceIdentity =
                            result.diagnostics.gpuDeviceIdentity;
                        outcome.report.gpuFallbackReason =
                            result.diagnostics.gpuFallbackReason;
                        outcome.report.gpuDispatchedTileCount =
                            result.diagnostics.gpuDispatchedTileCount;
                        outcome.report.registrationBackend =
                            result.diagnostics.registrationBackend;
                        outcome.report.registrationGpuDeviceIdentity =
                            result.diagnostics.registrationGpuDeviceIdentity;
                        outcome.report.registrationGpuFallbackReason =
                            result.diagnostics.registrationGpuFallbackReason;
                        outcome.report.registrationGpuDispatchCount =
                            result.diagnostics.registrationGpuDispatchCount;
                        outcome.report.registrationGpuScoredCandidateCount =
                            result.diagnostics.
                                registrationGpuScoredCandidateCount;
                        outcome.report.compatibleAlternateCount =
                            result.diagnostics.compatibleAlternateCount;
                        outcome.report.selectedCaptureCount =
                            request.frames.size();
                        outcome.report.acceptedAlternateCount =
                            result.diagnostics.acceptedAlternateCount;
                        outcome.report.exposureGroupedCaptureCount =
                            result.diagnostics.exposureGroupedCaptureCount;
                        outcome.report.exposureExcludedCaptureCount =
                            result.diagnostics.exposureExcludedCaptureCount;
                        outcome.report.estimatedPeakResidentBytes =
                            result.diagnostics.estimatedPeakResidentBytes;
                        outcome.report.memoryBudgetBytes =
                            memoryBudget.budgetBytes;
                        outcome.report.availablePhysicalBytesAtStart =
                            memoryBudget.physicalMemory.availablePhysicalBytes;
                        outcome.report.protectedMemoryReserveBytes =
                            memoryBudget.reserveBytes;
                        outcome.report.automaticMemoryBudget =
                            memoryBudget.automatic;
                        outcome.report.memoryBudgetConstrained =
                            memoryBudget.constrainedToSafeCeiling;
                        outcome.report.computedTileCount =
                            result.diagnostics.streaming.computedTileCount;
                        outcome.report.cacheHitTileCount =
                            result.diagnostics.streaming.cacheHitTileCount;
                        outcome.report.meanRobustAttenuation =
                            result.diagnostics.meanRobustAttenuation;
                        outcome.report.predictedIndependentNoiseReduction =
                            result.diagnostics.predictedIndependentNoiseReduction;
                        outcome.report.independentNoiseReductionClaimQualified =
                            result.diagnostics
                                .independentNoiseReductionClaimQualified;
                        for (const auto& frame : result.diagnostics.frames) {
                            MfdExperimentalFrameReport frameReport;
                            const auto label =
                                frameLabels.find(frame.stableFrameId);
                            frameReport.label = label == frameLabels.end()
                                ? frame.stableFrameId
                                : label->second;
                            frameReport.reference = frame.referenceFrame;
                            frameReport.attempted = frame.attempted;
                            frameReport.acceptedForFusion =
                                frame.acceptedForFusion;
                            frameReport.exposureGrouped =
                                frame.exposureGrouped;
                            frameReport.exposureDriftEv =
                                frame.exposureDriftEv;
                            frameReport.message =
                                !frame.referenceFrame && !frame.attempted
                                ? "Not attempted: " + result.message
                                : frame.message;
                            frameReport.noiseModelQuality =
                                Raw::Mfd::NoiseModelQualityName(
                                    frame.noiseQuality);
                            outcome.report.frames.push_back(
                                std::move(frameReport));
                        }
                        if (result.status ==
                                Raw::Mfd::MfdProcessingStatus::Canceled ||
                            canceled()) {
                            outcome.canceled = true;
                            outcome.message =
                                "MFD processing was canceled; the previous valid inspection remains available.";
                        } else if (
                            (result.status !=
                                 Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
                             result.status !=
                                 Raw::Mfd::MfdProcessingStatus::ReferenceOnly) ||
                            !result.published.result) {
                            outcome.message = result.message.empty()
                                ? "Shared Burst V1 did not publish a result."
                                : result.message;
                        } else {
                            const std::filesystem::path inspectionDirectory =
                                workingRoot / "inspection" /
                                ("input-" + std::to_string(inputRevision) +
                                 "-run-" + std::to_string(generation));
                            Raw::Mfd::MfdInspectionInputView inspection;
                            inspection.sampleId = sourceSetId;
                            inspection.cfaPattern = cfaPattern;
                            inspection.extent =
                                result.published.result->extent;
                            inspection.referenceNormalizedMosaic =
                                &result.referenceNormalizedMosaic;
                            inspection.outputNormalizedMosaic =
                                &result.published.result->normalizedMosaic;
                            inspection.fusionDiagnostics =
                                &result.published.result->diagnostics;
                            Raw::Mfd::MfdInspectionSummary summary;
                            std::string inspectionError;
                            Raw::Mfd::MfdProcessingProgress
                                inspectionProgress;
                            inspectionProgress.stage = Raw::Mfd::
                                MfdProcessingStage::WritingInspection;
                            inspectionProgress.overallFraction = 0.95;
                            inspectionProgress.stageFraction = 0.0;
                            inspectionProgress.message =
                                "Writing previews and inspection diagnostics.";
                            recordProgress(inspectionProgress);
                            if (!Raw::Mfd::WriteMfdInspectionPacketView(
                                    inspection,
                                    inspectionDirectory,
                                    summary,
                                    &inspectionError)) {
                                outcome.message = inspectionError.empty()
                                    ? "The Bayer result was produced, but its inspection packet could not be published."
                                    : inspectionError;
                            } else {
                                inspectionProgress.stage = Raw::Mfd::
                                    MfdProcessingStage::Finalizing;
                                inspectionProgress.overallFraction = 1.0;
                                inspectionProgress.stageFraction = 1.0;
                                inspectionProgress.message =
                                    "Processing and inspection output completed.";
                                recordProgress(inspectionProgress);
                                outcome.publish = true;
                                auto developedRaw =
                                    std::make_shared<Raw::RawImageData>();
                                developedRaw->metadata =
                                    result.referenceMetadata;
                                developedRaw->metadata.sourcePath =
                                    "mfd://" + projectId + "/" +
                                    sourceSetId;
                                developedRaw->metadata
                                    .sourceContentSha256.clear();
                                developedRaw->metadata.sourceByteSize = 0u;
                                developedRaw->metadata.rawWidth =
                                    static_cast<int>(
                                        result.published.result->extent.width);
                                developedRaw->metadata.rawHeight =
                                    static_cast<int>(
                                        result.published.result->extent.height);
                                developedRaw->metadata.visibleWidth =
                                    developedRaw->metadata.rawWidth;
                                developedRaw->metadata.visibleHeight =
                                    developedRaw->metadata.rawHeight;
                                developedRaw->metadata.leftMargin = 0;
                                developedRaw->metadata.topMargin = 0;
                                developedRaw->metadata.pixelLayout =
                                    Raw::RawPixelLayout::MosaicBayer;
                                developedRaw->metadata.mosaiced = true;
                                developedRaw->metadata.cfaPattern =
                                    result.outputCfaPattern;
                                developedRaw->metadata.dngActiveArea = {
                                    0,
                                    0,
                                    developedRaw->metadata.rawHeight,
                                    developedRaw->metadata.rawWidth
                                };
                                developedRaw->metadata.hasDngActiveArea = true;
                                developedRaw->metadata.dngMaskedAreas.clear();
                                developedRaw->metadata.uploadFormat = "R32F";
                                const auto publishedResult =
                                    result.published.result;
                                developedRaw->normalizedMosaicInputContract =
                                    Raw::NormalizedMosaicInputContract::
                                        MfdReferencePreGain;
                                // Inspection consumes the rich per-pixel
                                // diagnostics before adoption. RAW editing
                                // needs only the immutable mosaic; giving it
                                // independent ownership lets the much larger
                                // diagnostic publication be released when the
                                // worker outcome leaves scope.
                                developedRaw->normalizedMosaicBuffer =
                                    std::make_shared<const std::vector<float>>(
                                        publishedResult->normalizedMosaic);
                                developedRaw->normalizedMosaicContentHash =
                                    publishedResult->contentHash;
                                MfdAdoptedRawResult adopted;
                                adopted.projectId = projectId;
                                adopted.sourceSetId = sourceSetId;
                                adopted.inputRevision = inputRevision;
                                adopted.contentHash =
                                    publishedResult->contentHash;
                                adopted.rawData =
                                    std::move(developedRaw);
                                outcome.adoptedRawResult =
                                    std::move(adopted);
                                outcome.report.inspectionDirectory =
                                    inspectionDirectory;
                                outcome.report.outputPreviewPath =
                                    inspectionDirectory / "output-preview.png";
                                outcome.report.referencePreviewPath =
                                    inspectionDirectory / "reference-preview.png";
                                outcome.report.contributingPixelFraction =
                                    summary.contributingPixelFraction;
                                outcome.report.exactReferencePixelFraction =
                                    summary.exactReferencePixelFraction;
                                outcome.report.meanAbsoluteDelta =
                                    summary.meanAbsoluteDelta;
                                outcome.report.percentile99AbsoluteDelta =
                                    summary.percentile99AbsoluteDelta;
                                outcome.report.meanEffectiveSampleCount =
                                    summary.meanEffectiveSampleCount;
                                outcome.message = result.message;
                            }
                        }
                    }
                }
            } catch (const std::bad_alloc&) {
                outcome.message =
                    "The system could not satisfy an MFD memory allocation; the saved project remains intact and no new result was published.";
            } catch (const std::exception& exception) {
                outcome.message = std::string(
                    "MFD processing failed without replacing the previous result: ") +
                    exception.what();
            } catch (...) {
                outcome.message =
                    "MFD processing failed without replacing the previous result.";
            }

            ProjectTasks().PostToMain([
                this,
                notifier, activity, completionLease,
                generation,
                projectId,
                sourceSetId,
                inputRevision,
                parameters,
                sharedBurstSettings,
                frameTrust,
                outcome = std::move(outcome)
            ]() mutable {
                const auto activeSnapshot = m_Project->snapshot;
                const Raw::MultiFrame::PublicationFenceResult publicationFence =
                    Raw::MultiFrame::EvaluatePublicationFence(
                        { generation, projectId, inputRevision },
                        {
                            m_MfdExperimentalProcessingGeneration.load(
                                std::memory_order_relaxed),
                            static_cast<bool>(activeSnapshot),
                            activeSnapshot
                                ? std::string_view(activeSnapshot->projectId)
                                : std::string_view(),
                            activeSnapshot ? activeSnapshot->mfdInputRevision : 0u,
                            activeSnapshot && Stack::Project::FindSourceSet(
                                *activeSnapshot, sourceSetId)
                        });
                if (publicationFence ==
                    Raw::MultiFrame::PublicationFenceResult::SupersededGeneration) {
                    notifier.CancelActivity(activity, "Denoising run superseded.");
                    return;
                }
                if (outcome.canceled) {
                    m_MfdExperimentalProcessingTaskState =
                        Async::TaskState::Idle;
                    m_MultiFrameWorkspaceStatusText.clear();
                    m_MfdExperimentalProcessingStatusText =
                        outcome.message;
                    notifier.CancelActivity(activity, "Denoising cancelled.");
                    return;
                }
                if (publicationFence !=
                    Raw::MultiFrame::PublicationFenceResult::Current) {
                    m_MfdExperimentalProcessingTaskState =
                        Async::TaskState::Idle;
                    m_MultiFrameWorkspaceStatusText.clear();
                    m_MfdExperimentalProcessingStatusText =
                        "The run finished after its project inputs changed, so it was not adopted.";
                    notifier.CancelActivity(activity, "Burst inputs changed. The result was not adopted.");
                    return;
                }
                if (!outcome.publish || !outcome.adoptedRawResult || !outcome.adoptedRawResult->rawData) {
                    m_MfdExperimentalProcessingTaskState =
                        Async::TaskState::Failed;
                    m_MultiFrameWorkspaceStatusText.clear();
                    m_MfdExperimentalProcessingStatusText =
                        outcome.message.empty()
                            ? "MFD processing failed; the previous valid inspection remains available."
                            : outcome.message;
                    if (!outcome.report.sourceSetId.empty()) {
                        m_MfdExperimentalProcessingReport =
                            std::move(outcome.report);
                    }
                    notifier.FailActivity(activity, "Denoising failed.", m_MfdExperimentalProcessingStatusText);
                    return;
                }
                const std::filesystem::path completedCoverPath =
                    outcome.report.outputPreviewPath;
                m_MfdExperimentalProcessingReport =
                    std::move(outcome.report);
                m_MfdAdoptedRawResult =
                    std::move(outcome.adoptedRawResult);
                for (EditorNodeGraph::Node& node :
                     m_Project->graph.EditNodes()) {
                    if (node.kind ==
                            EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                        node.multiFrameDenoise.sourceSetId == sourceSetId &&
                        node.multiFrameDenoise.managed &&
                        !node.multiFrameDenoise.quarantined) {
                        node.multiFrameDenoise.presentationStatus =
                            "Developed MFD result ready.";
                        node.multiFrameDenoise.resultState = "ready";
                        break;
                    }
                }
                m_MfdExperimentalProcessingTaskState =
                    Async::TaskState::Ready;
                m_MultiFrameWorkspaceStatusText.clear();
                m_MfdExperimentalProcessingStatusText =
                    outcome.message;
                if (m_Project->snapshot) {
                    auto updated =
                        std::make_shared<Stack::Project::RawProjectSnapshot>(
                            *m_Project->snapshot);
                    if (auto* completedSet =
                            Stack::Project::FindSourceSet(
                                *updated, sourceSetId)) {
                        const nlohmann::json oldSettings =
                            completedSet->settings;
                        nlohmann::json current =
                            Stack::Project::MakeDefaultMfdOperationSettings();
                        current["parameters"] =
                            Raw::Mfd::SerializeParameters(parameters);
                        current["sharedBurstSettings"] =
                            Raw::Mfd::SerializeSharedBurstSettings(
                                sharedBurstSettings);
                        current["frameTrust"] = frameTrust;
                        for (const char* preserved : {
                                 "sharedPreMfdRecipe",
                                 "sharedPostMfdRecipe",
                                 "viewTransformPlacement",
                                 "graphViewTransformNodeUuid",
                                 "graphViewTransformSettings",
                                 "experimentalAlignmentMode",
                                 "experimentalMemoryBudgetGiB" }) {
                            const auto found = oldSettings.find(preserved);
                            if (found != oldSettings.end()) {
                                current[preserved] = *found;
                            }
                        }
                        completedSet->operationSchemaVersion =
                            Stack::Project::kMfdOperationSchemaVersion;
                        completedSet->settings = std::move(current);
                    }
                    std::ifstream coverInput(
                        completedCoverPath, std::ios::binary);
                    if (coverInput) {
                        std::vector<unsigned char> coverBytes {
                            std::istreambuf_iterator<char>(coverInput),
                            std::istreambuf_iterator<char>() };
                        if (!coverBytes.empty()) {
                            updated->coverThumbnailBytes =
                                std::move(coverBytes);
                        }
                    }
                    m_Project->snapshot = std::move(updated);
                    MarkDirty();
                }
                MarkRenderDirty();
                notifier.CompleteActivity(activity, "Denoised RAW result ready.");
            });
        });
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        m_MfdExperimentalProcessingProgress.reset();
        m_MfdExperimentalProcessingTaskState = Async::TaskState::Failed;
        m_MultiFrameWorkspaceStatusText.clear();
        m_MfdExperimentalProcessingStatusText =
            "The experimental MFD run could not be queued.";
        notifier.FailActivity(activity, m_MfdExperimentalProcessingStatusText);
        return FinishMfdUi(
            outError, m_MfdExperimentalProcessingStatusText, false);
    }
    return FinishMfdUi(outError, std::string(), true);
}

void EditorModule::CancelMfdExperimentalProcessing(
    const std::string& reason,
    bool clearPublishedReport) {
    const bool busy = IsMfdExperimentalProcessingBusy();
    if (busy) GetNotifier().CancelActivity(m_MfdProcessingActivity,
        reason.empty() ? "Denoising cancelled." : reason);
    m_MfdExperimentalProcessingGeneration.fetch_add(
        1u, std::memory_order_relaxed);
    m_MfdExperimentalProcessingTaskState = Async::TaskState::Idle;
    m_MultiFrameWorkspaceStatusText.clear();
    if (busy || !reason.empty()) {
        m_MfdExperimentalProcessingStatusText = reason.empty()
            ? "MFD processing canceled; the previous valid inspection remains available."
            : reason;
    }
    if (clearPublishedReport) {
        m_MfdExperimentalProcessingProjectId.clear();
        m_MfdExperimentalProcessingSourceSetId.clear();
        m_MfdExperimentalProcessingInputRevision = 0;
        m_MfdExperimentalProcessingReport.reset();
        m_MfdAdoptedRawResult.reset();
        m_MfdExperimentalParameterDraft = {};
    }
    m_MfdExperimentalProcessingProgress.reset();
}
