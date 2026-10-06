#pragma once
#include "Editor/EditorModuleTypes.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"

namespace Stack::Editor::RawLabInternal {
struct RawLabAreaImageResult {
    bool changed = false, active = false, finished = false, maskEdited = false, cancelled = false;
};
struct RawLabAreaImageMappings {
    std::function<std::optional<RawLabImageMapping>(const std::string&)> area;
    RawLabImageMapping measurement;
};
RawLabAreaImageResult InteractRawLabAreaImage(EditorModuleTypes::RawZoneAreaUiState& ui,
    RawRecipe::RawDevelopmentRecipe& recipe, const RawDevelopmentGraphScopeReadback& scope,
    const ImVec2& minimum, const ImVec2& maximum,const Async::ActivityMetadata& activity = {},
    const RawLabAreaImageMappings* mappings = nullptr);
void DrawRawLabAreaImage(EditorModuleTypes::RawZoneAreaUiState& ui,
    const RawRecipe::RawDevelopmentRecipe& recipe, const RawDevelopmentGraphScopeReadback& scope,
    const ImVec2& minimum, const ImVec2& maximum,const Async::ActivityMetadata& activity = {},
    const RawLabAreaImageMappings* mappings = nullptr);
}
