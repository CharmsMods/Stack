#pragma once

#include "Editor/EditorModuleTypes.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Renderer/RenderPipeline.h"

#include <cstddef>
#include <functional>
#include <string>

namespace Stack::Editor { class ColorWarpFeedback; struct ColorWarpEditorState; }

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
    ColorWarpFeedback* feedback = nullptr;
    EditorModuleTypes::RawWorkspaceColorWarpAreaOwner areaOwner;
    bool areaEditingAllowed = false;
    bool panel = false;
    ColorWarpEditorState* state = nullptr;
    std::uint64_t nodeRevision = 0;
};

bool RenderRawLabColorSurface(RawLabColorSurfaceArgs& args);
bool RenderRawLabColorSettings(RawLabColorSurfaceArgs& args);
bool CommitColorWarpSampling(RawLabColorSurfaceArgs& args);
void DrawColorWarpCloud(RawLabColorSurfaceArgs& args, ImVec2 minimum, float size);
bool DrawColorWarpWheel(RawLabColorSurfaceArgs& args, RawRecipe::RawColorWarpPin& pin, float size);
bool DrawColorWarpBrightness(RawLabColorSurfaceArgs& args, RawRecipe::RawColorWarpPin& pin);
bool DrawColorWarpRanges(RawLabColorSurfaceArgs& args, RawRecipe::RawColorWarpPin& pin);

} // namespace Stack::Editor::RawLabInternal
