// Included inside DevelopAutoSolveValidation.cpp::ValidateDevelopAutoSolveBehavior().
// Final validation aggregation, diagnostic report, and return value.

    const bool success =
        defaultIntentIsNatural &&
        exposureAuthored &&
        highlightAuthored &&
        cleanupAuthored &&
        scenePrepAuthored &&
        finishQueued &&
        finishGuidanceForwarded &&
        rejectedCandidateMemoryRecorded &&
        rejectedCandidateMemorySuppressesRepeat &&
        renderedRejectionMemorySuppressesRepeat &&
        selectedCandidateScoreComponentsWritten &&
        stagedAutoSolveDiagnosticsWritten &&
        candidateLearningRecordedNotApplied &&
        renderedFeedbackLoopAwaitingMetrics &&
        renderedContinuationPolicyAwaitingMetrics &&
        convergenceEvidenceAwaitingMetrics &&
        brightnessExposureTelemetryForwarded &&
        exposureDiagnosticsForwarded &&
        renderedVisualRiskMetricsPopulated &&
        renderedHighlightGrayMetricsPopulated &&
        renderedMeaningfulHighlightMetricsPopulated &&
        renderedLocalMetricsPopulated &&
        renderedSubjectMetricsPopulated &&
        renderedMarkedSubjectMetricsPopulated &&
        renderedMarkedLowPriorityMetricsPopulated &&
        renderedSpatialRiskMetricsPopulated &&
        renderedColorCastMetricsPopulated &&
        renderedDuplicateMetricDistanceWorks &&
        renderedStageBoundaryClassifierWorks &&
        renderedStageAwareDuplicateClusteringWorks &&
        renderedDamageClassifierWorks &&
        renderedRelativeComparisonWorks &&
        modeNeighborCandidateGenerated &&
        modeNeighborCandidateEligible &&
        modeNeighborCandidateHumanReadable &&
        modeNeighborCandidateMeaningfullyDifferent &&
        renderedLocalRefineIntentWorks &&
        renderedRefineIntentRelevanceWorks &&
        renderedFeedbackAdoptedCandidate &&
        renderedFeedbackLoopActive &&
        renderedContinuationPolicyContinues &&
        convergenceEvidenceContinuesAfterFeedback &&
        convergenceEvidenceAdmissionTightensMarginalContinuation &&
        continuationBiasDiagnosticsWritten &&
        continuationBiasBoostsFinishToneCandidates &&
        continuationExpansionDiagnosticsWritten &&
        continuationExpansionAddsFinishToneFamily &&
        renderedFeedbackRevisionStageWritten &&
        renderedFeedbackFinishToneStageOverride &&
        renderedFeedbackMergedCandidate &&
        renderedFeedbackPairMergedCandidate &&
        renderedFeedbackEnsembleMergedCandidate &&
        renderedFeedbackCandidateCarriedForward &&
        renderedSurvivorCandidateCarriedForward &&
        renderedSurvivorCarryForwardCountWritten &&
        renderedFeedbackRefinedCandidate &&
        renderedRefineRevisionStageIsScenePrep &&
        renderedRefineStagePlanTargetsScenePrep &&
        renderedCleanupRefinedCandidate &&
        renderedCleanupRevisionStageIsRawCleanup &&
        renderedCleanupStagePlanTargetsRawBase &&
        renderedRepeatedRefineStops &&
        renderedMonotonicShadowRiskStops &&
        renderedNoImprovementStops &&
        renderedStableMetricsConverge &&
        renderedStableLoopConverged &&
        renderedContinuationPolicyStopsStable &&
        convergenceEvidenceStopsStable &&
        renderedTrendConverges &&
        renderedTrendLoopConverged &&
        renderedContinuationPolicyStopsTrend &&
        convergenceEvidenceStopsTrend &&
        renderedContinuationPolicyStopsAtPassLimit &&
        convergenceEvidenceStopsAtPassLimit &&
        renderedFeedbackStopConvergenceClassifierWorks &&
        renderedNoBestStopsWithoutConverging &&
        convergenceEvidenceStopsNoBest &&
        cleanupTextureCandidateRenderPayloadsDiverge &&
        highlightProtectedMidsGenerated &&
        highlightProtectedMidsEligible &&
        highlightProtectedMidsMeaningfullyDifferent &&
        highlightProtectedMidsRenderPayloadDiverges &&
        finishToneProbeGenerated &&
        finishToneProbeEligible &&
        finishToneProbeHumanReadable &&
        finishToneProbeMeaningfullyDifferent &&
        finishToneProbeRenderPayloadConstrained &&
        dynamicRangeStrategyDiagnosticsWritten &&
        dynamicRangeStrategyMapDiagnosticsWritten &&
        localExposureStrategyDiagnosticsWritten &&
        localExposureStrategyAuthoredScenePrep &&
        localExposureStrategyCandidatePayloadCarried &&
        broadHighlightGuardGenerated &&
        broadHighlightGuardEligible &&
        broadHighlightGuardHumanReadable &&
        broadHighlightGuardDiagnosticsWritten &&
        broadHighlightGuardRenderPayloadConstrained &&
        naturalContrastGuardGenerated &&
        naturalContrastGuardEligible &&
        naturalContrastGuardHumanReadable &&
        naturalContrastGuardDiagnosticsWritten &&
        naturalContrastGuardRenderPayloadConstrained &&
        brightHighlightRolloffGenerated &&
        brightHighlightRolloffEligible &&
        brightHighlightRolloffHumanReadable &&
        brightHighlightRolloffDiagnosticsWritten &&
        brightHighlightRolloffRenderPayloadConstrained &&
        luminousHighlightAnchorGenerated &&
        luminousHighlightAnchorEligible &&
        luminousHighlightAnchorHumanReadable &&
        luminousHighlightAnchorDiagnosticsWritten &&
        luminousHighlightAnchorRenderPayloadConstrained &&
        renderedHighlightGrayEvidenceWritten &&
        specularHighlightToleranceGenerated &&
        specularHighlightToleranceEligible &&
        specularHighlightToleranceHumanReadable &&
        specularHighlightToleranceDiagnosticsWritten &&
        specularHighlightToleranceRenderPayloadConstrained &&
        regionalEvidenceDiagnosticsWritten &&
        subjectSceneIntentDiagnosticsWritten &&
        subjectSceneIntentScoreComponentsWritten &&
        subjectSceneIntentBiasesScoring &&
        userSubjectSceneIntentDiagnosticsWritten &&
        userSubjectSceneIntentScoreComponentsWritten &&
        subjectImportanceGuidanceDiagnosticsWritten &&
        subjectBrushGuidanceDiagnosticsWritten &&
        subjectBrushDisabledIgnored &&
        subjectBrushReduceGuidanceDiagnosticsWritten &&
        subjectReadableMidsGenerated &&
        subjectReadableMidsEligible &&
        subjectReadableMidsHumanReadable &&
        subjectReadableMidsDiagnosticsWritten &&
        subjectReadableMidsRenderPayloadConstrained &&
        sceneMoodPreservationGenerated &&
        sceneMoodPreservationEligible &&
        sceneMoodPreservationHumanReadable &&
        sceneMoodPreservationDiagnosticsWritten &&
        sceneMoodPreservationRenderPayloadConstrained &&
        haloSafeLocalRangeGenerated &&
        haloSafeLocalRangeEligible &&
        haloSafeLocalRangeHumanReadable &&
        haloSafeLocalRangeDiagnosticsWritten &&
        haloSafeLocalRangeRenderPayloadConstrained &&
        localRangeGuardGenerated &&
        localRangeGuardEligible &&
        localRangeGuardDiagnosticsWritten &&
        localRangeGuardRenderPayloadConstrained &&
        shadowReadabilityLiftGenerated &&
        shadowReadabilityLiftEligible &&
        shadowReadabilityLiftHumanReadable &&
        shadowReadabilityLiftDiagnosticsWritten &&
        shadowReadabilityLiftRenderPayloadConstrained &&
        shadowNoiseFloorGenerated &&
        shadowNoiseFloorEligible &&
        shadowNoiseFloorDiagnosticsWritten &&
        shadowNoiseFloorRenderPayloadConstrained &&
        whiteBalanceProbeGenerated &&
        whiteBalanceProbeEligible &&
        whiteBalanceProbeHumanReadable &&
        whiteBalanceProbeDiagnosticsWritten &&
        whiteBalanceProbeRenderPayloadDiverges &&
        scenePrepCandidateStageConstrained &&
        finishToneCandidateStageConstrained &&
        renderedStageRelevanceWorks &&
        renderedRefineIntentRelevanceWorks &&
        renderedStageCacheValidationWorks &&
        renderedStageSchedulerClassificationWorks &&
        developCandidateRenderBudgetAllowsMultiNodeCoverage &&
        developCandidateMetricReadbackBudgetWorks &&
        rawDevelopStageCacheMemoryPolicyWorks &&
        developAdaptiveRenderBudgetWorks &&
        developCandidateFeedbackGateDropsStale &&
        developCandidateFeedbackGateDefersRecent &&
        developCandidateFeedbackGateAppliesAfterQuiet &&
        developCandidateRenderAdmissionDefersRecent &&
        developCandidateFeedbackQuietRemainingWorks &&
        developStaleSnapshotAbortWorks &&
        developCandidateProgressLabelWorks &&
        partialSolvePreservedRawAuthorship &&
        partialSolveUpdatedPrepAndFinish &&
        repeatedFullSolveStable &&
        positiveExposureBiasPreserved &&
        highlightProfilePrefersHeadroom &&
        tinySpecularsDoNotDragExposure &&
        broadHighlightsProtectedMoreThanSpeculars &&
        underBrightBroadHighlightsLifted &&
        darkSceneLifted &&
        rawBaselineCarriesDarkLift &&
        rawBaselineDominatesDarkLift &&
        noisyProfilePrefersProtection &&
        modeIntentAffectsSolve &&
        modeIntentForwarded;

    if (!success) {
        std::cerr
            << "Develop auto solve validation failed:"
            << " defaultIntentIsNatural=" << defaultIntentIsNatural
            << " exposureAuthored=" << exposureAuthored
            << " highlightAuthored=" << highlightAuthored
            << " cleanupAuthored=" << cleanupAuthored
            << " scenePrepAuthored=" << scenePrepAuthored
            << " finishQueued=" << finishQueued
            << " finishGuidanceForwarded=" << finishGuidanceForwarded
            << " requestedGuidanceForwarded=" << requestedGuidanceForwarded
            << " selectedCandidateGuidanceForwarded=" << selectedCandidateGuidanceForwarded
            << " candidateDiagnosticsWritten=" << candidateDiagnosticsWritten
            << " selectedCandidateScoreComponentsWritten=" << selectedCandidateScoreComponentsWritten
            << " renderedFeedbackLoopAwaitingMetrics=" << renderedFeedbackLoopAwaitingMetrics
            << " initialRenderedLoopState=" << initialRenderedLoop.value("state", std::string())
            << " initialRenderedLoopNextStep=" << initialRenderedLoop.value("nextStep", std::string())
            << " renderedContinuationPolicyAwaitingMetrics=" << renderedContinuationPolicyAwaitingMetrics
            << " initialContinuationDecision=" << initialContinuationPolicy.value("decision", std::string())
            << " initialContinuationNextStep=" << initialContinuationPolicy.value("nextStep", std::string())
            << " convergenceEvidenceAwaitingMetrics=" << convergenceEvidenceAwaitingMetrics
            << " initialConvergenceState=" << initialConvergenceEvidence.value("state", std::string())
            << " initialConvergenceDecision=" << initialConvergenceEvidence.value("decision", std::string())
            << " initialConvergenceReason=" << initialConvergenceEvidence.value("reason", std::string())
            << " stagedAutoSolveDiagnosticsWritten=" << stagedAutoSolveDiagnosticsWritten
            << " autoStageSolveVersion=" << payload.integratedToneLayerJson.value("autoStageSolveVersion", std::string())
            << " autoStageEarliestDirtyStage=" << payload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string())
            << " autoStageValidationState=" << payload.integratedToneLayerJson.value("autoStageValidationState", std::string())
            << " autoStageStageCount=" << (stageSolves.is_array() ? stageSolves.size() : 0)
            << " autoStageRawBaseFingerprint=" << stageFingerprints.value("rawBase", static_cast<std::uint64_t>(0))
            << " autoStageScenePrepFingerprint=" << stageFingerprints.value("scenePrep", static_cast<std::uint64_t>(0))
            << " autoStageFinishToneFingerprint=" << stageFingerprints.value("finishTone", static_cast<std::uint64_t>(0))
            << " autoCandidateScoreVersion=" << payload.integratedToneLayerJson.value("autoCandidateScoreVersion", std::string())
            << " candidateLearningRecordedNotApplied=" << candidateLearningRecordedNotApplied
            << " candidateLearningVersion=" << payload.integratedToneLayerJson.value("autoCandidateLearningVersion", std::string())
            << " candidateLearningStatus=" << payload.integratedToneLayerJson.value("autoCandidateLearningStatus", std::string())
            << " candidateLearningEventCount=" << payload.integratedToneLayerJson.value("autoCandidateLearningEventCount", -1)
            << " rejectedCandidateMemoryRecorded=" << rejectedCandidateMemoryRecorded
            << " rejectedCandidateMemorySuppressesRepeat=" << rejectedCandidateMemorySuppressesRepeat
            << " repeatedCandidateRejectedFromMemory=" << repeatedCandidateRejectedFromMemory
            << " renderedRejectionMemorySuppressesRepeat=" << renderedRejectionMemorySuppressesRepeat
            << " renderedMemoryCandidateId=" << renderedMemoryCandidateId
            << " renderedMemoryCandidateFingerprint=" << renderedMemoryCandidateFingerprint
            << " renderedRejectedMemorySuppressionCount=" << renderedRejectionMemoryPayload.integratedToneLayerJson.value("autoCandidateRenderedRejectedMemorySuppressionCount", -1)
            << " rejectedMemoryInitialSize=" << (candidateRejectedMemory.is_array() ? candidateRejectedMemory.size() : 0)
            << " rejectedMemorySuppressionCount=" << rejectedMemoryPayload.integratedToneLayerJson.value("autoCandidateRejectedMemorySuppressionCount", -1)
            << " candidateSolveCanBiasAuthoredGuidance=" << candidateSolveCanBiasAuthoredGuidance
            << " cleanShadowCandidateGenerated=" << cleanShadowCandidateGenerated
            << " preserveTextureCandidateGenerated=" << preserveTextureCandidateGenerated
            << " cleanupTextureCandidateRenderPayloadsDiverge=" << cleanupTextureCandidateRenderPayloadsDiverge
            << " highlightProtectedMidsGenerated=" << highlightProtectedMidsGenerated
            << " highlightProtectedMidsEligible=" << highlightProtectedMidsEligible
            << " highlightProtectedMidsMeaningfullyDifferent=" << highlightProtectedMidsMeaningfullyDifferent
            << " highlightProtectedMidsRenderPayloadDiverges=" << highlightProtectedMidsRenderPayloadDiverges
            << " protectedMidsExposure=" << protectedMidsPayload.settings.exposureStops
            << " protectedMidsMaxEvBias=" << protectedMidsPayload.scenePrepSettings.maxEvBias
            << " protectedMidsHighlightBias=" << protectedMidsPayload.scenePrepSettings.highlightProtectionBias
            << " protectedMidsDynamicRange=" << protectedMidsPayload.integratedToneLayerJson.value("autoDynamicRange", -99.0f)
            << " protectedMidsShadowBias=" << protectedMidsPayload.integratedToneLayerJson.value("autoShadowBias", -99.0f)
            << " protectedMidsToneHighlightBias=" << protectedMidsPayload.integratedToneLayerJson.value("autoHighlightBias", -99.0f)
            << " finishToneProbeGenerated=" << finishToneProbeGenerated
            << " finishToneProbeEligible=" << finishToneProbeEligible
            << " finishToneProbeHumanReadable=" << finishToneProbeHumanReadable
            << " finishToneProbeMeaningfullyDifferent=" << finishToneProbeMeaningfullyDifferent
            << " finishToneProbeRenderPayloadConstrained=" << finishToneProbeRenderPayloadConstrained
            << " finishToneProbeId=" << finishToneProbeRenderId
            << " finishToneProbeStageConstraint=" << finishToneProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " finishToneProbeDiagnosticId=" << finishToneProbePayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " finishToneProbeContrastBias=" << finishToneProbePayload.integratedToneLayerJson.value("autoContrastBias", -99.0f)
            << " finishToneProbeHighlightCharacter=" << finishToneProbePayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " dynamicRangeStrategyDiagnosticsWritten=" << dynamicRangeStrategyDiagnosticsWritten
            << " dynamicRangeStrategyMapDiagnosticsWritten=" << dynamicRangeStrategyMapDiagnosticsWritten
            << " dynamicRangeStrategyMapVersion=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVersion", std::string())
            << " dynamicRangeStrategyMapHighlightShadowAxis=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightShadowAxis", -99.0f)
            << " dynamicRangeStrategyMapContrastRangeAxis=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapContrastRangeAxis", -99.0f)
            << " dynamicRangeStrategyMapHighlightPriority=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightPriority", -1.0f)
            << " dynamicRangeStrategyMapShadowVisibility=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapShadowVisibility", -1.0f)
            << " dynamicRangeStrategyMapNaturalContrast=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapNaturalContrast", -1.0f)
            << " dynamicRangeStrategyMapVisibleRange=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVisibleRange", -1.0f)
            << " dynamicRangeStrategyMapScoreComponentsWritten=" << dynamicRangeStrategyMapScoreComponentsWritten
            << " localExposureStrategyDiagnosticsWritten=" << localExposureStrategyDiagnosticsWritten
            << " localExposureStrategyAuthoredScenePrep=" << localExposureStrategyAuthoredScenePrep
            << " localExposureStrategyCandidatePayloadCarried=" << localExposureStrategyCandidatePayloadCarried
            << " localExposureStrategyVersion=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyVersion", std::string())
            << " localExposureStrategyId=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyId", std::string())
            << " localExposureRangeRedistribution=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureRangeRedistribution", -1.0f)
            << " localExposureHighlightCompression=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCompression", -1.0f)
            << " localExposureShadowOpening=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowOpening", -1.0f)
            << " localExposureNoiseGuard=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureNoiseGuard", -1.0f)
            << " localExposureHaloGuard=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloGuard", -1.0f)
            << " localExposureHighlightCrowding=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCrowding", -1.0f)
            << " localExposureShadowCrowding=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowCrowding", -1.0f)
            << " localExposureHaloStress=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloStress", -1.0f)
            << " localExposureFlatnessRisk=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureFlatnessRisk", -1.0f)
            << " localExposureDamageRisk=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureDamageRisk", -1.0f)
            << " authoredLocalExposureStrategyId=" << payload.integratedToneLayerJson.value("autoAuthoredLocalExposureStrategyId", std::string())
            << " candidateLocalExposureStrategyId=" << localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureStrategyId", std::string())
            << " dynamicRangeStrategyId=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyId", std::string())
            << " dynamicRangeStrategyLabel=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyLabel", std::string())
            << " dynamicRangeHighlightImportance=" << payload.integratedToneLayerJson.value("autoDynamicRangeHighlightImportance", -1.0f)
            << " dynamicRangeShadowReadability=" << payload.integratedToneLayerJson.value("autoDynamicRangeShadowReadability", -1.0f)
            << " dynamicRangeBrightHighlightRolloffNeed=" << payload.integratedToneLayerJson.value("autoDynamicRangeBrightHighlightRolloffNeed", -1.0f)
            << " dynamicRangeHighlightBrightnessAnchorNeed=" << payload.integratedToneLayerJson.value("autoDynamicRangeHighlightBrightnessAnchorNeed", -1.0f)
            << " broadHighlightGuardGenerated=" << broadHighlightGuardGenerated
            << " broadHighlightGuardEligible=" << broadHighlightGuardEligible
            << " broadHighlightGuardHumanReadable=" << broadHighlightGuardHumanReadable
            << " broadHighlightGuardDiagnosticsWritten=" << broadHighlightGuardDiagnosticsWritten
            << " broadHighlightGuardRenderPayloadConstrained=" << broadHighlightGuardRenderPayloadConstrained
            << " broadHighlightGuardNeed=" << highlightHeavyPayload.integratedToneLayerJson.value("autoDynamicRangeBroadHighlightGuardNeed", -1.0f)
            << " broadHighlightGuardStageConstraint=" << broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " broadHighlightGuardScenePrepProbe=" << broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string())
            << " broadHighlightGuardMinEvBias=" << broadHighlightGuardPayload.scenePrepSettings.minEvBias
            << " broadHighlightGuardHighlightBias=" << broadHighlightGuardPayload.scenePrepSettings.highlightProtectionBias
            << " naturalContrastGuardGenerated=" << naturalContrastGuardGenerated
            << " naturalContrastGuardEligible=" << naturalContrastGuardEligible
            << " naturalContrastGuardHumanReadable=" << naturalContrastGuardHumanReadable
            << " naturalContrastGuardDiagnosticsWritten=" << naturalContrastGuardDiagnosticsWritten
            << " naturalContrastGuardRenderPayloadConstrained=" << naturalContrastGuardRenderPayloadConstrained
            << " naturalContrastGuardNeed=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeNaturalContrastGuardNeed", -1.0f)
            << " naturalContrastGuardStageConstraint=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " naturalContrastGuardDiagnosticId=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " naturalContrastGuardDynamicRange=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoDynamicRange", -99.0f)
            << " naturalContrastGuardContrastBias=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f)
            << " naturalContrastGuardHighlightCharacter=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " brightHighlightRolloffGenerated=" << brightHighlightRolloffGenerated
            << " brightHighlightRolloffEligible=" << brightHighlightRolloffEligible
            << " brightHighlightRolloffHumanReadable=" << brightHighlightRolloffHumanReadable
            << " brightHighlightRolloffDiagnosticsWritten=" << brightHighlightRolloffDiagnosticsWritten
            << " brightHighlightRolloffRenderPayloadConstrained=" << brightHighlightRolloffRenderPayloadConstrained
            << " brightHighlightRolloffStageConstraint=" << brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " brightHighlightRolloffDiagnosticId=" << brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " brightHighlightRolloffHighlightCharacter=" << brightHighlightRolloffPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " luminousHighlightAnchorGenerated=" << luminousHighlightAnchorGenerated
            << " luminousHighlightAnchorEligible=" << luminousHighlightAnchorEligible
            << " luminousHighlightAnchorHumanReadable=" << luminousHighlightAnchorHumanReadable
            << " luminousHighlightAnchorDiagnosticsWritten=" << luminousHighlightAnchorDiagnosticsWritten
            << " luminousHighlightAnchorRenderPayloadConstrained=" << luminousHighlightAnchorRenderPayloadConstrained
            << " luminousHighlightAnchorNeed=" << payload.integratedToneLayerJson.value("autoDynamicRangeHighlightBrightnessAnchorNeed", -1.0f)
            << " luminousHighlightAnchorStageConstraint=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " luminousHighlightAnchorDiagnosticId=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " luminousHighlightAnchorDynamicRange=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoDynamicRange", -99.0f)
            << " luminousHighlightAnchorContrastBias=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f)
            << " luminousHighlightAnchorHighlightCharacter=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " renderedHighlightGrayEvidenceWritten=" << renderedHighlightGrayEvidenceWritten
            << " flatGrayHighlightGrayRisk=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightGrayRisk", -1.0f)
            << " flatGrayMeaningfulHighlightPressure=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeMeaningfulHighlightPressure", -1.0f)
            << " flatGrayHighlightBandFraction=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightBandFraction", -1.0f)
            << " flatGrayHighlightMeanLuma=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightMeanLuma", -1.0f)
            << " flatGrayHighlightLowSat=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightLowSaturationFraction", -1.0f)
            << " flatGrayHighlightTileCoverage=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightTileCoverage", -1.0f)
            << " flatGrayHighlightStructureScore=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightStructureScore", -1.0f)
            << " specularHighlightToleranceGenerated=" << specularHighlightToleranceGenerated
            << " specularHighlightToleranceEligible=" << specularHighlightToleranceEligible
            << " specularHighlightToleranceHumanReadable=" << specularHighlightToleranceHumanReadable
            << " specularHighlightToleranceDiagnosticsWritten=" << specularHighlightToleranceDiagnosticsWritten
            << " specularHighlightToleranceRenderPayloadConstrained=" << specularHighlightToleranceRenderPayloadConstrained
            << " specularHighlightToleranceNeed=" << specularPayload.integratedToneLayerJson.value("autoDynamicRangeSpecularHighlightToleranceNeed", -1.0f)
            << " specularHighlightToleranceStageConstraint=" << specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " specularHighlightToleranceDiagnosticId=" << specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " specularHighlightToleranceHighlightCharacter=" << specularHighlightTolerancePayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " specularHighlightToleranceHighlightBias=" << specularHighlightTolerancePayload.integratedToneLayerJson.value("autoHighlightBias", -99.0f)
            << " regionalEvidenceDiagnosticsWritten=" << regionalEvidenceDiagnosticsWritten
            << " subjectSceneIntentDiagnosticsWritten=" << subjectSceneIntentDiagnosticsWritten
            << " subjectSceneIntentScoreComponentsWritten=" << subjectSceneIntentScoreComponentsWritten
            << " subjectSceneIntentBiasesScoring=" << subjectSceneIntentBiasesScoring
            << " subjectSceneIntentVersion=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneIntentVersion", std::string())
            << " subjectSceneIntentId=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneIntentId", std::string())
            << " subjectSceneIntentLabel=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneIntentLabel", std::string())
            << " subjectSceneBrushStatus=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneBrushStatus", std::string())
            << " subjectSceneAutomaticConfidence=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneAutomaticConfidence", -1.0f)
            << " subjectSceneCenterPrior=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneCenterPrior", -1.0f)
            << " subjectSceneReadabilityPressure=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneReadabilityPressure", -1.0f)
            << " subjectSceneProtectionPressure=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneProtectionPressure", -1.0f)
            << " subjectSceneMoodPreservationPressure=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneMoodPreservationPressure", -1.0f)
            << " subjectSceneSubjectSceneAxis=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneSubjectSceneAxis", -99.0f)
            << " subjectSceneMoodReadabilityAxis=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneMoodReadabilityAxis", -99.0f)
            << " userSubjectSceneIntentDiagnosticsWritten=" << userSubjectSceneIntentDiagnosticsWritten
            << " userSubjectSceneIntentScoreComponentsWritten=" << userSubjectSceneIntentScoreComponentsWritten
            << " userSubjectSceneGuidanceStatus=" << userSubjectIntentPayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStatus", std::string())
            << " userSubjectSceneGuidanceStrength=" << userSubjectIntentPayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStrength", -1.0f)
            << " userSubjectSceneBias=" << userSubjectIntentPayload.integratedToneLayerJson.value("autoSubjectSceneUserSubjectSceneBias", -99.0f)
            << " userMoodReadabilityBias=" << userSubjectIntentPayload.integratedToneLayerJson.value("autoSubjectSceneUserMoodReadabilityBias", -99.0f)
            << " subjectImportanceGuidanceDiagnosticsWritten=" << subjectImportanceGuidanceDiagnosticsWritten
            << " subjectImportanceGuidanceStatus=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStatus", std::string())
            << " subjectImportanceBrushStatus=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneBrushStatus", std::string())
            << " subjectImportanceRegionCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceRegionCount", -1)
            << " subjectImportanceStrokeCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceStrokeCount", -1)
            << " subjectImportanceRequestedRegionCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoRequestedSubjectImportanceRegionCount", -1)
            << " subjectImportanceRequestedStrokeCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoRequestedSubjectImportanceStrokeCount", -1)
            << " subjectImportanceStrength=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceStrength", -1.0f)
            << " subjectImportanceReveal=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceReveal", -1.0f)
            << " subjectImportanceSolveNoteVersion=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneSolveNotesVersion", std::string())
            << " subjectImportanceSolveNoteCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneSolveNoteCount", -1)
            << " subjectImportancePrimarySolveNote=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectScenePrimarySolveNote", std::string())
            << " subjectImportanceFingerprintA=" << subjectImportanceFingerprintA
            << " subjectImportanceFingerprintB=" << subjectImportanceFingerprintB
            << " subjectImportanceVisualFingerprint=" << subjectImportanceVisualFingerprint
            << " subjectImportanceScoreComponentsWritten=" << subjectImportanceScoreComponentsWritten
            << " subjectBrushGuidanceDiagnosticsWritten=" << subjectBrushGuidanceDiagnosticsWritten
            << " subjectBrushGuidanceStatus=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStatus", std::string())
            << " subjectBrushStatus=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneBrushStatus", std::string())
            << " subjectBrushRegionCount=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneImportanceRegionCount", -1)
            << " subjectBrushStrokeCount=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneImportanceStrokeCount", -1)
            << " subjectBrushRequestedStrokeCount=" << subjectBrushPayload.integratedToneLayerJson.value("autoRequestedSubjectImportanceStrokeCount", -1)
            << " subjectBrushReveal=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneImportanceReveal", -1.0f)
            << " subjectBrushFingerprintA=" << subjectBrushFingerprintA
            << " subjectBrushFingerprintB=" << subjectBrushFingerprintB
            << " subjectBrushScoreComponentsWritten=" << subjectBrushScoreComponentsWritten
            << " subjectBrushDisabledIgnored=" << subjectBrushDisabledIgnored
            << " subjectBrushDisabledStrokeCount=" << subjectBrushDisabledPayload.integratedToneLayerJson.value("autoSubjectSceneImportanceStrokeCount", -1)
            << " subjectBrushDisabledGuidanceStatus=" << subjectBrushDisabledPayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStatus", std::string())
            << " subjectBrushReduceGuidanceDiagnosticsWritten=" << subjectBrushReduceGuidanceDiagnosticsWritten
            << " subjectBrushReduceStatus=" << subjectBrushReducePayload.integratedToneLayerJson.value("autoSubjectSceneBrushStatus", std::string())
            << " subjectBrushReduceGuidanceStrength=" << subjectBrushReducePayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStrength", -1.0f)
            << " subjectBrushReduceIgnore=" << subjectBrushReducePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceIgnore", -1.0f)
            << " subjectBrushReduceScoreComponentsWritten=" << subjectBrushReduceScoreComponentsWritten
            << " subjectReadableMidsGenerated=" << subjectReadableMidsGenerated
            << " subjectReadableMidsEligible=" << subjectReadableMidsEligible
            << " subjectReadableMidsHumanReadable=" << subjectReadableMidsHumanReadable
            << " subjectReadableMidsDiagnosticsWritten=" << subjectReadableMidsDiagnosticsWritten
            << " subjectReadableMidsRenderPayloadConstrained=" << subjectReadableMidsRenderPayloadConstrained
            << " subjectReadableMidsStageConstraint=" << subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " subjectReadableMidsSubjectProbe=" << subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateSubjectIntentProbe", std::string())
            << " subjectReadableMidsMaxEvBias=" << subjectReadableMidsPayload.scenePrepSettings.maxEvBias
            << " sceneMoodPreservationGenerated=" << sceneMoodPreservationGenerated
            << " sceneMoodPreservationEligible=" << sceneMoodPreservationEligible
            << " sceneMoodPreservationHumanReadable=" << sceneMoodPreservationHumanReadable
            << " sceneMoodPreservationDiagnosticsWritten=" << sceneMoodPreservationDiagnosticsWritten
            << " sceneMoodPreservationRenderPayloadConstrained=" << sceneMoodPreservationRenderPayloadConstrained
            << " sceneMoodPreservationStageConstraint=" << sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " sceneMoodPreservationSubjectProbe=" << sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateSubjectIntentProbe", std::string())
            << " sceneMoodPreservationMaxEvBias=" << sceneMoodPreservationPayload.scenePrepSettings.maxEvBias
            << " haloSafeLocalRangeGenerated=" << haloSafeLocalRangeGenerated
            << " haloSafeLocalRangeEligible=" << haloSafeLocalRangeEligible
            << " haloSafeLocalRangeHumanReadable=" << haloSafeLocalRangeHumanReadable
            << " haloSafeLocalRangeDiagnosticsWritten=" << haloSafeLocalRangeDiagnosticsWritten
            << " haloSafeLocalRangeRenderPayloadConstrained=" << haloSafeLocalRangeRenderPayloadConstrained
            << " haloSafeLocalRangeNeed=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalHaloGuardNeed", -1.0f)
            << " haloSafeLocalRangeStageConstraint=" << haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " haloSafeLocalRangeScenePrepProbe=" << haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string())
            << " haloSafeLocalRangeMaxEvBias=" << haloSafeLocalRangePayload.scenePrepSettings.maxEvBias
            << " haloSafeLocalRangeHaloGuard=" << haloSafeLocalRangePayload.scenePrepSettings.haloGuard
            << " haloSafeLocalRangeSmoothGradient=" << haloSafeLocalRangePayload.scenePrepSettings.smoothGradientProtection
            << " haloSafeLocalRangeEdgeAwareness=" << haloSafeLocalRangePayload.scenePrepSettings.edgeAwareness
            << " localRangeGuardGenerated=" << localRangeGuardGenerated
            << " localRangeGuardEligible=" << localRangeGuardEligible
            << " localRangeGuardDiagnosticsWritten=" << localRangeGuardDiagnosticsWritten
            << " localRangeGuardRenderPayloadConstrained=" << localRangeGuardRenderPayloadConstrained
            << " localRangeGuardStageConstraint=" << localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " localRangeGuardRegionSource=" << regionalEvidence.value("source", std::string())
            << " localRangeGuardLocalConflict=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalRangeConflict", -1.0f)
            << " localRangeGuardLocalEvConflict=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalEvConflict", -1.0f)
            << " localRangeGuardLocalEvSpreadStops=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalEvSpreadStops", -1.0f)
            << " shadowReadabilityLiftGenerated=" << shadowReadabilityLiftGenerated
            << " shadowReadabilityLiftEligible=" << shadowReadabilityLiftEligible
            << " shadowReadabilityLiftHumanReadable=" << shadowReadabilityLiftHumanReadable
            << " shadowReadabilityLiftDiagnosticsWritten=" << shadowReadabilityLiftDiagnosticsWritten
            << " shadowReadabilityLiftRenderPayloadConstrained=" << shadowReadabilityLiftRenderPayloadConstrained
            << " shadowReadabilityLiftNeed=" << readableShadowPayload.integratedToneLayerJson.value("autoDynamicRangeShadowReadabilityLiftNeed", -1.0f)
            << " shadowReadabilityLiftStageConstraint=" << shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " shadowReadabilityLiftScenePrepProbe=" << shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string())
            << " shadowReadabilityLiftMaxEvBias=" << shadowReadabilityLiftPayload.scenePrepSettings.maxEvBias
            << " shadowReadabilityLiftNoiseBias=" << shadowReadabilityLiftPayload.scenePrepSettings.noiseProtectionBias
            << " shadowReadabilityLiftShadowLimitBias=" << shadowReadabilityLiftPayload.scenePrepSettings.shadowLiftLimitBias
            << " shadowNoiseFloorGenerated=" << shadowNoiseFloorGenerated
            << " shadowNoiseFloorEligible=" << shadowNoiseFloorEligible
            << " shadowNoiseFloorDiagnosticsWritten=" << shadowNoiseFloorDiagnosticsWritten
            << " shadowNoiseFloorRenderPayloadConstrained=" << shadowNoiseFloorRenderPayloadConstrained
            << " shadowNoiseFloorNeed=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeShadowNoiseFloorNeed", -1.0f)
            << " shadowNoiseFloorStageConstraint=" << shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " shadowNoiseFloorScenePrepProbe=" << shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string())
            << " shadowNoiseFloorMaxEvBias=" << shadowNoiseFloorPayload.scenePrepSettings.maxEvBias
            << " shadowNoiseFloorNoiseBias=" << shadowNoiseFloorPayload.scenePrepSettings.noiseProtectionBias
            << " shadowNoiseFloorShadowLimitBias=" << shadowNoiseFloorPayload.scenePrepSettings.shadowLiftLimitBias
            << " whiteBalanceProbeGenerated=" << whiteBalanceProbeGenerated
            << " whiteBalanceProbeEligible=" << whiteBalanceProbeEligible
            << " whiteBalanceProbeHumanReadable=" << whiteBalanceProbeHumanReadable
            << " whiteBalanceProbeDiagnosticsWritten=" << whiteBalanceProbeDiagnosticsWritten
            << " whiteBalanceProbeRenderPayloadDiverges=" << whiteBalanceProbeRenderPayloadDiverges
            << " whiteBalanceProbeId=" << whiteBalanceProbeRenderId
            << " whiteBalanceProbeMode=" << whiteBalanceProbeMode
            << " whiteBalanceProbePayloadMode=" << Raw::WhiteBalanceModeName(whiteBalanceProbePayload.settings.whiteBalanceMode)
            << " whiteBalanceProbeStageConstraint=" << whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " whiteBalanceProbeDiagnosticId=" << whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateWhiteBalanceProbe", std::string())
            << " scenePrepCandidateStageConstrained=" << scenePrepCandidateStageConstrained
            << " scenePrepStageConstraint=" << scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " scenePrepStageExposure=" << scenePrepStagePayload.settings.exposureStops
            << " scenePrepStageBaseExposure=" << stageConstraintBasePayload.settings.exposureStops
            << " scenePrepStageBaseEvBias=" << scenePrepStagePayload.scenePrepSettings.baseEvBias
            << " scenePrepStageMaxEvBias=" << scenePrepStagePayload.scenePrepSettings.maxEvBias
            << " finishToneCandidateStageConstrained=" << finishToneCandidateStageConstrained
            << " finishToneStageConstraint=" << finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " finishToneStageExposure=" << finishToneStagePayload.settings.exposureStops
            << " finishToneStageBaseEvBias=" << finishToneStagePayload.scenePrepSettings.baseEvBias
            << " finishToneStageMaxEvBias=" << finishToneStagePayload.scenePrepSettings.maxEvBias
            << " finishToneContrastBias=" << finishToneStagePayload.integratedToneLayerJson.value("autoContrastBias", -99.0f)
            << " renderedStageRelevanceWorks=" << renderedStageRelevanceWorks
            << " renderedRefineIntentRelevanceWorks=" << renderedRefineIntentRelevanceWorks
            << " renderedStageCacheValidationWorks=" << renderedStageCacheValidationWorks
            << " finishToneObservedDirtyBoundary=" << finishToneObservedDirtyBoundary
            << " finishToneStageCacheExpected=" << finishToneStageCacheExpected
            << " finishToneStageCacheStatus=" << finishToneStageCacheStatus
            << " finishToneStageCacheMet=" << finishToneStageCacheMet
            << " scenePrepObservedDirtyBoundary=" << scenePrepObservedDirtyBoundary
            << " scenePrepStageCacheExpected=" << scenePrepStageCacheExpected
            << " scenePrepStageCacheStatus=" << scenePrepStageCacheStatus
            << " scenePrepStageCacheMet=" << scenePrepStageCacheMet
            << " scenePrepMissObservedDirtyBoundary=" << scenePrepMissObservedDirtyBoundary
            << " scenePrepStageCacheMissStatus=" << scenePrepStageCacheMissStatus
            << " scenePrepStageCacheMissMet=" << scenePrepStageCacheMissMet
            << " rawStageObservedDirtyBoundary=" << rawStageObservedDirtyBoundary
            << " rawStageCacheExpected=" << rawStageCacheExpected
            << " rawStageCacheStatus=" << rawStageCacheStatus
            << " rawStageCacheMet=" << rawStageCacheMet
            << " renderedStageSchedulerClassificationWorks=" << renderedStageSchedulerClassificationWorks
            << " developCandidateRenderBudgetAllowsMultiNodeCoverage=" << developCandidateRenderBudgetAllowsMultiNodeCoverage
            << " developCandidateMetricReadbackBudgetWorks=" << developCandidateMetricReadbackBudgetWorks
            << " rawDevelopStageCacheMemoryPolicyWorks=" << rawDevelopStageCacheMemoryPolicyWorks
            << " developAdaptiveRenderBudgetWorks=" << developAdaptiveRenderBudgetWorks
            << " adaptiveContinueBudget=" << adaptiveContinueBudget
            << " adaptiveContinueReason=" << adaptiveContinueReason
            << " adaptiveContinueExpanded=" << adaptiveContinueExpanded
            << " adaptiveInitialBudget=" << adaptiveInitialBudget
            << " adaptiveInitialReason=" << adaptiveInitialReason
            << " adaptiveInitialExpanded=" << adaptiveInitialExpanded
            << " adaptiveFocusedBudget=" << adaptiveFocusedBudget
            << " adaptiveFocusedReason=" << adaptiveFocusedReason
            << " adaptiveFocusedExpanded=" << adaptiveFocusedExpanded
            << " adaptiveFocusedNarrowed=" << adaptiveFocusedNarrowed
            << " developCandidateFeedbackGateDropsStale=" << developCandidateFeedbackGateDropsStale
            << " developCandidateFeedbackGateDefersRecent=" << developCandidateFeedbackGateDefersRecent
            << " developCandidateFeedbackGateAppliesAfterQuiet=" << developCandidateFeedbackGateAppliesAfterQuiet
            << " developCandidateRenderAdmissionDefersRecent=" << developCandidateRenderAdmissionDefersRecent
            << " developCandidateFeedbackQuietRemainingWorks=" << developCandidateFeedbackQuietRemainingWorks
            << " candidateFeedbackRemainingMidEdit=" << candidateFeedbackRemainingMidEdit
            << " developStaleSnapshotAbortWorks=" << developStaleSnapshotAbortWorks
            << " developCandidateProgressLabelWorks=" << developCandidateProgressLabelWorks
            << " candidateProgressLabel=\"" << candidateProgressLabel << "\""
            << " candidateFeedbackQuietSeconds=" << candidateFeedbackQuietSeconds
            << " selectedScheduleRank=" << selectedScheduleRank
            << " finishToneScheduleRank=" << finishToneScheduleRank
            << " scenePrepScheduleRank=" << scenePrepScheduleRank
            << " rawGlobalScheduleRank=" << rawGlobalScheduleRank
            << " multiStageScheduleRank=" << multiStageScheduleRank
            << " finishToneScheduleBoundary=" << finishToneScheduleBoundary
            << " scenePrepScheduleBoundary=" << scenePrepScheduleBoundary
            << " rawGlobalScheduleBoundary=" << rawGlobalScheduleBoundary
            << " cleanProbeLumaDenoise=" << cleanProbePayload.settings.mosaicDenoise.lumaStrength
            << " textureProbeLumaDenoise=" << textureProbePayload.settings.mosaicDenoise.lumaStrength
            << " cleanProbeFalseColor=" << cleanProbePayload.settings.falseColorSuppression
            << " textureProbeFalseColor=" << textureProbePayload.settings.falseColorSuppression
            << " cleanProbeTextureSensitivity=" << cleanProbePayload.scenePrepSettings.textureSensitivity
            << " textureProbeTextureSensitivity=" << textureProbePayload.scenePrepSettings.textureSensitivity
            << " brightnessExposureTelemetryForwarded=" << brightnessExposureTelemetryForwarded
            << " exposureDiagnosticsForwarded=" << exposureDiagnosticsForwarded
            << " renderedVisualRiskMetricsPopulated=" << renderedVisualRiskMetricsPopulated
            << " visualRiskContrastSpan=" << visualRiskMetrics.contrastSpan
            << " visualRiskLowSaturation=" << visualRiskMetrics.lowSaturationFraction
            << " visualRiskEdgeContrast=" << visualRiskMetrics.edgeContrast
            << " visualRiskHalo=" << visualRiskMetrics.haloRiskFraction
            << " visualRiskShadowTexture=" << visualRiskMetrics.shadowTextureRisk
            << " renderedHighlightGrayMetricsPopulated=" << renderedHighlightGrayMetricsPopulated
            << " visualRiskHighlightBandFraction=" << visualRiskMetrics.highlightBandFraction
            << " visualRiskHighlightMeanLuma=" << visualRiskMetrics.highlightMeanLuma
            << " visualRiskHighlightLowSaturationFraction=" << visualRiskMetrics.highlightLowSaturationFraction
            << " visualRiskHighlightGrayRisk=" << visualRiskMetrics.highlightGrayRisk
            << " renderedMeaningfulHighlightMetricsPopulated=" << renderedMeaningfulHighlightMetricsPopulated
            << " visualRiskHighlightTileCoverage=" << visualRiskMetrics.highlightTileCoverage
            << " visualRiskHighlightStructureScore=" << visualRiskMetrics.highlightStructureScore
            << " visualRiskMeaningfulHighlightPressure=" << visualRiskMetrics.meaningfulHighlightPressure
            << " renderedLocalMetricsPopulated=" << renderedLocalMetricsPopulated
            << " renderedLocalLumaSpread=" << visualRiskMetrics.localLumaSpread
            << " renderedLocalEvSpreadStops=" << visualRiskMetrics.localEvSpreadStops
            << " renderedLocalEvConflict=" << visualRiskMetrics.localEvConflict
            << " renderedLocalContrastPeak=" << visualRiskMetrics.localContrastPeak
            << " renderedLocalShadowPressure=" << visualRiskMetrics.localShadowPressure
            << " renderedCenterMeanLuma=" << visualRiskMetrics.centerMeanLuma
            << " renderedSubjectMetricsPopulated=" << renderedSubjectMetricsPopulated
            << " subjectRiskCenterPrior=" << subjectRiskMetrics.subjectCenterPrior
            << " subjectRiskConfidence=" << subjectRiskMetrics.subjectImportanceConfidence
            << " subjectRiskReadability=" << subjectRiskMetrics.subjectReadabilityPressure
            << " subjectRiskProtection=" << subjectRiskMetrics.subjectProtectionPressure
            << " subjectRiskMood=" << subjectRiskMetrics.subjectMoodPreservationPressure
            << " renderedMarkedSubjectMetricsPopulated=" << renderedMarkedSubjectMetricsPopulated
            << " markedSubjectCoverage=" << markedSubjectMetrics.subjectMarkedCoverage
            << " markedSubjectPositiveCoverage=" << markedSubjectMetrics.subjectMarkedPositiveCoverage
            << " markedSubjectRevealCoverage=" << markedSubjectMetrics.subjectMarkedRevealCoverage
            << " markedSubjectMeanLuma=" << markedSubjectMetrics.subjectMarkedMeanLuma
            << " markedSubjectReadabilityScore=" << markedSubjectMetrics.subjectMarkedReadabilityScore
            << " markedSubjectProtectionRisk=" << markedSubjectMetrics.subjectMarkedProtectionRisk
            << " renderedMarkedLowPriorityMetricsPopulated=" << renderedMarkedLowPriorityMetricsPopulated
            << " markedLowPriorityCoverage=" << lowPrioritySubjectMetrics.subjectMarkedLowPriorityCoverage
            << " markedLowPriorityMeanLuma=" << lowPrioritySubjectMetrics.subjectMarkedLowPriorityMeanLuma
            << " markedLowPriorityBrightFraction=" << lowPrioritySubjectMetrics.subjectMarkedLowPriorityBrightFraction
            << " markedLowPriorityPressure=" << lowPrioritySubjectMetrics.subjectMarkedLowPriorityPressure
            << " renderedSpatialRiskMetricsPopulated=" << renderedSpatialRiskMetricsPopulated
            << " renderedLocalDamageRiskMean=" << spatialRiskMetrics.localDamageRiskMean
            << " renderedLocalDamageRiskPeak=" << spatialRiskMetrics.localDamageRiskPeak
            << " renderedLocalDamageRiskPeakTile=" << spatialRiskMetrics.localDamageRiskPeakTile
            << " renderedColorCastMetricsPopulated=" << renderedColorCastMetricsPopulated
            << " colorCastMeanRed=" << colorCastMetrics.meanRed
            << " colorCastMeanGreen=" << colorCastMetrics.meanGreen
            << " colorCastMeanBlue=" << colorCastMetrics.meanBlue
            << " colorCastWarmCoolBias=" << colorCastMetrics.warmCoolBias
            << " colorCastMagentaGreenBias=" << colorCastMetrics.magentaGreenBias
            << " colorCastChannelImbalance=" << colorCastMetrics.channelImbalance
            << " colorCastRisk=" << colorCastMetrics.colorCastRisk
            << " renderedDuplicateMetricDistanceWorks=" << renderedDuplicateMetricDistanceWorks
            << " renderedDuplicateMetricDistance=" << renderedDuplicateMetricDistance
            << " renderedDistinctMetricDistance=" << renderedDistinctMetricDistance
            << " renderedLocalMetricDistance=" << renderedLocalMetricDistance
            << " renderedColorMetricDistance=" << renderedColorMetricDistance
            << " renderedMarkedSubjectMetricDistance=" << renderedMarkedSubjectMetricDistance
            << " renderedStageBoundaryClassifierWorks=" << renderedStageBoundaryClassifierWorks
            << " finishOnlyStageBoundary=" << finishOnlyStageBoundary
            << " finishOnlyFinalMetricDistance=" << finishOnlyFinalMetricDistance
            << " finishOnlyPreFinishMetricDistance=" << finishOnlyPreFinishMetricDistance
            << " preFinishChangedStageBoundary=" << preFinishChangedStageBoundary
            << " preFinishChangedFinalMetricDistance=" << preFinishChangedFinalMetricDistance
            << " preFinishChangedPreFinishMetricDistance=" << preFinishChangedPreFinishMetricDistance
            << " renderedStageAwareDuplicateClusteringWorks=" << renderedStageAwareDuplicateClusteringWorks
            << " stageAwareDuplicateFinalDistance=" << stageAwareDuplicateFinalDistance
            << " stageAwareDuplicatePreFinishDistance=" << stageAwareDuplicatePreFinishDistance
            << " stageAwareDuplicatePreFinishDistinct=" << stageAwareDuplicatePreFinishDistinct
            << " stageAwareMaskedFinalDistance=" << stageAwareMaskedFinalDistance
            << " stageAwareMaskedPreFinishDistance=" << stageAwareMaskedPreFinishDistance
            << " stageAwareMaskedPreFinishDistinct=" << stageAwareMaskedPreFinishDistinct
            << " renderedDamageClassifierWorks=" << renderedDamageClassifierWorks
            << " damagedClipReason=" << damagedClipReason
            << " damagedHaloReason=" << damagedHaloReason
            << " damagedGrayReason=" << damagedGrayReason
            << " damagedShadowNoiseReason=" << damagedShadowNoiseReason
            << " damagedSpatialHotspotReason=" << damagedSpatialHotspotReason
            << " damagedColorCastReason=" << damagedColorCastReason
            << " safeRenderedDamageReason=" << safeRenderedDamageReason
            << " renderedRelativeComparisonWorks=" << renderedRelativeComparisonWorks
            << " relativeAdjustedRawScore=" << relativeAdjustedRawScore
            << " relativeAdjustedIntentScore=" << relativeAdjustedIntentScore
            << " relativeRawScoreStatus=" << relativeRawScoreStatus
            << " relativeIntentStatus=" << relativeIntentStatus
            << " relativeRawScoreRepairDelta=" << relativeRawScoreRepairDelta
            << " relativeIntentRepairDelta=" << relativeIntentRepairDelta
            << " relativeRawScoreRegressionPenalty=" << relativeRawScoreRegressionPenalty
            << " modeNeighborCandidateGenerated=" << modeNeighborCandidateGenerated
            << " modeNeighborCandidateEligible=" << modeNeighborCandidateEligible
            << " modeNeighborCandidateHumanReadable=" << modeNeighborCandidateHumanReadable
            << " modeNeighborCandidateMeaningfullyDifferent=" << modeNeighborCandidateMeaningfullyDifferent
            << " renderedLocalRefineIntentWorks=" << renderedLocalRefineIntentWorks
            << " localCenterShadowIntent=" << localCenterShadowIntent
            << " localHighlightIntent=" << localHighlightIntent
            << " structuredHighlightPressureIntent=" << structuredHighlightPressureIntent
            << " structuredHighlightPressureReason=" << structuredHighlightPressureReason
            << " localSpatialHighlightRiskIntent=" << localSpatialHighlightRiskIntent
            << " localSpatialHighlightRiskReason=" << localSpatialHighlightRiskReason
            << " localFlatIntent=" << localFlatIntent
            << " renderedCleanShadowIntent=" << renderedCleanShadowIntent
            << " localSpatialShadowRiskIntent=" << localSpatialShadowRiskIntent
            << " localSpatialShadowRiskReason=" << localSpatialShadowRiskReason
            << " localSpatialFlatRiskIntent=" << localSpatialFlatRiskIntent
            << " localSpatialFlatRiskReason=" << localSpatialFlatRiskReason
            << " renderedPreserveTextureIntent=" << renderedPreserveTextureIntent
            << " renderedFeedbackCandidateId=" << renderedFeedbackCandidateId
            << " renderedFeedbackAdoptedCandidate=" << renderedFeedbackAdoptedCandidate
            << " renderedFeedbackLoopActive=" << renderedFeedbackLoopActive
            << " renderedFeedbackLoopState=" << renderedFeedbackLoop.value("state", std::string())
            << " renderedFeedbackLoopAction=" << renderedFeedbackLoop.value("action", std::string())
            << " renderedFeedbackLoopNextStep=" << renderedFeedbackLoop.value("nextStep", std::string())
            << " renderedContinuationPolicyContinues=" << renderedContinuationPolicyContinues
            << " renderedFeedbackContinuationDecision=" << renderedFeedbackContinuation.value("decision", std::string())
            << " renderedFeedbackContinuationReason=" << renderedFeedbackContinuation.value("reason", std::string())
            << " renderedFeedbackContinuationNextStep=" << renderedFeedbackContinuation.value("nextStep", std::string())
            << " convergenceEvidenceContinuesAfterFeedback=" << convergenceEvidenceContinuesAfterFeedback
            << " renderedFeedbackConvergenceState=" << renderedFeedbackConvergenceEvidence.value("state", std::string())
            << " renderedFeedbackConvergenceDecision=" << renderedFeedbackConvergenceEvidence.value("decision", std::string())
            << " renderedFeedbackConvergenceReason=" << renderedFeedbackConvergenceEvidence.value("reason", std::string())
            << " convergenceEvidenceAdmissionTightensMarginalContinuation=" << convergenceEvidenceAdmissionTightensMarginalContinuation
            << " convergenceAdmissionStopReason=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " convergenceAdmissionTightened=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateConvergenceAdmissionTightened", false)
            << " convergenceAdmissionMinimum=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateConvergenceAdmissionMinimumImprovement", -1.0f)
            << " convergenceAdmissionBaseMinimum=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateConvergenceAdmissionBaseMinimumImprovement", -1.0f)
            << " convergenceAdmissionEvidenceState=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateConvergenceAdmissionEvidenceState", std::string())
            << " convergenceAdmissionConvergenceState=" << convergenceAdmissionEvidence.value("state", std::string())
            << " convergenceAdmissionChallengerId=" << convergenceAdmissionChallengerId
            << " continuationBiasDiagnosticsWritten=" << continuationBiasDiagnosticsWritten
            << " continuationBiasBoostsFinishToneCandidates=" << continuationBiasBoostsFinishToneCandidates
            << " continuationBiasActive=" << continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasActive", false)
            << " continuationBiasReason=" << continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasReason", std::string())
            << " continuationBiasStage=" << continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasStage", std::string())
            << " continuationBiasAppliedCount=" << continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasAppliedCount", -1)
            << " continuationExpansionDiagnosticsWritten=" << continuationExpansionDiagnosticsWritten
            << " continuationExpansionAddsFinishToneFamily=" << continuationExpansionAddsFinishToneFamily
            << " continuationExpansionEligible=" << continuationExpansionPayload.integratedToneLayerJson.value("autoCandidateContinuationExpansionEligible", false)
            << " continuationExpansionActive=" << continuationExpansionPayload.integratedToneLayerJson.value("autoCandidateContinuationExpansionActive", false)
            << " continuationExpansionStage=" << continuationExpansionPayload.integratedToneLayerJson.value("autoCandidateContinuationExpansionStage", std::string())
            << " continuationExpansionAddedCount=" << continuationExpansionPayload.integratedToneLayerJson.value("autoCandidateContinuationExpansionAddedCount", -1)
            << " renderedFeedbackSelectedId=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedFeedbackSelectionSource=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedFeedbackPass=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedFeedbackRevisionStageWritten=" << renderedFeedbackRevisionStageWritten
            << " renderedFeedbackRevisionStage=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string())
            << " renderedFeedbackFinishToneStageOverride=" << renderedFeedbackFinishToneStageOverride
            << " renderedFeedbackRevisionReason=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionReason", std::string())
            << " renderedFeedbackMergedCandidate=" << renderedFeedbackMergedCandidate
            << " renderedMergeSelectedId=" << renderedMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedMergeSelectionSource=" << renderedMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedMergeFeedbackAction=" << renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string())
            << " renderedMergePass=" << renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedFeedbackPairMergedCandidate=" << renderedFeedbackPairMergedCandidate
            << " renderedFeedbackSecondCandidateId=" << renderedFeedbackSecondCandidateId
            << " renderedFeedbackEnsembleMergedCandidate=" << renderedFeedbackEnsembleMergedCandidate
            << " renderedFeedbackThirdCandidateId=" << renderedFeedbackThirdCandidateId
            << " renderedEnsembleMergeSelectedId=" << renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedEnsembleMergeSelectionSource=" << renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedEnsembleMergeThirdId=" << renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeThirdId", std::string())
            << " renderedEnsembleMergeWeights="
            << renderedEnsembleFirstWeight << ","
            << renderedEnsembleSecondWeight << ","
            << renderedEnsembleThirdWeight
            << " renderedPairMergeSelectedId=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedPairMergeSelectionSource=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedPairMergeFeedbackAction=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string())
            << " renderedPairMergeFirstId=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstId", std::string())
            << " renderedPairMergeSecondId=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondId", std::string())
            << " renderedFeedbackCandidateCarriedForward=" << renderedFeedbackCandidateCarriedForward
            << " renderedSurvivorCandidateCarriedForward=" << renderedSurvivorCandidateCarriedForward
            << " renderedSurvivorCarryForwardCountWritten=" << renderedSurvivorCarryForwardCountWritten
            << " renderedSurvivorCarryForwardCount=" << renderedSurvivorCarryPayload.integratedToneLayerJson.value("autoCandidateRenderedCarriedForwardCount", -1)
            << " renderedFeedbackRefinedCandidate=" << renderedFeedbackRefinedCandidate
            << " renderedRefineRevisionStageIsScenePrep=" << renderedRefineRevisionStageIsScenePrep
            << " renderedRefineStagePlanTargetsScenePrep=" << renderedRefineStagePlanTargetsScenePrep
            << " renderedRefineAutoStageEarliestDirtyStage=" << renderedRefinePayload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string())
            << " renderedRefineAutoStageRevisionStage=" << renderedRefinePayload.integratedToneLayerJson.value("autoStageRevisionStage", std::string())
            << " renderedRefineAutoStageResponsibleRevisionState=" << renderedRefinePayload.integratedToneLayerJson.value("autoStageResponsibleRevisionState", std::string())
            << " renderedRefineRevisionStage=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string())
            << " renderedLocalRefineCandidateGenerated=" << renderedLocalRefineCandidateGenerated
            << " renderedRefineSelectedId=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedRefineSelectionSource=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedRefineFeedbackAction=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string())
            << " renderedRefineIntent=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string())
            << " renderedRefinePass=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedCleanupRefinedCandidate=" << renderedCleanupRefinedCandidate
            << " renderedCleanupRevisionStageIsRawCleanup=" << renderedCleanupRevisionStageIsRawCleanup
            << " renderedCleanupStagePlanTargetsRawBase=" << renderedCleanupStagePlanTargetsRawBase
            << " renderedCleanupAutoStageEarliestDirtyStage=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string())
            << " renderedCleanupAutoStageRevisionStage=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageRevisionStage", std::string())
            << " renderedCleanupAutoStageResponsibleRevisionState=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageResponsibleRevisionState", std::string())
            << " renderedCleanupRevisionStage=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string())
            << " renderedCleanupRefineCandidateGenerated=" << renderedCleanupRefineCandidateGenerated
            << " renderedCleanupRefineSelectedId=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedCleanupRefineSelectionSource=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedCleanupRefineFeedbackAction=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string())
            << " renderedCleanupRefineIntent=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string())
            << " renderedCleanupRefineStopReason=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedCleanupRefineRenderedFingerprint=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0))
            << " renderedCleanupRefineSolveFingerprint=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0))
            << " renderedRepeatedRefineStops=" << renderedRepeatedRefineStops
            << " renderedRepeatedRefineSelectedId=" << renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedRepeatedRefineSelectionSource=" << renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedRepeatedRefineApplied=" << renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true)
            << " renderedRepeatedRefinePass=" << renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedMonotonicShadowRiskStops=" << renderedMonotonicShadowRiskStops
            << " renderedMonotonicShadowStopReason=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedMonotonicShadowStatus=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string())
            << " renderedMonotonicShadowMetric=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicMetric", std::string())
            << " renderedMonotonicShadowPrevious=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicPreviousValue", -1.0f)
            << " renderedMonotonicShadowCurrent=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicCurrentValue", -1.0f)
            << " renderedMonotonicShadowLoopState=" << renderedMonotonicShadowLoop.value("state", std::string())
            << " renderedNoImprovementStops=" << renderedNoImprovementStops
            << " renderedNoImprovementSelectedId=" << renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedNoImprovementApplied=" << renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true)
            << " renderedNoImprovementPass=" << renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedStableMetricsConverge=" << renderedStableMetricsConverge
            << " renderedStableLoopConverged=" << renderedStableLoopConverged
            << " renderedStableLoopState=" << renderedStableLoop.value("state", std::string())
            << " renderedStableLoopStopReason=" << renderedStableLoop.value("stopReason", std::string())
            << " renderedStableStopReason=" << renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedContinuationPolicyStopsStable=" << renderedContinuationPolicyStopsStable
            << " renderedStableContinuationDecision=" << renderedStableContinuation.value("decision", std::string())
            << " renderedStableContinuationReason=" << renderedStableContinuation.value("reason", std::string())
            << " renderedStableDistance=" << renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedStabilityDistance", -1.0f)
            << " renderedStableStatus=" << renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedStabilityStatus", std::string())
            << " convergenceEvidenceStopsStable=" << convergenceEvidenceStopsStable
            << " renderedStableConvergenceState=" << renderedStableConvergenceEvidence.value("state", std::string())
            << " renderedStableConvergenceReason=" << renderedStableConvergenceEvidence.value("reason", std::string())
            << " renderedTrendConverges=" << renderedTrendConverges
            << " renderedTrendLoopConverged=" << renderedTrendLoopConverged
            << " renderedTrendLoopState=" << renderedTrendLoop.value("state", std::string())
            << " renderedTrendLoopStopReason=" << renderedTrendLoop.value("stopReason", std::string())
            << " renderedTrendStopReason=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedContinuationPolicyStopsTrend=" << renderedContinuationPolicyStopsTrend
            << " renderedTrendContinuationDecision=" << renderedTrendContinuation.value("decision", std::string())
            << " renderedTrendContinuationReason=" << renderedTrendContinuation.value("reason", std::string())
            << " renderedTrendStatus=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendStatus", std::string())
            << " renderedTrendHistoryCount=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendHistoryCount", -1)
            << " renderedTrendSameBestCount=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendSameBestCount", -1)
            << " renderedTrendScoreSpread=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendScoreSpread", -1.0f)
            << " renderedTrendNearestDistance=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendNearestDistance", -1.0f)
            << " convergenceEvidenceStopsTrend=" << convergenceEvidenceStopsTrend
            << " renderedTrendConvergenceState=" << renderedTrendConvergenceEvidence.value("state", std::string())
            << " renderedTrendConvergenceReason=" << renderedTrendConvergenceEvidence.value("reason", std::string())
            << " renderedFeedbackStopConvergenceClassifierWorks=" << renderedFeedbackStopConvergenceClassifierWorks
            << " renderedContinuationPolicyStopsAtPassLimit=" << renderedContinuationPolicyStopsAtPassLimit
            << " renderedPassLimitLoopState=" << renderedPassLimitLoop.value("state", std::string())
            << " renderedPassLimitStopReason=" << renderedPassLimitLoop.value("stopReason", std::string())
            << " renderedPassLimitContinuationDecision=" << renderedPassLimitContinuation.value("decision", std::string())
            << " renderedPassLimitContinuationReason=" << renderedPassLimitContinuation.value("reason", std::string())
            << " renderedPassLimitContinuationPass=" << renderedPassLimitContinuation.value("pass", -1)
            << " convergenceEvidenceStopsAtPassLimit=" << convergenceEvidenceStopsAtPassLimit
            << " renderedPassLimitConvergenceState=" << renderedPassLimitConvergenceEvidence.value("state", std::string())
            << " renderedPassLimitConvergenceReason=" << renderedPassLimitConvergenceEvidence.value("reason", std::string())
            << " renderedNoBestStopsWithoutConverging=" << renderedNoBestStopsWithoutConverging
            << " renderedNoBestStopReason=" << renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedNoBestConverged=" << renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", true)
            << " renderedNoBestRevisionStage=" << renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string())
            << " renderedNoBestLoopState=" << renderedNoBestLoop.value("state", std::string())
            << " renderedNoBestLoopStopReason=" << renderedNoBestLoop.value("stopReason", std::string())
            << " convergenceEvidenceStopsNoBest=" << convergenceEvidenceStopsNoBest
            << " renderedNoBestConvergenceState=" << renderedNoBestConvergenceEvidence.value("state", std::string())
            << " renderedNoBestConvergenceReason=" << renderedNoBestConvergenceEvidence.value("reason", std::string())
            << " partialSolvePreservedRawAuthorship=" << partialSolvePreservedRawAuthorship
            << " partialSolveUpdatedPrepAndFinish=" << partialSolveUpdatedPrepAndFinish
            << " repeatedFullSolveStable=" << repeatedFullSolveStable
            << " positiveExposureBiasPreserved=" << positiveExposureBiasPreserved
            << " highlightProfilePrefersHeadroom=" << highlightProfilePrefersHeadroom
            << " tinySpecularsDoNotDragExposure=" << tinySpecularsDoNotDragExposure
            << " broadHighlightsProtectedMoreThanSpeculars=" << broadHighlightsProtectedMoreThanSpeculars
            << " underBrightBroadHighlightsLifted=" << underBrightBroadHighlightsLifted
            << " darkSceneLifted=" << darkSceneLifted
            << " rawBaselineCarriesDarkLift=" << rawBaselineCarriesDarkLift
            << " rawBaselineDominatesDarkLift=" << rawBaselineDominatesDarkLift
            << " noisyProfilePrefersProtection=" << noisyProfilePrefersProtection
            << " modeIntentAffectsSolve=" << modeIntentAffectsSolve
            << " modeIntentForwarded=" << modeIntentForwarded
            << " exposureBefore=" << settingsBefore.exposureStops
            << " exposureAfter=" << payload.settings.exposureStops
            << " highlightModeAfter=" << static_cast<int>(payload.settings.highlightMode)
            << " highlightStrengthBefore=" << settingsBefore.highlightStrength
            << " highlightStrengthAfter=" << payload.settings.highlightStrength
            << " highlightThresholdBefore=" << settingsBefore.highlightThreshold
            << " highlightThresholdAfter=" << payload.settings.highlightThreshold
            << " lumaDenoiseBefore=" << settingsBefore.mosaicDenoise.lumaStrength
            << " lumaDenoiseAfter=" << payload.settings.mosaicDenoise.lumaStrength
            << " falseColorBefore=" << settingsBefore.falseColorSuppression
            << " falseColorAfter=" << payload.settings.falseColorSuppression
            << " autoBrightnessIntent=" << payload.integratedToneLayerJson.value("autoBrightnessIntent", -99.0f)
            << " autoRawExposurePreferenceEv=" << payload.integratedToneLayerJson.value("autoRawExposurePreferenceEv", -99.0f)
            << " autoAuthoredRawExposureEv=" << payload.integratedToneLayerJson.value("autoAuthoredRawExposureEv", -99.0f)
            << " autoAuthoredRawExposureScale=" << payload.integratedToneLayerJson.value("autoAuthoredRawExposureScale", -99.0f)
            << " autoAuthoredLocalMinEvBias=" << payload.integratedToneLayerJson.value("autoAuthoredLocalMinEvBias", -99.0f)
            << " autoAuthoredLocalMaxEvBias=" << payload.integratedToneLayerJson.value("autoAuthoredLocalMaxEvBias", -99.0f)
            << " autoExposureDiagnosticHighlightPressure=" << payload.integratedToneLayerJson.value("autoExposureDiagnosticHighlightPressure", -99.0f)
            << " autoExposureDiagnosticNoiseRisk=" << payload.integratedToneLayerJson.value("autoExposureDiagnosticNoiseRisk", -99.0f)
            << " autoExposureDiagnosticHdrSpreadEv=" << payload.integratedToneLayerJson.value("autoExposureDiagnosticHdrSpreadEv", -99.0f)
            << " autoCandidateSelectedId=" << payload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " autoCandidateSelectedLabel=" << payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", std::string())
            << " autoCandidateSurvivorCount=" << payload.integratedToneLayerJson.value("autoCandidateSurvivorCount", -1)
            << " autoCandidateRejectedCount=" << payload.integratedToneLayerJson.value("autoCandidateRejectedCount", -1)
            << " autoCandidateConvergencePass=" << payload.integratedToneLayerJson.value("autoCandidateConvergencePass", -1)
            << " prepStrengthBefore=" << scenePrepBefore.strength
            << " prepStrengthAfter=" << payload.scenePrepSettings.strength
            << " prepMaxBiasBefore=" << scenePrepBefore.maxEvBias
            << " prepMaxBiasAfter=" << payload.scenePrepSettings.maxEvBias
            << " prepHighlightBiasBefore=" << scenePrepBefore.highlightProtectionBias
            << " prepHighlightBiasAfter=" << payload.scenePrepSettings.highlightProtectionBias
            << " balancedExposure=" << balancedPayload.settings.exposureStops
            << " neutralExposure=" << neutralExposurePayload.settings.exposureStops
            << " biasedExposure=" << biasedExposurePayload.settings.exposureStops
            << " neutralBaseEvBias=" << neutralExposurePayload.scenePrepSettings.baseEvBias
            << " biasedBaseEvBias=" << biasedExposurePayload.scenePrepSettings.baseEvBias
            << " highlightHeavyExposure=" << highlightHeavyPayload.settings.exposureStops
            << " specularExposure=" << specularPayload.settings.exposureStops
            << " underBrightHighlightExposure=" << underBrightHighlightPayload.settings.exposureStops
            << " darkSceneExposure=" << darkScenePayload.settings.exposureStops
            << " darkRawExposureLift=" << darkRawExposureLift
            << " darkScenePrepAmountLift=" << darkScenePrepAmountLift
            << " balancedHighlightThreshold=" << balancedPayload.settings.highlightThreshold
            << " highlightHeavyHighlightThreshold=" << highlightHeavyPayload.settings.highlightThreshold
            << " balancedHighlightBias=" << balancedPayload.scenePrepSettings.highlightProtectionBias
            << " highlightHeavyHighlightBias=" << highlightHeavyPayload.scenePrepSettings.highlightProtectionBias
            << " specularHighlightBias=" << specularPayload.scenePrepSettings.highlightProtectionBias
            << " underBrightHighlightBias=" << underBrightHighlightPayload.scenePrepSettings.highlightProtectionBias
            << " balancedMinEvBias=" << balancedPayload.scenePrepSettings.minEvBias
            << " highlightHeavyMinEvBias=" << highlightHeavyPayload.scenePrepSettings.minEvBias
            << " specularMinEvBias=" << specularPayload.scenePrepSettings.minEvBias
            << " balancedHighlightProtection=" << balancedPayload.scenePrepSettings.highlightProtection
            << " highlightHeavyHighlightProtection=" << highlightHeavyPayload.scenePrepSettings.highlightProtection
            << " darkSceneMaxEvBias=" << darkScenePayload.scenePrepSettings.maxEvBias
            << " darkSceneStrength=" << darkScenePayload.scenePrepSettings.strength
            << " darkSceneShadowLiftLimitBias=" << darkScenePayload.scenePrepSettings.shadowLiftLimitBias
            << " balancedNoiseBias=" << balancedPayload.scenePrepSettings.noiseProtectionBias
            << " noisyNoiseBias=" << noisyLowLightPayload.scenePrepSettings.noiseProtectionBias
            << " balancedMaxEvBias=" << balancedPayload.scenePrepSettings.maxEvBias
            << " flatMaxEvBias=" << flatIntentPayload.scenePrepSettings.maxEvBias
            << " balancedToneContrast=" << balancedPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f)
            << " flatToneContrast=" << flatIntentPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f)
            << " punchyToneContrast=" << punchyIntentPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f)
            << " requestIdBefore=" << requestIdBefore
            << " requestIdAfter=" << payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0))
            << "\n";
    }

    return success;
}

