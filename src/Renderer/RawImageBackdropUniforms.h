#pragma once
#include <string>

namespace Stack::Renderer::BackdropUniforms {
// Explicit uniform locations are part of the existing OpenGL 4.3 contract.
// Generate declarations from the same locations used by the draw code so no
// shared-program lookup is necessary while a background render is running.
inline std::string Declare(const char* type, const char* name, int location) {
    return "layout(location=" + std::to_string(location) + ") uniform " + type + " " + name + ";\n";
}
namespace Copy {
enum { Image, Previous, Rect, TextureRect, ExtendEdges, PreviousOffset,
    PreviousScale, FadeAmount, EncodedSrgb, VisibleColorOnly };
}
namespace Present {
enum { Image = 0, Blurred = 1, ColorField = 8, Sigma = 9, Photo = 16,
    Controls, BlurPhotoBounds, ScreenSize, Background, ExtendImage,
    ExtensionOpacity, EdgeOverlap, ControlsAmount, NoiseVariance,
    NoiseCovariance, NoiseGrain, NoiseBrightnessVariance, NoiseBrightnessCovariance,
    NoisePhysicalSize, NoiseNativeExtent, NoiseEnabled };
}
}
