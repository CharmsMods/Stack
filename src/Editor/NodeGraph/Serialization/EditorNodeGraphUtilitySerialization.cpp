#include "EditorNodeGraphUtilitySerialization.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <stdexcept>

namespace EditorNodeGraph {

std::string ScopeKindToString(ScopeKind kind) {
    switch (kind) {
        case ScopeKind::Histogram: return "Histogram";
        case ScopeKind::Vectorscope: return "Vectorscope";
        case ScopeKind::RGBParade: return "RGBParade";
    }
    return "Histogram";
}

ScopeKind ScopeKindFromString(const std::string& value) {
    if (value == "Vectorscope") return ScopeKind::Vectorscope;
    if (value == "RGBParade") return ScopeKind::RGBParade;
    return ScopeKind::Histogram;
}

std::string MaskGeneratorKindToString(MaskGeneratorKind kind) {
    switch (kind) {
        case MaskGeneratorKind::Solid: return "Solid";
        case MaskGeneratorKind::LinearGradient: return "LinearGradient";
        case MaskGeneratorKind::RadialGradient: return "RadialGradient";
        case MaskGeneratorKind::Noise: return "Noise";
        case MaskGeneratorKind::Square: return "Square";
        case MaskGeneratorKind::RawGradient: return "RawGradient";
        case MaskGeneratorKind::PaintedArea: return "PaintedArea";
    }
    return "Solid";
}

MaskGeneratorKind MaskGeneratorKindFromString(const std::string& value) {
    if (value == "LinearGradient") return MaskGeneratorKind::LinearGradient;
    if (value == "RadialGradient") return MaskGeneratorKind::RadialGradient;
    if (value == "Noise") return MaskGeneratorKind::Noise;
    if (value == "Square") return MaskGeneratorKind::Square;
    if (value == "RawGradient") return MaskGeneratorKind::RawGradient;
    if (value == "PaintedArea") return MaskGeneratorKind::PaintedArea;
    return MaskGeneratorKind::Solid;
}

std::string MaskUtilityKindToString(MaskUtilityKind kind) {
    switch (kind) {
        case MaskUtilityKind::Invert: return "Invert";
        case MaskUtilityKind::Levels: return "Levels";
        case MaskUtilityKind::Threshold: return "Threshold";
    }
    return "Invert";
}

MaskUtilityKind MaskUtilityKindFromString(const std::string& value) {
    if (value == "Levels") return MaskUtilityKind::Levels;
    if (value == "Threshold") return MaskUtilityKind::Threshold;
    return MaskUtilityKind::Invert;
}

std::string MaskCombineModeToString(MaskCombineMode mode) {
    switch (mode) {
        case MaskCombineMode::Add: return "Add";
        case MaskCombineMode::Subtract: return "Subtract";
        case MaskCombineMode::Intersect: return "Intersect";
        case MaskCombineMode::Exclude: return "Exclude";
    }
    return "Intersect";
}

MaskCombineMode MaskCombineModeFromString(const std::string& value) {
    if (value == "Add") return MaskCombineMode::Add;
    if (value == "Subtract") return MaskCombineMode::Subtract;
    if (value == "Exclude") return MaskCombineMode::Exclude;
    return MaskCombineMode::Intersect;
}

std::string ImageGeneratorKindToString(ImageGeneratorKind kind) {
    switch (kind) {
        case ImageGeneratorKind::SolidColor: return "SolidColor";
        case ImageGeneratorKind::ColorGradient: return "ColorGradient";
        case ImageGeneratorKind::Square: return "Square";
        case ImageGeneratorKind::Circle: return "Circle";
        case ImageGeneratorKind::Text: return "Text";
    }
    return "SolidColor";
}

ImageGeneratorKind ImageGeneratorKindFromString(const std::string& value) {
    if (value == "ColorGradient") return ImageGeneratorKind::ColorGradient;
    if (value == "Square") return ImageGeneratorKind::Square;
    if (value == "Circle") return ImageGeneratorKind::Circle;
    if (value == "Text") return ImageGeneratorKind::Text;
    return ImageGeneratorKind::SolidColor;
}

nlohmann::json SerializeMaskUtilitySettings(const MaskUtilitySettings& settings) {
    return {
        { "blackPoint", settings.blackPoint },
        { "whitePoint", settings.whitePoint },
        { "gamma", settings.gamma },
        { "threshold", settings.threshold },
        { "softness", settings.softness },
        { "enabled", settings.enabled },
        { "invert", settings.invert }
    };
}

MaskUtilitySettings DeserializeMaskUtilitySettings(const nlohmann::json& value) {
    MaskUtilitySettings settings;
    if (!value.is_object()) return settings;
    settings.blackPoint = value.value("blackPoint", settings.blackPoint);
    settings.whitePoint = value.value("whitePoint", settings.whitePoint);
    settings.gamma = value.value("gamma", settings.gamma);
    settings.threshold = value.value("threshold", settings.threshold);
    settings.softness = value.value("softness", settings.softness);
    settings.enabled = value.value("enabled", settings.enabled);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeImageToMaskSettings(const ImageToMaskSettings& settings) {
    nlohmann::json extraSampleRgb = nlohmann::json::array();
    for (int i = 0; i < 4; ++i) {
        extraSampleRgb.push_back({ settings.extraSampleRgb[i][0], settings.extraSampleRgb[i][1], settings.extraSampleRgb[i][2] });
    }
    return {
        { "low", settings.low },
        { "high", settings.high },
        { "softness", settings.softness },
        { "invert", settings.invert },
        { "sampleCount", settings.sampleCount },
        { "sampleRgb", { settings.sampleRgb[0], settings.sampleRgb[1], settings.sampleRgb[2] } },
        { "sampleLuma", settings.sampleLuma },
        { "extraSampleRgb", extraSampleRgb },
        { "extraSampleLuma", { settings.extraSampleLuma[0], settings.extraSampleLuma[1], settings.extraSampleLuma[2], settings.extraSampleLuma[3] } },
        { "sampleU", settings.sampleU },
        { "sampleV", settings.sampleV },
        { "toneSimilarity", settings.toneSimilarity },
        { "colorSimilarity", settings.colorSimilarity },
        { "regionRadius", settings.regionRadius },
        { "regionFeather", settings.regionFeather },
        { "edgeSensitivity", settings.edgeSensitivity },
        { "localCoherence", settings.localCoherence }
    };
}

ImageToMaskSettings DeserializeImageToMaskSettings(const nlohmann::json& value) {
    ImageToMaskSettings settings;
    if (!value.is_object()) return settings;
    settings.low = value.value("low", settings.low);
    settings.high = value.value("high", settings.high);
    settings.softness = value.value("softness", settings.softness);
    settings.invert = value.value("invert", settings.invert);
    settings.sampleCount = std::clamp(value.value("sampleCount", settings.sampleCount), 1, 5);
    if (value.contains("sampleRgb") && value["sampleRgb"].is_array() && value["sampleRgb"].size() >= 3) {
        settings.sampleRgb[0] = value["sampleRgb"][0].get<float>();
        settings.sampleRgb[1] = value["sampleRgb"][1].get<float>();
        settings.sampleRgb[2] = value["sampleRgb"][2].get<float>();
    }
    settings.sampleLuma = value.value("sampleLuma", settings.sampleLuma);
    if (value.contains("extraSampleRgb") && value["extraSampleRgb"].is_array()) {
        for (std::size_t i = 0; i < std::min<std::size_t>(4, value["extraSampleRgb"].size()); ++i) {
            const nlohmann::json& sample = value["extraSampleRgb"][i];
            if (!sample.is_array() || sample.size() < 3) {
                continue;
            }
            settings.extraSampleRgb[i][0] = sample[0].get<float>();
            settings.extraSampleRgb[i][1] = sample[1].get<float>();
            settings.extraSampleRgb[i][2] = sample[2].get<float>();
        }
    }
    if (value.contains("extraSampleLuma") && value["extraSampleLuma"].is_array()) {
        for (std::size_t i = 0; i < std::min<std::size_t>(4, value["extraSampleLuma"].size()); ++i) {
            settings.extraSampleLuma[i] = value["extraSampleLuma"][i].get<float>();
        }
    }
    settings.sampleU = value.value("sampleU", settings.sampleU);
    settings.sampleV = value.value("sampleV", settings.sampleV);
    settings.toneSimilarity = value.value("toneSimilarity", settings.toneSimilarity);
    settings.colorSimilarity = value.value("colorSimilarity", settings.colorSimilarity);
    settings.regionRadius = value.value("regionRadius", settings.regionRadius);
    settings.regionFeather = value.value("regionFeather", settings.regionFeather);
    settings.edgeSensitivity = value.value("edgeSensitivity", settings.edgeSensitivity);
    settings.localCoherence = value.value("localCoherence", settings.localCoherence);
    return settings;
}

nlohmann::json SerializeImageGeneratorSettings(const ImageGeneratorSettings& settings) {
    return {
        { "colorA", { settings.colorA[0], settings.colorA[1], settings.colorA[2], settings.colorA[3] } },
        { "colorB", { settings.colorB[0], settings.colorB[1], settings.colorB[2], settings.colorB[3] } },
        { "angle", settings.angle },
        { "offset", settings.offset },
        { "text", settings.text },
        { "fontSize", settings.fontSize },
        { "textBackdropBlur", settings.textBackdropBlur },
        { "textBackdropOpacity", settings.textBackdropOpacity },
        { "textBackdropPadding", settings.textBackdropPadding }
    };
}

ImageGeneratorSettings DeserializeImageGeneratorSettings(const nlohmann::json& value) {
    ImageGeneratorSettings settings;
    if (!value.is_object()) return settings;
    const nlohmann::json colorA = value.value("colorA", nlohmann::json::array());
    const nlohmann::json colorB = value.value("colorB", nlohmann::json::array());
    for (int i = 0; i < 4; ++i) {
        if (colorA.is_array() && static_cast<int>(colorA.size()) > i) settings.colorA[i] = colorA[i].get<float>();
        if (colorB.is_array() && static_cast<int>(colorB.size()) > i) settings.colorB[i] = colorB[i].get<float>();
    }
    settings.angle = value.value("angle", settings.angle);
    settings.offset = value.value("offset", settings.offset);
    settings.text = value.value("text", settings.text);
    settings.fontSize = value.value("fontSize", settings.fontSize);
    settings.textBackdropBlur = std::clamp(value.value("textBackdropBlur", settings.textBackdropBlur), 0.0f, 128.0f);
    settings.textBackdropOpacity = std::clamp(value.value("textBackdropOpacity", settings.textBackdropOpacity), 0.0f, 1.0f);
    settings.textBackdropPadding = std::clamp(value.value("textBackdropPadding", settings.textBackdropPadding), 0.0f, 256.0f);
    return settings;
}

nlohmann::json SerializeMaskSettings(const MaskGeneratorSettings& settings) {
    return {
        { "value", settings.value },
        { "angle", settings.angle },
        { "offset", settings.offset },
        { "scale", settings.scale },
        { "centerX", settings.centerX },
        { "centerY", settings.centerY },
        { "radius", settings.radius },
        { "radiusY", settings.radiusY },
        { "feather", settings.feather },
        { "invert", settings.invert }
    };
}

MaskGeneratorSettings DeserializeMaskSettings(const nlohmann::json& value) {
    MaskGeneratorSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    settings.value = value.value("value", settings.value);
    settings.angle = value.value("angle", settings.angle);
    settings.offset = value.value("offset", settings.offset);
    settings.scale = value.value("scale", settings.scale);
    settings.centerX = value.value("centerX", settings.centerX);
    settings.centerY = value.value("centerY", settings.centerY);
    settings.radius = value.value("radius", settings.radius);
    settings.radiusY = value.value("radiusY", settings.radius);
    settings.feather = value.value("feather", settings.feather);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

std::string MixBlendModeToString(MixBlendMode mode) {
    switch (mode) {
        case MixBlendMode::Normal: return "Normal";
        case MixBlendMode::Average: return "Average";
        case MixBlendMode::Add: return "Add";
        case MixBlendMode::Multiply: return "Multiply";
        case MixBlendMode::Screen: return "Screen";
        case MixBlendMode::StraightSourceOver: return "StraightSourceOver";
        case MixBlendMode::PremultipliedSourceOver: return "PremultipliedSourceOver";
    }
    return "Normal";
}

MixBlendMode MixBlendModeFromString(const std::string& value) {
    if (value == "Average") return MixBlendMode::Average;
    if (value == "Add") return MixBlendMode::Add;
    if (value == "Multiply") return MixBlendMode::Multiply;
    if (value == "Screen") return MixBlendMode::Screen;
    if (value == "StraightSourceOver") return MixBlendMode::StraightSourceOver;
    if (value == "PremultipliedSourceOver") return MixBlendMode::PremultipliedSourceOver;
    return MixBlendMode::Normal;
}

std::string DataMathModeToString(DataMathMode mode) {
    switch (mode) {
        case DataMathMode::Clamp: return "Clamp";
        case DataMathMode::Add: return "Add";
        case DataMathMode::Subtract: return "Subtract";
        case DataMathMode::Multiply: return "Multiply";
        case DataMathMode::Divide: return "Divide";
        case DataMathMode::Average: return "Average";
        case DataMathMode::Min: return "Min";
        case DataMathMode::Max: return "Max";
        case DataMathMode::Difference: return "Difference";
        case DataMathMode::Remap: return "Remap";
        case DataMathMode::ImageAverage: return "ImageAverage";
    }
    return "Clamp";
}

DataMathMode DataMathModeFromString(const std::string& value) {
    if (value == "Add") return DataMathMode::Add;
    if (value == "Subtract") return DataMathMode::Subtract;
    if (value == "Multiply") return DataMathMode::Multiply;
    if (value == "Divide") return DataMathMode::Divide;
    if (value == "Average") return DataMathMode::Average;
    if (value == "Min") return DataMathMode::Min;
    if (value == "Max") return DataMathMode::Max;
    if (value == "Difference") return DataMathMode::Difference;
    if (value == "Remap") return DataMathMode::Remap;
    if (value == "ImageAverage") {
        return DataMathMode::ImageAverage;
    }
    return DataMathMode::Clamp;
}

nlohmann::json SerializeDataMathSettings(const DataMathSettings& settings) {
    nlohmann::json value = {
        { "domain", settings.domain == Stack::NodeMath::ArithmeticDomain::Values ? "values" : settings.domain == Stack::NodeMath::ArithmeticDomain::Channel ? "channel" : "spatial" },
        { "constantA", settings.constantA },
        { "constantB", settings.constantB },
        { "minValue", settings.minValue },
        { "maxValue", settings.maxValue },
        { "outMin", settings.outMin },
        { "outMax", settings.outMax }
    };
    if (!settings.requiredLinearBasis.empty()) value["requiredLinearBasis"] = settings.requiredLinearBasis;
    if (settings.boundedMask) value["boundedMask"] = true;
    if (settings.percentageInput) value["percentageInput"] = true;
    return value;
}

DataMathSettings DeserializeDataMathSettings(const nlohmann::json& value) {
    DataMathSettings settings;
    if (!value.is_object()) return settings;
    settings.requiredLinearBasis = value.value("requiredLinearBasis", std::string{});
    settings.boundedMask = value.value("boundedMask", false);
    settings.percentageInput = value.value("percentageInput", false);
    const auto domain = value.value("domain", std::string("spatial"));
    if (domain != "values" && domain != "spatial" && domain != "channel") throw std::runtime_error("Unknown arithmetic domain.");
    settings.domain = domain == "values" ? Stack::NodeMath::ArithmeticDomain::Values : domain == "channel" ? Stack::NodeMath::ArithmeticDomain::Channel : Stack::NodeMath::ArithmeticDomain::Spatial;
    settings.constantA = value.value("constantA", settings.constantA);
    settings.constantB = value.value("constantB", settings.constantB);
    settings.minValue = value.value("minValue", settings.minValue);
    settings.maxValue = value.value("maxValue", settings.maxValue);
    settings.outMin = value.value("outMin", settings.outMin);
    settings.outMax = value.value("outMax", settings.outMax);
    return settings;
}

std::string TechnicalImageOperationToString(Stack::NodeMath::TechnicalImageOperation operation) {
    using Operation = Stack::NodeMath::TechnicalImageOperation;
    switch (operation) {
        case Operation::AssignSrgb: return "AssignSrgb";
        case Operation::AssignLinearSrgb: return "AssignLinearSrgb";
        case Operation::AssignLinearDisplayP3: return "AssignLinearDisplayP3";
        case Operation::SrgbDecode: return "SrgbDecode";
        case Operation::SrgbEncode: return "SrgbEncode";
        case Operation::LinearSrgbToDisplayP3: return "LinearSrgbToDisplayP3";
        case Operation::LinearDisplayP3ToSrgb: return "LinearDisplayP3ToSrgb";
        case Operation::Exposure: return "Exposure";
        case Operation::Premultiply: return "Premultiply";
        case Operation::Unpremultiply: return "Unpremultiply";
    }
    return "Exposure";
}

Stack::NodeMath::TechnicalImageOperation TechnicalImageOperationFromString(const std::string& value) {
    using Operation = Stack::NodeMath::TechnicalImageOperation;
    if (value == "AssignSrgb") return Operation::AssignSrgb;
    if (value == "AssignLinearSrgb") return Operation::AssignLinearSrgb;
    if (value == "AssignLinearDisplayP3") return Operation::AssignLinearDisplayP3;
    if (value == "SrgbDecode") return Operation::SrgbDecode;
    if (value == "SrgbEncode") return Operation::SrgbEncode;
    if (value == "LinearSrgbToDisplayP3") return Operation::LinearSrgbToDisplayP3;
    if (value == "LinearDisplayP3ToSrgb") return Operation::LinearDisplayP3ToSrgb;
    if (value == "Premultiply") return Operation::Premultiply;
    if (value == "Unpremultiply") return Operation::Unpremultiply;
    return Operation::Exposure;
}

nlohmann::json SerializeTechnicalImageSettings(const TechnicalImageSettings& settings) {
    return {
        { "operation", TechnicalImageOperationToString(settings.operation) },
        { "exposureValue", settings.exposureValue }
    };
}

TechnicalImageSettings DeserializeTechnicalImageSettings(const nlohmann::json& value) {
    TechnicalImageSettings settings;
    if (!value.is_object()) return settings;
    settings.operation = TechnicalImageOperationFromString(
        value.value("operation", std::string("Exposure")));
    settings.exposureValue = value.value("exposureValue", settings.exposureValue);
    return settings;
}

std::string ReconstructionFilterToString(Stack::NodeMath::ReconstructionFilter filter) {
    return filter == Stack::NodeMath::ReconstructionFilter::Nearest ? "Nearest" : "Linear";
}

Stack::NodeMath::ReconstructionFilter ReconstructionFilterFromString(const std::string& value) {
    return value == "Nearest"
        ? Stack::NodeMath::ReconstructionFilter::Nearest
        : Stack::NodeMath::ReconstructionFilter::Linear;
}

nlohmann::json SerializeReformatSettings(const ReformatSettings& settings) {
    return {
        { "width", settings.width },
        { "canvasRatio", settings.canvasRatio },
        { "customCanvasAspect", settings.customCanvasAspect },
        { "visibleAspect", settings.visibleAspect },
        { "sideBars", settings.sideBars },
        { "topBottomBars", settings.topBottomBars },
        { "sourceScale", settings.sourceScale },
        { "sourceX", settings.sourceX },
        { "sourceY", settings.sourceY },
        { "height", settings.height },
        { "filter", ReconstructionFilterToString(settings.filter) },
        { "border", "Clamp" }
    };
}

ReformatSettings DeserializeReformatSettings(const nlohmann::json& value) {
    ReformatSettings settings;
    if (!value.is_object()) return settings;
    settings.width = std::clamp(
        value.value("width", settings.width), 1,
        Stack::NodeMath::kMaximumReformatDimension);
    settings.height = std::clamp(
        value.value("height", settings.height), 1,
        Stack::NodeMath::kMaximumReformatDimension);
    settings.filter = ReconstructionFilterFromString(
        value.value("filter", std::string("Linear")));
    settings.canvasRatio = value.value("canvasRatio", settings.canvasRatio);
    settings.customCanvasAspect = value.value("customCanvasAspect", settings.customCanvasAspect);
    settings.visibleAspect = value.value("visibleAspect", settings.visibleAspect);
    settings.sideBars = value.value("sideBars", settings.sideBars);
    settings.topBottomBars = value.value("topBottomBars", settings.topBottomBars);
    settings.sourceScale = value.value("sourceScale", settings.sourceScale);
    settings.sourceX = value.value("sourceX", settings.sourceX);
    settings.sourceY = value.value("sourceY", settings.sourceY);
    if (!Stack::NodeMath::ValidateReformatSettings(settings).empty()) return ReformatSettings{};
    settings.border = Stack::NodeMath::BorderPolicy::Clamp;
    return settings;
}

















































































std::string ImageToMaskKindToString(ImageToMaskKind kind) {
    switch (kind) {
        case ImageToMaskKind::Luminance: return "Luminance";
        case ImageToMaskKind::SampledRange: return "SampledRange";
    }
    return "Luminance";
}

ImageToMaskKind ImageToMaskKindFromString(const std::string& value) {
    if (value == "SampledRange") return ImageToMaskKind::SampledRange;
    return ImageToMaskKind::Luminance;
}

} // namespace EditorNodeGraph
