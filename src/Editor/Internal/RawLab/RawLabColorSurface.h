#pragma once

#include "Editor/EditorModuleTypes.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Renderer/RenderPipeline.h"

#include <cstddef>
#include <functional>
#include <string>

namespace Stack::Editor::RawLabInternal {

struct RawLabColorSurfaceArgs {
    EditorModuleTypes::RawWorkspaceLabUiState& ui;
    RawRecipe::RawColorWarpRecipe& colorWarp;
    Raw::RawWorkingSpace workingSpace;
    const RawDevelopmentGraphScopeReadback& scope;
    std::size_t cloudInputFingerprint = 0;
    const std::string& previewIdentity;
    std::function<void()> saveAppState;
    bool strengthDriven = false;
};

bool RenderRawLabColorSurface(RawLabColorSurfaceArgs& args);
bool RenderRawLabColorSettings(RawLabColorSurfaceArgs& args);

} // namespace Stack::Editor::RawLabInternal
