#include "Raw/RawGraphOperation.h"

#include <stdexcept>

namespace Stack::RawRecipe {
const char* GraphOperationId(GraphOperationKind kind) {
    switch (kind) {
        case GraphOperationKind::Calibration: return "calibration";
        case GraphOperationKind::Exposure: return "exposure";
        case GraphOperationKind::LocalEv: return "local-ev";
        case GraphOperationKind::LuminanceTone: return "luminance-tone";
        case GraphOperationKind::RgbCurves: return "rgb-curves";
        case GraphOperationKind::ColorWarp: return "color-warp";
        case GraphOperationKind::DetailContrast: return "detail-contrast";
        default: return "invalid";
    }
}
const char* GraphOperationLabel(GraphOperationKind kind) {
    switch (kind) {
        case GraphOperationKind::Calibration: return "Color Calibration";
        case GraphOperationKind::Exposure: return "Exposure";
        case GraphOperationKind::LocalEv: return "Local EV";
        case GraphOperationKind::LuminanceTone: return "Luminance Tone";
        case GraphOperationKind::RgbCurves: return "RGB Curves";
        case GraphOperationKind::ColorWarp: return "Color Warp";
        case GraphOperationKind::DetailContrast: return "Detail Contrast";
        default: return "Unknown photo operation";
    }
}
GraphOperation MakeGraphOperation(GraphOperationKind kind) {
    GraphOperation operation; operation.kind = kind;
    WriteGraphOperation(operation, MakeDefaultRecipe({}));
    return operation;
}
RawDevelopmentRecipe ReadGraphOperation(const GraphOperation& operation) {
    auto recipe = MakeDefaultRecipe({});
    const auto& p = operation.parameters;
    switch (operation.kind) {
        case GraphOperationKind::Calibration: recipe.colorCalibration = DeserializeColorCalibration(p); break;
        case GraphOperationKind::Exposure: recipe.preToneExposureEv = p.value("ev", 0.f); break;
        case GraphOperationKind::LocalEv: {
            auto serialized = SerializeRecipe(recipe);
            if (p.contains("localRange")) serialized["localRange"] = p.at("localRange");
            if (p.contains("evGradients")) serialized["evGradients"] = p.at("evGradients");
            recipe = DeserializeRecipe(serialized); break;
        }
        case GraphOperationKind::LuminanceTone: recipe.finishTone.layerJson["luminanceTone"] = SerializeSceneTone(ReadSceneTone(p)); break;
        case GraphOperationKind::RgbCurves:
            if (!p.empty()) recipe.finishTone.layerJson = p;
            recipe.finishTone.layerJson["luminanceTone"] = SerializeSceneTone(SceneTone{}); break;
        case GraphOperationKind::ColorWarp: recipe.colorWarp = DeserializeColorWarpRecipe(p); break;
        case GraphOperationKind::DetailContrast: recipe.detailContrast = ReadDetailContrast(p); break;
        default: throw std::runtime_error("Unknown RAW graph operation.");
    }
    return recipe;
}
void WriteGraphOperation(GraphOperation& operation, const RawDevelopmentRecipe& edited) {
    switch (operation.kind) {
        case GraphOperationKind::Calibration: operation.parameters = SerializeColorCalibration(edited.colorCalibration); break;
        case GraphOperationKind::Exposure: operation.parameters = {{"ev", edited.preToneExposureEv}}; break;
        case GraphOperationKind::LocalEv: {
            const auto serialized = SerializeRecipe(edited);
            operation.parameters = {{"localRange", serialized.at("localRange")}, {"evGradients", serialized.at("evGradients")}}; break;
        }
        case GraphOperationKind::LuminanceTone: operation.parameters = SerializeSceneTone(ReadSceneTone(edited.finishTone.layerJson.value("luminanceTone", nlohmann::json::object()))); break;
        case GraphOperationKind::RgbCurves:
            operation.parameters = edited.finishTone.layerJson;
            operation.parameters.erase("luminanceTone"); break;
        case GraphOperationKind::ColorWarp: operation.parameters = SerializeColorWarpRecipe(edited.colorWarp); break;
        case GraphOperationKind::DetailContrast: operation.parameters = SerializeDetailContrast(edited.detailContrast); break;
        default: throw std::runtime_error("Unknown RAW graph operation.");
    }
}
nlohmann::json SerializeGraphOperation(const GraphOperation& operation) {
    return {{"kind", GraphOperationId(operation.kind)}, {"enabled", operation.enabled}, {"parameters", operation.parameters}};
}
GraphOperation DeserializeGraphOperation(const nlohmann::json& value) {
    for (int i = 0; i < static_cast<int>(GraphOperationKind::Count); ++i) {
        const auto kind = static_cast<GraphOperationKind>(i);
        if (value.at("kind") != GraphOperationId(kind)) continue;
        GraphOperation operation;
        operation.kind = kind; operation.enabled = value.at("enabled").get<bool>();
        operation.parameters = value.at("parameters");
        if (!operation.parameters.is_object()) throw std::runtime_error("Invalid photo operation parameters.");
        return operation;
    }
    throw std::runtime_error("Unknown photo operation definition.");
}
RawDevelopmentRecipe BuildTechnicalSourceRecipe(const RawDevelopmentRecipe& current) {
    auto source = current;
    source.colorCalibration = {};
    source.preToneExposureEv = 0;
    source.localRange = DefaultLocalRangeRecipe();
    source.evGradients.clear(); source.toneGradients.clear();
    source.finishTone.layerJson = DefaultFinishToneJson();
    source.colorWarp = {}; source.colorWarp.enabled = false;
    source.detailContrast = {};
    source.viewTransform.layerJson = DefaultViewTransformJson();
    source.viewTransform.layerJson["enabled"] = false;
    source.technical.encodeSrgbOutput = false;
    source.cropRotation.cropEnabled = false;
    source.requireFullSpatialInput = true;
    return source;
}
RawDevelopmentRecipe BuildWorkspaceSourceRecipe(const RawDevelopmentRecipe& current) {
    auto recipe = BuildTechnicalSourceRecipe(current);
    recipe.viewTransform = current.viewTransform;
    recipe.technical.encodeSrgbOutput = current.technical.encodeSrgbOutput;
    recipe.cropRotation = current.cropRotation;
    recipe.requireFullSpatialInput = current.requireFullSpatialInput;
    return recipe;
}
nlohmann::json SerializeWorkspaceSourceRecipe(const RawDevelopmentRecipe& current) {
    auto serialized = SerializeRecipe(BuildWorkspaceSourceRecipe(current));
    serialized["rawRecipeRole"] = "workspace-source";
    for (const auto* key : {"exposureEv", "colorCalibration", "localRange", "evGradients", "finishTone",
            "toneGradients", "colorWarp", "detailContrast", "stageOrder"}) serialized.erase(key);
    return serialized;
}
RawDevelopmentRecipe BuildUneditedSourceRecipe(const RawDevelopmentRecipe& current) {
    auto source = BuildTechnicalSourceRecipe(BuildNeutralComparisonRecipe(current));
    source.whiteBalance = {};
    source.technical.mosaicDenoise.enabled = false;
    source.rgbDenoise.enabled = false;
    source.rgbDenoise.diagnosticMode = RawDenoiseDiagnosticMode::None;
    source.rgbDenoise.diagnosticPointId = 0;
    source.cropRotation = {};
    return source;
}
} // namespace Stack::RawRecipe
