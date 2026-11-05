#pragma once
#include "Raw/RawDevelopmentRecipe.h"

namespace Stack::RawRecipe {
float ColorWarpHue(float a, float b);
float ColorWarpHueDelta(float degrees);
std::array<float,2> ColorWarpSelectionCoordinate(const RawColorWarpCoordinate& input);
std::array<float,2> ColorWarpDestination(const RawColorWarpPin& pin);
float ColorWarpRangeWeight(const RawColorWarpRange& range, float value);
bool ColorWarpHasAdjustment(const RawColorWarpPin& pin);
bool ColorWarpParticipates(const RawColorWarpPin& pin);
bool SetColorWarpSample(RawColorWarpPin& pin, const RawColorWarpCoordinate& input);
RawColorWarpPin MakeColorWarpFamily(int family);
const char* ColorWarpFamilyName(int family);
std::size_t ColorWarpSampleCount(const RawColorWarpRecipe& recipe);
void SanitizeColorWarpPin(RawColorWarpPin& pin);
}
