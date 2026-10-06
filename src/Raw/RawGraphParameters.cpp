#include "Raw/RawGraphParameters.h"
namespace Stack::RawRecipe {
std::vector<GraphParameter> GraphParameters(GraphOperationKind kind) {
    std::vector<GraphParameter> values;
    const auto add = [&](std::string id, std::string label, std::string path, float initial, float low, float high, std::string units = "unitless") {
        values.push_back({std::move(id), std::move(label), std::move(path), std::move(units), initial, low, high});
    };
    switch (kind) {
        case GraphOperationKind::Exposure: add("ev", "Exposure", "/ev", 0, -32, 32, "EV"); break;
        case GraphOperationKind::Calibration:
            for (int i = 0; i < 3; ++i) {
                const std::string channel = i == 0 ? "Red" : i == 1 ? "Green" : "Blue";
                add("hue-" + std::to_string(i), channel + " hue", "/primaries/" + std::to_string(i) + "/hue", 0, -100, 100);
                add("saturation-" + std::to_string(i), channel + " saturation", "/primaries/" + std::to_string(i) + "/saturation", 0, -100, 100);
            }
            break;
        case GraphOperationKind::LocalEv: add("strength", "Strength", "/localRange/strength", 1, 0, 2, "ratio"); break;
        case GraphOperationKind::LuminanceTone:
            add("contrast", "Contrast", "/contrast", 1, .025f, 8, "ratio");
            add("pivot", "Pivot", "/pivotEv", 0, -16, 16, "EV");
            add("anchor", "Anchor offset", "/anchorOffsetEv", 0, -16, 16, "EV");
            add("width", "Range half width", "/coreHalfWidthEv", 1, .1f, 16, "EV");
            add("transition", "Transition", "/transitionEv", 2, .1f, 16, "EV");
            add("protection", "Outer tone protection", "/outerProtection", 0, 0, 1, "ratio");
            break;
        case GraphOperationKind::ColorWarp: add("strength", "Strength", "/strength", 1, 0, 2, "ratio"); break;
        case GraphOperationKind::DetailContrast:
            add("scale", "Maximum scale", "/maximumScale", 256, 4, 2048, "source-pixels");
            add("edge-protection", "Edge protection", "/edgeProtection", .75f, 0, 1, "ratio");
            add("target", "Target EV", "/targetEv", 0, -16, 16, "EV");
            add("target-width", "Target half width", "/targetHalfWidthEv", 2, .1f, 16, "EV");
            add("target-feather", "Target feather", "/targetFeatherEv", 2, .1f, 16, "EV");
            for (int i = 0; i < kDetailBands; ++i)
                add("band-" + std::to_string(i), "Scale band " + std::to_string(i + 1), "/scaleGains/" + std::to_string(i), 1, 0, 3, "ratio");
            break;
        default: break;
    }
    return values;
}
}
