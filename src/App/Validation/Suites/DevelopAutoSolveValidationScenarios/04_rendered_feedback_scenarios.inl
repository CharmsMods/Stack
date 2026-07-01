// Included inside DevelopAutoSolveValidation.cpp::ValidateDevelopAutoSolveBehavior().
// Rendered metric fixtures, feedback adoption/merge/refine, convergence, and stop-condition checks.

    const Stack::Validation::Detail::DevelopAutoSolveRenderedMetricFixtures renderedMetricFixtures =
        Stack::Validation::Detail::BuildDevelopAutoSolveRenderedMetricFixtures();

    const auto& visualRiskMetrics = renderedMetricFixtures.visualRiskMetrics;
    const auto& spatialRiskMetrics = renderedMetricFixtures.spatialRiskMetrics;
    const auto& subjectRiskMetrics = renderedMetricFixtures.subjectRiskMetrics;
    const auto& markedSubjectMetrics = renderedMetricFixtures.markedSubjectMetrics;
    const auto& lowPrioritySubjectMetrics = renderedMetricFixtures.lowPrioritySubjectMetrics;
    const auto& colorCastMetrics = renderedMetricFixtures.colorCastMetrics;
    const auto& similarRenderedMetrics = renderedMetricFixtures.similarRenderedMetrics;
    const auto& distinctRenderedMetrics = renderedMetricFixtures.distinctRenderedMetrics;
    const auto& renderedCleanShadowMetrics = renderedMetricFixtures.renderedCleanShadowMetrics;

    const bool renderedVisualRiskMetricsPopulated = renderedMetricFixtures.renderedVisualRiskMetricsPopulated;
    const bool renderedHighlightGrayMetricsPopulated = renderedMetricFixtures.renderedHighlightGrayMetricsPopulated;
    const bool renderedMeaningfulHighlightMetricsPopulated = renderedMetricFixtures.renderedMeaningfulHighlightMetricsPopulated;
    const bool renderedLocalMetricsPopulated = renderedMetricFixtures.renderedLocalMetricsPopulated;
    const bool renderedSubjectMetricsPopulated = renderedMetricFixtures.renderedSubjectMetricsPopulated;
    const bool renderedMarkedSubjectMetricsPopulated = renderedMetricFixtures.renderedMarkedSubjectMetricsPopulated;
    const bool renderedMarkedLowPriorityMetricsPopulated = renderedMetricFixtures.renderedMarkedLowPriorityMetricsPopulated;
    const bool renderedSpatialRiskMetricsPopulated = renderedMetricFixtures.renderedSpatialRiskMetricsPopulated;
    const bool renderedColorCastMetricsPopulated = renderedMetricFixtures.renderedColorCastMetricsPopulated;
    const bool renderedDuplicateMetricDistanceWorks = renderedMetricFixtures.renderedDuplicateMetricDistanceWorks;
    const bool renderedStageBoundaryClassifierWorks = renderedMetricFixtures.renderedStageBoundaryClassifierWorks;
    const bool renderedStageAwareDuplicateClusteringWorks = renderedMetricFixtures.renderedStageAwareDuplicateClusteringWorks;
    const bool renderedDamageClassifierWorks = renderedMetricFixtures.renderedDamageClassifierWorks;
    const bool renderedLocalRefineIntentWorks = renderedMetricFixtures.renderedLocalRefineIntentWorks;
    const bool renderedRelativeComparisonWorks = renderedMetricFixtures.renderedRelativeComparisonWorks;

    const float renderedDuplicateMetricDistance = renderedMetricFixtures.renderedDuplicateMetricDistance;
    const float renderedDistinctMetricDistance = renderedMetricFixtures.renderedDistinctMetricDistance;
    const float renderedLocalMetricDistance = renderedMetricFixtures.renderedLocalMetricDistance;
    const float renderedColorMetricDistance = renderedMetricFixtures.renderedColorMetricDistance;
    const float renderedMarkedSubjectMetricDistance = renderedMetricFixtures.renderedMarkedSubjectMetricDistance;
    const float finishOnlyFinalMetricDistance = renderedMetricFixtures.finishOnlyFinalMetricDistance;
    const float finishOnlyPreFinishMetricDistance = renderedMetricFixtures.finishOnlyPreFinishMetricDistance;
    const float preFinishChangedFinalMetricDistance = renderedMetricFixtures.preFinishChangedFinalMetricDistance;
    const float preFinishChangedPreFinishMetricDistance = renderedMetricFixtures.preFinishChangedPreFinishMetricDistance;
    const float stageAwareDuplicateFinalDistance = renderedMetricFixtures.stageAwareDuplicateFinalDistance;
    const float stageAwareDuplicatePreFinishDistance = renderedMetricFixtures.stageAwareDuplicatePreFinishDistance;
    const bool stageAwareDuplicatePreFinishDistinct = renderedMetricFixtures.stageAwareDuplicatePreFinishDistinct;
    const float stageAwareMaskedFinalDistance = renderedMetricFixtures.stageAwareMaskedFinalDistance;
    const float stageAwareMaskedPreFinishDistance = renderedMetricFixtures.stageAwareMaskedPreFinishDistance;
    const bool stageAwareMaskedPreFinishDistinct = renderedMetricFixtures.stageAwareMaskedPreFinishDistinct;
    const float relativeAdjustedRawScore = renderedMetricFixtures.relativeAdjustedRawScore;
    const float relativeAdjustedIntentScore = renderedMetricFixtures.relativeAdjustedIntentScore;
    const float relativeRawScoreRepairDelta = renderedMetricFixtures.relativeRawScoreRepairDelta;
    const float relativeIntentRepairDelta = renderedMetricFixtures.relativeIntentRepairDelta;
    const float relativeRawScoreRegressionPenalty = renderedMetricFixtures.relativeRawScoreRegressionPenalty;

    const std::string& finishOnlyStageBoundary = renderedMetricFixtures.finishOnlyStageBoundary;
    const std::string& preFinishChangedStageBoundary = renderedMetricFixtures.preFinishChangedStageBoundary;
    const std::string& damagedClipReason = renderedMetricFixtures.damagedClipReason;
    const std::string& damagedHaloReason = renderedMetricFixtures.damagedHaloReason;
    const std::string& damagedGrayReason = renderedMetricFixtures.damagedGrayReason;
    const std::string& damagedShadowNoiseReason = renderedMetricFixtures.damagedShadowNoiseReason;
    const std::string& damagedSpatialHotspotReason = renderedMetricFixtures.damagedSpatialHotspotReason;
    const std::string& damagedColorCastReason = renderedMetricFixtures.damagedColorCastReason;
    const std::string& safeRenderedDamageReason = renderedMetricFixtures.safeRenderedDamageReason;
    const std::string& relativeRawScoreStatus = renderedMetricFixtures.relativeRawScoreStatus;
    const std::string& relativeIntentStatus = renderedMetricFixtures.relativeIntentStatus;
    const std::string& localCenterShadowIntent = renderedMetricFixtures.localCenterShadowIntent;
    const std::string& localHighlightIntent = renderedMetricFixtures.localHighlightIntent;
    const std::string& structuredHighlightPressureIntent = renderedMetricFixtures.structuredHighlightPressureIntent;
    const std::string& structuredHighlightPressureReason = renderedMetricFixtures.structuredHighlightPressureReason;
    const std::string& localSpatialHighlightRiskIntent = renderedMetricFixtures.localSpatialHighlightRiskIntent;
    const std::string& localSpatialHighlightRiskReason = renderedMetricFixtures.localSpatialHighlightRiskReason;
    const std::string& localFlatIntent = renderedMetricFixtures.localFlatIntent;
    const std::string& renderedCleanShadowIntent = renderedMetricFixtures.renderedCleanShadowIntent;
    const std::string& localSpatialShadowRiskIntent = renderedMetricFixtures.localSpatialShadowRiskIntent;
    const std::string& localSpatialShadowRiskReason = renderedMetricFixtures.localSpatialShadowRiskReason;
    const std::string& localSpatialFlatRiskIntent = renderedMetricFixtures.localSpatialFlatRiskIntent;
    const std::string& localSpatialFlatRiskReason = renderedMetricFixtures.localSpatialFlatRiskReason;
    const std::string& renderedPreserveTextureIntent = renderedMetricFixtures.renderedPreserveTextureIntent;

    const auto renderedMetricsJson =
        Stack::Validation::Detail::DevelopAutoSolveRenderedMetricsToJson;

    std::string renderedFeedbackCandidateId;
    std::string renderedFeedbackCandidateLabel;
    std::string renderedFeedbackSecondCandidateId;
    std::string renderedFeedbackSecondCandidateLabel;
    std::string renderedFeedbackThirdCandidateId;
    std::string renderedFeedbackThirdCandidateLabel;
    if (candidateSolves.is_array()) {
        for (const nlohmann::json& candidate : candidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) == selectedCandidateId ||
                candidate.value("status", std::string()) != "survivor") {
                continue;
            }
            if (renderedFeedbackCandidateId.empty()) {
                renderedFeedbackCandidateId = candidate.value("id", std::string());
                renderedFeedbackCandidateLabel = candidate.value("label", renderedFeedbackCandidateId);
            } else if (renderedFeedbackSecondCandidateId.empty()) {
                renderedFeedbackSecondCandidateId = candidate.value("id", std::string());
                renderedFeedbackSecondCandidateLabel = candidate.value("label", renderedFeedbackSecondCandidateId);
            } else {
                renderedFeedbackThirdCandidateId = candidate.value("id", std::string());
                renderedFeedbackThirdCandidateLabel = candidate.value("label", renderedFeedbackThirdCandidateId);
                break;
            }
        }
    }
    EditorNodeGraph::RawDevelopPayload renderedFeedbackPayload = payload;
    const std::uint64_t renderedFeedbackFingerprint =
        renderedFeedbackPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.86f;
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedStageBoundarySignal"] = "finishToneOnly";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedRevisionStage"] = "finishTone";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedRevisionReason"] =
            "Synthetic validation: final rendered metrics changed while pre-finish stayed stable.";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.54f }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.86f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedFeedbackPayload, metadata, true);
    const bool renderedFeedbackAdoptedCandidate =
        !renderedFeedbackCandidateId.empty() &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetrics" &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == renderedFeedbackCandidateId &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedFeedbackPayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";
    const bool renderedFeedbackRevisionStageWritten =
        !renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()).empty() &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) != "none" &&
        !renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionReason", std::string()).empty();
    const bool renderedFeedbackFinishToneStageOverride =
        !renderedFeedbackCandidateId.empty() &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) == "finishTone" &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionReason", std::string()).find("pre-finish stayed stable") != std::string::npos;
    const nlohmann::json renderedFeedbackLoop =
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedFeedbackLoopActive =
        renderedFeedbackLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedFeedbackLoop.value("state", std::string()) == "active" &&
        renderedFeedbackLoop.value("action", std::string()) == "adopted" &&
        renderedFeedbackLoop.value("nextStep", std::string()) == "renderUpdatedSolve" &&
        renderedFeedbackLoop.value("requiresRenderedMetrics", false) &&
        !renderedFeedbackLoop.value("requiresAutoSolve", true) &&
        renderedFeedbackLoop.value("pass", -1) == 1 &&
        renderedFeedbackLoop.value("maxPasses", 0) == 3 &&
        renderedFeedbackLoop.value("appliedRenderedFingerprint", static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint;
    const nlohmann::json renderedFeedbackContinuation =
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const nlohmann::json renderedFeedbackLoopContinuation =
        renderedFeedbackLoop.value("continuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyContinues =
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationVersion", std::string()) == "RenderedContinuationV1" &&
        renderedFeedbackContinuation.value("version", std::string()) == "RenderedContinuationV1" &&
        renderedFeedbackContinuation.value("decision", std::string()) == "continue" &&
        renderedFeedbackContinuation.value("reason", std::string()) == "adopted" &&
        renderedFeedbackContinuation.value("nextStep", std::string()) == "renderUpdatedSolve" &&
        renderedFeedbackContinuation.value("requiresRenderedMetrics", false) &&
        !renderedFeedbackContinuation.value("requiresAutoSolve", true) &&
        renderedFeedbackContinuation.value("shouldContinue", false) &&
        renderedFeedbackContinuation.value("pass", -1) == 1 &&
        renderedFeedbackContinuation.value("remainingPasses", -1) == 2 &&
        renderedFeedbackContinuation.value("stageFocus", std::string()) == "finishTone" &&
        renderedFeedbackLoopContinuation.value("decision", std::string()) == "continue";
    const nlohmann::json renderedFeedbackConvergenceEvidence =
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceContinuesAfterFeedback =
        renderedFeedbackConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedFeedbackConvergenceEvidence.value("state", std::string()) == "continuing" &&
        renderedFeedbackConvergenceEvidence.value("decision", std::string()) == "continue" &&
        renderedFeedbackConvergenceEvidence.value("reason", std::string()) == "adopted" &&
        renderedFeedbackConvergenceEvidence.value("shouldContinue", false) &&
        renderedFeedbackConvergenceEvidence.value("requiresRenderedMetrics", false) &&
        !renderedFeedbackConvergenceEvidence.value("requiresAutoSolve", true) &&
        renderedFeedbackConvergenceEvidence.value("pass", -1) == 1 &&
        renderedFeedbackConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "continue" &&
        renderedFeedbackConvergenceEvidence.value("rendered", nlohmann::json::object()).value("feedbackApplied", false) &&
        renderedFeedbackConvergenceEvidence.value("rendered", nlohmann::json::object()).value("revisionStage", std::string()) == "finishTone";

    EditorNodeGraph::RawDevelopPayload convergenceAdmissionPayload = payload;
    convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 1;
    convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateConvergenceEvidence"] = {
        { "version", "ConvergenceEvidenceV1" },
        { "state", "awaitingRenderedMetrics" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "shouldContinue", true },
        { "pass", 1 },
        { "nextPass", 1 },
        { "maxPasses", 3 }
    };
    convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "nextStep", "renderCandidates" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "shouldContinue", true },
        { "pass", 1 },
        { "nextPass", 1 },
        { "maxPasses", 3 }
    };
    const std::string convergenceAdmissionSelectedId =
        convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    std::string convergenceAdmissionChallengerId;
    std::string convergenceAdmissionChallengerLabel;
    const nlohmann::json convergenceAdmissionCandidates =
        convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (convergenceAdmissionCandidates.is_array()) {
        for (const nlohmann::json& candidate : convergenceAdmissionCandidates) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            if (!id.empty() &&
                id != convergenceAdmissionSelectedId &&
                candidate.value("status", std::string()) == "survivor") {
                convergenceAdmissionChallengerId = id;
                convergenceAdmissionChallengerLabel = candidate.value("label", id);
                break;
            }
        }
    }
    const std::uint64_t convergenceAdmissionFingerprint =
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!convergenceAdmissionSelectedId.empty() &&
        !convergenceAdmissionChallengerId.empty() &&
        convergenceAdmissionFingerprint != 0) {
        constexpr float kAdmissionSelectedScore = 0.700f;
        constexpr float kAdmissionMarginalBestScore = 0.729f;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] =
            convergenceAdmissionFingerprint;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] =
            convergenceAdmissionChallengerId;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            convergenceAdmissionChallengerLabel;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] =
            kAdmissionMarginalBestScore;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackAppliedFingerprint"] =
            static_cast<std::uint64_t>(0);
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] =
            nlohmann::json::array({
                {
                    { "id", convergenceAdmissionSelectedId },
                    { "label", convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", convergenceAdmissionSelectedId) },
                    { "success", true },
                    { "renderScore", kAdmissionSelectedScore }
                },
                {
                    { "id", convergenceAdmissionChallengerId },
                    { "label", convergenceAdmissionChallengerLabel },
                    { "success", true },
                    { "renderScore", kAdmissionMarginalBestScore }
                }
            });
    }
    EditorModule::ApplyDevelopAutoSolve(convergenceAdmissionPayload, metadata, true);
    const nlohmann::json convergenceAdmissionEvidence =
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceEvidence",
            nlohmann::json::object());
    const nlohmann::json convergenceAdmissionRecord =
        convergenceAdmissionEvidence.value("admission", nlohmann::json::object());
    const bool convergenceEvidenceAdmissionTightensMarginalContinuation =
        !convergenceAdmissionChallengerId.empty() &&
        !convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackStopReason",
            std::string()) == "convergenceAdmissionNoMeaningfulImprovement" &&
        convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", false) &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionVersion",
            std::string()) == "ConvergenceAdmissionV1" &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionTightened",
            false) &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionMinimumImprovement",
            0.0f) >
            convergenceAdmissionPayload.integratedToneLayerJson.value(
                "autoCandidateConvergenceAdmissionBaseMinimumImprovement",
                1.0f) &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionEvidenceState",
            std::string()) == "awaitingRenderedMetrics" &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionEvidenceDecision",
            std::string()) == "waitForRenderedMetrics" &&
        convergenceAdmissionEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        convergenceAdmissionEvidence.value("state", std::string()) == "converged" &&
        convergenceAdmissionEvidence.value("reason", std::string()) == "convergenceAdmissionNoMeaningfulImprovement" &&
        convergenceAdmissionRecord.value("version", std::string()) == "ConvergenceAdmissionV1" &&
        convergenceAdmissionRecord.value("tightened", false);

    EditorNodeGraph::RawDevelopPayload continuationBiasPayload = renderedFeedbackPayload;
    EditorModule::ApplyDevelopAutoSolve(continuationBiasPayload, metadata, true);
    const bool continuationBiasDiagnosticsWritten =
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasVersion", std::string()) == "ContinuationCandidateBiasV1" &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasActive", false) &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasDecision", std::string()) == "continue" &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasReason", std::string()) == "responsibleStage" &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasStage", std::string()) == "finishTone" &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasAppliedCount", 0) > 0;
    bool continuationBiasBoostsFinishToneCandidates = false;
    const nlohmann::json continuationBiasCandidates =
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (continuationBiasCandidates.is_array()) {
        for (const nlohmann::json& candidate : continuationBiasCandidates) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            const bool isFinishToneCandidate =
                id == "strongerContrast" ||
                id == "toneSofterRolloff" ||
                id == "brightHighlightRolloff" ||
                id == "luminousHighlightAnchor" ||
                id == "naturalContrastGuard" ||
                id == "tonePunchierShape" ||
                id == "toneFlatterEditing" ||
                id == "toneDarkerToe" ||
                id == "renderedLocalContrastShape";
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json continuationBias =
                scoreComponents.value("renderedContinuationBias", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            continuationBiasBoostsFinishToneCandidates =
                continuationBiasBoostsFinishToneCandidates ||
                (isFinishToneCandidate &&
                 candidate.value("continuationBiasVersion", std::string()) == "ContinuationCandidateBiasV1" &&
                 candidate.value("continuationBiasStage", std::string()) == "finishTone" &&
                 candidate.value("continuationBiasBonus", 0.0f) > 0.0f &&
                 continuationBias.value("active", false) &&
                 continuationBias.value("bonus", 0.0f) > 0.0f &&
                 dimensions.value("renderedContinuationFit", 0.0f) > 0.50f);
        }
    }

    EditorNodeGraph::RawDevelopPayload continuationExpansionPayload;
    continuationExpansionPayload.scenePrepEnabled = true;
    continuationExpansionPayload.integratedToneEnabled = true;
    continuationExpansionPayload.uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
    continuationExpansionPayload.integratedToneLayerJson = ToneCurveLayer().Serialize();
    continuationExpansionPayload.integratedToneLayerJson["autoSceneStatsValid"] = true;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.16f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.34f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.62f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.0f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.14f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.05f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.30f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.40f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    continuationExpansionPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.0f;
    continuationExpansionPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.02f;
    continuationExpansionPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.02f;
    continuationExpansionPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.02f;
    continuationExpansionPayload.integratedToneLayerJson["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "continue" },
        { "reason", "adopted" },
        { "nextStep", "renderUpdatedSolve" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "shouldContinue", true },
        { "pass", 1 },
        { "remainingPasses", 2 },
        { "stageFocus", "finishTone" }
    };
    continuationExpansionPayload.integratedToneLayerJson["autoCandidateRenderedRevisionStage"] = "finishTone";
    EditorModule::ApplyDevelopAutoSolve(continuationExpansionPayload, metadata, true);
    const bool continuationExpansionDiagnosticsWritten =
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionVersion",
            std::string()) == "ContinuationCandidateExpansionV1" &&
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionEligible",
            false) &&
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionActive",
            false) &&
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionStage",
            std::string()) == "finishTone" &&
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionAddedCount",
            0) > 0;
    bool continuationExpansionAddsFinishToneFamily = false;
    const nlohmann::json continuationExpansionCandidates =
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (continuationExpansionCandidates.is_array()) {
        for (const nlohmann::json& candidate : continuationExpansionCandidates) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            const bool isExpandedFinishToneCandidate =
                id == "toneSofterRolloff" ||
                id == "brightHighlightRolloff" ||
                id == "luminousHighlightAnchor" ||
                id == "naturalContrastGuard" ||
                id == "tonePunchierShape" ||
                id == "toneFlatterEditing" ||
                id == "toneDarkerToe";
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json expansion =
                scoreComponents.value("renderedContinuationExpansion", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            continuationExpansionAddsFinishToneFamily =
                continuationExpansionAddsFinishToneFamily ||
                (isExpandedFinishToneCandidate &&
                 candidate.value("continuationExpansionVersion", std::string()) == "ContinuationCandidateExpansionV1" &&
                 candidate.value("continuationExpansionStage", std::string()) == "finishTone" &&
                 expansion.value("active", false) &&
                 expansion.value("stageFocus", std::string()) == "finishTone" &&
                 dimensions.value("renderedContinuationCoverage", 0.0f) >= 1.0f);
        }
    }

    EditorNodeGraph::RawDevelopPayload renderedMergePayload = payload;
    if (!renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.76f;
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.70f }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.76f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedMergePayload, metadata, true);
    const bool renderedFeedbackMergedCandidate =
        !renderedFeedbackCandidateId.empty() &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsMerge" &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedFeedbackMerge" &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateMergeApplied", false) &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstId", std::string()) == selectedCandidateId &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondId", std::string()) == renderedFeedbackCandidateId &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "merged" &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackMerged", false) &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedMergePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";

    EditorNodeGraph::RawDevelopPayload renderedPairMergePayload = payload;
    if (!renderedFeedbackCandidateId.empty() &&
        !renderedFeedbackSecondCandidateId.empty() &&
        renderedFeedbackFingerprint != 0) {
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.78f;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeSuggested"] = true;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeFirstId"] = renderedFeedbackCandidateId;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeFirstLabel"] = renderedFeedbackCandidateLabel;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeFirstScore"] = 0.78f;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeSecondId"] = renderedFeedbackSecondCandidateId;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeSecondLabel"] = renderedFeedbackSecondCandidateLabel;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeSecondScore"] = 0.72f;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeMetricDistance"] = 0.22f;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.56f }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.78f }
            },
            {
                { "id", renderedFeedbackSecondCandidateId },
                { "label", renderedFeedbackSecondCandidateLabel },
                { "success", true },
                { "renderScore", 0.72f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedPairMergePayload, metadata, true);
    const bool renderedFeedbackPairMergedCandidate =
        !renderedFeedbackCandidateId.empty() &&
        !renderedFeedbackSecondCandidateId.empty() &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsPairMerge" &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedFeedbackPairMerge" &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeApplied", false) &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstId", std::string()) == renderedFeedbackCandidateId &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondId", std::string()) == renderedFeedbackSecondCandidateId &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "merged" &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackMerged", false) &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedPairMergePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";

    EditorNodeGraph::RawDevelopPayload renderedEnsembleMergePayload = payload;
    if (!renderedFeedbackCandidateId.empty() &&
        !renderedFeedbackSecondCandidateId.empty() &&
        !renderedFeedbackThirdCandidateId.empty() &&
        renderedFeedbackFingerprint != 0) {
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.80f;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeSuggested"] = true;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeIds"] =
            nlohmann::json::array({
                renderedFeedbackCandidateId,
                renderedFeedbackSecondCandidateId,
                renderedFeedbackThirdCandidateId
            });
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeLabels"] =
            nlohmann::json::array({
                renderedFeedbackCandidateLabel,
                renderedFeedbackSecondCandidateLabel,
                renderedFeedbackThirdCandidateLabel
            });
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeScores"] =
            nlohmann::json::array({ 0.80f, 0.74f, 0.68f });
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeMetricSpread"] = 0.18f;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeScoreSpread"] = 0.12f;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.58f }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.80f }
            },
            {
                { "id", renderedFeedbackSecondCandidateId },
                { "label", renderedFeedbackSecondCandidateLabel },
                { "success", true },
                { "renderScore", 0.74f }
            },
            {
                { "id", renderedFeedbackThirdCandidateId },
                { "label", renderedFeedbackThirdCandidateLabel },
                { "success", true },
                { "renderScore", 0.68f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedEnsembleMergePayload, metadata, true);
    const float renderedEnsembleFirstWeight =
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstWeight", 0.0f);
    const float renderedEnsembleSecondWeight =
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondWeight", 0.0f);
    const float renderedEnsembleThirdWeight =
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeThirdWeight", 0.0f);
    const bool renderedFeedbackEnsembleMergedCandidate =
        !renderedFeedbackCandidateId.empty() &&
        !renderedFeedbackSecondCandidateId.empty() &&
        !renderedFeedbackThirdCandidateId.empty() &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsEnsembleMerge" &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedFeedbackEnsembleMerge" &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeApplied", false) &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstId", std::string()) == renderedFeedbackCandidateId &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondId", std::string()) == renderedFeedbackSecondCandidateId &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeThirdId", std::string()) == renderedFeedbackThirdCandidateId &&
        renderedEnsembleFirstWeight > 0.20f &&
        renderedEnsembleSecondWeight > 0.20f &&
        renderedEnsembleThirdWeight > 0.20f &&
        std::fabs((renderedEnsembleFirstWeight + renderedEnsembleSecondWeight + renderedEnsembleThirdWeight) - 1.0f) < 0.01f &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "merged" &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackMerged", false) &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";

    EditorNodeGraph::RawDevelopPayload renderedMergeContinuationPayload = renderedMergePayload;
    EditorModule::ApplyDevelopAutoSolve(renderedMergeContinuationPayload, metadata, true);
    bool renderedFeedbackCandidateCarriedForward = false;
    const nlohmann::json renderedMergeContinuationCandidates =
        renderedMergeContinuationPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (renderedMergeContinuationCandidates.is_array()) {
        for (const nlohmann::json& candidate : renderedMergeContinuationCandidates) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) == "renderedFeedbackMerge" &&
                candidate.value("reason", std::string()).find("Preserved the prior authored candidate") != std::string::npos) {
                renderedFeedbackCandidateCarriedForward = true;
                break;
            }
        }
    }

    EditorNodeGraph::RawDevelopPayload renderedSurvivorCarryPayload = payload;
    const std::string priorRenderedSurvivorId = "previousRenderedSurvivorProbe";
    nlohmann::json priorCandidateSolves =
        renderedSurvivorCarryPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (!priorCandidateSolves.is_array()) {
        priorCandidateSolves = nlohmann::json::array();
    }
    priorCandidateSolves.push_back({
        { "id", priorRenderedSurvivorId },
        { "label", "Previous Rendered Survivor Probe" },
        { "reason", "Synthetic prior rendered survivor for carry-forward validation." },
        { "score", 0.63f },
        { "status", "survivor" },
        { "guidance", {
            { "autoStrength", payload.autoGuidance.autoStrength },
            { "brightnessIntent", -0.66f },
            { "dynamicRange", 2.72f },
            { "shadowLift", 1.05f },
            { "highlightGuard", 1.12f },
            { "highlightCharacter", -0.52f },
            { "contrastBias", -0.48f }
        } }
    });
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateSolves"] = std::move(priorCandidateSolves);
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 1;
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackApplied"] = true;
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "pending";
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
        {
            { "id", priorRenderedSurvivorId },
            { "label", "Previous Rendered Survivor Probe" },
            { "success", true },
            { "renderScore", 0.74f },
            { "renderedStatus", "survivor" }
        }
    });
    EditorModule::ApplyDevelopAutoSolve(renderedSurvivorCarryPayload, metadata, true);
    bool renderedSurvivorCandidateCarriedForward = false;
    const nlohmann::json renderedSurvivorCarryCandidates =
        renderedSurvivorCarryPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (renderedSurvivorCarryCandidates.is_array()) {
        for (const nlohmann::json& candidate : renderedSurvivorCarryCandidates) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) == priorRenderedSurvivorId &&
                candidate.value("reason", std::string()).find("actual rendered survivor") != std::string::npos &&
                candidate.value("status", std::string()) != "rejectedDuplicate" &&
                candidate.value("status", std::string()) != "rejectedDamage") {
                renderedSurvivorCandidateCarriedForward = true;
                break;
            }
        }
    }
    const bool renderedSurvivorCarryForwardCountWritten =
        renderedSurvivorCarryPayload.integratedToneLayerJson.value("autoCandidateRenderedCarriedForwardCount", 0) >= 1;

    EditorNodeGraph::RawDevelopPayload renderedRefinePayload = payload;
    if (!selectedCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = selectedCandidateId;
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId);
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.53f;
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineIntent"] = "brightenMids";
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineReason"] =
            "Rendered metrics showed the selected result landing too dark without highlight pressure, so the solver should try brighter mids.";
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.53f },
                { "metrics", {
                    { "meanLuma", 0.14f },
                    { "medianLuma", 0.13f },
                    { "p10Luma", 0.03f },
                    { "p90Luma", 0.46f },
                    { "shadowFraction", 0.58f },
                    { "highlightFraction", 0.04f },
                    { "clippedFraction", 0.0f },
                    { "contrastSpan", 0.43f }
                } }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedRefinePayload, metadata, true);
    bool renderedLocalRefineCandidateGenerated = false;
    const nlohmann::json renderedRefineCandidateSolves =
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (renderedRefineCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : renderedRefineCandidateSolves) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) == "renderedLocalBrightenMids" &&
                candidate.value("reason", std::string()).find("Generated a rendered-local candidate family") != std::string::npos) {
                renderedLocalRefineCandidateGenerated = true;
                break;
            }
        }
    }
    const bool renderedFeedbackRefinedCandidate =
        !selectedCandidateId.empty() &&
        renderedLocalRefineCandidateGenerated &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsRefine" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedLocalBrightenMids" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "refined" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string()) == "brightenMids" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedRefinePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";
    const bool renderedRefineRevisionStageIsScenePrep =
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) == "scenePrep";
    const bool renderedRefineStagePlanTargetsScenePrep =
        renderedRefinePayload.integratedToneLayerJson.value("autoStageSolveVersion", std::string()) == "StagedAutoSolveV1" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoStageResponsibleRevisionState", std::string()) == "SOLVE_SCENE_PREP" &&
        !renderedRefinePayload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string()).empty() &&
        renderedRefinePayload.integratedToneLayerJson.value("autoStageRevisionStage", std::string()) == "scenePrep";

    EditorNodeGraph::RawDevelopPayload renderedCleanupRefinePayload = payload;
    if (!selectedCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = selectedCandidateId;
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId);
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.55f;
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineIntent"] = "cleanShadows";
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineReason"] =
            "Rendered metrics showed shadow texture/noise pressure without matching highlight trouble, so the solver should try a cleaner-shadow refinement instead of simply lifting more.";
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.55f },
                { "metrics", renderedMetricsJson(renderedCleanShadowMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedCleanupRefinePayload, metadata, true);
    bool renderedCleanupRefineCandidateGenerated = false;
    const nlohmann::json renderedCleanupRefineCandidateSolves =
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (renderedCleanupRefineCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : renderedCleanupRefineCandidateSolves) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) == "renderedLocalCleanShadows" &&
                candidate.value("reason", std::string()).find("Generated a rendered-local candidate family") != std::string::npos) {
                renderedCleanupRefineCandidateGenerated = true;
                break;
            }
        }
    }
    const bool renderedCleanupRefinedCandidate =
        !selectedCandidateId.empty() &&
        renderedCleanupRefineCandidateGenerated &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsRefine" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedLocalCleanShadows" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "refined" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string()) == "cleanShadows" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";
    const bool renderedCleanupRevisionStageIsRawCleanup =
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) == "rawCleanup";
    const bool renderedCleanupStagePlanTargetsRawBase =
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageSolveVersion", std::string()) == "StagedAutoSolveV1" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string()) == "RENDER_RAW_BASE" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageRevisionStage", std::string()) == "rawCleanup";

    EditorNodeGraph::RawDevelopPayload renderedRepeatedRefinePayload = renderedRefinePayload;
    const std::string renderedRefineSelectedId =
        renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    const std::uint64_t renderedRefineFingerprint =
        renderedRepeatedRefinePayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!renderedRefineSelectedId.empty() && renderedRefineFingerprint != 0) {
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedRefineFingerprint;
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedRefineSelectedId;
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedRefineSelectedId);
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.535f;
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineIntent"] = "brightenMids";
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineReason"] =
            "Rendered metrics still looked dark, but the previous refinement did not improve enough.";
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", "mergedAutoPick" },
                { "label", "Merged Auto Pick" },
                { "success", true },
                { "renderScore", 0.535f }
            },
            {
                { "id", renderedRefineSelectedId },
                { "label", renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedRefineSelectedId) },
                { "success", true },
                { "renderScore", 0.535f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedRepeatedRefinePayload, metadata, true);
    const bool renderedRepeatedRefineStops =
        !renderedRefineSelectedId.empty() &&
        !renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) != "renderedMetricsRefine";
    EditorNodeGraph::RawDevelopPayload renderedMonotonicShadowPayload = renderedCleanupRefinePayload;
    EditorRenderWorker::DevelopCandidateRenderMetrics previousShadowRefineMetrics = renderedCleanShadowMetrics;
    previousShadowRefineMetrics.shadowTextureRisk = 0.54f;
    previousShadowRefineMetrics.localShadowPressure = 0.46f;
    previousShadowRefineMetrics.localDamageRiskPeak = 0.38f;
    previousShadowRefineMetrics.localDamageRiskMean = 0.12f;
    EditorRenderWorker::DevelopCandidateRenderMetrics currentShadowRefineMetrics = previousShadowRefineMetrics;
    currentShadowRefineMetrics.shadowTextureRisk = 0.66f;
    currentShadowRefineMetrics.localShadowPressure = 0.60f;
    currentShadowRefineMetrics.localDamageRiskPeak = 0.58f;
    currentShadowRefineMetrics.localDamageRiskMean = 0.18f;
    const std::string previousMonotonicShadowSelectedId =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    const std::uint64_t previousMonotonicShadowFingerprint =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 1;
    renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackHistory"] = nlohmann::json::array({
        {
            { "fingerprint", static_cast<std::uint64_t>(42) },
            { "selectedId", previousMonotonicShadowSelectedId },
            { "selectedRenderScore", 0.555f },
            { "selectedRenderScoreValid", true },
            { "bestId", previousMonotonicShadowSelectedId },
            { "bestRenderScore", 0.555f },
            { "successCount", 1 },
            { "failureCount", 0 },
            { "action", "solveRequested" },
            { "stopReason", "renderedSelectedNeedsRefinement" },
            { "refineIntent", "cleanShadows" },
            { "refineReason", "Previous rendered pass requested cleaner shadows." },
            { "selectedMetrics", renderedMetricsJson(previousShadowRefineMetrics) },
            { "bestMetrics", renderedMetricsJson(previousShadowRefineMetrics) }
        }
    });
    renderedMonotonicShadowPayload.integratedToneLayerJson.erase("autoCandidateRenderedVersion");
    renderedMonotonicShadowPayload.integratedToneLayerJson.erase("autoCandidateRenderedFingerprint");
    renderedMonotonicShadowPayload.integratedToneLayerJson.erase("autoCandidateRenderMetricsStatus");
    renderedMonotonicShadowPayload.integratedToneLayerJson.erase("autoCandidateRenderedSolves");
    EditorModule::ApplyDevelopAutoSolve(renderedMonotonicShadowPayload, metadata, true);
    const std::string renderedMonotonicShadowSelectedId =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    const std::uint64_t renderedMonotonicShadowFingerprint =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!renderedMonotonicShadowSelectedId.empty() && renderedMonotonicShadowFingerprint != 0) {
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] =
            renderedMonotonicShadowFingerprint;
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] =
            renderedMonotonicShadowSelectedId;
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedMonotonicShadowSelectedId);
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.572f;
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedRefineIntent"] = "cleanShadows";
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedRefineReason"] =
            "Rendered metrics still showed shadow texture pressure, but the same cleanup direction is making the protected risk worse.";
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", renderedMonotonicShadowSelectedId },
                { "label", renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedMonotonicShadowSelectedId) },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            },
            {
                { "id", "renderedLocalCleanShadows" },
                { "label", "Rendered Local Cleaner Shadows" },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            },
            {
                { "id", "cleanShadows" },
                { "label", "Clean Shadows" },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            },
            {
                { "id", "mergedAutoPick" },
                { "label", "Merged Auto Pick" },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            },
            {
                { "id", "base" },
                { "label", "Base" },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedMonotonicShadowPayload, metadata, true);
    const nlohmann::json renderedMonotonicShadowLoop =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedMonotonicShadowRiskStops =
        !renderedMonotonicShadowSelectedId.empty() &&
        !renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "renderedRefineMonotonicShadowRisk" &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", false) &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string()) == "stopped" &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicMetric", std::string()) == "shadowTextureRisk" &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicCurrentValue", 0.0f) >
            renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicPreviousValue", 1.0f) &&
        renderedMonotonicShadowLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedMonotonicShadowLoop.value("state", std::string()) == "converged" &&
        renderedMonotonicShadowLoop.value("stopReason", std::string()) == "renderedRefineMonotonicShadowRisk" &&
        renderedMonotonicShadowLoop.value("monotonicGuardStatus", std::string()) == "stopped";
    EditorNodeGraph::RawDevelopPayload renderedNoImprovementPayload = renderedFeedbackPayload;
    const std::string renderedAdoptedSelectedId =
        renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    const std::uint64_t renderedAdoptedFingerprint =
        renderedNoImprovementPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!renderedAdoptedSelectedId.empty() && renderedAdoptedFingerprint != 0) {
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedAdoptedFingerprint;
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedAdoptedSelectedId;
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedAdoptedSelectedId);
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.82f;
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", renderedAdoptedSelectedId },
                { "label", renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedAdoptedSelectedId) },
                { "success", true },
                { "renderScore", 0.82f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedNoImprovementPayload, metadata, true);
    const bool renderedNoImprovementStops =
        !renderedAdoptedSelectedId.empty() &&
        !renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) != "renderedMetrics";
    EditorNodeGraph::RawDevelopPayload renderedStablePayload = payload;
    if (!selectedCandidateId.empty() && !renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.815f;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 1;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedFeedbackHistory"] = nlohmann::json::array({
            {
                { "fingerprint", static_cast<std::uint64_t>(renderedFeedbackFingerprint - 1) },
                { "selectedId", selectedCandidateId },
                { "selectedRenderScore", 0.80f },
                { "selectedRenderScoreValid", true },
                { "bestId", renderedFeedbackCandidateId },
                { "bestRenderScore", 0.812f },
                { "successCount", 2 },
                { "failureCount", 0 },
                { "action", "solveRequested" },
                { "stopReason", "renderedBestImproved" },
                { "bestMetrics", renderedMetricsJson(visualRiskMetrics) }
            }
        });
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.80f },
                { "metrics", renderedMetricsJson(similarRenderedMetrics) }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.815f },
                { "metrics", renderedMetricsJson(visualRiskMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedStablePayload, metadata, true);
    const bool renderedStableMetricsConverge =
        !selectedCandidateId.empty() &&
        !renderedFeedbackCandidateId.empty() &&
        !renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "renderedMetricsStable" &&
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", false) &&
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedStabilityStatus", std::string()) == "stable" &&
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedStabilityDistance", 1.0f) < 0.045f;
    const nlohmann::json renderedStableLoop =
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedStableLoopConverged =
        renderedStableLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedStableLoop.value("state", std::string()) == "converged" &&
        renderedStableLoop.value("action", std::string()) == "stopped" &&
        renderedStableLoop.value("stopReason", std::string()) == "renderedMetricsStable" &&
        renderedStableLoop.value("nextStep", std::string()) == "none" &&
        !renderedStableLoop.value("requiresRenderedMetrics", true) &&
        !renderedStableLoop.value("requiresAutoSolve", true) &&
        renderedStableLoop.value("pass", -1) == 1 &&
        renderedStableLoop.value("maxPasses", 0) == 3;
    const nlohmann::json renderedStableContinuation =
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyStopsStable =
        renderedStableContinuation.value("version", std::string()) == "RenderedContinuationV1" &&
        renderedStableContinuation.value("decision", std::string()) == "stop" &&
        renderedStableContinuation.value("reason", std::string()) == "renderedMetricsStable" &&
        renderedStableContinuation.value("nextStep", std::string()) == "none" &&
        !renderedStableContinuation.value("requiresRenderedMetrics", true) &&
        !renderedStableContinuation.value("requiresAutoSolve", true) &&
        !renderedStableContinuation.value("shouldContinue", true) &&
        renderedStableContinuation.value("pass", -1) == 1 &&
        renderedStableContinuation.value("remainingPasses", -1) == 2;
    const nlohmann::json renderedStableConvergenceEvidence =
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceStopsStable =
        renderedStableConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedStableConvergenceEvidence.value("state", std::string()) == "converged" &&
        renderedStableConvergenceEvidence.value("decision", std::string()) == "stop" &&
        renderedStableConvergenceEvidence.value("reason", std::string()) == "renderedMetricsStable" &&
        !renderedStableConvergenceEvidence.value("shouldContinue", true) &&
        !renderedStableConvergenceEvidence.value("requiresRenderedMetrics", true) &&
        renderedStableConvergenceEvidence.value("rendered", nlohmann::json::object()).value("stopConverged", false) &&
        renderedStableConvergenceEvidence.value("rendered", nlohmann::json::object()).value("stabilityStatus", std::string()) == "stable" &&
        renderedStableConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "stop";
    EditorNodeGraph::RawDevelopPayload renderedTrendPayload = payload;
    EditorRenderWorker::DevelopCandidateRenderMetrics trendPreviousBestMetrics = visualRiskMetrics;
    // Keep this close enough to prove a stalled trend, but far enough from
    // the current result that the earlier "stable metrics" stop does not win.
    trendPreviousBestMetrics.meanLuma += 0.060f;
    trendPreviousBestMetrics.medianLuma += 0.030f;
    if (!selectedCandidateId.empty() && !renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.795f;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 2;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackHistory"] = nlohmann::json::array({
            {
                { "fingerprint", static_cast<std::uint64_t>(renderedFeedbackFingerprint - 2) },
                { "selectedId", selectedCandidateId },
                { "selectedRenderScore", 0.748f },
                { "selectedRenderScoreValid", true },
                { "bestId", renderedFeedbackCandidateId },
                { "bestRenderScore", 0.786f },
                { "successCount", 2 },
                { "failureCount", 0 },
                { "action", "merged" },
                { "stopReason", "" },
                { "bestMetrics", renderedMetricsJson(visualRiskMetrics) }
            },
            {
                { "fingerprint", static_cast<std::uint64_t>(renderedFeedbackFingerprint - 1) },
                { "selectedId", selectedCandidateId },
                { "selectedRenderScore", 0.752f },
                { "selectedRenderScoreValid", true },
                { "bestId", renderedFeedbackCandidateId },
                { "bestRenderScore", 0.792f },
                { "successCount", 2 },
                { "failureCount", 0 },
                { "action", "solveRequested" },
                { "stopReason", "renderedBestImproved" },
                { "bestMetrics", renderedMetricsJson(trendPreviousBestMetrics) }
            }
        });
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.750f },
                { "metrics", renderedMetricsJson(similarRenderedMetrics) }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.795f },
                { "metrics", renderedMetricsJson(visualRiskMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedTrendPayload, metadata, true);
    const bool renderedTrendConverges =
        !selectedCandidateId.empty() &&
        !renderedFeedbackCandidateId.empty() &&
        !renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "renderedFeedbackNoImprovementTrend" &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", false) &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendStatus", std::string()) == "stalled" &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendHistoryCount", 0) >= 2 &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendSameBestCount", 0) >= 2 &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendScoreSpread", 1.0f) < 0.030f &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendNearestDistance", 1.0f) < 0.055f;
    const nlohmann::json renderedTrendLoop =
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedTrendLoopConverged =
        renderedTrendLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedTrendLoop.value("state", std::string()) == "converged" &&
        renderedTrendLoop.value("action", std::string()) == "stopped" &&
        renderedTrendLoop.value("stopReason", std::string()) == "renderedFeedbackNoImprovementTrend" &&
        renderedTrendLoop.value("nextStep", std::string()) == "none" &&
        !renderedTrendLoop.value("requiresRenderedMetrics", true) &&
        !renderedTrendLoop.value("requiresAutoSolve", true) &&
        renderedTrendLoop.value("pass", -1) == 2 &&
        renderedTrendLoop.value("historyCount", 0) >= 2 &&
        renderedTrendLoop.value("maxPasses", 0) == 3;
    const nlohmann::json renderedTrendContinuation =
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyStopsTrend =
        renderedTrendContinuation.value("version", std::string()) == "RenderedContinuationV1" &&
        renderedTrendContinuation.value("decision", std::string()) == "stop" &&
        renderedTrendContinuation.value("reason", std::string()) == "renderedFeedbackNoImprovementTrend" &&
        renderedTrendContinuation.value("nextStep", std::string()) == "none" &&
        renderedTrendContinuation.value("pass", -1) == 2 &&
        renderedTrendContinuation.value("remainingPasses", -1) == 1 &&
        renderedTrendContinuation.value("evidence", nlohmann::json::object()).value("trendStatus", std::string()) == "stalled";
    const nlohmann::json renderedTrendConvergenceEvidence =
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceStopsTrend =
        renderedTrendConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedTrendConvergenceEvidence.value("state", std::string()) == "converged" &&
        renderedTrendConvergenceEvidence.value("decision", std::string()) == "stop" &&
        renderedTrendConvergenceEvidence.value("reason", std::string()) == "renderedFeedbackNoImprovementTrend" &&
        !renderedTrendConvergenceEvidence.value("shouldContinue", true) &&
        renderedTrendConvergenceEvidence.value("pass", -1) == 2 &&
        renderedTrendConvergenceEvidence.value("rendered", nlohmann::json::object()).value("trendStatus", std::string()) == "stalled" &&
        renderedTrendConvergenceEvidence.value("rendered", nlohmann::json::object()).value("trendHistoryCount", 0) >= 2 &&
        renderedTrendConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "stop";
    EditorNodeGraph::RawDevelopPayload renderedPassLimitPayload = payload;
    if (!selectedCandidateId.empty() && !renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.910f;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 3;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.720f },
                { "metrics", renderedMetricsJson(similarRenderedMetrics) }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.910f },
                { "metrics", renderedMetricsJson(visualRiskMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedPassLimitPayload, metadata, true);
    const nlohmann::json renderedPassLimitLoop =
        renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const nlohmann::json renderedPassLimitContinuation =
        renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyStopsAtPassLimit =
        !selectedCandidateId.empty() &&
        !renderedFeedbackCandidateId.empty() &&
        !renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "renderedFeedbackPassLimit" &&
        !renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", true) &&
        renderedPassLimitLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedPassLimitLoop.value("state", std::string()) == "stopped" &&
        renderedPassLimitLoop.value("stopReason", std::string()) == "renderedFeedbackPassLimit" &&
        renderedPassLimitContinuation.value("version", std::string()) == "RenderedContinuationV1" &&
        renderedPassLimitContinuation.value("decision", std::string()) == "stop" &&
        renderedPassLimitContinuation.value("reason", std::string()) == "renderedFeedbackPassLimit" &&
        renderedPassLimitContinuation.value("pass", -1) == 3 &&
        renderedPassLimitContinuation.value("remainingPasses", -1) == 0 &&
        !renderedPassLimitContinuation.value("shouldContinue", true);
    const nlohmann::json renderedPassLimitConvergenceEvidence =
        renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceStopsAtPassLimit =
        renderedPassLimitConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedPassLimitConvergenceEvidence.value("state", std::string()) == "stopped" &&
        renderedPassLimitConvergenceEvidence.value("decision", std::string()) == "stop" &&
        renderedPassLimitConvergenceEvidence.value("reason", std::string()) == "renderedFeedbackPassLimit" &&
        !renderedPassLimitConvergenceEvidence.value("shouldContinue", true) &&
        renderedPassLimitConvergenceEvidence.value("pass", -1) == 3 &&
        renderedPassLimitConvergenceEvidence.value("remainingPasses", -1) == 0 &&
        !renderedPassLimitConvergenceEvidence.value("rendered", nlohmann::json::object()).value("stopConverged", true) &&
        renderedPassLimitConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "stop";
    const bool renderedFeedbackStopConvergenceClassifierWorks =
        EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("renderedMetricsStable") &&
        EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("noMeaningfulRenderedImprovement") &&
        EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("convergenceAdmissionNoMeaningfulImprovement") &&
        EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("renderedRefineMonotonicShadowRisk") &&
        !EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("noRenderedBestCandidate") &&
        !EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("allRenderedCandidatesRejectedForDamage") &&
        !EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("renderedBestBelowQualityFloor") &&
        !EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("renderedFeedbackPassLimit");
    EditorNodeGraph::RawDevelopPayload renderedNoBestPayload = payload;
    const std::uint64_t renderedNoBestFingerprint =
        renderedNoBestPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!selectedCandidateId.empty() && renderedNoBestFingerprint != 0) {
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedNoBestFingerprint;
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = "";
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = "";
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = -1.0f;
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] =
            nlohmann::json::array();
    }
    EditorModule::ApplyDevelopAutoSolve(renderedNoBestPayload, metadata, true);
    const nlohmann::json renderedNoBestLoop =
        renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedNoBestStopsWithoutConverging =
        !selectedCandidateId.empty() &&
        !renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "noRenderedBestCandidate" &&
        !renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", true) &&
        renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) == "none" &&
        renderedNoBestLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedNoBestLoop.value("state", std::string()) == "stopped" &&
        renderedNoBestLoop.value("stopReason", std::string()) == "noRenderedBestCandidate" &&
        renderedNoBestLoop.value("nextStep", std::string()) == "none" &&
        !renderedNoBestLoop.value("requiresAutoSolve", true) &&
        !renderedNoBestLoop.value("requiresRenderedMetrics", true);
    const nlohmann::json renderedNoBestConvergenceEvidence =
        renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceStopsNoBest =
        renderedNoBestConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedNoBestConvergenceEvidence.value("state", std::string()) == "stopped" &&
        renderedNoBestConvergenceEvidence.value("decision", std::string()) == "stop" &&
        renderedNoBestConvergenceEvidence.value("reason", std::string()) == "noRenderedBestCandidate" &&
        !renderedNoBestConvergenceEvidence.value("shouldContinue", true) &&
        !renderedNoBestConvergenceEvidence.value("rendered", nlohmann::json::object()).value("stopConverged", true) &&
        renderedNoBestConvergenceEvidence.value("rendered", nlohmann::json::object()).value("revisionStage", std::string()) == "none";
