#pragma once
#include "Editor/Internal/RawLab/RawLabColorCloudRenderer.h"
#include "Editor/Internal/RawLab/RawLabColorWheelRenderer.h"
#include "Editor/NodeEditors/ColorWarpCloudPreparation.h"
#include "Raw/RawDevelopmentRecipe.h"
#include <cstdint>
#include <limits>
#include <vector>

namespace Stack::Editor {
struct ColorWarpEditorState {
    std::uint64_t swatchRevision=~std::uint64_t(0),swatchLocalRevision=~std::uint64_t(0);
    int swatchSelection=-1;
    Raw::RawWorkingSpace swatchSpace=Raw::RawWorkingSpace::LinearSrgbD65;
    ImVec4 sourceSwatch{},resultSwatch{};
    bool selectRange=false;
    int dragging=-1; // 0 adjustment; 1..8 core/soft range boundaries.
    float lastHue=0, startChroma=0, startSaturation=0;
    ImVec2 movePress{},moveStart{};
    bool details=false;
    int brightnessDrag=-1;
    float brightnessPress=0, brightnessViewLow=-20, brightnessViewHigh=20;
    RawRecipe::RawColorWarpRange brightnessStart;
    std::size_t brightnessInputKey=0;
    bool brightnessInputReady=false;
    Raw::RawWorkingSpace brightnessSpace=Raw::RawWorkingSpace::LinearSrgbD65;
    float brightnessInputLow=-8,brightnessInputHigh=8;

    std::uint64_t localRevision=0;
    ColorWarpCloudPreparation cloudPreparation;
    RawLabInternal::ColorWarpCloudRenderState cloud;
    RawLabInternal::RawLabColorWheelRenderer wheel;
    void EndInteraction() { dragging=-1; }
    ~ColorWarpEditorState() { RawLabInternal::ShutdownColorWarpCloud(cloud);wheel.Shutdown(); }
};
}
