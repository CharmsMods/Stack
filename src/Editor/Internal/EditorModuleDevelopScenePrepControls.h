#pragma once

#include "Raw/RawImageData.h"
#include "Renderer/RenderPipeline.h"

namespace Stack::Editor::DevelopScenePrepControls {

bool SameDevelopScenePrepSettings(
    const Raw::RawDetailFusionSettings& a,
    const Raw::RawDetailFusionSettings& b);

bool RenderDevelopScenePrepControls(
    Raw::RawDetailFusionSettings& settings,
    const RenderPipeline::PreLocalExposureSummary* liveSummary,
    bool hasRawSourceInput,
    float controlWidth,
    bool showAdvancedControls);

void NormalizeDevelopScenePrepSettings(Raw::RawDetailFusionSettings& settings);

} // namespace Stack::Editor::DevelopScenePrepControls
