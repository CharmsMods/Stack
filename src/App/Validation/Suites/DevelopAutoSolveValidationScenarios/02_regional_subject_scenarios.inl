// Included inside DevelopAutoSolveValidation.cpp::ValidateDevelopAutoSolveBehavior().
// Regional dynamic-range evidence, subject intent, subject importance, and subject brush checks.

    const auto guidanceFromToneJson =
        Stack::Validation::Detail::DevelopAutoSolveGuidanceFromToneJson;
    const auto guidanceFromCandidateJson =
        Stack::Validation::Detail::DevelopAutoSolveGuidanceFromCandidateJson;

    const Stack::Validation::Detail::DevelopAutoSolveCandidateProbeSummary candidateProbeSummary =
        Stack::Validation::Detail::BuildDevelopAutoSolveCandidateProbeSummary(
            candidateSolves,
            payload.autoGuidance);
    EditorNodeGraph::DevelopAutoGuidance cleanShadowCandidateGuidance =
        candidateProbeSummary.cleanShadowCandidateGuidance;
    EditorNodeGraph::DevelopAutoGuidance preserveTextureCandidateGuidance =
        candidateProbeSummary.preserveTextureCandidateGuidance;
    EditorNodeGraph::DevelopAutoGuidance highlightProtectedMidsGuidance =
        candidateProbeSummary.highlightProtectedMidsGuidance;
    EditorNodeGraph::DevelopAutoGuidance broadHighlightGuardGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance finishToneProbeGuidance =
        candidateProbeSummary.finishToneProbeGuidance;
    EditorNodeGraph::DevelopAutoGuidance naturalContrastGuardGuidance =
        candidateProbeSummary.naturalContrastGuardGuidance;
    EditorNodeGraph::DevelopAutoGuidance brightHighlightRolloffGuidance =
        candidateProbeSummary.brightHighlightRolloffGuidance;
    EditorNodeGraph::DevelopAutoGuidance luminousHighlightAnchorGuidance =
        candidateProbeSummary.luminousHighlightAnchorGuidance;
    EditorNodeGraph::DevelopAutoGuidance specularHighlightToleranceGuidance =
        candidateProbeSummary.specularHighlightToleranceGuidance;
    EditorNodeGraph::DevelopAutoGuidance haloSafeLocalRangeGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance localRangeGuardGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance shadowReadabilityLiftGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance shadowNoiseFloorGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance whiteBalanceProbeGuidance =
        candidateProbeSummary.whiteBalanceProbeGuidance;
    bool cleanShadowCandidateGenerated = candidateProbeSummary.cleanShadowCandidateGenerated;
    bool preserveTextureCandidateGenerated = candidateProbeSummary.preserveTextureCandidateGenerated;
    bool highlightProtectedMidsGenerated = candidateProbeSummary.highlightProtectedMidsGenerated;
    bool highlightProtectedMidsEligible = candidateProbeSummary.highlightProtectedMidsEligible;
    bool highlightProtectedMidsMeaningfullyDifferent =
        candidateProbeSummary.highlightProtectedMidsMeaningfullyDifferent;
    bool broadHighlightGuardGenerated = false;
    bool broadHighlightGuardEligible = false;
    bool broadHighlightGuardHumanReadable = false;
    bool broadHighlightGuardDiagnosticsWritten = false;
    bool finishToneProbeGenerated = candidateProbeSummary.finishToneProbeGenerated;
    bool finishToneProbeEligible = candidateProbeSummary.finishToneProbeEligible;
    bool finishToneProbeHumanReadable = candidateProbeSummary.finishToneProbeHumanReadable;
    bool finishToneProbeMeaningfullyDifferent =
        candidateProbeSummary.finishToneProbeMeaningfullyDifferent;
    bool naturalContrastGuardGenerated = candidateProbeSummary.naturalContrastGuardGenerated;
    bool naturalContrastGuardEligible = candidateProbeSummary.naturalContrastGuardEligible;
    bool naturalContrastGuardHumanReadable = candidateProbeSummary.naturalContrastGuardHumanReadable;
    bool naturalContrastGuardDiagnosticsWritten =
        candidateProbeSummary.naturalContrastGuardDiagnosticsWritten;
    bool brightHighlightRolloffGenerated = candidateProbeSummary.brightHighlightRolloffGenerated;
    bool brightHighlightRolloffEligible = candidateProbeSummary.brightHighlightRolloffEligible;
    bool brightHighlightRolloffHumanReadable =
        candidateProbeSummary.brightHighlightRolloffHumanReadable;
    bool brightHighlightRolloffDiagnosticsWritten =
        candidateProbeSummary.brightHighlightRolloffDiagnosticsWritten;
    bool luminousHighlightAnchorGenerated = candidateProbeSummary.luminousHighlightAnchorGenerated;
    bool luminousHighlightAnchorEligible = candidateProbeSummary.luminousHighlightAnchorEligible;
    bool luminousHighlightAnchorHumanReadable =
        candidateProbeSummary.luminousHighlightAnchorHumanReadable;
    bool luminousHighlightAnchorDiagnosticsWritten =
        candidateProbeSummary.luminousHighlightAnchorDiagnosticsWritten;
    bool specularHighlightToleranceGenerated =
        candidateProbeSummary.specularHighlightToleranceGenerated;
    bool specularHighlightToleranceEligible =
        candidateProbeSummary.specularHighlightToleranceEligible;
    bool specularHighlightToleranceHumanReadable =
        candidateProbeSummary.specularHighlightToleranceHumanReadable;
    bool specularHighlightToleranceDiagnosticsWritten =
        candidateProbeSummary.specularHighlightToleranceDiagnosticsWritten;
    bool regionalEvidenceDiagnosticsWritten = false;
    bool haloSafeLocalRangeGenerated = false;
    bool haloSafeLocalRangeEligible = false;
    bool haloSafeLocalRangeHumanReadable = false;
    bool haloSafeLocalRangeDiagnosticsWritten = false;
    bool localRangeGuardGenerated = false;
    bool localRangeGuardEligible = false;
    bool localRangeGuardDiagnosticsWritten = false;
    bool shadowReadabilityLiftGenerated = false;
    bool shadowReadabilityLiftEligible = false;
    bool shadowReadabilityLiftHumanReadable = false;
    bool shadowReadabilityLiftDiagnosticsWritten = false;
    bool shadowNoiseFloorGenerated = false;
    bool shadowNoiseFloorEligible = false;
    bool shadowNoiseFloorDiagnosticsWritten = false;
    std::string finishToneProbeId = candidateProbeSummary.finishToneProbeId;
    bool modeNeighborCandidateGenerated = candidateProbeSummary.modeNeighborCandidateGenerated;
    bool modeNeighborCandidateEligible = candidateProbeSummary.modeNeighborCandidateEligible;
    bool modeNeighborCandidateHumanReadable = candidateProbeSummary.modeNeighborCandidateHumanReadable;
    bool modeNeighborCandidateMeaningfullyDifferent =
        candidateProbeSummary.modeNeighborCandidateMeaningfullyDifferent;
    bool whiteBalanceProbeGenerated = candidateProbeSummary.whiteBalanceProbeGenerated;
    bool whiteBalanceProbeEligible = candidateProbeSummary.whiteBalanceProbeEligible;
    bool whiteBalanceProbeHumanReadable = candidateProbeSummary.whiteBalanceProbeHumanReadable;
    bool whiteBalanceProbeDiagnosticsWritten =
        candidateProbeSummary.whiteBalanceProbeDiagnosticsWritten;
    std::string whiteBalanceProbeId = candidateProbeSummary.whiteBalanceProbeId;
    std::string whiteBalanceProbeMode = candidateProbeSummary.whiteBalanceProbeMode;
    EditorNodeGraph::RawDevelopPayload regionalEvidencePayload = payload;
    const std::string regionalRenderedCandidateId =
        selectedCandidateId.empty() ? std::string("base") : selectedCandidateId;
    regionalEvidencePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
    regionalEvidencePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    regionalEvidencePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] =
        nlohmann::json::array({
            {
                { "id", regionalRenderedCandidateId },
                { "label", "Selected Regional Evidence Fixture" },
                { "success", true },
                { "renderScore", 0.62f },
                { "metrics", {
                    { "meanLuma", 0.33f },
                    { "medianLuma", 0.28f },
                    { "p10Luma", 0.05f },
                    { "p90Luma", 0.78f },
                    { "shadowFraction", 0.42f },
                    { "highlightFraction", 0.28f },
                    { "clippedFraction", 0.008f },
                    { "contrastSpan", 0.34f },
                    { "meanRed", 0.34f },
                    { "meanGreen", 0.33f },
                    { "meanBlue", 0.32f },
                    { "warmCoolBias", 0.03f },
                    { "magentaGreenBias", 0.01f },
                    { "channelImbalance", 0.04f },
                    { "colorCastRisk", 0.0f },
                    { "meanSaturation", 0.18f },
                    { "lowSaturationFraction", 0.72f },
                    { "edgeContrast", 0.46f },
                    { "haloRiskFraction", 0.12f },
                    { "shadowTextureRisk", 0.45f },
                    { "localMeanLuma3x3", std::array<float, 9>{ 0.09f, 0.18f, 0.82f, 0.16f, 0.26f, 0.74f, 0.11f, 0.21f, 0.67f } },
                    { "localContrastSpan3x3", std::array<float, 9>{ 0.28f, 0.44f, 0.90f, 0.36f, 0.52f, 0.84f, 0.32f, 0.40f, 0.78f } },
                    { "localDamageRiskScore3x3", std::array<float, 9>{ 0.38f, 0.48f, 0.80f, 0.44f, 0.56f, 0.76f, 0.36f, 0.42f, 0.70f } },
                    { "localLumaSpread", 0.73f },
                    { "localEvSpreadStops", 3.20f },
                    { "localEvConflict", 0.68f },
                    { "localContrastPeak", 0.90f },
                    { "localShadowPressure", 0.66f },
                    { "localHighlightPressure", 0.74f },
                    { "localDamageRiskMean", 0.54f },
                    { "localDamageRiskPeak", 0.80f },
                    { "localDamageRiskPeakTile", 2 },
                    { "localExposureHighlightCrowding", 0.62f },
                    { "localExposureShadowCrowding", 0.58f },
                    { "localExposureHaloStress", 0.64f },
                    { "localExposureFlatnessRisk", 0.42f },
                    { "localExposureDamageRisk", 0.60f },
                    { "centerMeanLuma", 0.26f },
                    { "centerShadowFraction", 0.44f },
                    { "centerHighlightFraction", 0.32f },
                    { "subjectCenterPrior", 0.74f },
                    { "subjectReadabilityPressure", 0.44f },
                    { "subjectProtectionPressure", 0.22f },
                    { "subjectMoodPreservationPressure", 0.18f },
                    { "subjectImportanceConfidence", 0.68f }
                } }
            }
        });
    EditorModule::ApplyDevelopAutoSolve(regionalEvidencePayload, metadata, true);
    const nlohmann::json regionalEvidence =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeRegionEvidence",
            nlohmann::json::object());
    regionalEvidenceDiagnosticsWritten =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeRegionEvidenceVersion",
            std::string()) == "DynamicRangeRegionEvidenceV1" &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeRegionEvidenceValid",
            false) &&
        regionalEvidence.value("version", std::string()) == "DynamicRangeRegionEvidenceV1" &&
        regionalEvidence.value("source", std::string()) == "selectedRenderedCandidate" &&
        regionalEvidence.value("localEvSpreadStops", 0.0f) > 1.0f &&
        regionalEvidence.value("localEvConflict", 0.0f) > 0.20f &&
        regionalEvidence.value("subjectCenterPrior", 0.0f) > 0.50f &&
        regionalEvidence.value("subjectReadabilityPressure", 0.0f) > 0.30f &&
        regionalEvidence.value("subjectImportanceConfidence", 0.0f) > 0.50f &&
        regionalEvidence.value("localExposureHighlightCrowding", 0.0f) > 0.40f &&
        regionalEvidence.value("localExposureShadowCrowding", 0.0f) > 0.30f &&
        regionalEvidence.value("localExposureHaloStress", 0.0f) > 0.40f &&
        regionalEvidence.value("localExposureDamageRisk", 0.0f) > 0.35f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalHighlightHotspotRisk",
            -1.0f) > 0.20f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalRangeConflict",
            -1.0f) > 0.20f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalEvConflict",
            -1.0f) > 0.20f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalExposureDamageRisk",
            -1.0f) > 0.35f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalExposureHaloStress",
            -1.0f) > 0.40f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalEvSpreadStops",
            -1.0f) > 1.0f;
    const nlohmann::json regionalCandidateSolves =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (regionalCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : regionalCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "haloSafeLocalRange") {
                continue;
            }
            haloSafeLocalRangeGenerated = true;
            const std::string status = candidate.value("status", std::string());
            haloSafeLocalRangeEligible =
                haloSafeLocalRangeEligible ||
                status == "selected" ||
                status == "survivor";
            haloSafeLocalRangeGuidance =
                guidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    regionalEvidencePayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            haloSafeLocalRangeHumanReadable =
                haloSafeLocalRangeHumanReadable ||
                (label.find("Halo") != std::string::npos &&
                 label.find("Local") != std::string::npos &&
                 reason.find("halo") != std::string::npos &&
                 reason.find("smooth-gradient") != std::string::npos);
            haloSafeLocalRangeDiagnosticsWritten =
                haloSafeLocalRangeDiagnosticsWritten ||
                (signals.value("localHaloSafetySignal", -1.0f) > 0.28f &&
                 dimensions.value("localHaloSafety", -1.0f) > 0.54f &&
                 dimensions.value("localArtifactSafety", -1.0f) > 0.40f &&
                 changes.value("dynamicRangeDelta", 0.0f) < -0.04f &&
                 changes.value("highlightGuardDelta", 0.0f) > 0.08f);
        }
        for (const nlohmann::json& candidate : regionalCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "localRangeGuard") {
                continue;
            }
            localRangeGuardGenerated = true;
            const std::string status = candidate.value("status", std::string());
            localRangeGuardEligible =
                localRangeGuardEligible ||
                status == "selected" ||
                status == "survivor";
            localRangeGuardGuidance =
                guidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    regionalEvidencePayload.autoGuidance);
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            localRangeGuardDiagnosticsWritten =
                localRangeGuardDiagnosticsWritten ||
                signals.value("regionEvidenceValid", false) &&
                    signals.value("localRangeConflict", 0.0f) > 0.20f &&
                    signals.value("localEvConflict", 0.0f) > 0.20f &&
                    signals.value("localEvSpreadStops", 0.0f) > 1.0f &&
                    risks.value("localRangeConflict", 0.0f) > 0.20f &&
                    risks.value("localEvConflict", 0.0f) > 0.20f;
        }
        for (const nlohmann::json& candidate : regionalCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "shadowNoiseFloor") {
                continue;
            }
            shadowNoiseFloorGenerated = true;
            const std::string status = candidate.value("status", std::string());
            shadowNoiseFloorEligible =
                shadowNoiseFloorEligible ||
                status == "selected" ||
                status == "survivor";
            shadowNoiseFloorGuidance =
                guidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    regionalEvidencePayload.autoGuidance);
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            shadowNoiseFloorDiagnosticsWritten =
                shadowNoiseFloorDiagnosticsWritten ||
                signals.value("regionEvidenceValid", false) &&
                    signals.value("shadowNoiseLiftRisk", 0.0f) > 0.20f &&
                    dimensions.value("shadowCleanliness", 0.0f) > 0.50f &&
                    risks.value("shadowNoiseRisk", 0.0f) > 0.20f;
        }
    }
    const nlohmann::json subjectSceneIntent =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    const bool subjectSceneIntentDiagnosticsWritten =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntentVersion",
            std::string()) == "SubjectSceneIntentV1" &&
        subjectSceneIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneBrushStatus",
            std::string()) == "deferred" &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneAutomaticOnly",
            false) &&
        !regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            true) &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneAutomaticConfidence",
            0.0f) > 0.50f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneCenterPrior",
            0.0f) > 0.50f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneReadabilityPressure",
            0.0f) > 0.30f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneSubjectPriority",
            0.0f) > 0.50f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImproveReadability",
            0.0f) > 0.45f &&
        std::abs(regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneSubjectSceneAxis",
            99.0f)) <= 1.0f &&
        std::abs(regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneMoodReadabilityAxis",
            99.0f)) <= 1.0f;
    bool subjectSceneIntentScoreComponentsWritten = false;
    bool subjectSceneIntentBiasesScoring = false;
    if (regionalCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : regionalCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            const bool hasSubjectScoreShape =
                candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "notAvailable" &&
                signals.value("subjectAutomaticConfidence", 0.0f) > 0.50f &&
                signals.value("subjectReadabilityPressure", 0.0f) > 0.30f &&
                dimensions.contains("subjectPriorityFit") &&
                dimensions.contains("subjectReadabilityFit") &&
                dimensions.contains("subjectProtectionFit") &&
                dimensions.contains("subjectMoodFit") &&
                risks.contains("subjectOverLiftRisk") &&
                risks.contains("subjectProtectionTradeoffRisk");
            subjectSceneIntentScoreComponentsWritten =
                subjectSceneIntentScoreComponentsWritten || hasSubjectScoreShape;
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            subjectSceneIntentBiasesScoring =
                subjectSceneIntentBiasesScoring ||
                (eligible &&
                 hasSubjectScoreShape &&
                (signals.value("subjectReadabilityBias", 0.0f) > 0.02f ||
                  dimensions.value("subjectReadabilityFit", 0.0f) > 0.55f ||
                  dimensions.value("subjectPriorityFit", 0.0f) > 0.55f));
        }
    }
    EditorNodeGraph::RawDevelopPayload userSubjectIntentPayload = regionalEvidencePayload;
    userSubjectIntentPayload.autoGuidance.subjectSceneBias = 0.68f;
    userSubjectIntentPayload.autoGuidance.moodReadabilityBias = 0.46f;
    EditorModule::ApplyDevelopAutoSolve(userSubjectIntentPayload, metadata, true);
    const nlohmann::json userSubjectSceneIntent =
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    const bool userSubjectSceneIntentDiagnosticsWritten =
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntentVersion",
            std::string()) == "SubjectSceneIntentV1" &&
        userSubjectSceneIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        userSubjectSceneIntent.value("userGuidanceStatus", std::string()) == "intentControls" &&
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStatus",
            std::string()) == "intentControls" &&
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            false) &&
        !userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneAutomaticOnly",
            true) &&
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStrength",
            0.0f) > 0.60f &&
        std::abs(userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserSubjectSceneBias",
            -99.0f) - userSubjectIntentPayload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserMoodReadabilityBias",
            -99.0f) - userSubjectIntentPayload.autoGuidance.moodReadabilityBias) < 0.0001f &&
        std::abs(userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoRequestedSubjectSceneBias",
            -99.0f) - userSubjectIntentPayload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoRequestedMoodReadabilityBias",
            -99.0f) - userSubjectIntentPayload.autoGuidance.moodReadabilityBias) < 0.0001f &&
        userSubjectSceneIntent.value("id", std::string()).find("userGuided") == 0;
    bool userSubjectSceneIntentScoreComponentsWritten = false;
    const nlohmann::json userSubjectCandidateSolves =
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    bool subjectReadableMidsGenerated = false;
    bool subjectReadableMidsEligible = false;
    bool subjectReadableMidsHumanReadable = false;
    bool subjectReadableMidsDiagnosticsWritten = false;
    EditorNodeGraph::DevelopAutoGuidance subjectReadableMidsGuidance =
        userSubjectIntentPayload.autoGuidance;
    if (userSubjectCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : userSubjectCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            const std::string status = candidate.value("status", std::string());
            const nlohmann::json candidateGuidance =
                candidate.value("guidance", nlohmann::json::object());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            userSubjectSceneIntentScoreComponentsWritten =
                userSubjectSceneIntentScoreComponentsWritten ||
                (candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                 candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "intentControls" &&
                 candidateSubjectIntent.value("userGuidanceActive", false) &&
                 candidateSubjectIntent.value("userGuidanceStrength", 0.0f) > 0.60f &&
                 signals.value("subjectUserGuidanceStrength", 0.0f) > 0.60f &&
                 signals.value("subjectUserSubjectSceneBias", 0.0f) > 0.60f &&
                 signals.value("subjectUserMoodReadabilityBias", 0.0f) > 0.40f &&
                 std::abs(candidateGuidance.value("subjectSceneBias", -99.0f) -
                     userSubjectIntentPayload.autoGuidance.subjectSceneBias) < 0.0001f &&
                 std::abs(candidateGuidance.value("moodReadabilityBias", -99.0f) -
                     userSubjectIntentPayload.autoGuidance.moodReadabilityBias) < 0.0001f);
            if (id == "subjectReadableMids") {
                subjectReadableMidsGenerated = true;
                subjectReadableMidsEligible =
                    subjectReadableMidsEligible ||
                    status == "selected" ||
                    status == "survivor";
                subjectReadableMidsHumanReadable =
                    candidate.value("label", std::string()).find("Subject") != std::string::npos &&
                    candidate.value("reason", std::string()).find("subject-priority") != std::string::npos;
                subjectReadableMidsGuidance =
                    guidanceFromCandidateJson(candidateGuidance, userSubjectIntentPayload.autoGuidance);
                subjectReadableMidsDiagnosticsWritten =
                    subjectReadableMidsDiagnosticsWritten ||
                    candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                    candidateSubjectIntent.value("userGuidanceActive", false) &&
                    signals.value("subjectUserSubjectSceneBias", 0.0f) > 0.60f &&
                    signals.value("subjectUserMoodReadabilityBias", 0.0f) > 0.40f &&
                    dimensions.value("subjectReadabilityFit", 0.0f) > 0.58f &&
                    dimensions.value("subjectPriorityFit", 0.0f) > 0.56f &&
                    risks.contains("subjectOverLiftRisk");
            }
        }
    }
    EditorNodeGraph::RawDevelopPayload subjectImportancePayload = regionalEvidencePayload;
    subjectImportancePayload.subjectImportance.enabled = true;
    subjectImportancePayload.subjectImportance.showOverlay = true;
    subjectImportancePayload.subjectImportance.overlayOpacity = 0.42f;
    subjectImportancePayload.subjectImportance.nextRegionId = 8;
    EditorNodeGraph::DevelopSubjectImportanceRegion subjectRegion;
    subjectRegion.id = 7;
    subjectRegion.mode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    subjectRegion.enabled = true;
    subjectRegion.centerX = 0.46f;
    subjectRegion.centerY = 0.54f;
    subjectRegion.radiusX = 0.28f;
    subjectRegion.radiusY = 0.22f;
    subjectRegion.feather = 0.40f;
    subjectRegion.strength = 0.86f;
    subjectImportancePayload.subjectImportance.regions.push_back(subjectRegion);
    EditorModule::ApplyDevelopAutoSolve(subjectImportancePayload, metadata, true);
    const nlohmann::json subjectImportanceIntent =
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    const nlohmann::json subjectImportanceSolveNotes =
        subjectImportanceIntent.value("solveNotes", nlohmann::json::array());
    const std::uint64_t subjectImportanceFingerprintA =
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    EditorNodeGraph::RawDevelopPayload subjectImportanceChangedPayload = subjectImportancePayload;
    subjectImportanceChangedPayload.subjectImportance.regions[0].strength = 0.28f;
    EditorModule::ApplyDevelopAutoSolve(subjectImportanceChangedPayload, metadata, true);
    const std::uint64_t subjectImportanceFingerprintB =
        subjectImportanceChangedPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    EditorNodeGraph::RawDevelopPayload subjectImportanceVisualPayload = regionalEvidencePayload;
    subjectImportanceVisualPayload.subjectImportance = subjectImportancePayload.subjectImportance;
    subjectImportanceVisualPayload.subjectImportance.showOverlay = false;
    subjectImportanceVisualPayload.subjectImportance.overlayOpacity = 0.93f;
    subjectImportanceVisualPayload.subjectImportance.showInterpretedMapOverlay = true;
    subjectImportanceVisualPayload.subjectImportance.interpretedMapOpacity = 0.81f;
    subjectImportanceVisualPayload.subjectImportance.showRefinedMapOverlay = true;
    subjectImportanceVisualPayload.subjectImportance.refinedMapOpacity = 0.76f;
    EditorModule::ApplyDevelopAutoSolve(subjectImportanceVisualPayload, metadata, true);
    const std::uint64_t subjectImportanceVisualFingerprint =
        subjectImportanceVisualPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    bool subjectImportanceScoreComponentsWritten = false;
    const nlohmann::json subjectImportanceCandidateSolves =
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (subjectImportanceCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : subjectImportanceCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json candidateImportanceMap =
                candidateSubjectIntent.value("importanceMap", nlohmann::json::object());
            const nlohmann::json candidateRefinedMap =
                candidateSubjectIntent.value("refinedImportanceMap", nlohmann::json::object());
            const nlohmann::json candidateSolveNotes =
                candidateSubjectIntent.value("solveNotes", nlohmann::json::array());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            subjectImportanceScoreComponentsWritten =
                subjectImportanceScoreComponentsWritten ||
                (candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                 candidateSubjectIntent.value("solveNotesVersion", std::string()) == "SubjectImportanceSolveNotesV1" &&
                 candidateSolveNotes.is_array() &&
                 !candidateSolveNotes.empty() &&
                 candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "importanceRegions" &&
                 candidateImportanceMap.value("version", std::string()) == "SubjectImportanceMapV1" &&
                 candidateImportanceMap.value("status", std::string()) == "interpretedUserMarks" &&
                 candidateRefinedMap.value("version", std::string()) == "SubjectRefinedMapV1" &&
                 candidateRefinedMap.value("status", std::string()) == "refinedUserMarks" &&
                 signals.value("subjectImportanceRegionCount", 0) == 1 &&
                 signals.value("subjectImportanceReveal", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapRevealCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapConfidence", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapReadabilityCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapConfidence", 0.0f) > 0.0f);
        }
    }
    const bool subjectImportanceGuidanceDiagnosticsWritten =
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntentVersion",
            std::string()) == "SubjectSceneIntentV1" &&
        subjectImportanceIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        subjectImportanceIntent.value("solveNotesVersion", std::string()) == "SubjectImportanceSolveNotesV1" &&
        subjectImportanceSolveNotes.is_array() &&
        !subjectImportanceSolveNotes.empty() &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneSolveNotesVersion",
            std::string()) == "SubjectImportanceSolveNotesV1" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneSolveNoteCount",
            0) > 0 &&
        !subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectScenePrimarySolveNote",
            std::string()).empty() &&
        subjectImportanceIntent.value("userGuidanceStatus", std::string()) == "importanceRegions" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneBrushStatus",
            std::string()) == "regionGuidanceActive" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStatus",
            std::string()) == "importanceRegions" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            false) &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceRegionCount",
            0) == 1 &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceRegionCount",
            0) == 1 &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrokeCount",
            -1) == 0 &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceStrokeCount",
            -1) == 0 &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceReveal",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrength",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapVersion",
            std::string()) == "SubjectImportanceMapV1" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapStatus",
            std::string()) == "interpretedUserMarks" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapActive",
            false) &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapCoverage",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapRevealCoverage",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapConfidence",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapVersion",
            std::string()) == "SubjectRefinedMapV1" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapStatus",
            std::string()) == "refinedUserMarks" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapActive",
            false) &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapCoverage",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapReadabilityCoverage",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapConfidence",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceMapVersion",
            std::string()) == "SubjectImportanceMapV1" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceMapCoverage",
            0.0f) > 0.0f &&
        subjectImportanceFingerprintA != 0 &&
        subjectImportanceFingerprintB != 0 &&
        subjectImportanceFingerprintA != subjectImportanceFingerprintB &&
        subjectImportanceVisualFingerprint == subjectImportanceFingerprintA &&
        subjectImportanceScoreComponentsWritten;
    EditorNodeGraph::RawDevelopPayload subjectBrushPayload = regionalEvidencePayload;
    subjectBrushPayload.subjectImportance.enabled = true;
    subjectBrushPayload.subjectImportance.showOverlay = true;
    subjectBrushPayload.subjectImportance.brushEnabled = true;
    subjectBrushPayload.subjectImportance.brushMode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    subjectBrushPayload.subjectImportance.brushRadius = 0.052f;
    subjectBrushPayload.subjectImportance.brushFeather = 0.42f;
    subjectBrushPayload.subjectImportance.brushStrength = 0.88f;
    subjectBrushPayload.subjectImportance.activeStrokeId = 11;
    subjectBrushPayload.subjectImportance.nextStrokeId = 12;
    EditorNodeGraph::DevelopSubjectImportanceStroke subjectStroke;
    subjectStroke.id = 11;
    subjectStroke.mode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    subjectStroke.enabled = true;
    subjectStroke.subtract = false;
    subjectStroke.radius = 0.052f;
    subjectStroke.feather = 0.42f;
    subjectStroke.strength = 0.88f;
    subjectStroke.points.push_back({ 0.35f, 0.44f });
    subjectStroke.points.push_back({ 0.44f, 0.51f });
    subjectStroke.points.push_back({ 0.56f, 0.58f });
    subjectBrushPayload.subjectImportance.strokes.push_back(subjectStroke);
    EditorModule::ApplyDevelopAutoSolve(subjectBrushPayload, metadata, true);
    const nlohmann::json subjectBrushIntent =
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    const std::uint64_t subjectBrushFingerprintA =
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    EditorNodeGraph::RawDevelopPayload subjectBrushChangedPayload = subjectBrushPayload;
    subjectBrushChangedPayload.subjectImportance.strokes[0].points[1].x = 0.64f;
    EditorModule::ApplyDevelopAutoSolve(subjectBrushChangedPayload, metadata, true);
    const std::uint64_t subjectBrushFingerprintB =
        subjectBrushChangedPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    bool subjectBrushScoreComponentsWritten = false;
    const nlohmann::json subjectBrushCandidateSolves =
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (subjectBrushCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : subjectBrushCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json candidateImportanceMap =
                candidateSubjectIntent.value("importanceMap", nlohmann::json::object());
            const nlohmann::json candidateRefinedMap =
                candidateSubjectIntent.value("refinedImportanceMap", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            subjectBrushScoreComponentsWritten =
                subjectBrushScoreComponentsWritten ||
                (candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                 candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "importanceBrush" &&
                 candidateImportanceMap.value("version", std::string()) == "SubjectImportanceMapV1" &&
                 candidateImportanceMap.value("status", std::string()) == "interpretedUserMarks" &&
                 candidateRefinedMap.value("version", std::string()) == "SubjectRefinedMapV1" &&
                 candidateRefinedMap.value("status", std::string()) == "refinedUserMarks" &&
                 signals.value("subjectImportanceStrokeCount", 0) == 1 &&
                 signals.value("subjectImportanceReveal", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapRevealCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapConfidence", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapConfidence", 0.0f) > 0.0f);
        }
    }
    const bool subjectBrushGuidanceDiagnosticsWritten =
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntentVersion",
            std::string()) == "SubjectSceneIntentV1" &&
        subjectBrushIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        subjectBrushIntent.value("userGuidanceStatus", std::string()) == "importanceBrush" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneBrushStatus",
            std::string()) == "brushStrokesActiveEdgeRefineDeferred" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStatus",
            std::string()) == "importanceBrush" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            false) &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceRegionCount",
            -1) == 0 &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrokeCount",
            0) == 1 &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceRegionCount",
            -1) == 0 &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceStrokeCount",
            0) == 1 &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceReveal",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrength",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapVersion",
            std::string()) == "SubjectImportanceMapV1" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapStatus",
            std::string()) == "interpretedUserMarks" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapCoverage",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapPeak",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapConfidence",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapVersion",
            std::string()) == "SubjectRefinedMapV1" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapStatus",
            std::string()) == "refinedUserMarks" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapCoverage",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapConfidence",
            0.0f) > 0.0f &&
        subjectBrushFingerprintA != 0 &&
        subjectBrushFingerprintB != 0 &&
        subjectBrushFingerprintA != subjectBrushFingerprintB &&
        subjectBrushScoreComponentsWritten;
    EditorNodeGraph::RawDevelopPayload subjectBrushDisabledPayload = regionalEvidencePayload;
    subjectBrushDisabledPayload.subjectImportance = subjectBrushPayload.subjectImportance;
    subjectBrushDisabledPayload.subjectImportance.strokes[0].enabled = false;
    EditorModule::ApplyDevelopAutoSolve(subjectBrushDisabledPayload, metadata, true);
    const std::uint64_t subjectBrushDisabledFingerprint =
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    const bool subjectBrushDisabledIgnored =
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrokeCount",
            -1) == 0 &&
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceStrokeCount",
            -1) == 0 &&
        !subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            true) &&
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStatus",
            std::string()) != "importanceBrush" &&
        !subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapActive",
            true) &&
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapCoverage",
            1.0f) <= 0.001f &&
        subjectBrushDisabledFingerprint != 0 &&
        subjectBrushDisabledFingerprint != subjectBrushFingerprintA;

    EditorNodeGraph::RawDevelopPayload subjectBrushReducePayload = regionalEvidencePayload;
    subjectBrushReducePayload.subjectImportance = subjectBrushPayload.subjectImportance;
    subjectBrushReducePayload.subjectImportance.strokes[0].subtract = true;
    subjectBrushReducePayload.subjectImportance.strokes[0].mode =
        EditorNodeGraph::DevelopSubjectImportanceMode::Ignore;
    EditorModule::ApplyDevelopAutoSolve(subjectBrushReducePayload, metadata, true);
    const nlohmann::json subjectBrushReduceIntent =
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    bool subjectBrushReduceScoreComponentsWritten = false;
    const nlohmann::json subjectBrushReduceCandidateSolves =
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (subjectBrushReduceCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : subjectBrushReduceCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json candidateImportanceMap =
                candidateSubjectIntent.value("importanceMap", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            subjectBrushReduceScoreComponentsWritten =
                subjectBrushReduceScoreComponentsWritten ||
                (candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                 candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "importanceBrush" &&
                 candidateImportanceMap.value("version", std::string()) == "SubjectImportanceMapV1" &&
                 candidateImportanceMap.value("status", std::string()) == "interpretedUserMarks" &&
                 signals.value("subjectImportanceStrokeCount", 0) == 1 &&
                 signals.value("subjectImportanceIgnore", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapLowPriorityCoverage", 0.0f) > 0.0f);
        }
    }
    const bool subjectBrushReduceGuidanceDiagnosticsWritten =
        subjectBrushReduceIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        subjectBrushReduceIntent.value("userGuidanceStatus", std::string()) == "importanceBrush" &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneBrushStatus",
            std::string()) == "brushStrokesActiveEdgeRefineDeferred" &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            false) &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrokeCount",
            0) == 1 &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceStrokeCount",
            0) == 1 &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceIgnore",
            0.0f) > 0.0f &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStrength",
            0.0f) > 0.0f &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapVersion",
            std::string()) == "SubjectImportanceMapV1" &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapStatus",
            std::string()) == "interpretedUserMarks" &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapLowPriorityCoverage",
            0.0f) > 0.0f &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapConfidence",
            0.0f) > 0.0f &&
        subjectBrushReduceScoreComponentsWritten;
    EditorNodeGraph::RawDevelopPayload sceneMoodIntentPayload = regionalEvidencePayload;
    sceneMoodIntentPayload.autoGuidance.subjectSceneBias = -0.62f;
    sceneMoodIntentPayload.autoGuidance.moodReadabilityBias = -0.56f;
    EditorModule::ApplyDevelopAutoSolve(sceneMoodIntentPayload, metadata, true);
    const nlohmann::json sceneMoodCandidateSolves =
        sceneMoodIntentPayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    bool sceneMoodPreservationGenerated = false;
    bool sceneMoodPreservationEligible = false;
    bool sceneMoodPreservationHumanReadable = false;
    bool sceneMoodPreservationDiagnosticsWritten = false;
    EditorNodeGraph::DevelopAutoGuidance sceneMoodPreservationGuidance =
        sceneMoodIntentPayload.autoGuidance;
    if (sceneMoodCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : sceneMoodCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "sceneMoodPreservation") {
                continue;
            }
            sceneMoodPreservationGenerated = true;
            const std::string status = candidate.value("status", std::string());
            sceneMoodPreservationEligible =
                sceneMoodPreservationEligible ||
                status == "selected" ||
                status == "survivor";
            sceneMoodPreservationHumanReadable =
                candidate.value("label", std::string()).find("Mood") != std::string::npos &&
                candidate.value("reason", std::string()).find("scene-integrity") != std::string::npos;
            sceneMoodPreservationGuidance =
                guidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    sceneMoodIntentPayload.autoGuidance);
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            sceneMoodPreservationDiagnosticsWritten =
                sceneMoodPreservationDiagnosticsWritten ||
                candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "intentControls" &&
                candidateSubjectIntent.value("userGuidanceActive", false) &&
                signals.value("subjectUserSubjectSceneBias", 0.0f) < -0.50f &&
                signals.value("subjectUserMoodReadabilityBias", 0.0f) < -0.45f &&
                dimensions.value("subjectMoodFit", 0.0f) > 0.58f;
        }
    }
    RenderGraphRawDevelopPayload baseCandidateRenderPayload;
    baseCandidateRenderPayload.settings = payload.settings;
    baseCandidateRenderPayload.scenePrepEnabled = payload.scenePrepEnabled;
    baseCandidateRenderPayload.scenePrepSettings = payload.scenePrepSettings;
    baseCandidateRenderPayload.integratedToneEnabled = payload.integratedToneEnabled;
    baseCandidateRenderPayload.integratedToneLayerJson = payload.integratedToneLayerJson;
    EditorNodeGraph::DevelopAutoGuidance currentRenderedGuidance =
        guidanceFromToneJson(payload.integratedToneLayerJson, payload.autoGuidance);
    currentRenderedGuidance.intent = payload.autoGuidance.intent;
    RenderGraphRawDevelopPayload regionalBaseCandidateRenderPayload;
    regionalBaseCandidateRenderPayload.settings = regionalEvidencePayload.settings;
    regionalBaseCandidateRenderPayload.scenePrepEnabled = regionalEvidencePayload.scenePrepEnabled;
    regionalBaseCandidateRenderPayload.scenePrepSettings = regionalEvidencePayload.scenePrepSettings;
    regionalBaseCandidateRenderPayload.integratedToneEnabled = regionalEvidencePayload.integratedToneEnabled;
    regionalBaseCandidateRenderPayload.integratedToneLayerJson =
        regionalEvidencePayload.integratedToneLayerJson;
    EditorNodeGraph::DevelopAutoGuidance currentRegionalRenderedGuidance =
        guidanceFromToneJson(
            regionalEvidencePayload.integratedToneLayerJson,
            regionalEvidencePayload.autoGuidance);
    currentRegionalRenderedGuidance.intent = regionalEvidencePayload.autoGuidance.intent;
    RenderGraphRawDevelopPayload userSubjectBaseCandidateRenderPayload;
    userSubjectBaseCandidateRenderPayload.settings = userSubjectIntentPayload.settings;
    userSubjectBaseCandidateRenderPayload.scenePrepEnabled = userSubjectIntentPayload.scenePrepEnabled;
    userSubjectBaseCandidateRenderPayload.scenePrepSettings = userSubjectIntentPayload.scenePrepSettings;
    userSubjectBaseCandidateRenderPayload.integratedToneEnabled = userSubjectIntentPayload.integratedToneEnabled;
    userSubjectBaseCandidateRenderPayload.integratedToneLayerJson =
        userSubjectIntentPayload.integratedToneLayerJson;
    EditorNodeGraph::DevelopAutoGuidance currentUserSubjectRenderedGuidance =
        guidanceFromToneJson(
            userSubjectIntentPayload.integratedToneLayerJson,
            userSubjectIntentPayload.autoGuidance);
    currentUserSubjectRenderedGuidance.intent = userSubjectIntentPayload.autoGuidance.intent;
    RenderGraphRawDevelopPayload sceneMoodBaseCandidateRenderPayload;
    sceneMoodBaseCandidateRenderPayload.settings = sceneMoodIntentPayload.settings;
    sceneMoodBaseCandidateRenderPayload.scenePrepEnabled = sceneMoodIntentPayload.scenePrepEnabled;
    sceneMoodBaseCandidateRenderPayload.scenePrepSettings = sceneMoodIntentPayload.scenePrepSettings;
    sceneMoodBaseCandidateRenderPayload.integratedToneEnabled = sceneMoodIntentPayload.integratedToneEnabled;
    sceneMoodBaseCandidateRenderPayload.integratedToneLayerJson =
        sceneMoodIntentPayload.integratedToneLayerJson;
    EditorNodeGraph::DevelopAutoGuidance currentSceneMoodRenderedGuidance =
        guidanceFromToneJson(
            sceneMoodIntentPayload.integratedToneLayerJson,
            sceneMoodIntentPayload.autoGuidance);
    currentSceneMoodRenderedGuidance.intent = sceneMoodIntentPayload.autoGuidance.intent;
