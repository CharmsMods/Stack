#include "Editor/NodeGraph/Definitions/OperationInventory.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include <algorithm>
#include <set>
#include <utility>

namespace EditorNodeGraphDefinitions {
using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;

const std::vector<LiveNodeDefinition>& GetUnifiedNodeDefinitionRegistry() {
    static const std::vector<LiveNodeDefinition> registry = [] {
        std::vector<LiveNodeDefinition> definitions;
        for (const auto& registration : GetOperationInventory()) {
            auto definition = registration.definition;
            const auto prototype = BuildOperationPrototype(definition.kind, definition.variant);
            definition.sockets = BuildSockets(prototype, false);
            definition.parameters = BuildOperationParameters(prototype);
            definition.identity.contentHash = ComputeLiveNodeDefinitionHash(definition);
            definitions.push_back(std::move(definition));
        }
        return definitions;
    }();
    return registry;
}

std::vector<NodeCatalogEntry> BuildRegisteredNodeCatalogEntries() {
    std::vector<NodeCatalogEntry> entries;
    for (const LiveNodeDefinition& definition : GetUnifiedNodeDefinitionRegistry()) {
        // Keep this definition and its saved hash unchanged. Its painting UI is
        // retired; captures and saved assets create populated textures instead.
        if (!definition.visibleInBrowser || definition.kind==NodeKind::CustomMask) continue;
        NodeCatalogEntry entry;
        entry.kind = definition.kind;
        entry.value = definition.variant;
        entry.label = definition.label;
        entry.category = definition.category;
        entry.searchAliases = definition.searchAliases;
        entry.previewKey = definition.previewKey;
        entry.previewRecipeVersion = definition.previewRecipeVersion;
        entry.previewStrategy = definition.previewStrategy;
        entries.push_back(std::move(entry));
    }
    
    const auto& compounds = GetShippedCompoundTemplates();
    for (std::size_t index = 0; index < compounds.size(); ++index) {
        NodeCatalogEntry entry;
        entry.kind = NodeKind::Compound;
        entry.value = static_cast<int>(index);
        entry.label = compounds[index].label;
        entry.category = "Compounds";
        entry.previewKey = "compound/" + compounds[index].identity.id;
        entry.previewRecipeVersion = 1;
        entry.previewStrategy = NodeCatalogPreviewStrategy::FallbackOnly;
        entries.push_back(std::move(entry));
    }
    return entries;
}
const LiveNodeDefinition* FindLiveNodeDefinition(NodeKind kind, int variant) {
    const auto& registry = GetUnifiedNodeDefinitionRegistry();
    const auto it = std::find_if(registry.begin(), registry.end(), [&](const LiveNodeDefinition& definition) {
        return definition.kind == kind && definition.variant == variant;
    });
    return it == registry.end() ? nullptr : &(*it);
}

const LiveNodeDefinition* FindLiveNodeDefinition(const Node& node) {
    return FindLiveNodeDefinition(node.kind, OperationVariant(node));
}

bool ValidateUnifiedNodeDefinitionRegistry(std::vector<std::string>* errors) {
    std::vector<std::string> local;
    std::set<std::string> identities;
    std::set<std::pair<int, int>> variants;
    for (const LiveNodeDefinition& definition : GetUnifiedNodeDefinitionRegistry()) {
        if (!Stack::NodeMath::IsValidDefinitionId(definition.identity.id)) {
            local.push_back("Invalid definition ID: " + definition.identity.id);
        }
        if (!Stack::NodeMath::IsValidContentHash(definition.identity.contentHash) ||
            definition.identity.contentHash != ComputeLiveNodeDefinitionHash(definition)) {
            local.push_back("Invalid definition content hash: " + definition.identity.id);
        }
        if (!identities.insert(definition.identity.id).second) {
            local.push_back("Duplicate definition ID: " + definition.identity.id);
        }
        if (!variants.insert({ static_cast<int>(definition.kind), definition.variant }).second) {
            local.push_back("Duplicate node kind/variant: " + definition.identity.id);
        }
        std::set<std::string> portIds;
        for (const auto& socket : definition.sockets) {
            if (socket.id.empty() || !portIds.insert(socket.id + ":" + std::to_string(static_cast<int>(socket.direction))).second) {
                local.push_back("Duplicate or empty port identity: " + definition.identity.id);
            }
        }
        std::set<std::string> parameterIds;
        for (const LiveParameterDefinition& parameter : definition.parameters) {
            if (parameter.id.empty() || !parameterIds.insert(parameter.id).second) {
                local.push_back("Duplicate or empty parameter identity: " + definition.identity.id);
            }
            if (parameter.hasNumericDomain && parameter.minimum > parameter.maximum) {
                local.push_back("Invalid parameter domain: " + definition.identity.id + "/" + parameter.id);
            }
        }
    }
    ValidateOperationInventoryCoverage(local);
    if (errors) *errors = local;
    return local.empty();
}
} // namespace EditorNodeGraphDefinitions
