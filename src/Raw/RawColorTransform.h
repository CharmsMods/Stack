#pragma once
#include "RawImageData.h"
#include <array>

namespace Raw {
// Shared color-domain math for RAW GPU development and file output. These
// helpers do not allocate GPU resources or require an editor/context.
std::array<float, 3> ResolveRawWhiteBalance(
    const RawMetadata&, const RawDevelopSettings&);
std::array<float, 9> BuildRawCameraToWorkingTransform(
    const RawMetadata&, const RawDevelopSettings&, bool inputAlreadyWhiteBalanced);
// Camera-domain vector that maps to neutral working RGB after the active matrix.
std::array<float, 3> ResolveRawHighlightNeutral(
    const std::array<float, 9>& cameraToWorking, const RawDevelopSettings&);
}
