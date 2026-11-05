#pragma once
#include "Editor/Internal/RawLab/RawLabColorCloudRenderer.h"
#include "Editor/Internal/RawLab/RawLabColorWheelRenderer.h"
#include "Raw/RawDevelopmentRecipe.h"
#include <cstdint>
#include <limits>
#include <vector>

namespace Stack::Editor {
struct ColorWarpEditorState {
    struct Point {
        RawRecipe::RawColorWarpCoordinate input;
        std::array<float,3> rgb;
        float u=0,v=0;
    };
    bool selectRange=false;
    int dragging=-1; // 0 adjustment; 1..8 core/soft range boundaries.
    float lastHue=0, startChroma=0, startSaturation=0;
    bool details=false;
    std::uint64_t localRevision=0;
    std::uint64_t cloudRevision=~std::uint64_t(0),cloudLocalRevision=0;
    std::size_t inputKey=0,cloudFeedbackKey=0;
    int cloudSelection=-1;
    bool cloudLive=false,cloudPreview=false,cloudReady=false;
    Raw::RawWorkingSpace workingSpace=Raw::RawWorkingSpace::LinearSrgbD65;
    std::vector<Point> points;
    RawLabInternal::ColorWarpCloudRenderState cloud;
    RawLabInternal::RawLabColorWheelRenderer wheel;
    void EndInteraction() { dragging=-1; }
    ~ColorWarpEditorState() { RawLabInternal::ShutdownColorWarpCloud(cloud);wheel.Shutdown(); }
};
}
