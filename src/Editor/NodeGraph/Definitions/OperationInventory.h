#pragma once

#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"

namespace EditorNodeGraphDefinitions {

// Families organize implementation ownership. Browser categories are separate
// presentation metadata on each definition and can change independently.
enum class OperationFamily { SourcePreparation, ImageEffects, FieldsAndValues };
enum class OperationDisposition { Builtin, EmbeddedCompound, UnavailableIdentity };

struct OperationRegistration {
    OperationFamily family;
    LiveNodeDefinition definition;
};

const std::vector<OperationRegistration>& GetOperationInventory();
bool ValidateOperationInventoryCoverage(std::vector<std::string>& errors);
OperationDisposition GetOperationDisposition(EditorNodeGraph::NodeKind kind);
int OperationVariant(const EditorNodeGraph::Node& node);
EditorNodeGraph::Node BuildOperationPrototype(EditorNodeGraph::NodeKind kind, int variant);
std::vector<LiveParameterDefinition> BuildOperationParameters(const EditorNodeGraph::Node& node);

namespace OperationDefinitions {

LiveNodeDefinition MakeOperation(EditorNodeGraph::NodeKind kind, int variant,
    std::string label, std::string category, std::string operationKey,
    NodeCatalogPreviewStrategy strategy = NodeCatalogPreviewStrategy::Auto,
    std::uint32_t previewRecipeVersion = 1, bool visibleInBrowser = true);

void AppendSourceOperations(std::vector<OperationRegistration>& operations);
void AppendImageEffectOperations(std::vector<OperationRegistration>& operations);
void AppendFieldOperations(std::vector<OperationRegistration>& operations);

} // namespace OperationDefinitions
} // namespace EditorNodeGraphDefinitions
