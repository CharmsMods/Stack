#pragma once

#include "Raw/RawImageData.h"
#include "Renderer/RenderPipeline.h"

namespace Stack::Editor::PreLocalExposureControls {

bool SameRawDetailFusionSettings(
    const Raw::RawDetailFusionSettings& a,
    const Raw::RawDetailFusionSettings& b);

void NormalizeIntegratedScenePrepSettings(Raw::RawDetailFusionSettings& settings);

bool RenderAutoGainPresetControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix,
    bool includeStrength);

bool RenderDevelopScenePrepNormalControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix);

bool RenderDevelopScenePrepAdvancedBiasControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix);

bool RenderAutoGainDiagnosticsControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix,
    bool advanced,
    const char* label);

void RenderPreLocalExposureSummarySection(
    const RenderPipeline::PreLocalExposureSummary* summary,
    bool hasImageInput,
    float controlWidth);

bool RenderPreLocalExposureExpertOverrides(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix);

bool RenderPreLocalExposureSpatialModel(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix);

bool RenderPreLocalExposureSmoothing(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix);

} // namespace Stack::Editor::PreLocalExposureControls
