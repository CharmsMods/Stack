#pragma once

#include "ThirdParty/json.hpp"

class EditorModule;

namespace Stack::Editor::DevelopFinishToneControls {

bool RenderDevelopFinishToneControls(
    EditorModule& editor,
    int nodeId,
    nlohmann::json& integratedToneLayerJson,
    bool upstreamDevelopSettingsChanged,
    float controlWidth,
    bool showAdvancedControls);

} // namespace Stack::Editor::DevelopFinishToneControls
