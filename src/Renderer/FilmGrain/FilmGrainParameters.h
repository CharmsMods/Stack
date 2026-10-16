#pragma once
#include <array>
#include <cstdint>
#include "Raw/Curves/CurveTypes.h"

namespace Stack::Renderer::FilmGrain {
// Authored values. Radius and optical blur refer to the film plane, not the
// current preview raster. This is a stochastic emulsion texture, not a stock LUT.
struct Parameters {
    float densityDeviation = .035f;
    float radiusMicrons = 3.f, radiusVariation = .35f, maximumRadiusFactor = 3.f;
    float filmWidthMm = 36.f, opticalBlurMicrons = 1.5f, coverage = .5f;
    float colorAmount = .25f, channelCorrelation = .65f;
    std::array<float,3> channelSize{1.f,1.f,1.f}, channelStrength{1.f,1.f,1.f};
    float shadows = 1.2f, midtones = 1.f, highlights = .6f, tonalPivot = .18f;
    float exposureFloor = .005f, mix = 1.f;
    int encoding = 0; // 0: declared input transfer, 1: linear light, 2: sRGB.
    int output = 0;   // 0: optical density, 1: additive, 2: grain inspection.
    int samples = 16, seed = 125775, frame = 0;
    bool compensateMean = true;
    // An additional 0..3 response multiplier over linear luminance 0..1.
    // Stored Y is normalized to the shared curve canvas. Flat 1x preserves
    // existing three-zone response, and there is no authored point-count cap.
    std::vector<RawRecipe::RawBezierCurvePoint> responseCurve{
        {0.f,1.f/3.f,{1},{1}}, {1.f,1.f/3.f,{1},{1}}};
    void Sanitize();
};

bool SameResponseCurve(const std::vector<RawRecipe::RawBezierCurvePoint>& a,
    const std::vector<RawRecipe::RawBezierCurvePoint>& b);
float EvaluateResponse(const Parameters& parameters, float linearLuminance);

// E[min(R,Rmax)^2] for a lognormal radius distribution whose unclipped mean is R.
double ClippedRadiusSecondMoment(double mean, double variation, double maximumFactor);
}
