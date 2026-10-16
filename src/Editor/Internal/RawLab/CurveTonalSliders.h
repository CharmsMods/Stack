#pragma once

#include "Raw/Curves/CurveTypes.h"
#include <array>

namespace Stack::Editor::RawLabInternal {
struct CurveCanvasState;

// The owning editor supplies positions in its authored domain. Existing owned
// points retain their saved x when the display or domain settings change.
struct CurveTonalSliderDomain {
    std::array<float, 5> positions {{.05f, .25f, .5f, .75f, .95f}};
};

CurveTonalSliderDomain ExposureTonalSliderDomain(float minimumEv, float maximumEv);
bool DrawCurveTonalSliders(std::vector<RawRecipe::RawBezierCurvePoint>& points,
    CurveCanvasState& state, const CurveTonalSliderDomain& domain,
    int maximumPoints, bool disabled);
}
