// Included inside DevelopAutoSolveValidation.cpp::ValidateDevelopAutoSolveBehavior().
// Partial solve stability, balanced profiles, dynamic-range profiles, and intent profile checks.

    const Raw::RawDevelopSettings authoredAfterFullSolve = payload.settings;
    const Raw::RawDetailFusionSettings prepAfterFullSolve = payload.scenePrepSettings;
    payload.autoGuidance.contrastBias = 0.58f;
    const std::uint64_t partialRequestIdBefore =
        payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));
    EditorModule::ApplyDevelopAutoSolve(payload, metadata, true, false);
    const bool partialSolvePreservedRawAuthorship =
        std::abs(payload.settings.exposureStops - authoredAfterFullSolve.exposureStops) < 0.0001f &&
        payload.settings.highlightMode == authoredAfterFullSolve.highlightMode &&
        std::abs(payload.settings.highlightStrength - authoredAfterFullSolve.highlightStrength) < 0.0001f &&
        payload.settings.mosaicDenoise.enabled == authoredAfterFullSolve.mosaicDenoise.enabled &&
        std::abs(payload.settings.mosaicDenoise.lumaStrength - authoredAfterFullSolve.mosaicDenoise.lumaStrength) < 0.0001f;
    const bool partialSolveUpdatedPrepAndFinish =
        std::abs(payload.scenePrepSettings.wellExposedTargetBias - prepAfterFullSolve.wellExposedTargetBias) > 0.001f &&
        std::abs(payload.scenePrepSettings.detailWeight - prepAfterFullSolve.detailWeight) > 0.001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedContrastBias", -99.0f) - payload.autoGuidance.contrastBias) < 0.0001f &&
        payload.integratedToneLayerJson.value("autoCalibratePending", false) &&
        payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0)) > partialRequestIdBefore;

    EditorNodeGraph::RawDevelopPayload balancedPayload;
    balancedPayload.integratedToneEnabled = true;
    balancedPayload.scenePrepEnabled = true;
    balancedPayload.integratedToneLayerJson = payload.integratedToneLayerJson;
    balancedPayload.autoGuidance = payload.autoGuidance;
    balancedPayload.autoGuidance.autoStrength = 1.05f;
    balancedPayload.autoGuidance.dynamicRange = 1.20f;
    balancedPayload.autoGuidance.shadowLift = 0.08f;
    balancedPayload.autoGuidance.highlightGuard = 0.10f;
    balancedPayload.autoGuidance.highlightCharacter = -0.05f;
    balancedPayload.autoGuidance.contrastBias = 0.06f;
    balancedPayload.integratedToneLayerJson["autoSceneStatsValid"] = true;
    balancedPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.05f;
    balancedPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.18f;
    balancedPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.80f;
    balancedPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.001f;
    balancedPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.16f;
    balancedPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.24f;
    balancedPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.68f;
    balancedPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.7f;
    balancedPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    balancedPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.06f;
    balancedPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.04f;
    balancedPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.10f;
    balancedPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.08f;
    EditorModule::ApplyDevelopAutoSolve(balancedPayload, metadata, true);
    const Raw::RawDevelopSettings balancedFirstSettings = balancedPayload.settings;
    const Raw::RawDetailFusionSettings balancedFirstPrep = balancedPayload.scenePrepSettings;
    EditorModule::ApplyDevelopAutoSolve(balancedPayload, metadata, true);
    const bool repeatedFullSolveStable =
        std::abs(balancedPayload.settings.exposureStops - balancedFirstSettings.exposureStops) < 0.0001f &&
        balancedPayload.settings.highlightMode == balancedFirstSettings.highlightMode &&
        std::abs(balancedPayload.settings.highlightStrength - balancedFirstSettings.highlightStrength) < 0.0001f &&
        std::abs(balancedPayload.settings.highlightThreshold - balancedFirstSettings.highlightThreshold) < 0.0001f &&
        balancedPayload.settings.mosaicDenoise.enabled == balancedFirstSettings.mosaicDenoise.enabled &&
        std::abs(balancedPayload.settings.mosaicDenoise.lumaStrength - balancedFirstSettings.mosaicDenoise.lumaStrength) < 0.0001f &&
        std::abs(balancedPayload.scenePrepSettings.baseEvBias - balancedFirstPrep.baseEvBias) < 0.0001f &&
        std::abs(balancedPayload.scenePrepSettings.maxEvBias - balancedFirstPrep.maxEvBias) < 0.0001f &&
        std::abs(balancedPayload.scenePrepSettings.highlightProtectionBias - balancedFirstPrep.highlightProtectionBias) < 0.0001f;

    EditorNodeGraph::RawDevelopPayload neutralExposurePayload = balancedPayload;
    neutralExposurePayload.autoGuidance.exposureBias = 0.0f;
    EditorModule::ApplyDevelopAutoSolve(neutralExposurePayload, metadata, true);
    EditorNodeGraph::RawDevelopPayload biasedExposurePayload = balancedPayload;
    biasedExposurePayload.autoGuidance.exposureBias = 0.65f;
    EditorModule::ApplyDevelopAutoSolve(biasedExposurePayload, metadata, true);
    const bool positiveExposureBiasPreserved =
        biasedExposurePayload.settings.exposureStops > neutralExposurePayload.settings.exposureStops + 0.80f &&
        biasedExposurePayload.scenePrepSettings.baseEvBias > neutralExposurePayload.scenePrepSettings.baseEvBias + 0.20f;

    EditorNodeGraph::RawDevelopPayload highlightHeavyPayload = balancedPayload;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.02f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.11f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.96f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.018f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.24f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.84f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.62f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 5.8f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneProfile"] = 1;
    highlightHeavyPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.42f;
    highlightHeavyPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.18f;
    highlightHeavyPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.22f;
    highlightHeavyPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.40f;
    EditorModule::ApplyDevelopAutoSolve(highlightHeavyPayload, metadata, true);
    const nlohmann::json& highlightHeavyCandidateSolves =
        highlightHeavyPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (highlightHeavyCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : highlightHeavyCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "broadHighlightGuard") {
                continue;
            }
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            broadHighlightGuardGenerated = true;
            broadHighlightGuardEligible = broadHighlightGuardEligible || eligible;
            broadHighlightGuardGuidance =
                guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), highlightHeavyPayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            broadHighlightGuardHumanReadable =
                broadHighlightGuardHumanReadable ||
                (label.find("Broad") != std::string::npos &&
                 label.find("Highlight") != std::string::npos &&
                 reason.find("broad bright regions") != std::string::npos &&
                 reason.find("does not recover") != std::string::npos);
            broadHighlightGuardDiagnosticsWritten =
                broadHighlightGuardDiagnosticsWritten ||
                (dimensions.value("broadHighlightControl", -1.0f) > 0.54f &&
                 dimensions.value("highlightIntegrity", -1.0f) > 0.42f &&
                 changes.value("highlightGuardDelta", 0.0f) > 0.24f &&
                 changes.value("dynamicRangeDelta", 0.0f) > 0.16f);
        }
    }
    RenderGraphRawDevelopPayload broadHighlightBaseCandidateRenderPayload;
    broadHighlightBaseCandidateRenderPayload.settings = highlightHeavyPayload.settings;
    broadHighlightBaseCandidateRenderPayload.scenePrepEnabled = highlightHeavyPayload.scenePrepEnabled;
    broadHighlightBaseCandidateRenderPayload.scenePrepSettings = highlightHeavyPayload.scenePrepSettings;
    broadHighlightBaseCandidateRenderPayload.integratedToneEnabled = highlightHeavyPayload.integratedToneEnabled;
    broadHighlightBaseCandidateRenderPayload.integratedToneLayerJson = highlightHeavyPayload.integratedToneLayerJson;
    const RenderGraphRawDevelopPayload broadHighlightGuardPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            broadHighlightBaseCandidateRenderPayload,
            highlightHeavyPayload.autoGuidance,
            broadHighlightGuardGuidance,
            "broadHighlightGuard",
            highlightHeavyPayload.autoGuidance.intent);
    const bool broadHighlightGuardRenderPayloadConstrained =
        broadHighlightGuardGenerated &&
        broadHighlightGuardEligible &&
        broadHighlightGuardHumanReadable &&
        broadHighlightGuardDiagnosticsWritten &&
        std::abs(broadHighlightGuardPayload.settings.exposureStops -
            broadHighlightBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(broadHighlightGuardPayload.settings.highlightStrength -
            broadHighlightBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        broadHighlightGuardPayload.scenePrepSettings.minEvBias <=
            broadHighlightBaseCandidateRenderPayload.scenePrepSettings.minEvBias - 0.08f &&
        broadHighlightGuardPayload.scenePrepSettings.highlightProtectionBias >=
            broadHighlightBaseCandidateRenderPayload.scenePrepSettings.highlightProtectionBias - 0.0001f &&
        broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "broadHighlightGuard";
    const bool highlightProfilePrefersHeadroom =
        highlightHeavyPayload.settings.highlightMode == Raw::HighlightReconstructionMode::ColorReconstruction &&
        highlightHeavyPayload.settings.highlightStrength > balancedPayload.settings.highlightStrength + 0.08f &&
        highlightHeavyPayload.scenePrepSettings.highlightProtectionBias > balancedPayload.scenePrepSettings.highlightProtectionBias + 0.18f &&
        highlightHeavyPayload.settings.highlightThreshold < balancedPayload.settings.highlightThreshold - 0.03f &&
        highlightHeavyPayload.scenePrepSettings.minEvBias < balancedPayload.scenePrepSettings.minEvBias - 0.18f &&
        highlightHeavyPayload.scenePrepSettings.highlightProtection > balancedPayload.scenePrepSettings.highlightProtection + 0.06f;

    EditorNodeGraph::RawDevelopPayload specularPayload = balancedPayload;
    specularPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.05f;
    specularPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.19f;
    specularPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.89f;
    specularPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.002f;
    specularPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.12f;
    specularPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.20f;
    specularPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.72f;
    specularPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 3.2f;
    specularPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    specularPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.18f;
    specularPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.04f;
    specularPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.08f;
    specularPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.12f;
    EditorModule::ApplyDevelopAutoSolve(specularPayload, metadata, true);
    const nlohmann::json& specularCandidateSolves =
        specularPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (specularCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : specularCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "specularHighlightTolerance") {
                continue;
            }
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            specularHighlightToleranceGenerated = true;
            specularHighlightToleranceEligible =
                specularHighlightToleranceEligible || eligible;
            specularHighlightToleranceGuidance =
                guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), specularPayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            specularHighlightToleranceHumanReadable =
                specularHighlightToleranceHumanReadable ||
                (label.find("Specular") != std::string::npos &&
                 label.find("Highlight") != std::string::npos &&
                 reason.find("tiny specular") != std::string::npos &&
                 reason.find("not clipped-data recovery") != std::string::npos);
            specularHighlightToleranceDiagnosticsWritten =
                specularHighlightToleranceDiagnosticsWritten ||
                (dimensions.value("specularTolerance", -1.0f) > 0.54f &&
                 dimensions.value("brightnessHierarchy", -1.0f) > 0.50f &&
                 changes.value("highlightCharacterDelta", 0.0f) > 0.24f &&
                 changes.value("highlightGuardDelta", 0.0f) < -0.10f);
        }
    }
    RenderGraphRawDevelopPayload specularBaseCandidateRenderPayload;
    specularBaseCandidateRenderPayload.settings = specularPayload.settings;
    specularBaseCandidateRenderPayload.scenePrepEnabled = specularPayload.scenePrepEnabled;
    specularBaseCandidateRenderPayload.scenePrepSettings = specularPayload.scenePrepSettings;
    specularBaseCandidateRenderPayload.integratedToneEnabled = specularPayload.integratedToneEnabled;
    specularBaseCandidateRenderPayload.integratedToneLayerJson = specularPayload.integratedToneLayerJson;
    const RenderGraphRawDevelopPayload specularHighlightTolerancePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            specularBaseCandidateRenderPayload,
            specularPayload.autoGuidance,
            specularHighlightToleranceGuidance,
            "specularHighlightTolerance",
            specularPayload.autoGuidance.intent);
    const bool specularHighlightToleranceRenderPayloadConstrained =
        specularHighlightToleranceGenerated &&
        specularHighlightToleranceEligible &&
        specularHighlightToleranceHumanReadable &&
        specularHighlightToleranceDiagnosticsWritten &&
        std::abs(specularHighlightTolerancePayload.settings.exposureStops -
            specularBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(specularHighlightTolerancePayload.scenePrepSettings.maxEvBias -
            specularBaseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == "specularHighlightTolerance" &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) >
            specularPayload.autoGuidance.highlightCharacter + 0.24f &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoHighlightBias", 99.0f) <
            specularPayload.autoGuidance.highlightGuard - 0.10f;
    const bool tinySpecularsDoNotDragExposure =
        specularPayload.settings.exposureStops >= balancedPayload.settings.exposureStops - 0.20f &&
        specularPayload.scenePrepSettings.highlightProtectionBias < highlightHeavyPayload.scenePrepSettings.highlightProtectionBias - 0.35f &&
        specularPayload.scenePrepSettings.minEvBias > highlightHeavyPayload.scenePrepSettings.minEvBias + 0.18f;
    const bool broadHighlightsProtectedMoreThanSpeculars =
        highlightHeavyPayload.settings.exposureStops < specularPayload.settings.exposureStops - 0.20f &&
        highlightHeavyPayload.scenePrepSettings.highlightProtectionBias > specularPayload.scenePrepSettings.highlightProtectionBias + 0.30f &&
        highlightHeavyPayload.scenePrepSettings.minEvBias < specularPayload.scenePrepSettings.minEvBias - 0.20f;

    EditorNodeGraph::RawDevelopPayload flatGrayHierarchyPayload = balancedPayload;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.08f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.25f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.56f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.001f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.18f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.18f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.72f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.0f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.18f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 0.96f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.02f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.02f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
    flatGrayHierarchyPayload.integratedToneLayerJson["autoCandidateSelectedId"] = "base";
    flatGrayHierarchyPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
        {
            { "id", "base" },
            { "success", true },
            { "renderScore", 0.58f },
            { "metrics", {
                { "meanLuma", 0.30f },
                { "medianLuma", 0.29f },
                { "p10Luma", 0.18f },
                { "p90Luma", 0.46f },
                { "shadowFraction", 0.14f },
                { "highlightFraction", 0.08f },
                { "clippedFraction", 0.001f },
                { "contrastSpan", 0.18f },
                { "meanRed", 0.31f },
                { "meanGreen", 0.30f },
                { "meanBlue", 0.29f },
                { "meanSaturation", 0.12f },
                { "lowSaturationFraction", 0.86f },
                { "highlightBandFraction", 0.28f },
                { "highlightMeanLuma", 0.56f },
                { "highlightLowSaturationFraction", 0.82f },
                { "highlightGrayRisk", 0.64f },
                { "highlightTileCoverage", 0.46f },
                { "highlightStructureScore", 0.38f },
                { "meaningfulHighlightPressure", 0.58f },
                { "edgeContrast", 0.18f },
                { "haloRiskFraction", 0.01f },
                { "shadowTextureRisk", 0.12f },
                { "localLumaSpread", 0.07f },
                { "localContrastPeak", 0.18f },
                { "localShadowPressure", 0.18f },
                { "localHighlightPressure", 0.12f },
                { "localDamageRiskMean", 0.08f },
                { "localDamageRiskPeak", 0.16f },
                { "localDamageRiskPeakTile", 4 },
                { "centerMeanLuma", 0.30f },
                { "centerShadowFraction", 0.10f },
                { "centerHighlightFraction", 0.04f }
            } }
        }
    });
    EditorModule::ApplyDevelopAutoSolve(flatGrayHierarchyPayload, metadata, true);
    const nlohmann::json flatGrayRegionEvidence =
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeRegionEvidence", nlohmann::json::object());
    const bool renderedHighlightGrayEvidenceWritten =
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightGrayRisk", 0.0f) > 0.50f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeMeaningfulHighlightPressure", 0.0f) > 0.48f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightBandFraction", 0.0f) > 0.20f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightMeanLuma", 1.0f) < 0.62f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightLowSaturationFraction", 0.0f) > 0.70f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightTileCoverage", 0.0f) > 0.40f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightStructureScore", 0.0f) > 0.30f &&
        flatGrayRegionEvidence.value("meaningfulHighlightPressure", 0.0f) > 0.48f &&
        flatGrayRegionEvidence.value("highlightGrayRisk", 0.0f) > 0.50f;
    const nlohmann::json& flatGrayCandidateSolves =
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (flatGrayCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : flatGrayCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "naturalContrastGuard") {
                continue;
            }
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            naturalContrastGuardGenerated = true;
            naturalContrastGuardEligible =
                naturalContrastGuardEligible || eligible;
            naturalContrastGuardGuidance =
                guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), flatGrayHierarchyPayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            naturalContrastGuardHumanReadable =
                naturalContrastGuardHumanReadable ||
                (label.find("Natural") != std::string::npos &&
                 label.find("Contrast") != std::string::npos &&
                 reason.find("believable separation") != std::string::npos &&
                 reason.find("lighting hierarchy") != std::string::npos);
            naturalContrastGuardDiagnosticsWritten =
                naturalContrastGuardDiagnosticsWritten ||
                (dimensions.value("naturalContrastGuard", -1.0f) > 0.54f &&
                 dimensions.value("brightnessHierarchy", -1.0f) > 0.54f &&
                 dimensions.value("contrastShape", -1.0f) > 0.54f &&
                 changes.value("contrastBiasDelta", 0.0f) > 0.18f &&
                 changes.value("dynamicRangeDelta", 0.0f) < -0.06f &&
                 std::fabs(changes.value("brightnessIntentDelta", 0.0f)) < 0.04f);
        }
    }
    RenderGraphRawDevelopPayload flatGrayBaseCandidateRenderPayload;
    flatGrayBaseCandidateRenderPayload.settings = flatGrayHierarchyPayload.settings;
    flatGrayBaseCandidateRenderPayload.scenePrepEnabled = flatGrayHierarchyPayload.scenePrepEnabled;
    flatGrayBaseCandidateRenderPayload.scenePrepSettings = flatGrayHierarchyPayload.scenePrepSettings;
    flatGrayBaseCandidateRenderPayload.integratedToneEnabled = flatGrayHierarchyPayload.integratedToneEnabled;
    flatGrayBaseCandidateRenderPayload.integratedToneLayerJson = flatGrayHierarchyPayload.integratedToneLayerJson;
    const RenderGraphRawDevelopPayload naturalContrastGuardPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            flatGrayBaseCandidateRenderPayload,
            flatGrayHierarchyPayload.autoGuidance,
            naturalContrastGuardGuidance,
            "naturalContrastGuard",
            flatGrayHierarchyPayload.autoGuidance.intent);
    const bool naturalContrastGuardRenderPayloadConstrained =
        naturalContrastGuardGenerated &&
        naturalContrastGuardEligible &&
        naturalContrastGuardHumanReadable &&
        naturalContrastGuardDiagnosticsWritten &&
        std::abs(naturalContrastGuardPayload.settings.exposureStops -
            flatGrayBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(naturalContrastGuardPayload.scenePrepSettings.maxEvBias -
            flatGrayBaseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == "naturalContrastGuard" &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) >
            flatGrayHierarchyPayload.autoGuidance.contrastBias + 0.18f &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) >
            flatGrayHierarchyPayload.autoGuidance.highlightCharacter + 0.10f &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoDynamicRange", 99.0f) <
            flatGrayHierarchyPayload.autoGuidance.dynamicRange - 0.06f;

    EditorNodeGraph::RawDevelopPayload underBrightHighlightPayload = balancedPayload;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.012f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.17f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.32f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.001f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.18f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.02f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.70f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 4.8f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    underBrightHighlightPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.82f;
    underBrightHighlightPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.08f;
    underBrightHighlightPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.16f;
    underBrightHighlightPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.12f;
    EditorModule::ApplyDevelopAutoSolve(underBrightHighlightPayload, metadata, true);
    const bool underBrightBroadHighlightsLifted =
        underBrightHighlightPayload.settings.exposureStops > balancedPayload.settings.exposureStops + 0.65f &&
        underBrightHighlightPayload.settings.exposureStops > specularPayload.settings.exposureStops + 0.30f &&
        underBrightHighlightPayload.scenePrepSettings.highlightProtectionBias < highlightHeavyPayload.scenePrepSettings.highlightProtectionBias - 0.30f;

    EditorNodeGraph::RawDevelopPayload readableShadowPayload = balancedPayload;
    readableShadowPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.018f;
    readableShadowPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.105f;
    readableShadowPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.68f;
    readableShadowPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.0f;
    readableShadowPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.18f;
    readableShadowPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.12f;
    readableShadowPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.76f;
    readableShadowPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 3.6f;
    readableShadowPayload.integratedToneLayerJson["autoSceneProfile"] = 2;
    readableShadowPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.46f;
    readableShadowPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.16f;
    readableShadowPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.46f;
    readableShadowPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.04f;
    EditorModule::ApplyDevelopAutoSolve(readableShadowPayload, metadata, true);
    const nlohmann::json& readableShadowCandidateSolves =
        readableShadowPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (readableShadowCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : readableShadowCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "shadowReadabilityLift") {
                continue;
            }
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            shadowReadabilityLiftGenerated = true;
            shadowReadabilityLiftEligible =
                shadowReadabilityLiftEligible || eligible;
            shadowReadabilityLiftGuidance =
                guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), readableShadowPayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            shadowReadabilityLiftHumanReadable =
                shadowReadabilityLiftHumanReadable ||
                (label.find("Shadow") != std::string::npos &&
                 label.find("Readability") != std::string::npos &&
                 reason.find("readable shadows") != std::string::npos &&
                 reason.find("RAW placement stable") != std::string::npos);
            shadowReadabilityLiftDiagnosticsWritten =
                shadowReadabilityLiftDiagnosticsWritten ||
                (dimensions.value("shadowReadabilityLift", -1.0f) > 0.54f &&
                 dimensions.value("midtonePlacement", -1.0f) > 0.46f &&
                 changes.value("shadowLiftDelta", 0.0f) > 0.20f &&
                 changes.value("dynamicRangeDelta", 0.0f) > 0.14f);
        }
    }
    RenderGraphRawDevelopPayload readableShadowBaseCandidateRenderPayload;
    readableShadowBaseCandidateRenderPayload.settings = readableShadowPayload.settings;
    readableShadowBaseCandidateRenderPayload.scenePrepEnabled = readableShadowPayload.scenePrepEnabled;
    readableShadowBaseCandidateRenderPayload.scenePrepSettings = readableShadowPayload.scenePrepSettings;
    readableShadowBaseCandidateRenderPayload.integratedToneEnabled = readableShadowPayload.integratedToneEnabled;
    readableShadowBaseCandidateRenderPayload.integratedToneLayerJson = readableShadowPayload.integratedToneLayerJson;
    const RenderGraphRawDevelopPayload shadowReadabilityLiftPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            readableShadowBaseCandidateRenderPayload,
            readableShadowPayload.autoGuidance,
            shadowReadabilityLiftGuidance,
            "shadowReadabilityLift",
            readableShadowPayload.autoGuidance.intent);
    const bool shadowReadabilityLiftRenderPayloadConstrained =
        shadowReadabilityLiftGenerated &&
        shadowReadabilityLiftEligible &&
        shadowReadabilityLiftHumanReadable &&
        shadowReadabilityLiftDiagnosticsWritten &&
        std::abs(shadowReadabilityLiftPayload.settings.exposureStops -
            readableShadowBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(shadowReadabilityLiftPayload.settings.highlightStrength -
            readableShadowBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        shadowReadabilityLiftPayload.scenePrepSettings.maxEvBias >=
            readableShadowBaseCandidateRenderPayload.scenePrepSettings.maxEvBias + 0.08f &&
        shadowReadabilityLiftPayload.scenePrepSettings.noiseProtectionBias >=
            readableShadowBaseCandidateRenderPayload.scenePrepSettings.noiseProtectionBias - 0.0001f &&
        shadowReadabilityLiftPayload.scenePrepSettings.shadowLiftLimitBias <=
            readableShadowBaseCandidateRenderPayload.scenePrepSettings.shadowLiftLimitBias + 0.0001f &&
        shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "shadowReadabilityLift";

    EditorNodeGraph::RawDevelopPayload darkScenePayload = balancedPayload;
    darkScenePayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.006f;
    darkScenePayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.045f;
    darkScenePayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.58f;
    darkScenePayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.0f;
    darkScenePayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.20f;
    darkScenePayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.08f;
    darkScenePayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.74f;
    darkScenePayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.2f;
    darkScenePayload.integratedToneLayerJson["autoSceneProfile"] = 2;
    darkScenePayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 1.02f;
    darkScenePayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.02f;
    darkScenePayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.32f;
    darkScenePayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.00f;
    EditorModule::ApplyDevelopAutoSolve(darkScenePayload, metadata, true);
    const bool darkSceneLifted =
        darkScenePayload.settings.exposureStops > balancedPayload.settings.exposureStops + 1.00f &&
        darkScenePayload.scenePrepSettings.maxEvBias > 1.00f &&
        darkScenePayload.scenePrepSettings.strength >= balancedPayload.scenePrepSettings.strength - 0.02f;
    const bool rawBaselineCarriesDarkLift =
        darkScenePayload.settings.exposureStops > balancedPayload.settings.exposureStops + 1.00f &&
        darkScenePayload.scenePrepSettings.strength <= scenePrepBefore.strength + 0.25f;
    const float darkRawExposureLift = darkScenePayload.settings.exposureStops - balancedPayload.settings.exposureStops;
    const float darkScenePrepAmountLift = darkScenePayload.scenePrepSettings.strength - balancedPayload.scenePrepSettings.strength;
    const bool rawBaselineDominatesDarkLift =
        darkRawExposureLift > 1.00f &&
        darkScenePrepAmountLift < 0.25f &&
        darkRawExposureLift > darkScenePrepAmountLift * 4.0f;

    EditorNodeGraph::RawDevelopPayload noisyLowLightPayload = balancedPayload;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.01f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.09f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.62f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.000f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.88f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.16f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.30f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.6f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneProfile"] = 4;
    noisyLowLightPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.92f;
    noisyLowLightPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 0.96f;
    noisyLowLightPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.06f;
    noisyLowLightPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.12f;
    EditorModule::ApplyDevelopAutoSolve(noisyLowLightPayload, metadata, true);
    const bool noisyProfilePrefersProtection =
        noisyLowLightPayload.settings.mosaicDenoise.enabled &&
        noisyLowLightPayload.settings.mosaicDenoise.radius >= 3 &&
        noisyLowLightPayload.settings.mosaicDenoise.iterations >= 2 &&
        noisyLowLightPayload.settings.mosaicDenoise.lumaStrength > balancedPayload.settings.mosaicDenoise.lumaStrength + 0.10f &&
        noisyLowLightPayload.scenePrepSettings.noiseProtectionBias > balancedPayload.scenePrepSettings.noiseProtectionBias + 0.18f &&
        noisyLowLightPayload.scenePrepSettings.shadowLiftLimit > balancedPayload.scenePrepSettings.shadowLiftLimit + 0.05f;

    EditorNodeGraph::RawDevelopPayload flatIntentPayload = balancedPayload;
    flatIntentPayload.autoGuidance.intent = EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    EditorModule::ApplyDevelopAutoSolve(flatIntentPayload, metadata, true);
    EditorNodeGraph::RawDevelopPayload punchyIntentPayload = balancedPayload;
    punchyIntentPayload.autoGuidance.intent = EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    EditorModule::ApplyDevelopAutoSolve(punchyIntentPayload, metadata, true);
    const bool modeIntentAffectsSolve =
        flatIntentPayload.scenePrepSettings.maxEvBias > balancedPayload.scenePrepSettings.maxEvBias + 0.10f &&
        flatIntentPayload.scenePrepSettings.highlightProtectionBias > balancedPayload.scenePrepSettings.highlightProtectionBias + 0.05f &&
        flatIntentPayload.integratedToneLayerJson.value("autoContrastBias", 99.0f) <
            balancedPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f) - 0.20f &&
        punchyIntentPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) >
            balancedPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f) + 0.20f;
    const bool modeIntentForwarded =
        flatIntentPayload.integratedToneLayerJson.value("autoIntent", std::string()) == "FlatEditingBase" &&
        punchyIntentPayload.integratedToneLayerJson.value("autoIntent", std::string()) == "PunchyHighContrast";
