#pragma once

#include "Raw/RawDevelopmentRecipe.h"

#include <cstdint>

namespace Stack::Editor::RawLabInternal {

struct RawDenoiseMapEditorState {
    int activeLayer = 0;
    std::uint64_t selectedPointId = 0;
    std::uint64_t hoveredPointId = 0;
    bool draggingPoint = false;
    bool pointDragDiagnosticActive = false;
    bool linkNewPoints = false;
    bool fullResolutionDiagnostics = true;
    int diagnosticMode = 0;
};

struct RawDenoiseMapEditorResult {
    bool recipeChanged = false;
    bool viewChanged = false;
};

RawDenoiseMapEditorResult RenderRawDenoiseMapEditor(
    Stack::RawRecipe::RawRgbDenoiseRecipe& recipe,
    RawDenoiseMapEditorState& state);

} // namespace Stack::Editor::RawLabInternal
