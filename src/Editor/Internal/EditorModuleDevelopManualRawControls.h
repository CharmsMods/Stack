#pragma once

#include "Editor/EditorModuleTypes.h"
#include "Raw/RawImageData.h"

namespace Stack::Editor::DevelopManualRawControls {

struct ManualRawBasicControlResult {
    bool changed = false;
    bool recordInteraction = false;
};

ManualRawBasicControlResult RenderDevelopManualRawBasicControls(
    Raw::RawDevelopSettings& settings,
    const Raw::RawDevelopSettings& defaultSettings,
    const Raw::RawMetadata& metadata,
    Stack::EditorModuleTypes::RawDevelopExposureDraftState& exposureDraft,
    float controlWidth);

bool RenderDevelopManualRawAdvancedControls(
    Raw::RawDevelopSettings& settings,
    const Raw::RawDevelopSettings& defaultSettings,
    const Raw::RawMetadata& metadata,
    float controlWidth,
    bool showAdvancedControls);

} // namespace Stack::Editor::DevelopManualRawControls
