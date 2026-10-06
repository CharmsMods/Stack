#pragma once
#include <functional>
#include <string>
#include "Raw/Tone/SceneTone.h"
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include "Editor/Internal/RawLab/RawLabControlSection.h"
namespace Stack::Editor::RawLabInternal {
bool DrawSceneToneEditor(RawRecipe::SceneTone& tone, bool contrastView,
    int& selectedPoint, int& draggingPoint, float& viewMin, float& viewMax, const RawLabGraphHistogram& histogram, const std::function<bool(const std::string&)>& driven = {},
    RawLabControlSection section = RawLabControlSection::All);
}
