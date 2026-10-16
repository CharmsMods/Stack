#pragma once
#include "Assets/CreatedAsset.h"
#include "Raw/RawDevelopmentRecipe.h"
#include <array>
#include <functional>

namespace Stack::RawAssetCapture {
enum class Shape { Line, Circle, Stroke };
enum class Output { Samples, LumaStrip, ChromaStrip, ColorStrip, LumaImage, ChromaImage, SpatialMask, LumaMatch, ColorMatch, BothMatch };
enum class Channels { Luminance, Chroma, Both };
struct Selection {
    Shape shape = Shape::Line;
    Channels channels = Channels::Luminance;
    Output output = Output::Samples;
    std::vector<std::array<float,2>> path, exclusion;
    float strokeRadius = .01f;
    // Top-left normalized source coordinates, matching Color Warp circles.
    std::array<float,2> start{.5f,.5f}, end{.5f,.5f};
    float radiusU = 0, radiusV = 0;
    // Fractions of each sampled channel's span. Inner contracts full coverage;
    // outer expands the zero-coverage boundary.
    float innerFeather = .1f, outerFeather = .1f;
};
struct Range {
    std::array<float,3> minimum{}, maximum{};
    std::size_t count = 0;
};
struct Qualifier {
    Channels channels = Channels::Luminance;
    std::array<float,3> innerMinimum{}, innerMaximum{}, outerMinimum{}, outerMaximum{};
};
struct PreviewField {
    int width = 0, height = 0;
    // Top-down linear Y, Oklab a, Oklab b. Temporary feedback data.
    std::vector<float> values;
};
std::shared_ptr<const PreviewField> PreparePreview(const std::vector<float>& rgba,
    int width, int height, Raw::RawWorkingSpace workingSpace, const std::function<bool()>& canceled);
Range Measure(const PreviewField& field, const Selection& selection);
Qualifier MakeQualifier(const Range& range, const Selection& selection);
float Coverage(const std::array<float,3>& value, const Qualifier& qualifier);
Assets::Record CaptureStroke(const std::vector<float>& rgba, int width, int height,
    Raw::RawWorkingSpace workingSpace, const Selection& selection,
    const std::function<bool()>& canceled);
Assets::Record Capture(const std::vector<float>& rgba, int width, int height,
    Raw::RawWorkingSpace workingSpace, const Selection& selection,
    const std::function<bool()>& canceled);
} // namespace Stack::RawAssetCapture
