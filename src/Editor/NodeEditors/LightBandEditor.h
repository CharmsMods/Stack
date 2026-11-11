#pragma once
#include "Raw/RawDevelopmentRecipe.h"
#include <imgui.h>
#include <string>
namespace Stack::Editor {
struct RawWorkspaceEditContext;
struct LightBandViewState {
    bool advanced=false;
    int dragging=-1;
    float before=0;
    std::array<float,129> response{};
    RawRecipe::LightBands responseBands;
    RawRecipe::LightRange responseRange;
    std::vector<RawRecipe::RawLocalRangePoint> responsePoints;
    float responseLow=0,responseHigh=0;
    bool responseValid=false;
    RawRecipe::LightRange retainedRange;
    std::uint64_t retainedInputRevision=0;
    void EndInteraction(){dragging=-1;}
};
bool DrawLightRangeSettings(RawWorkspaceEditContext&,RawRecipe::LightRange fallback={},float measuredExposure=0);
bool DrawLightBandGraph(RawWorkspaceEditContext&,RawRecipe::LightRange,ImVec2);
}
