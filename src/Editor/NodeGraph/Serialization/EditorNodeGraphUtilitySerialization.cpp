#include "EditorNodeGraphUtilitySerialization.h"

#include <algorithm>
#include <cstddef>
#include <string>

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
    if (value == "RGBParade" || value == "RGB Parade") return ScopeKind::RGBParade;
    return ScopeKind::Histogram;
}

std::string MaskGeneratorKindToString(MaskGeneratorKind kind) {
    switch (kind) {
        case MaskGeneratorKind::Solid: return "Solid";
        case MaskGeneratorKind::LinearGradient: return "LinearGradient";
        case MaskGeneratorKind::RadialGradient: return "RadialGradient";
        case MaskGeneratorKind::Noise: return "Noise";
    }
    return "Solid";
}

MaskGeneratorKind MaskGeneratorKindFromString(const std::string& value) {
    if (value == "LinearGradient" || value == "Linear Gradient") return MaskGeneratorKind::LinearGradient;
    if (value == "RadialGradient" || value == "Radial Gradient") return MaskGeneratorKind::RadialGradient;
    if (value == "Noise" || value == "Noise Mask") return MaskGeneratorKind::Noise;
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
    if (value == "ColorGradient" || value == "Color Gradient") return ImageGeneratorKind::ColorGradient;
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
        case MixBlendMode::AlphaOver: return "AlphaOver";
    }
    return "Normal";
}

MixBlendMode MixBlendModeFromString(const std::string& value) {
    if (value == "Average") return MixBlendMode::Average;
    if (value == "Add") return MixBlendMode::Add;
    if (value == "Multiply") return MixBlendMode::Multiply;
    if (value == "Screen") return MixBlendMode::Screen;
    if (value == "AlphaOver" || value == "Alpha Over") return MixBlendMode::AlphaOver;
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
    if (value == "Min" || value == "Minimum") return DataMathMode::Min;
    if (value == "Max" || value == "Maximum") return DataMathMode::Max;
    if (value == "Difference" || value == "AbsDiff") return DataMathMode::Difference;
    if (value == "Remap") return DataMathMode::Remap;
    if (value == "ImageAverage" || value == "Image Average" || value == "AverageImages" || value == "Average Images") {
        return DataMathMode::ImageAverage;
    }
    return DataMathMode::Clamp;
}

nlohmann::json SerializeDataMathSettings(const DataMathSettings& settings) {
    return {
        { "constantA", settings.constantA },
        { "constantB", settings.constantB },
        { "minValue", settings.minValue },
        { "maxValue", settings.maxValue },
        { "outMin", settings.outMin },
        { "outMax", settings.outMax }
    };
}

DataMathSettings DeserializeDataMathSettings(const nlohmann::json& value) {
    DataMathSettings settings;
    if (!value.is_object()) return settings;
    settings.constantA = value.value("constantA", settings.constantA);
    settings.constantB = value.value("constantB", settings.constantB);
    settings.minValue = value.value("minValue", settings.minValue);
    settings.maxValue = value.value("maxValue", settings.maxValue);
    settings.outMin = value.value("outMin", settings.outMin);
    settings.outMax = value.value("outMax", settings.outMax);
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
    if (value == "SampledRange" || value == "Sampled Range") return ImageToMaskKind::SampledRange;
    return ImageToMaskKind::Luminance;
}

} // namespace EditorNodeGraph
