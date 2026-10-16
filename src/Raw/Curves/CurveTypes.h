#pragma once
#include <vector>
namespace Stack::RawRecipe {
// Curve handles are stored as normalized offsets from their anchor point so
// moving an anchor carries its manually authored handle with it. Strength is
// normalized to the UI's 0%-150% range: 0.0 is straight, 1.0 is the normal
// smooth/custom handle, and 1.5 is an exact neighboring-tangent match.
struct RawBezierHandleState {
    float strength = 0.0f;
    bool manual = false;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

// Slider ownership travels with authored points and amount references. Both
// baselineY and originalX use normalized curve coordinates, including when the
// containing Local EV point stores its ordinary coordinates in EV.
struct CurveTonalPointState {
    int region = -1;
    float baselineY = 0.0f;
    bool replacesPoint = false;
    float originalX = 0.0f;
    RawBezierHandleState incoming;
    RawBezierHandleState outgoing;
};

struct RawBezierCurvePoint {
    float x = 0.0f;
    float y = 0.0f;
    RawBezierHandleState incoming;
    RawBezierHandleState outgoing;
    CurveTonalPointState tonal;
};

struct RawBezierSegment {
    RawBezierCurvePoint left;
    RawBezierCurvePoint right;
    float leftHandleX = 0.0f;
    float leftHandleY = 0.0f;
    float rightHandleX = 0.0f;
    float rightHandleY = 0.0f;
};


// The reference stays authored while the visible/effective points are attenuated.
struct CurveAmountState {
    bool locked = false;
    float amount = 1.0f;
    std::vector<RawBezierCurvePoint> reference;
};
}
