// Included inside DevelopAutoSolveValidation.cpp::ValidateDevelopAutoSolveBehavior().
// Initial solve, candidate diagnostics, staged state, and rejection memory checks.

    const bool defaultIntentIsNatural =
        EditorNodeGraph::DevelopAutoGuidance{}.intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;

    EditorNodeGraph::RawDevelopPayload payload;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
    payload.integratedToneLayerJson = ToneCurveLayer().Serialize();
    payload.integratedToneLayerJson["autoSceneStatsValid"] = true;
    payload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.018f;
    payload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.072f;
    payload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.91f;
    payload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.68f;
    payload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.57f;
    payload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.71f;
    payload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 4.6f;
    payload.integratedToneLayerJson["autoRecommendedBaseEv"] = 1.35f;
    payload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.14f;
    payload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.28f;
    payload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.23f;

    payload.autoGuidance.autoStrength = 1.35f;
    payload.autoGuidance.exposureBias = 0.32f;
    payload.autoGuidance.dynamicRange = 1.85f;
    payload.autoGuidance.shadowLift = 0.70f;
    payload.autoGuidance.highlightGuard = 0.55f;
    payload.autoGuidance.highlightCharacter = -0.20f;
    payload.autoGuidance.contrastBias = 0.18f;

    const Raw::RawDevelopSettings settingsBefore = payload.settings;
    const Raw::RawDetailFusionSettings scenePrepBefore = payload.scenePrepSettings;
    const std::uint64_t requestIdBefore =
        payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));

    Raw::RawMetadata metadata;
    metadata.isDng = true;
    metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    metadata.whiteLevel = 16383.0f;
    metadata.blackLevel = 512.0f;
    metadata.hasDngBaselineExposure = true;
    metadata.dngBaselineExposure = -0.10f;
    metadata.hasDngForwardMatrix1 = true;
    metadata.cameraWhiteBalance = { 2.02f, 1.0f, 1.61f, 1.0f };
    metadata.daylightWhiteBalance = { 1.0f, 1.0f, 1.0f, 1.0f };

    EditorModule::ApplyDevelopAutoSolve(payload, metadata, true);

    const bool exposureAuthored = payload.settings.exposureStops > settingsBefore.exposureStops + 0.35f;
    const bool highlightAuthored =
        payload.settings.highlightMode != Raw::HighlightReconstructionMode::Off &&
        payload.settings.highlightStrength > settingsBefore.highlightStrength + 0.02f &&
        payload.settings.highlightThreshold < settingsBefore.highlightThreshold - 0.01f;
    const bool cleanupAuthored =
        payload.settings.mosaicDenoise.enabled &&
        payload.settings.mosaicDenoise.lumaStrength > settingsBefore.mosaicDenoise.lumaStrength &&
        payload.settings.falseColorSuppression > settingsBefore.falseColorSuppression;
    const bool scenePrepAuthored =
        payload.scenePrepSettings.maxEvBias > scenePrepBefore.maxEvBias + 0.20f &&
        payload.scenePrepSettings.highlightProtectionBias > scenePrepBefore.highlightProtectionBias + 0.10f &&
        payload.scenePrepSettings.strength > scenePrepBefore.strength;
    const bool finishQueued =
        payload.integratedToneLayerJson.value("autoCalibratePending", false) &&
        payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0)) > requestIdBefore;
    const bool requestedGuidanceForwarded =
        std::abs(payload.integratedToneLayerJson.value("autoRequestedSceneAssistStrength", -99.0f) - payload.autoGuidance.autoStrength) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedDynamicRange", -99.0f) - payload.autoGuidance.dynamicRange) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedHighlightBias", -99.0f) - payload.autoGuidance.highlightGuard) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedBrightnessIntent", -99.0f) - payload.autoGuidance.exposureBias) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedRawExposurePreferenceEv", -99.0f) - payload.autoGuidance.exposureBias * 2.0f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedSubjectSceneBias", -99.0f) - payload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedMoodReadabilityBias", -99.0f) - payload.autoGuidance.moodReadabilityBias) < 0.0001f;
    bool selectedCandidateGuidanceForwarded = false;
    bool selectedCandidateScoreComponentsWritten = false;
    const nlohmann::json& candidateSolves = payload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    const std::string selectedCandidateId = payload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    if (candidateSolves.is_array() && !selectedCandidateId.empty()) {
        for (const nlohmann::json& candidate : candidateSolves) {
            if (!candidate.is_object() || candidate.value("id", std::string()) != selectedCandidateId) {
                continue;
            }
            const nlohmann::json& guidance = candidate.value("guidance", nlohmann::json::object());
            selectedCandidateGuidanceForwarded =
                std::abs(payload.integratedToneLayerJson.value("autoSceneAssistStrength", -99.0f) - guidance.value("autoStrength", -98.0f)) < 0.0001f &&
                std::abs(payload.integratedToneLayerJson.value("autoBrightnessIntent", -99.0f) - guidance.value("brightnessIntent", -98.0f)) < 0.0001f &&
                std::abs(payload.integratedToneLayerJson.value("autoDynamicRange", -99.0f) - guidance.value("dynamicRange", -98.0f)) < 0.0001f &&
                std::abs(payload.integratedToneLayerJson.value("autoHighlightBias", -99.0f) - guidance.value("highlightGuard", -98.0f)) < 0.0001f &&
                std::abs(payload.integratedToneLayerJson.value("autoContrastBias", -99.0f) - guidance.value("contrastBias", -98.0f)) < 0.0001f;
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            const float uniqueness = dimensions.value("candidateUniqueness", -1.0f);
            selectedCandidateScoreComponentsWritten =
                scoreComponents.value("version", std::string()) == "ParameterScoreComponentsV1" &&
                scoreComponents.value("scoreSource", std::string()).find("parameter") != std::string::npos &&
                std::abs(scoreComponents.value("finalScore", -1.0f) - candidate.value("score", -2.0f)) < 0.0001f &&
                dimensions.contains("midtonePlacement") &&
                dimensions.contains("highlightIntegrity") &&
                dimensions.contains("shadowCleanliness") &&
                dimensions.contains("dynamicRangeFit") &&
                dimensions.contains("contrastShape") &&
                dimensions.contains("brightnessHierarchy") &&
                dimensions.contains("noiseTextureQuality") &&
                dimensions.contains("localArtifactSafety") &&
                dimensions.contains("modeIntentFit") &&
                uniqueness >= 0.0f &&
                uniqueness <= 1.0f &&
                risks.contains("highlightDamageRisk") &&
                risks.contains("shadowNoiseRisk") &&
                risks.contains("flatteningRisk") &&
                risks.contains("dataRiskPenalty");
            break;
        }
    }
    const bool candidateDiagnosticsWritten =
        payload.integratedToneLayerJson.value("autoCandidateSolveVersion", std::string()) == "ParameterCandidatesV1" &&
        payload.integratedToneLayerJson.value("autoCandidateScoreVersion", std::string()) == "ParameterScoreComponentsV1" &&
        candidateSolves.is_array() &&
        candidateSolves.size() >= 2 &&
        payload.integratedToneLayerJson.value("autoCandidateSelectionIsAuthoredState", false) &&
        payload.integratedToneLayerJson.value("autoCandidateSurvivorCount", 0) >= 1 &&
        payload.integratedToneLayerJson.value("autoCandidateSelectedScore", 0.0f) > 0.0f &&
        payload.integratedToneLayerJson.value("autoCandidateConvergencePass", 0) > 0 &&
        payload.integratedToneLayerJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0)) != 0;
    const nlohmann::json initialRenderedLoop =
        payload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedFeedbackLoopAwaitingMetrics =
        payload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoopVersion", std::string()) == "RenderedFeedbackLoopV1" &&
        initialRenderedLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        initialRenderedLoop.value("state", std::string()) == "awaitingRenderedMetrics" &&
        initialRenderedLoop.value("nextStep", std::string()) == "renderCandidates" &&
        initialRenderedLoop.value("requiresRenderedMetrics", false) &&
        !initialRenderedLoop.value("requiresAutoSolve", true) &&
        initialRenderedLoop.value("pass", -1) == 0 &&
        initialRenderedLoop.value("maxPasses", 0) == 3 &&
        initialRenderedLoop.value("solveFingerprint", static_cast<std::uint64_t>(0)) ==
            payload.integratedToneLayerJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(1));
    const nlohmann::json initialContinuationPolicy =
        payload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyAwaitingMetrics =
        payload.integratedToneLayerJson.value("autoCandidateRenderedContinuationVersion", std::string()) == "RenderedContinuationV1" &&
        initialContinuationPolicy.value("version", std::string()) == "RenderedContinuationV1" &&
        initialContinuationPolicy.value("decision", std::string()) == "waitForRenderedMetrics" &&
        initialContinuationPolicy.value("nextStep", std::string()) == "renderCandidates" &&
        initialContinuationPolicy.value("requiresRenderedMetrics", false) &&
        !initialContinuationPolicy.value("requiresAutoSolve", true) &&
        initialContinuationPolicy.value("bounded", false) &&
        initialContinuationPolicy.value("pass", -1) == 0 &&
        initialContinuationPolicy.value("maxPasses", 0) == 3;
    const nlohmann::json initialConvergenceEvidence =
        payload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceAwaitingMetrics =
        payload.integratedToneLayerJson.value("autoCandidateConvergenceEvidenceVersion", std::string()) == "ConvergenceEvidenceV1" &&
        initialConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        payload.integratedToneLayerJson.value("autoCandidateConvergenceState", std::string()) == "awaitingRenderedMetrics" &&
        payload.integratedToneLayerJson.value("autoCandidateConvergenceDecision", std::string()) == "waitForRenderedMetrics" &&
        payload.integratedToneLayerJson.value("autoCandidateConvergenceShouldContinue", false) &&
        initialConvergenceEvidence.value("shouldContinue", false) &&
        initialConvergenceEvidence.value("requiresRenderedMetrics", false) &&
        initialConvergenceEvidence.value("rendered", nlohmann::json::object()).value("metricsReadyForCurrentSolve", true) == false &&
        initialConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "waitForRenderedMetrics";
    std::uint64_t renderedMemoryCandidateFingerprint = 0;
    std::string renderedMemoryCandidateId;
    std::string renderedMemoryCandidateLabel;
    if (candidateSolves.is_array()) {
        for (const nlohmann::json& candidate : candidateSolves) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) != "base" &&
                candidate.value("status", std::string()) == "survivor") {
                renderedMemoryCandidateId = candidate.value("id", std::string());
                renderedMemoryCandidateLabel = candidate.value("label", renderedMemoryCandidateId);
                renderedMemoryCandidateFingerprint =
                    candidate.value("guidanceFingerprint", static_cast<std::uint64_t>(0));
                break;
            }
        }
    }
    EditorNodeGraph::RawDevelopPayload renderedRejectionMemoryPayload = payload;
    if (renderedMemoryCandidateFingerprint != 0) {
        renderedRejectionMemoryPayload.integratedToneLayerJson["autoCandidateRenderedRejectionMemory"] =
            nlohmann::json::array({
                {
                    { "id", renderedMemoryCandidateId },
                    { "label", renderedMemoryCandidateLabel },
                    { "guidanceFingerprint", renderedMemoryCandidateFingerprint },
                    { "status", "renderedRejectedDamage" },
                    { "reason", "Synthetic rendered memory rejected this exact authored survivor state." },
                    { "renderScore", 0.18f },
                    { "solveFingerprint", renderedRejectionMemoryPayload.integratedToneLayerJson.value(
                        "autoCandidateSolveFingerprint",
                        static_cast<std::uint64_t>(0)) }
                }
            });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedRejectionMemoryPayload, metadata, true);
    bool renderedRejectionMemorySuppressesRepeat = false;
    const nlohmann::json repeatedRenderedMemoryCandidates =
        renderedRejectionMemoryPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (repeatedRenderedMemoryCandidates.is_array()) {
        for (const nlohmann::json& candidate : repeatedRenderedMemoryCandidates) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != renderedMemoryCandidateId) {
                continue;
            }
            renderedRejectionMemorySuppressesRepeat =
                candidate.value("status", std::string()) == "rejectedMemory" &&
                candidate.value("renderedMemoryRejected", false) &&
                candidate.value("guidanceFingerprint", static_cast<std::uint64_t>(0)) ==
                    renderedMemoryCandidateFingerprint &&
                renderedRejectionMemoryPayload.integratedToneLayerJson.value(
                    "autoCandidateRenderedRejectedMemorySuppressionCount",
                    0) >= 1;
            break;
        }
    }
    const nlohmann::json stageSolves =
        payload.integratedToneLayerJson.value("autoStageSolveStages", nlohmann::json::array());
    const nlohmann::json stageFingerprints =
        payload.integratedToneLayerJson.value("autoStageFingerprints", nlohmann::json::object());
    const auto hasStageState = [&](const std::string& state) {
        return Stack::Validation::Detail::DevelopAutoSolveStageHasState(stageSolves, state);
    };
    const bool stagedAutoSolveDiagnosticsWritten =
        payload.integratedToneLayerJson.value("autoStageSolveVersion", std::string()) == "StagedAutoSolveV1" &&
        payload.integratedToneLayerJson.value("autoStageCacheSplitStatus", std::string()).find("logicalOnly") != std::string::npos &&
        payload.integratedToneLayerJson.value("autoStageCurrentRawExposureInsideRawBase", false) &&
        !payload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string()).empty() &&
        stageFingerprints.is_object() &&
        stageFingerprints.value("metadata", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("rawBase", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("rawGlobal", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("scenePrep", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("finishTone", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("finalValidation", static_cast<std::uint64_t>(0)) != 0 &&
        stageSolves.is_array() &&
        stageSolves.size() >= 13 &&
        hasStageState("RENDER_RAW_BASE") &&
        hasStageState("SOLVE_GLOBAL") &&
        hasStageState("RENDER_PREFINISH") &&
        hasStageState("SOLVE_FINISH_TONE") &&
        hasStageState("VALIDATE_FINAL");
    const nlohmann::json learningRecord =
        payload.integratedToneLayerJson.value("autoCandidateLearningRecord", nlohmann::json::object());
    const nlohmann::json learningEvents =
        learningRecord.value("events", nlohmann::json::array());
    bool learningHasSelectedOutcome = false;
    if (learningEvents.is_array()) {
        for (const nlohmann::json& event : learningEvents) {
            if (!event.is_object()) {
                continue;
            }
            if (event.value("type", std::string()) == "candidateSelected" &&
                event.value("candidateId", std::string()) == selectedCandidateId &&
                event.value("status", std::string()) == "selected" &&
                event.value("guidanceVector", nlohmann::json::object()).is_object()) {
                learningHasSelectedOutcome = true;
                break;
            }
        }
    }
    const nlohmann::json currentImageLearning =
        learningRecord.value("currentImageLearning", nlohmann::json::object());
    const nlohmann::json futureImageLearning =
        learningRecord.value("futureImageLearning", nlohmann::json::object());
    const nlohmann::json userChoiceLearning =
        learningRecord.value("userChoiceLearning", nlohmann::json::object());
    const bool candidateLearningRecordedNotApplied =
        payload.integratedToneLayerJson.value("autoCandidateLearningVersion", std::string()) == "CandidateOutcomeLearningV1" &&
        payload.integratedToneLayerJson.value("autoCandidateLearningStatus", std::string()) == "recordedNotApplied" &&
        payload.integratedToneLayerJson.value("autoCandidateLearningRecorded", false) &&
        !payload.integratedToneLayerJson.value("autoCandidateLearningApplied", true) &&
        !payload.integratedToneLayerJson.value("autoCandidateLearningAppliedToCurrentImage", true) &&
        !payload.integratedToneLayerJson.value("autoCandidateLearningAppliedToFutureImages", true) &&
        payload.integratedToneLayerJson.value("autoCandidateLearningEventCount", 0) >= 1 &&
        learningRecord.is_object() &&
        learningRecord.value("selectedId", std::string()) == selectedCandidateId &&
        learningRecord.value("applied", true) == false &&
        learningRecord.value("eventCount", 0) == payload.integratedToneLayerJson.value("autoCandidateLearningEventCount", -1) &&
        currentImageLearning.value("recorded", false) &&
        !currentImageLearning.value("applied", true) &&
        !futureImageLearning.value("applied", true) &&
        userChoiceLearning.value("status", std::string()) == "deferredUntilCandidateSelectionUi" &&
        learningHasSelectedOutcome;
    EditorNodeGraph::RawDevelopPayload rejectedMemorySeedPayload = payload;
    rejectedMemorySeedPayload.autoGuidance.autoStrength = 1.70f;
    rejectedMemorySeedPayload.autoGuidance.exposureBias = 0.55f;
    rejectedMemorySeedPayload.autoGuidance.dynamicRange = 2.25f;
    rejectedMemorySeedPayload.autoGuidance.shadowLift = 1.00f;
    rejectedMemorySeedPayload.autoGuidance.highlightGuard = 0.10f;
    rejectedMemorySeedPayload.autoGuidance.highlightCharacter = -0.25f;
    rejectedMemorySeedPayload.autoGuidance.contrastBias = 0.18f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.004f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.052f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.985f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.055f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.94f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.95f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.18f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 5.5f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 1.20f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.20f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.35f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.35f;
    EditorModule::ApplyDevelopAutoSolve(rejectedMemorySeedPayload, metadata, true);

    const nlohmann::json& candidateRejectedMemory =
        rejectedMemorySeedPayload.integratedToneLayerJson.value("autoCandidateRejectedMemory", nlohmann::json::array());
    const bool rejectedCandidateMemoryRecorded =
        candidateRejectedMemory.is_array() &&
        !candidateRejectedMemory.empty() &&
        rejectedMemorySeedPayload.integratedToneLayerJson.value("autoCandidateContextFingerprint", static_cast<std::uint64_t>(0)) != 0;
    EditorNodeGraph::RawDevelopPayload rejectedMemoryPayload = rejectedMemorySeedPayload;
    EditorModule::ApplyDevelopAutoSolve(rejectedMemoryPayload, metadata, true);
    const nlohmann::json& repeatedCandidateSolves =
        rejectedMemoryPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    bool repeatedCandidateRejectedFromMemory = false;
    if (repeatedCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : repeatedCandidateSolves) {
            if (candidate.is_object() &&
                candidate.value("status", std::string()) == "rejectedMemory") {
                repeatedCandidateRejectedFromMemory = true;
                break;
            }
        }
    }
    const bool rejectedCandidateMemorySuppressesRepeat =
        rejectedCandidateMemoryRecorded &&
        repeatedCandidateRejectedFromMemory &&
        rejectedMemoryPayload.integratedToneLayerJson.value("autoCandidateRejectedMemorySuppressionCount", 0) > 0 &&
        rejectedMemoryPayload.integratedToneLayerJson.value("autoCandidateRejectedCount", 0) >=
            payload.integratedToneLayerJson.value("autoCandidateRejectedCount", 0);
    const bool candidateSolveCanBiasAuthoredGuidance =
        std::abs(payload.integratedToneLayerJson.value("autoBrightnessIntent", -99.0f) - payload.autoGuidance.exposureBias) > 0.0001f ||
        std::abs(payload.integratedToneLayerJson.value("autoDynamicRange", -99.0f) - payload.autoGuidance.dynamicRange) > 0.0001f ||
        std::abs(payload.integratedToneLayerJson.value("autoContrastBias", -99.0f) - payload.autoGuidance.contrastBias) > 0.0001f;

