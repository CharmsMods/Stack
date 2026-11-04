#pragma once
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ContractTypes.h"

namespace Stack::Timeline { struct TimelineAnimationState; }

namespace EditorNodeGraphDefinitions {
bool RebindExpandedBrightnessAnimation(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::NodeGroup& group,
    const EditorNodeGraph::Node& original, Stack::Timeline::TimelineAnimationState& animation, std::string& error);
bool IsBrightnessRecipe(const EditorNodeGraph::Node &node);
bool BindBrightnessRecipe(EditorNodeGraph::Graph &graph, int nodeId,
                          const Stack::NodeMath::ValueDescriptor &input, const nlohmann::json &settings,
                          std::string &error);
std::string InspectArithmeticRecipe(const EditorNodeGraph::Graph &graph, const EditorNodeGraph::Node &node,
                                    const Stack::NodeMath::ValueDescriptor &input);
}
