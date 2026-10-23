#include "Editor/NodeGraph/Definitions/OperationInventory.h"

namespace EditorNodeGraphDefinitions {
namespace {
using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;
std::string VersionString(const Stack::NodeMath::SemanticVersion& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}
} // namespace

std::string ComputeLiveNodeDefinitionHash(const LiveNodeDefinition& definition) {
    nlohmann::json canonical = {
        { "schema", "stack.live-node-definition.v1" },
        { "id", definition.identity.id },
        { "version", VersionString(definition.identity.version) },
        { "inspectability", static_cast<int>(definition.inspectability) },
        { "canonicalRecipeId", definition.canonicalRecipeId },
        { "decompositionReason", definition.decompositionReason },
        { "kind", static_cast<int>(definition.kind) },
        { "variant", definition.variant },
        { "label", definition.label },
        { "category", definition.category },
        { "searchAliases", definition.searchAliases },
        { "previewKey", definition.previewKey },
        { "previewRecipeVersion", definition.previewRecipeVersion },
        { "previewStrategy", static_cast<int>(definition.previewStrategy) },
        { "visibleInBrowser", definition.visibleInBrowser }
    };
    canonical["ports"] = nlohmann::json::array();
    for (const EditorNodeGraph::SocketDefinition& socket : definition.sockets) {
        canonical["ports"].push_back({
            { "id", socket.id },
            { "direction", static_cast<int>(socket.direction) },
            { "type", static_cast<int>(socket.type) },
            { "label", socket.label },
            { "optional", socket.optional },
            { "visible", socket.visible }
        });
    }
    canonical["executable"] = definition.executable;
    canonical["graphRoles"] = definition.graphRoles;
    canonical["requiresSceneLinearRgb"] = definition.requiresSceneLinearRgb;
    canonical["bypass"] = definition.bypassBindings;
    canonical["outputDependencies"] = nlohmann::json::array();
    for (const auto& rule : definition.outputDependencies)
        canonical["outputDependencies"].push_back({{"output", rule.output}, {"inputs", rule.inputs}});
    canonical["parameters"] = nlohmann::json::array();
    for (const LiveParameterDefinition& parameter : definition.parameters) {
        canonical["parameters"].push_back({
            { "id", parameter.id }, { "label", parameter.label },
            { "type", static_cast<int>(parameter.logicalType) }, { "units", parameter.units },
            { "default", parameter.defaultValue }, { "hasDomain", parameter.hasNumericDomain },
            { "minimum", parameter.minimum }, { "maximum", parameter.maximum },
            { "uiHint", parameter.uiHint }, { "serialized", parameter.serialized },
            { "graphInputCapable", parameter.graphInputCapable },
            { "animation", static_cast<int>(parameter.animation) }, { "storageKey", parameter.storageKey }
        });
    }
    return Stack::NodeMath::Sha256ContentIdentity(canonical.dump());
}

void ApplyLiveDefinitionIdentity(Node& node) {
    const LiveNodeDefinition* definition = FindLiveNodeDefinition(node);
    if (!definition) {
        node.definitionId.clear();
        node.definitionVersion.clear();
        node.definitionHash.clear();
        node.definitionResolved = false;
        node.definitionResolutionError = "No live definition is registered for this node variant.";
        return;
    }
    node.definitionId = definition->identity.id;
    node.definitionVersion = VersionString(definition->identity.version);
    node.definitionHash = definition->identity.contentHash;
    node.definitionResolved = true;
    node.definitionResolutionError.clear();
}

bool ResolveSavedLiveDefinition(
    Node& node,
    const std::string& savedId,
    const std::string& savedVersion,
    const std::string& savedHash,
    std::string* error) {
    const LiveNodeDefinition* definition = FindLiveNodeDefinition(node);
    const std::string currentVersion = definition ? VersionString(definition->identity.version) : std::string();
    if (!definition) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved node variant has no registered definition.";
    } else if (savedId.empty() || savedVersion.empty() || savedHash.empty()) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved node is missing its exact definition ID, version, or content hash.";
    } else if (savedId != definition->identity.id) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved definition ID is not installed for this node variant.";
    } else if (savedVersion != currentVersion) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved definition version does not match the installed exact version.";
    } else if (savedHash != definition->identity.contentHash) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved definition content hash does not match the installed definition.";
    } else {
        node.definitionResolved = true;
        node.definitionResolutionError.clear();
    }
    node.definitionId = savedId;
    node.definitionVersion = savedVersion;
    node.definitionHash = savedHash;
    if (error) *error = node.definitionResolutionError;
    return node.definitionResolved;
}
} // namespace EditorNodeGraphDefinitions
