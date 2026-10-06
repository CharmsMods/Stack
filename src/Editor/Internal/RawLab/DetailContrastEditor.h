#pragma once
#include <functional>
#include <string>
#include "Raw/Detail/DetailContrast.h"
#include "Editor/Internal/RawLab/RawLabControlSection.h"

namespace Stack::Editor::RawLabInternal {
struct DetailContrastEditorState {
    bool fieldView = false;
    int selectedBand = 3;
    int selectedRow = 8;
    float brushGain = 1.5f;
};
bool DrawDetailContrastEditor(RawRecipe::DetailContrast& settings, DetailContrastEditorState& ui, const std::function<bool(const std::string&)>& driven = {},
    RawLabControlSection section = RawLabControlSection::All);
}
