#pragma once

#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "NodeMath/NodeDefinition.h"
#include "ThirdParty/json.hpp"

#include <string>
#include <vector>

namespace EditorNodeGraphDefinitions {

enum class LiveAnimationPolicy {
    NotAnimatable,
    Hold,
    Linear
};

struct LiveParameterDefinition {
    std::string id;
    std::string label;
    Stack::NodeMath::LogicalValueType logicalType = Stack::NodeMath::LogicalValueType::Invalid;
    std::string units = "unspecified";
    nlohmann::json defaultValue;
    bool hasNumericDomain = false;
    double minimum = 0.0;
    double maximum = 0.0;
    std::string uiHint;
    bool serialized = true;
    bool graphInputCapable = false;
    LiveAnimationPolicy animation = LiveAnimationPolicy::NotAnimatable;
    std::string storageKey;
};

struct LiveNodeDefinition {
    EditorNodeGraph::NodeKind kind = EditorNodeGraph::NodeKind::Layer;
    int variant = 0;
    Stack::NodeMath::DefinitionReference identity;
    Stack::NodeMath::Inspectability inspectability = Stack::NodeMath::Inspectability::TransparentGraph;
    std::string label;
    std::string category;
    std::string searchAliases;
    std::string previewKey;
    std::uint32_t previewRecipeVersion = 1;
    NodeCatalogPreviewStrategy previewStrategy = NodeCatalogPreviewStrategy::Auto;
    bool visibleInBrowser = true;
    std::vector<EditorNodeGraph::SocketDefinition> sockets;
    std::vector<LiveParameterDefinition> parameters;
};

const std::vector<LiveNodeDefinition>& GetUnifiedNodeDefinitionRegistry();
std::vector<NodeCatalogEntry> BuildRegisteredNodeCatalogEntries();
std::vector<EditorNodeGraph::SocketDefinition> BuildRegisteredSockets(
    const EditorNodeGraph::Node& node,
    bool visibleOnly);
const LiveNodeDefinition* FindLiveNodeDefinition(const EditorNodeGraph::Node& node);
const LiveNodeDefinition* FindLiveNodeDefinition(
    EditorNodeGraph::NodeKind kind,
    int variant);

void ApplyLiveDefinitionIdentity(EditorNodeGraph::Node& node);
bool ResolveSavedLiveDefinition(
    EditorNodeGraph::Node& node,
    const std::string& savedId,
    const std::string& savedVersion,
    const std::string& savedHash,
    std::string* error = nullptr);
bool ValidateUnifiedNodeDefinitionRegistry(std::vector<std::string>* errors = nullptr);
std::string ComputeLiveNodeDefinitionHash(const LiveNodeDefinition& definition);

} // namespace EditorNodeGraphDefinitions
