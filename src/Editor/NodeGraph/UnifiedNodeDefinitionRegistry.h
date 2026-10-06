#pragma once

#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "NodeMath/NodeDefinition.h"
#include "Graph/OutputDependencies.h"
#include "ThirdParty/json.hpp"

#include <string>
#include <optional>
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

enum class LiveGraphRole : unsigned { Composition = 1, RawLayer = 2, Compound = 4 };

struct LivePortContract {
    Stack::NodeMath::PortDefinition port;
    Stack::NodeMath::ValueDescriptor requiredSemantics;
    std::vector<Stack::NodeMath::AlphaMode> acceptedAlpha;
    bool acceptsUnknownSemantics = true;
    nlohmann::json defaultValue;
    std::string defaultInput;
    std::vector<Stack::NodeMath::DefinitionReference> explicitConversions;
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
    bool executable = true;
    unsigned graphRoles = 7;
    bool requiresSceneLinearRgb = false;
    // Each output may bypass through exactly one connected declared input.
    std::vector<std::pair<std::string, std::string>> bypassBindings;
    std::vector<EditorNodeGraph::SocketDefinition> sockets;
    std::vector<LiveParameterDefinition> parameters;
    std::vector<Stack::GraphModel::OutputDependency> outputDependencies;
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

std::optional<LivePortContract> GetLivePortContract(const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node, const std::string& socketId);
bool DefinitionSupportsGraphRole(const LiveNodeDefinition& definition, LiveGraphRole role);
bool ValidateInputDescriptor(const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node, const std::string& socketId,
    const Stack::NodeMath::ValueDescriptor& value, std::string& error);

bool AcceptsTypedParameterInput(const EditorNodeGraph::Node& node, const std::string& socketId, EditorNodeGraph::SocketType type);

bool OutputDependsOnInput(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node,
    const std::string& output, const std::string& input);
bool OutputDependsOnInput(const EditorNodeGraph::Node& node,
    const std::string& output, const std::string& input);

} // namespace EditorNodeGraphDefinitions
