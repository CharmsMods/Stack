#pragma once
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <optional>
namespace Stack::Editor::NodeGraphUIVisuals {
std::optional<double> NodeNumericDefault(const EditorNodeGraph::Node& node, const float* value);
std::optional<double> LayerNumericDefault(const EditorNodeGraph::Node& node,
    const char* label, double minimum, double maximum, const char* format);
std::optional<float> CompactControlNodeWidth(const EditorNodeGraph::Node& node);
bool SharesIdentityWithParameter(const EditorNodeGraph::Node& node);
}
