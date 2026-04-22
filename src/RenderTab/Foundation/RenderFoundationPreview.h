#pragma once

#include "RenderFoundationState.h"

#include <string>
#include <vector>

namespace RenderFoundation {

enum class PreviewGlyph : std::uint8_t {
    Sphere = 0,
    Cube = 1,
    Plane = 2,
    LightPoint = 3,
    LightSpot = 4,
    LightArea = 5,
    LightDirectional = 6
};

struct ProjectedElement {
    SelectionType selectionType = SelectionType::None;
    Id id = 0;
    PreviewGlyph glyph = PreviewGlyph::Sphere;
    Vec3 color { 1.0f, 1.0f, 1.0f };
    Vec2 center {};
    Vec2 halfSize {};
    float depth = 0.0f;
    bool selected = false;
    std::string label;
};

std::vector<ProjectedElement> BuildProjectedElements(const State& state, float width, float height);

void BuildPreviewPixels(
    const State& state,
    int width,
    int height,
    int sampleCount,
    std::vector<unsigned char>& outPixels);

} // namespace RenderFoundation
