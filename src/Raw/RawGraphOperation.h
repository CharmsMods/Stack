#pragma once

#include "Raw/RawDevelopmentRecipe.h"

namespace Stack::RawRecipe {

enum class GraphOperationKind {
    Calibration, Exposure, LocalEv, LuminanceTone, RgbCurves, ColorWarp, DetailContrast, Count
};

// Each record owns only this operation's authored parameters. The recipe
// conversion is a temporary typed editing/execution value, never saved state.
struct GraphOperation {
    GraphOperationKind kind = GraphOperationKind::Exposure;
    bool enabled = true;
    nlohmann::json parameters = nlohmann::json::object();
};

const char* GraphOperationId(GraphOperationKind kind);
const char* GraphOperationLabel(GraphOperationKind kind);
GraphOperation MakeGraphOperation(GraphOperationKind kind);
RawDevelopmentRecipe ReadGraphOperation(const GraphOperation& operation);
void WriteGraphOperation(GraphOperation& operation, const RawDevelopmentRecipe& edited);
nlohmann::json SerializeGraphOperation(const GraphOperation& operation);
GraphOperation DeserializeGraphOperation(const nlohmann::json& value);
// Authored camera preparation plus project presentation. Creative parameters
// live exclusively in layer operation records.
RawDevelopmentRecipe BuildWorkspaceSourceRecipe(const RawDevelopmentRecipe& current);
nlohmann::json SerializeWorkspaceSourceRecipe(const RawDevelopmentRecipe& current);
RawDevelopmentRecipe BuildUneditedSourceRecipe(const RawDevelopmentRecipe& current);
RawDevelopmentRecipe BuildTechnicalSourceRecipe(const RawDevelopmentRecipe& current);

} // namespace Stack::RawRecipe
