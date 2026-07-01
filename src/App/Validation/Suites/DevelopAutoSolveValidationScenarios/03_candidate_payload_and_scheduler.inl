// Included inside DevelopAutoSolveValidation.cpp::ValidateDevelopAutoSolveBehavior().
// Candidate render payload constraints, stage scheduling, budget, gate, and telemetry checks.

    const RenderGraphRawDevelopPayload cleanProbePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            cleanShadowCandidateGuidance,
            "cleanShadows",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload textureProbePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            preserveTextureCandidateGuidance,
            "preserveTexture",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload protectedMidsPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            highlightProtectedMidsGuidance,
            "highlightProtectedMids",
            payload.autoGuidance.intent);
    const std::string finishToneProbeRenderId =
        finishToneProbeId.empty() ? std::string("toneSofterRolloff") : finishToneProbeId;
    const RenderGraphRawDevelopPayload finishToneProbePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            finishToneProbeGuidance,
            finishToneProbeRenderId,
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload brightHighlightRolloffPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            brightHighlightRolloffGuidance,
            "brightHighlightRolloff",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload luminousHighlightAnchorPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            luminousHighlightAnchorGuidance,
            "luminousHighlightAnchor",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload localRangeGuardPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            regionalBaseCandidateRenderPayload,
            currentRegionalRenderedGuidance,
            localRangeGuardGuidance,
            "localRangeGuard",
            regionalEvidencePayload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload haloSafeLocalRangePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            regionalBaseCandidateRenderPayload,
            currentRegionalRenderedGuidance,
            haloSafeLocalRangeGuidance,
            "haloSafeLocalRange",
            regionalEvidencePayload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload shadowNoiseFloorPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            regionalBaseCandidateRenderPayload,
            currentRegionalRenderedGuidance,
            shadowNoiseFloorGuidance,
            "shadowNoiseFloor",
            regionalEvidencePayload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload subjectReadableMidsPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            userSubjectBaseCandidateRenderPayload,
            currentUserSubjectRenderedGuidance,
            subjectReadableMidsGuidance,
            "subjectReadableMids",
            userSubjectIntentPayload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload sceneMoodPreservationPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            sceneMoodBaseCandidateRenderPayload,
            currentSceneMoodRenderedGuidance,
            sceneMoodPreservationGuidance,
            "sceneMoodPreservation",
            sceneMoodIntentPayload.autoGuidance.intent);
    const std::string whiteBalanceProbeRenderId =
        whiteBalanceProbeId.empty() ? std::string("wbDaylightCorrection") : whiteBalanceProbeId;
    const RenderGraphRawDevelopPayload whiteBalanceProbePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            whiteBalanceProbeGuidance,
            whiteBalanceProbeRenderId,
            payload.autoGuidance.intent);
    RenderGraphRawDevelopPayload stageConstraintBasePayload = baseCandidateRenderPayload;
    stageConstraintBasePayload.settings.exposureStops = 0.75f;
    stageConstraintBasePayload.settings.highlightStrength = 0.32f;
    stageConstraintBasePayload.settings.highlightThreshold = 0.95f;
    stageConstraintBasePayload.scenePrepSettings.strength = 0.55f;
    stageConstraintBasePayload.scenePrepSettings.maxEvBias = 0.35f;
    stageConstraintBasePayload.scenePrepSettings.minEvBias = -0.35f;
    stageConstraintBasePayload.scenePrepSettings.baseEvBias = 0.0f;
    stageConstraintBasePayload.scenePrepSettings.highlightProtectionBias = 0.0f;
    stageConstraintBasePayload.scenePrepSettings.noiseProtectionBias = 0.0f;
    stageConstraintBasePayload.scenePrepSettings.shadowLiftLimitBias = 0.0f;
    stageConstraintBasePayload.scenePrepSettings.wellExposedTargetBias = 0.0f;
    EditorNodeGraph::DevelopAutoGuidance stageConstraintCurrentGuidance = payload.autoGuidance;
    stageConstraintCurrentGuidance.autoStrength = 0.50f;
    stageConstraintCurrentGuidance.exposureBias = 0.0f;
    stageConstraintCurrentGuidance.dynamicRange = 0.0f;
    stageConstraintCurrentGuidance.shadowLift = 0.0f;
    stageConstraintCurrentGuidance.highlightGuard = 0.0f;
    stageConstraintCurrentGuidance.highlightCharacter = 0.0f;
    stageConstraintCurrentGuidance.contrastBias = 0.0f;
    stageConstraintCurrentGuidance.intent = payload.autoGuidance.intent;
    EditorModule::NormalizeDevelopAutoGuidance(stageConstraintCurrentGuidance);
    EditorNodeGraph::DevelopAutoGuidance scenePrepStageGuidance = stageConstraintCurrentGuidance;
    scenePrepStageGuidance.exposureBias = 0.36f;
    scenePrepStageGuidance.dynamicRange = 0.34f;
    scenePrepStageGuidance.shadowLift = 0.28f;
    scenePrepStageGuidance.highlightGuard = 0.18f;
    EditorModule::NormalizeDevelopAutoGuidance(scenePrepStageGuidance);
    EditorNodeGraph::DevelopAutoGuidance finishToneStageGuidance = stageConstraintCurrentGuidance;
    finishToneStageGuidance.exposureBias = 0.30f;
    finishToneStageGuidance.dynamicRange = 0.24f;
    finishToneStageGuidance.shadowLift = 0.16f;
    finishToneStageGuidance.highlightGuard = 0.16f;
    finishToneStageGuidance.contrastBias = 0.46f;
    EditorModule::NormalizeDevelopAutoGuidance(finishToneStageGuidance);
    const RenderGraphRawDevelopPayload scenePrepStagePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            stageConstraintBasePayload,
            stageConstraintCurrentGuidance,
            scenePrepStageGuidance,
            "renderedLocalBrightenMids",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload finishToneStagePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            stageConstraintBasePayload,
            stageConstraintCurrentGuidance,
            finishToneStageGuidance,
            "renderedLocalContrastShape",
            payload.autoGuidance.intent);
    const bool cleanupTextureCandidateRenderPayloadsDiverge =
        cleanShadowCandidateGenerated &&
        preserveTextureCandidateGenerated &&
        cleanProbePayload.settings.mosaicDenoise.enabled &&
        cleanProbePayload.settings.mosaicDenoise.lumaStrength >
            textureProbePayload.settings.mosaicDenoise.lumaStrength + 0.16f &&
        cleanProbePayload.settings.falseColorSuppression >
            textureProbePayload.settings.falseColorSuppression + 0.06f &&
        textureProbePayload.settings.preserveRealColor >
            cleanProbePayload.settings.preserveRealColor + 0.02f &&
        textureProbePayload.scenePrepSettings.textureSensitivity >
            cleanProbePayload.scenePrepSettings.textureSensitivity + 0.10f &&
        cleanProbePayload.integratedToneLayerJson.value("autoCandidateCleanupProbe", std::string()) == "cleanerShadows" &&
        textureProbePayload.integratedToneLayerJson.value("autoCandidateCleanupProbe", std::string()) == "preserveTexture";
    const bool highlightProtectedMidsRenderPayloadDiverges =
        highlightProtectedMidsGenerated &&
        protectedMidsPayload.settings.exposureStops <
            baseCandidateRenderPayload.settings.exposureStops - 0.05f &&
        std::abs(protectedMidsPayload.integratedToneLayerJson.value("autoDynamicRange", -99.0f) -
            highlightProtectedMidsGuidance.dynamicRange) < 0.0001f &&
        std::abs(protectedMidsPayload.integratedToneLayerJson.value("autoShadowBias", -99.0f) -
            highlightProtectedMidsGuidance.shadowLift) < 0.0001f &&
        std::abs(protectedMidsPayload.integratedToneLayerJson.value("autoHighlightBias", -99.0f) -
            highlightProtectedMidsGuidance.highlightGuard) < 0.0001f &&
        protectedMidsPayload.integratedToneLayerJson.value("autoCandidateRenderedProbeId", std::string()) == "highlightProtectedMids";
    const bool scenePrepCandidateStageConstrained =
        std::abs(scenePrepStagePayload.settings.exposureStops -
            stageConstraintBasePayload.settings.exposureStops) < 0.0001f &&
        std::abs(scenePrepStagePayload.settings.highlightStrength -
            stageConstraintBasePayload.settings.highlightStrength) < 0.0001f &&
        std::abs(scenePrepStagePayload.settings.highlightThreshold -
            stageConstraintBasePayload.settings.highlightThreshold) < 0.0001f &&
        scenePrepStagePayload.scenePrepSettings.baseEvBias >
            stageConstraintBasePayload.scenePrepSettings.baseEvBias + 0.05f &&
        scenePrepStagePayload.scenePrepSettings.maxEvBias >
            stageConstraintBasePayload.scenePrepSettings.maxEvBias + 0.20f &&
        scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        std::abs(scenePrepStagePayload.integratedToneLayerJson.value("autoBrightnessIntent", -99.0f) -
            scenePrepStageGuidance.exposureBias) < 0.0001f;
    const bool finishToneCandidateStageConstrained =
        std::abs(finishToneStagePayload.settings.exposureStops -
            stageConstraintBasePayload.settings.exposureStops) < 0.0001f &&
        std::abs(finishToneStagePayload.settings.highlightStrength -
            stageConstraintBasePayload.settings.highlightStrength) < 0.0001f &&
        std::abs(finishToneStagePayload.scenePrepSettings.baseEvBias -
            stageConstraintBasePayload.scenePrepSettings.baseEvBias) < 0.0001f &&
        std::abs(finishToneStagePayload.scenePrepSettings.maxEvBias -
            stageConstraintBasePayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        std::abs(finishToneStagePayload.scenePrepSettings.minEvBias -
            stageConstraintBasePayload.scenePrepSettings.minEvBias) < 0.0001f &&
        finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        std::abs(finishToneStagePayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) -
            finishToneStageGuidance.contrastBias) < 0.0001f;
    const bool finishToneProbeRenderPayloadConstrained =
        finishToneProbeGenerated &&
        finishToneProbeEligible &&
        std::abs(finishToneProbePayload.settings.exposureStops -
            baseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(finishToneProbePayload.settings.highlightStrength -
            baseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        std::abs(finishToneProbePayload.scenePrepSettings.baseEvBias -
            baseCandidateRenderPayload.scenePrepSettings.baseEvBias) < 0.0001f &&
        std::abs(finishToneProbePayload.scenePrepSettings.maxEvBias -
            baseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        finishToneProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        finishToneProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        finishToneProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        finishToneProbePayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == finishToneProbeRenderId &&
        std::abs(finishToneProbePayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) -
            finishToneProbeGuidance.contrastBias) < 0.0001f &&
        std::abs(finishToneProbePayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) -
            finishToneProbeGuidance.highlightCharacter) < 0.0001f;
    const nlohmann::json dynamicRangeStrategy =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategy", nlohmann::json::object());
    const bool dynamicRangeStrategyDiagnosticsWritten =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyVersion", std::string()) == "DynamicRangeStrategyV1" &&
        dynamicRangeStrategy.value("version", std::string()) == "DynamicRangeStrategyV1" &&
        !payload.integratedToneLayerJson.value("autoDynamicRangeStrategyId", std::string()).empty() &&
        !payload.integratedToneLayerJson.value("autoDynamicRangeStrategyLabel", std::string()).empty() &&
        payload.integratedToneLayerJson.value("autoDynamicRangeHighlightImportance", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeShadowReadability", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeNoiseConstraint", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalHaloGuardNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeNaturalContrastGuardNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeBrightHighlightRolloffNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeHighlightBrightnessAnchorNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeBroadHighlightGuardNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeSpecularHighlightToleranceNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeShadowReadabilityLiftNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeShadowNoiseFloorNeed", -1.0f) >= 0.0f;
    const nlohmann::json dynamicRangeStrategyMap =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMap", nlohmann::json::object());
    bool dynamicRangeStrategyMapScoreComponentsWritten = false;
    const nlohmann::json strategyMapCandidateSolves =
        payload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (strategyMapCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : strategyMapCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json scoreMap =
                scoreComponents.value("dynamicRangeStrategyMap", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            if (scoreMap.value("version", std::string()) == "DynamicRangeStrategyMapV1" &&
                dimensions.value("strategyHighlightFit", -1.0f) >= 0.0f &&
                dimensions.value("strategyShadowFit", -1.0f) >= 0.0f &&
                dimensions.value("strategyVisibleRangeFit", -1.0f) >= 0.0f &&
                dimensions.value("strategyNaturalContrastFit", -1.0f) >= 0.0f) {
                dynamicRangeStrategyMapScoreComponentsWritten = true;
                break;
            }
        }
    }
    const float strategyMapHighlightShadowAxis =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightShadowAxis", -99.0f);
    const float strategyMapContrastRangeAxis =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapContrastRangeAxis", -99.0f);
    const bool dynamicRangeStrategyMapDiagnosticsWritten =
        dynamicRangeStrategyDiagnosticsWritten &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVersion", std::string()) == "DynamicRangeStrategyMapV1" &&
        dynamicRangeStrategyMap.value("version", std::string()) == "DynamicRangeStrategyMapV1" &&
        dynamicRangeStrategy.value("strategyMapVersion", std::string()) == "DynamicRangeStrategyMapV1" &&
        strategyMapHighlightShadowAxis >= -1.0f &&
        strategyMapHighlightShadowAxis <= 1.0f &&
        strategyMapContrastRangeAxis >= -1.0f &&
        strategyMapContrastRangeAxis <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightPriority", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightPriority", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapShadowVisibility", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapShadowVisibility", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapNaturalContrast", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapNaturalContrast", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVisibleRange", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVisibleRange", -1.0f) <= 1.0f &&
        std::abs(strategyMapHighlightShadowAxis -
            dynamicRangeStrategyMap.value("highlightShadowAxis", -98.0f)) < 0.0001f &&
        std::abs(strategyMapContrastRangeAxis -
            dynamicRangeStrategyMap.value("contrastRangeAxis", -98.0f)) < 0.0001f &&
        dynamicRangeStrategyMapScoreComponentsWritten;
    const nlohmann::json localExposureStrategy =
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategy", nlohmann::json::object());
    const std::string localExposureStrategyId =
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyId", std::string());
    const bool localExposureStrategyDiagnosticsWritten =
        dynamicRangeStrategyDiagnosticsWritten &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1" &&
        localExposureStrategy.value("version", std::string()) == "LocalExposureStrategyV1" &&
        dynamicRangeStrategy.value("localExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1" &&
        !localExposureStrategyId.empty() &&
        !payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyLabel", std::string()).empty() &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureRangeRedistribution", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureRangeRedistribution", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCompression", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCompression", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowOpening", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowOpening", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureNoiseGuard", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureNoiseGuard", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloGuard", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloGuard", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureTextureGuard", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureTextureGuard", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrengthTarget", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrengthTarget", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCrowding", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCrowding", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowCrowding", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowCrowding", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloStress", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloStress", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureFlatnessRisk", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureFlatnessRisk", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureDamageRisk", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureDamageRisk", -1.0f) <= 1.0f;
    const bool localExposureStrategyAuthoredScenePrep =
        localExposureStrategyDiagnosticsWritten &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1" &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureStrategyId", std::string()) == localExposureStrategyId &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureStrengthTarget", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureShadowEvBudget", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureHighlightEvBudget", -1.0f) >= 0.0f;
    const bool brightHighlightRolloffRenderPayloadConstrained =
        brightHighlightRolloffGenerated &&
        brightHighlightRolloffEligible &&
        brightHighlightRolloffHumanReadable &&
        brightHighlightRolloffDiagnosticsWritten &&
        std::abs(brightHighlightRolloffPayload.settings.exposureStops -
            baseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(brightHighlightRolloffPayload.scenePrepSettings.maxEvBias -
            baseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == "brightHighlightRolloff" &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) >
            payload.autoGuidance.highlightCharacter + 0.16f;
    const bool luminousHighlightAnchorRenderPayloadConstrained =
        luminousHighlightAnchorGenerated &&
        luminousHighlightAnchorEligible &&
        luminousHighlightAnchorHumanReadable &&
        luminousHighlightAnchorDiagnosticsWritten &&
        std::abs(luminousHighlightAnchorPayload.settings.exposureStops -
            baseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(luminousHighlightAnchorPayload.scenePrepSettings.maxEvBias -
            baseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == "luminousHighlightAnchor" &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) >
            payload.autoGuidance.highlightCharacter + 0.22f &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) >
            payload.autoGuidance.contrastBias + 0.10f &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoDynamicRange", 99.0f) <
            payload.autoGuidance.dynamicRange - 0.04f;
    const bool localRangeGuardRenderPayloadConstrained =
        regionalEvidenceDiagnosticsWritten &&
        localRangeGuardGenerated &&
        localRangeGuardEligible &&
        localRangeGuardDiagnosticsWritten &&
        std::abs(localRangeGuardPayload.settings.exposureStops -
            regionalBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(localRangeGuardPayload.settings.highlightStrength -
            regionalBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        localRangeGuardPayload.scenePrepSettings.maxEvBias >=
            regionalBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.0001f &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true);
    const bool localExposureStrategyCandidatePayloadCarried =
        localExposureStrategyDiagnosticsWritten &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1" &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureStrategyId", std::string()) ==
            regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyId", std::string()) &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureRangeRedistribution", -1.0f) >= 0.0f &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureHaloGuard", -1.0f) >= 0.0f &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureStrengthTarget", -1.0f) >= 0.0f;
    const auto raisedTowardScenePrepGuardCap =
        [](float candidateValue, float baseValue, float intendedDelta) {
            return candidateValue >= std::min(1.0f, baseValue + intendedDelta) - 0.0001f;
        };
    const bool haloSafeLocalRangeRenderPayloadConstrained =
        regionalEvidenceDiagnosticsWritten &&
        haloSafeLocalRangeGenerated &&
        haloSafeLocalRangeEligible &&
        haloSafeLocalRangeHumanReadable &&
        haloSafeLocalRangeDiagnosticsWritten &&
        std::abs(haloSafeLocalRangePayload.settings.exposureStops -
            regionalBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(haloSafeLocalRangePayload.settings.highlightStrength -
            regionalBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        haloSafeLocalRangePayload.scenePrepSettings.maxEvBias <
            regionalBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.04f &&
        raisedTowardScenePrepGuardCap(
            haloSafeLocalRangePayload.scenePrepSettings.haloGuard,
            regionalBaseCandidateRenderPayload.scenePrepSettings.haloGuard,
            0.16f) &&
        raisedTowardScenePrepGuardCap(
            haloSafeLocalRangePayload.scenePrepSettings.smoothGradientProtection,
            regionalBaseCandidateRenderPayload.scenePrepSettings.smoothGradientProtection,
            0.14f) &&
        raisedTowardScenePrepGuardCap(
            haloSafeLocalRangePayload.scenePrepSettings.edgeAwareness,
            regionalBaseCandidateRenderPayload.scenePrepSettings.edgeAwareness,
            0.10f) &&
        haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "haloSafeLocalRange";
    const bool shadowNoiseFloorRenderPayloadConstrained =
        regionalEvidenceDiagnosticsWritten &&
        shadowNoiseFloorGenerated &&
        shadowNoiseFloorEligible &&
        shadowNoiseFloorDiagnosticsWritten &&
        std::abs(shadowNoiseFloorPayload.settings.exposureStops -
            regionalBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(shadowNoiseFloorPayload.settings.highlightStrength -
            regionalBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        shadowNoiseFloorPayload.scenePrepSettings.maxEvBias <=
            regionalBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.08f &&
        shadowNoiseFloorPayload.scenePrepSettings.noiseProtectionBias >=
            regionalBaseCandidateRenderPayload.scenePrepSettings.noiseProtectionBias + 0.08f &&
        shadowNoiseFloorPayload.scenePrepSettings.shadowLiftLimitBias >=
            regionalBaseCandidateRenderPayload.scenePrepSettings.shadowLiftLimitBias + 0.08f &&
        shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "shadowNoiseFloor";
    const bool subjectReadableMidsRenderPayloadConstrained =
        subjectReadableMidsGenerated &&
        subjectReadableMidsEligible &&
        subjectReadableMidsHumanReadable &&
        subjectReadableMidsDiagnosticsWritten &&
        std::abs(subjectReadableMidsPayload.settings.exposureStops -
            userSubjectBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(subjectReadableMidsPayload.settings.highlightStrength -
            userSubjectBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        subjectReadableMidsPayload.scenePrepSettings.maxEvBias >=
            userSubjectBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.0001f &&
        subjectReadableMidsPayload.scenePrepSettings.noiseProtectionBias >=
            userSubjectBaseCandidateRenderPayload.scenePrepSettings.noiseProtectionBias + 0.02f &&
        subjectReadableMidsPayload.scenePrepSettings.shadowLiftLimitBias <=
            userSubjectBaseCandidateRenderPayload.scenePrepSettings.shadowLiftLimitBias + 0.0001f &&
        subjectReadableMidsPayload.scenePrepSettings.wellExposedTargetBias >=
            userSubjectBaseCandidateRenderPayload.scenePrepSettings.wellExposedTargetBias + 0.02f &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "subjectReadableMids" &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateSubjectIntentProbe", std::string()) == "subjectReadableMids";
    const bool sceneMoodPreservationRenderPayloadConstrained =
        sceneMoodPreservationGenerated &&
        sceneMoodPreservationEligible &&
        sceneMoodPreservationHumanReadable &&
        sceneMoodPreservationDiagnosticsWritten &&
        std::abs(sceneMoodPreservationPayload.settings.exposureStops -
            sceneMoodBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(sceneMoodPreservationPayload.settings.highlightStrength -
            sceneMoodBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        sceneMoodPreservationPayload.scenePrepSettings.maxEvBias <=
            sceneMoodBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.06f &&
        sceneMoodPreservationPayload.scenePrepSettings.noiseProtectionBias >=
            sceneMoodBaseCandidateRenderPayload.scenePrepSettings.noiseProtectionBias + 0.05f &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "sceneMoodPreservation" &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateSubjectIntentProbe", std::string()) == "sceneMoodPreservation";
    const bool whiteBalanceProbeRenderPayloadDiverges =
        whiteBalanceProbeGenerated &&
        whiteBalanceProbeEligible &&
        whiteBalanceProbePayload.settings.whiteBalanceMode !=
            baseCandidateRenderPayload.settings.whiteBalanceMode &&
        whiteBalanceProbePayload.settings.manualWhiteBalance ==
            baseCandidateRenderPayload.settings.manualWhiteBalance &&
        whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "rawGlobal" &&
        !whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", true) &&
        whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateWhiteBalanceProbe", std::string()) ==
            whiteBalanceProbeRenderId &&
        whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateWhiteBalanceMode", std::string()) ==
            Raw::WhiteBalanceModeName(whiteBalanceProbePayload.settings.whiteBalanceMode);
    const bool renderedStageRelevanceWorks =
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "renderedLocalBrightenMids",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "maximumRange",
            "rawGlobal") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "modeNeighborNaturalMoreContrast",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "toneSofterRolloff",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "brightHighlightRolloff",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "luminousHighlightAnchor",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "specularHighlightTolerance",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "naturalContrastGuard",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "broadHighlightGuard",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "haloSafeLocalRange",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "localRangeGuard",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "shadowReadabilityLift",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "shadowNoiseFloor",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "renderedLocalCleanShadows",
            "rawCleanup") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "wbDaylightCorrection",
            "rawGlobal") &&
        !EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "renderedLocalBrightenMids",
            "none");
    const bool renderedRefineIntentRelevanceWorks =
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalHighlightRestraint",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "highlightProtectedMids",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "brightHighlightRolloff",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "luminousHighlightAnchor",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "specularHighlightTolerance",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "broadHighlightGuard",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "localRangeGuard",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "haloSafeLocalRange",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "haloSafeLocalRange",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "localRangeGuard",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "shadowReadabilityLift",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "shadowNoiseFloor",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "shadowNoiseFloor",
            "cleanShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalCleanShadows",
            "cleanShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "tonePunchierShape",
            "addContrast") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "naturalContrastGuard",
            "addContrast") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "luminousHighlightAnchor",
            "addContrast") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalShadowOpening",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalContrastShape",
            "addContrast") &&
        !EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalContrastShape",
            "cleanShadows") &&
        !EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalBrightenMids",
            "");
    bool finishToneStageCacheMet = false;
    std::string finishToneStageCacheExpected;
    std::string finishToneStageCacheStatus;
    const std::string finishToneObservedDirtyBoundary =
        EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
            "finishTone",
            true,
            true,
            finishToneStageCacheMet,
            finishToneStageCacheExpected,
            finishToneStageCacheStatus);
    bool scenePrepStageCacheMet = false;
    std::string scenePrepStageCacheExpected;
    std::string scenePrepStageCacheStatus;
    const std::string scenePrepObservedDirtyBoundary =
        EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
            "scenePrep",
            true,
            false,
            scenePrepStageCacheMet,
            scenePrepStageCacheExpected,
            scenePrepStageCacheStatus);
    bool scenePrepStageCacheMissMet = true;
    std::string scenePrepStageCacheMissExpected;
    std::string scenePrepStageCacheMissStatus;
    const std::string scenePrepMissObservedDirtyBoundary =
        EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
            "scenePrep",
            false,
            false,
            scenePrepStageCacheMissMet,
            scenePrepStageCacheMissExpected,
            scenePrepStageCacheMissStatus);
    bool rawStageCacheMet = false;
    std::string rawStageCacheExpected;
    std::string rawStageCacheStatus;
    const std::string rawStageObservedDirtyBoundary =
        EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
            "rawGlobal",
            false,
            false,
            rawStageCacheMet,
            rawStageCacheExpected,
            rawStageCacheStatus);
    const bool renderedStageCacheValidationWorks =
        finishToneObservedDirtyBoundary == "finishTone" &&
        finishToneStageCacheMet &&
        finishToneStageCacheExpected == "finishTone" &&
        finishToneStageCacheStatus == "met" &&
        scenePrepObservedDirtyBoundary == "scenePrep" &&
        scenePrepStageCacheMet &&
        scenePrepStageCacheExpected == "scenePrep" &&
        scenePrepStageCacheStatus == "met" &&
        scenePrepMissObservedDirtyBoundary == "rawBase" &&
        !scenePrepStageCacheMissMet &&
        scenePrepStageCacheMissStatus == "missedRawBaseReuse" &&
        rawStageObservedDirtyBoundary == "rawBase" &&
        rawStageCacheMet &&
        rawStageCacheExpected == "rawBase" &&
        rawStageCacheStatus == "notRequired";
    std::string selectedScheduleBoundary;
    std::string selectedScheduleReason;
    const int selectedScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "multiStage",
            true,
            selectedScheduleBoundary,
            selectedScheduleReason);
    std::string finishToneScheduleBoundary;
    std::string finishToneScheduleReason;
    const int finishToneScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "finishTone",
            false,
            finishToneScheduleBoundary,
            finishToneScheduleReason);
    std::string scenePrepScheduleBoundary;
    std::string scenePrepScheduleReason;
    const int scenePrepScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "scenePrep",
            false,
            scenePrepScheduleBoundary,
            scenePrepScheduleReason);
    std::string rawGlobalScheduleBoundary;
    std::string rawGlobalScheduleReason;
    const int rawGlobalScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "rawGlobal",
            false,
            rawGlobalScheduleBoundary,
            rawGlobalScheduleReason);
    std::string multiStageScheduleBoundary;
    std::string multiStageScheduleReason;
    const int multiStageScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "multiStage",
            false,
            multiStageScheduleBoundary,
            multiStageScheduleReason);
    const bool renderedStageSchedulerClassificationWorks =
        selectedScheduleRank == 0 &&
        finishToneScheduleRank > selectedScheduleRank &&
        finishToneScheduleRank < scenePrepScheduleRank &&
        scenePrepScheduleRank < rawGlobalScheduleRank &&
        rawGlobalScheduleRank < multiStageScheduleRank &&
        selectedScheduleBoundary == "rawBase" &&
        finishToneScheduleBoundary == "finishTone" &&
        scenePrepScheduleBoundary == "scenePrep" &&
        rawGlobalScheduleBoundary == "rawBase" &&
        multiStageScheduleBoundary == "rawBase" &&
        finishToneScheduleReason.find("pre-finish caches") != std::string::npos &&
        rawGlobalScheduleReason.find("after downstream") != std::string::npos;
    const bool developCandidateRenderBudgetAllowsMultiNodeCoverage =
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(0, 0) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(3, 3) &&
        !EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(4, 4) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(4, 0) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(19, 0) &&
        !EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(20, 0) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(4, 4, 6) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(5, 5, 6) &&
        !EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(6, 6, 6);
    const bool developCandidateMetricReadbackBudgetWorks =
        EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(4000, 3000) == 0 &&
        EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(5000, 4000) == 1800 &&
        EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(7000, 5000) == 1536 &&
        EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(9000, 6000) == 1280;
    const bool rawDevelopStageCacheMemoryPolicyWorks =
        RenderPipeline::EstimateRawDevelopStageCacheTextureBytesForValidation(1000, 1000) == 8000000ull &&
        RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(2000, 1500) == 8 &&
        RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(4000, 3000) == 3 &&
        RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(5000, 4000) == 2 &&
        RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(7000, 5000) == 1 &&
        !RenderPipeline::ShouldCacheRawDevelopStageTextureForValidation(9000, 6000);
    nlohmann::json adaptiveContinueTone = nlohmann::json::object();
    adaptiveContinueTone["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "continue" },
        { "reason", "merged" },
        { "nextStep", "renderUpdatedSolve" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "pass", 1 },
        { "remainingPasses", 2 }
    };
    std::string adaptiveContinueReason;
    bool adaptiveContinueExpanded = false;
    const std::size_t adaptiveContinueBudget =
        EditorModule::ResolveDevelopAdaptiveRenderBudgetForValidation(
            adaptiveContinueTone,
            42,
            41,
            7,
            "finishTone",
            "protectHighlights",
            adaptiveContinueReason,
            adaptiveContinueExpanded);
    nlohmann::json adaptiveInitialTone = nlohmann::json::object();
    adaptiveInitialTone["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "nextStep", "renderCandidates" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "pass", 0 },
        { "remainingPasses", 3 }
    };
    std::string adaptiveInitialReason;
    bool adaptiveInitialExpanded = true;
    const std::size_t adaptiveInitialBudget =
        EditorModule::ResolveDevelopAdaptiveRenderBudgetForValidation(
            adaptiveInitialTone,
            42,
            41,
            7,
            "",
            "",
            adaptiveInitialReason,
            adaptiveInitialExpanded);
    nlohmann::json adaptiveFocusedTone = nlohmann::json::object();
    adaptiveFocusedTone["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "nextStep", "renderCandidates" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "pass", 2 },
        { "remainingPasses", 1 }
    };
    adaptiveFocusedTone["autoCandidateConvergenceEvidence"] = {
        { "version", "ConvergenceEvidenceV1" },
        { "state", "awaitingRenderedMetrics" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "pass", 2 },
        { "shouldContinue", true }
    };
    adaptiveFocusedTone["autoCandidateConvergenceAdmissionTightened"] = true;
    std::string adaptiveFocusedReason;
    bool adaptiveFocusedExpanded = true;
    bool adaptiveFocusedNarrowed = false;
    const std::size_t adaptiveFocusedBudget =
        EditorModule::ResolveDevelopAdaptiveRenderBudgetForValidation(
            adaptiveFocusedTone,
            42,
            41,
            7,
            "",
            "",
            adaptiveFocusedReason,
            adaptiveFocusedExpanded,
            &adaptiveFocusedNarrowed);
    const bool developAdaptiveRenderBudgetWorks =
        adaptiveContinueBudget == 6 &&
        adaptiveContinueExpanded &&
        adaptiveContinueReason == "validateActiveRefineIntent" &&
        adaptiveInitialBudget == 4 &&
        !adaptiveInitialExpanded &&
        adaptiveInitialReason == "initialRenderedMetrics" &&
        adaptiveFocusedBudget == 3 &&
        !adaptiveFocusedExpanded &&
        adaptiveFocusedNarrowed &&
        adaptiveFocusedReason == "convergenceEvidenceFocusedValidation";
    const double candidateFeedbackQuietSeconds =
        EditorModule::DevelopCandidateFeedbackQuietSecondsForValidation();
    const bool developCandidateFeedbackGateDropsStale =
        EditorModule::ClassifyDevelopCandidateFeedbackGateForValidation(
            7,
            8,
            10.0,
            11.0) ==
        EditorModule::DevelopCandidateFeedbackGateDecision::DropStaleInteraction;
    const bool developCandidateFeedbackGateDefersRecent =
        candidateFeedbackQuietSeconds >= 0.59 &&
        EditorModule::ClassifyDevelopCandidateFeedbackGateForValidation(
            8,
            8,
            10.0,
            10.0 + candidateFeedbackQuietSeconds * 0.50) ==
        EditorModule::DevelopCandidateFeedbackGateDecision::DeferRecentInteraction;
    const bool developCandidateFeedbackGateAppliesAfterQuiet =
        EditorModule::ClassifyDevelopCandidateFeedbackGateForValidation(
            8,
            8,
            10.0,
            10.0 + candidateFeedbackQuietSeconds + 0.01) ==
        EditorModule::DevelopCandidateFeedbackGateDecision::Apply;
    const bool developCandidateRenderAdmissionDefersRecent =
        EditorModule::ShouldDeferDevelopCandidateRenderRequestForValidation(
            10.0,
            10.0 + candidateFeedbackQuietSeconds * 0.50) &&
        !EditorModule::ShouldDeferDevelopCandidateRenderRequestForValidation(
            10.0,
            10.0 + candidateFeedbackQuietSeconds + 0.01);
    const double candidateFeedbackRemainingMidEdit =
        EditorModule::DevelopCandidateFeedbackQuietRemainingSecondsForValidation(
            10.0,
            10.0 + candidateFeedbackQuietSeconds * 0.25);
    const bool developCandidateFeedbackQuietRemainingWorks =
        std::abs(candidateFeedbackRemainingMidEdit - candidateFeedbackQuietSeconds * 0.75) < 0.000001 &&
        EditorModule::DevelopCandidateFeedbackQuietRemainingSecondsForValidation(
            10.0,
            10.0 + candidateFeedbackQuietSeconds + 0.01) == 0.0 &&
        EditorModule::DevelopCandidateFeedbackQuietRemainingSecondsForValidation(
            10.0,
            9.99) == 0.0;
    const bool developStaleSnapshotAbortWorks =
        !EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(12, false, false, 0) &&
        !EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(12, false, true, 12) &&
        EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(12, false, true, 13) &&
        EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(12, true, false, 0);
    const std::string candidateProgressLabel =
        EditorRenderWorker::BuildDevelopCandidateProgressLabelForValidation(
            "Luminous Highlight Anchor Candidate With A Very Long User-Facing Name",
            "finishTone",
            1,
            4);
    const bool developCandidateProgressLabelWorks =
        candidateProgressLabel.find("2/4") != std::string::npos &&
        candidateProgressLabel.find("Luminous Highlight Anchor") != std::string::npos &&
        candidateProgressLabel.find("finishTone") != std::string::npos &&
        candidateProgressLabel.size() < 110;
    const bool finishGuidanceForwarded =
        requestedGuidanceForwarded &&
        selectedCandidateGuidanceForwarded &&
        candidateDiagnosticsWritten;
    const bool brightnessExposureTelemetryForwarded =
        requestedGuidanceForwarded &&
        selectedCandidateGuidanceForwarded &&
        candidateSolveCanBiasAuthoredGuidance &&
        std::abs(payload.integratedToneLayerJson.value("autoRawExposurePreferenceEv", -99.0f) -
            payload.integratedToneLayerJson.value("autoBrightnessIntent", -98.0f) * 2.0f) < 0.0001f;
    const bool exposureDiagnosticsForwarded =
        payload.integratedToneLayerJson.value("autoExposureDiagnosticStatsValid", false) &&
        std::abs(payload.integratedToneLayerJson.value("autoAuthoredRawExposureEv", -99.0f) - payload.settings.exposureStops) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoAuthoredRawExposureScale", -99.0f) - std::exp2(payload.settings.exposureStops)) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoAuthoredLocalMinEvBias", -99.0f) - payload.scenePrepSettings.minEvBias) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoAuthoredLocalMaxEvBias", -99.0f) - payload.scenePrepSettings.maxEvBias) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticClippingRatio", -99.0f) - 0.0f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticHighlightPressure", -99.0f) - 0.68f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticNoiseRisk", -99.0f) - 0.57f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticHdrSpreadEv", -99.0f) - 4.6f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticRecommendedBaseEv", -99.0f) - 1.35f) < 0.0001f;

