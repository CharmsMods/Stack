#include "Raw/RawGraphOperation.h"
#include "NodeMath/ToneCurveParameters.h"

#include <stdexcept>

namespace Stack::RawRecipe {
const char* GraphOperationId(GraphOperationKind kind) {
    switch (kind) {
        case GraphOperationKind::Calibration: return "calibration";
        case GraphOperationKind::Exposure: return "exposure";
        case GraphOperationKind::LocalEv: return "local-ev";
        case GraphOperationKind::ToneCurve: return "tone-curve";
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
        case GraphOperationKind::ToneCurve: return "Tone Curve";
        case GraphOperationKind::ColorWarp: return "Color Warp";
        case GraphOperationKind::DetailContrast: return "Detail Contrast";
        default: return "Unknown photo operation";
    }
}
GraphOperation MakeGraphOperation(GraphOperationKind kind) {
    GraphOperation operation; operation.kind = kind;
    if (kind==GraphOperationKind::ToneCurve) {
        operation.parameters=NodeMath::DefaultToneCurveParameters(); return operation;
    }
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
            recipe = DeserializeRecipe(serialized);
            recipe.preToneExposureEv = p.value("ev",0.f);
            break;
        }
        case GraphOperationKind::ToneCurve: recipe.finishTone.layerJson=p; break;
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
            operation.parameters = {{"localRange", serialized.at("localRange")}, {"evGradients", serialized.at("evGradients")}};
            if (edited.preToneExposureEv != 0.f) operation.parameters["ev"] = edited.preToneExposureEv;
            break;
        }
        case GraphOperationKind::ToneCurve: operation.parameters=NodeMath::CaptureToneCurveParameters(edited.finishTone.layerJson); break;
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
        if (kind==GraphOperationKind::ToneCurve) {
            std::string error;
            if (!NodeMath::ValidateToneCurveParameters(operation.parameters,error)) throw std::runtime_error(error);
        }
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
