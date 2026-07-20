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
    if (value == "StraightSourceOver" || value == "Source Over (Straight)") return MixBlendMode::StraightSourceOver;
    if (value == "PremultipliedSourceOver" || value == "Source Over (Premultiplied)") return MixBlendMode::PremultipliedSourceOver;
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
    settings.border = Stack::NodeMath::BorderPolicy::Clamp;
    return settings;
}

std::string SpectrumViewLutToString(SpectrumViewLut lut) {
    switch (lut) {
        case SpectrumViewLut::Turbo: return "Turbo";
        case SpectrumViewLut::Viridis: return "Viridis";
        case SpectrumViewLut::Inferno: return "Inferno";
        case SpectrumViewLut::Grayscale: return "Grayscale";
    }
    return "Turbo";
}

SpectrumViewLut SpectrumViewLutFromString(const std::string& value) {
    if (value == "Viridis") return SpectrumViewLut::Viridis;
    if (value == "Inferno") return SpectrumViewLut::Inferno;
    if (value == "Grayscale" || value == "Gray" || value == "Grey") return SpectrumViewLut::Grayscale;
    return SpectrumViewLut::Turbo;
}

std::string FrequencyMaskShapeToString(FrequencyMaskShape shape) {
    switch (shape) {
        case FrequencyMaskShape::LowPass: return "LowPass";
        case FrequencyMaskShape::HighPass: return "HighPass";
        case FrequencyMaskShape::BandPass: return "BandPass";
        case FrequencyMaskShape::BandStop: return "BandStop";
        case FrequencyMaskShape::Notch: return "Notch";
        case FrequencyMaskShape::Gaussian: return "Gaussian";
        case FrequencyMaskShape::Butterworth: return "Butterworth";
    }
    return "LowPass";
}

FrequencyMaskShape FrequencyMaskShapeFromString(const std::string& value) {
    if (value == "HighPass" || value == "High Pass") return FrequencyMaskShape::HighPass;
    if (value == "BandPass" || value == "Band Pass") return FrequencyMaskShape::BandPass;
    if (value == "BandStop" || value == "Band Stop") return FrequencyMaskShape::BandStop;
    if (value == "Notch") return FrequencyMaskShape::Notch;
    if (value == "Gaussian") return FrequencyMaskShape::Gaussian;
    if (value == "Butterworth") return FrequencyMaskShape::Butterworth;
    return FrequencyMaskShape::LowPass;
}

std::string SpectrumMathModeToString(SpectrumMathMode mode) {
    switch (mode) {
        case SpectrumMathMode::Multiply: return "Multiply";
        case SpectrumMathMode::Add: return "Add";
        case SpectrumMathMode::Subtract: return "Subtract";
        case SpectrumMathMode::Difference: return "Difference";
    }
    return "Multiply";
}

SpectrumMathMode SpectrumMathModeFromString(const std::string& value) {
    if (value == "Add") return SpectrumMathMode::Add;
    if (value == "Subtract") return SpectrumMathMode::Subtract;
    if (value == "Difference") return SpectrumMathMode::Difference;
    return SpectrumMathMode::Multiply;
}

std::string MagnitudePhaseModeToString(MagnitudePhaseMode mode) {
    switch (mode) {
        case MagnitudePhaseMode::Magnitude: return "Magnitude";
        case MagnitudePhaseMode::Phase: return "Phase";
        case MagnitudePhaseMode::Recombine: return "Recombine";
    }
    return "Magnitude";
}

MagnitudePhaseMode MagnitudePhaseModeFromString(const std::string& value) {
    if (value == "Phase") return MagnitudePhaseMode::Phase;
    if (value == "Recombine") return MagnitudePhaseMode::Recombine;
    return MagnitudePhaseMode::Magnitude;
}

std::string SpectrumAnalyzerModeToString(SpectrumAnalyzerMode mode) {
    switch (mode) {
        case SpectrumAnalyzerMode::RadialEnergy: return "RadialEnergy";
        case SpectrumAnalyzerMode::DominantFrequency: return "DominantFrequency";
    }
    return "RadialEnergy";
}

SpectrumAnalyzerMode SpectrumAnalyzerModeFromString(const std::string& value) {
    if (value == "DominantFrequency" || value == "Dominant Frequency") {
        return SpectrumAnalyzerMode::DominantFrequency;
    }
    return SpectrumAnalyzerMode::RadialEnergy;
}

nlohmann::json SerializeFrequencyFftSettings(const FrequencyFftSettings& settings) {
    return {
        { "luminanceOnly", settings.luminanceOnly }
    };
}

FrequencyFftSettings DeserializeFrequencyFftSettings(const nlohmann::json& value) {
    FrequencyFftSettings settings;
    if (!value.is_object()) return settings;
    settings.luminanceOnly = value.value("luminanceOnly", settings.luminanceOnly);
    return settings;
}

nlohmann::json SerializeSpectrumViewSettings(const SpectrumViewSettings& settings) {
    return {
        { "lut", SpectrumViewLutToString(settings.lut) },
        { "exposure", settings.exposure },
        { "gamma", settings.gamma },
        { "centerDc", settings.centerDc }
    };
}

SpectrumViewSettings DeserializeSpectrumViewSettings(const nlohmann::json& value) {
    SpectrumViewSettings settings;
    if (!value.is_object()) return settings;
    settings.lut = SpectrumViewLutFromString(value.value("lut", SpectrumViewLutToString(settings.lut)));
    settings.exposure = std::clamp(value.value("exposure", settings.exposure), 0.01f, 32.0f);
    settings.gamma = std::clamp(value.value("gamma", settings.gamma), 0.1f, 4.0f);
    settings.centerDc = value.value("centerDc", settings.centerDc);
    return settings;
}

nlohmann::json SerializeFrequencyMaskSettings(const FrequencyMaskSettings& settings) {
    return {
        { "shape", FrequencyMaskShapeToString(settings.shape) },
        { "cutoff", settings.cutoff },
        { "width", settings.width },
        { "feather", settings.feather },
        { "order", settings.order },
        { "centerX", settings.centerX },
        { "centerY", settings.centerY },
        { "invert", settings.invert }
    };
}

FrequencyMaskSettings DeserializeFrequencyMaskSettings(const nlohmann::json& value) {
    FrequencyMaskSettings settings;
    if (!value.is_object()) return settings;
    settings.shape = FrequencyMaskShapeFromString(value.value("shape", FrequencyMaskShapeToString(settings.shape)));
    settings.cutoff = std::clamp(value.value("cutoff", settings.cutoff), 0.0f, 1.0f);
    settings.width = std::clamp(value.value("width", settings.width), 0.0f, 1.0f);
    settings.feather = std::clamp(value.value("feather", settings.feather), 0.0f, 1.0f);
    settings.order = std::clamp(value.value("order", settings.order), 1.0f, 12.0f);
    settings.centerX = std::clamp(value.value("centerX", settings.centerX), 0.0f, 1.0f);
    settings.centerY = std::clamp(value.value("centerY", settings.centerY), 0.0f, 1.0f);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeSpectrumMathSettings(const SpectrumMathSettings& settings) {
    return {
        { "amount", settings.amount }
    };
}

SpectrumMathSettings DeserializeSpectrumMathSettings(const nlohmann::json& value) {
    SpectrumMathSettings settings;
    if (!value.is_object()) return settings;
    settings.amount = std::clamp(value.value("amount", settings.amount), 0.0f, 4.0f);
    return settings;
}

nlohmann::json SerializeMagnitudePhaseSettings(const MagnitudePhaseSettings& settings) {
    return {
        { "exposure", settings.exposure },
        { "gamma", settings.gamma }
    };
}

MagnitudePhaseSettings DeserializeMagnitudePhaseSettings(const nlohmann::json& value) {
    MagnitudePhaseSettings settings;
    if (!value.is_object()) return settings;
    settings.exposure = std::clamp(value.value("exposure", settings.exposure), 0.01f, 32.0f);
    settings.gamma = std::clamp(value.value("gamma", settings.gamma), 0.1f, 4.0f);
    return settings;
}

nlohmann::json SerializeSpectrumAnalyzerSettings(const SpectrumAnalyzerSettings& settings) {
    return {
        { "innerRadius", settings.innerRadius },
        { "outerRadius", settings.outerRadius }
    };
}

SpectrumAnalyzerSettings DeserializeSpectrumAnalyzerSettings(const nlohmann::json& value) {
    SpectrumAnalyzerSettings settings;
    if (!value.is_object()) return settings;
    settings.innerRadius = std::clamp(value.value("innerRadius", settings.innerRadius), 0.0f, 1.0f);
    settings.outerRadius = std::clamp(value.value("outerRadius", settings.outerRadius), 0.0f, 1.0f);
    if (settings.outerRadius < settings.innerRadius) {
        std::swap(settings.innerRadius, settings.outerRadius);
    }
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
