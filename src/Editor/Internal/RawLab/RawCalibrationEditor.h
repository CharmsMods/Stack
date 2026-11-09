#pragma once
#include "Raw/RawColorCalibration.h"
#include <imgui.h>
#include <array>
#include <string>

namespace Stack::Editor::RawLabInternal {
struct CalibrationEditorState {
    void EndInteraction() { dragging=-1; dialGesture=false; zoomActive=false; }
    std::string source;
    int dragging = -1;
    int selected = -1;
    bool dialGesture = false;
    float dialPhase = 0.f;
    double radius = .65;
    bool zoomActive = false;
    double zoomFrom=.65,zoomTarget=.65,zoomStarted=0;
    int lastFrame=-1;
    RawRecipe::RawColorCalibrationRecipe gestureStart;
    RawRecipe::RawColorCalibrationRecipe checkedRecipe;
    Raw::RawWorkingSpace checkedSpace{};
    bool transformChecked = false;
    std::string transformError;
};

// Only presentation/gesture state lives here; the supplied recipe is the
// operation's existing edit draft, committed by its project action.
bool DrawCalibrationEditor(RawRecipe::RawColorCalibrationRecipe& recipe,
    CalibrationEditorState& state, const ImVec2& size,
    const std::array<bool, 3>& hueDriven, const std::array<bool, 3>& saturationDriven, bool fixedFit = false);
} // namespace Stack::Editor::RawLabInternal
