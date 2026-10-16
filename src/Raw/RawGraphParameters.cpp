#include "Raw/RawGraphParameters.h"
#include <limits>
namespace Stack::RawRecipe {
std::vector<GraphParameter> GraphParameters(const GraphOperation& operation) {
    return GraphParameters(operation.kind);
}
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
                add("hue-" + std::to_string(i), channel + " hue", "/primaries/" + std::to_string(i) + "/hue", 0,
                    std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
                add("saturation-" + std::to_string(i), channel + " saturation", "/primaries/" + std::to_string(i) + "/saturation", 0,
                    std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
            }
            break;
        case GraphOperationKind::LocalEv:
            add("strength", "Strength", "/localRange/strength", 1, 0, 2, "ratio");
            add("normalization", "Curve normalization", "/localRange/curveNormalization", 0, 0, 1, "ratio");
            break;
        case GraphOperationKind::ToneCurve:
            add("luminance-amount", "Luminance curve amount", "/luminanceTone/curveAmount/amount", 1, 0, 1, "ratio");
            for (int channel=0;channel<4;++channel) {
                const auto key=RawPointCurveChannelKey(static_cast<RawPointCurveChannel>(channel));
                add(std::string("amount-")+key,std::string(key)+" curve amount",
                    std::string("/curveAmounts/")+key+"/amount",1,0,1,"ratio");
            }
            add("contrast", "Contrast", "/luminanceTone/contrast", 1, .05f, 4, "ratio");
            add("pivot", "Pivot", "/luminanceTone/pivotEv", 0, -12, 12, "EV");
            add("anchor", "Anchor offset", "/luminanceTone/anchorOffsetEv", 0, -12, 12, "EV");
            add("width", "Range half width", "/luminanceTone/coreHalfWidthEv", 1, .125f, 8, "EV");
            add("transition", "Transition", "/luminanceTone/transitionEv", 2, .25f, 12, "EV");
            add("protection", "Outer tone protection", "/luminanceTone/outerProtection", 0, 0, 1, "ratio");
            break;
        case GraphOperationKind::ColorWarp: add("strength", "Strength", "/strength", 1, 0, 2, "ratio"); break;
        case GraphOperationKind::DetailContrast:
            add("scale", "Maximum scale", "/maximumScale", 256, 4, 2048, "source-pixels");
            add("edge-protection", "Edge protection", "/edgeProtection", .75f, 0, 1, "ratio");
            add("target", "Target EV", "/targetEv", 0, -16, 16, "EV");
            add("target-width", "Target half width", "/targetHalfWidthEv", 2, .25f, 12, "EV");
            add("target-feather", "Target feather", "/targetFeatherEv", 2, .25f, 12, "EV");
            for (int i = 0; i < kDetailBands; ++i)
                add("band-" + std::to_string(i), "Scale band " + std::to_string(i + 1), "/scaleGains/" + std::to_string(i), 1, 0, 3, "ratio");
            break;
        default: break;
    }
    return values;
}
}
