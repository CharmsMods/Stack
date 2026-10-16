#pragma once
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <imgui.h>
#include <optional>
#include <vector>

class EditorModule;
enum class RawDevelopmentGraphScopeStage;
namespace Stack::EditorModuleTypes { enum class RawLabTool; }
namespace Stack::Editor {
struct RawWorkspaceEditContext;
struct NodeEditorDefinition {
    std::string definitionId;
    // Transitional adapters for existing tool analysis and viewport behavior.
    std::optional<EditorModuleTypes::RawLabTool> legacyTool;
    int toneView = -1;
    ImVec2 preferredSize{400,560};
    using Draw = bool (*)(EditorModule&,RawWorkspaceEditContext&);
    Draw primary = nullptr;
    Draw settings = nullptr;
    Draw reset = nullptr;
    using Overlay = bool (*)(EditorModule&,RawWorkspaceEditContext&,ImVec2,ImVec2);
    Overlay overlay = nullptr;
    // Existing asynchronous analysis service, sampled at this node's actual
    // image input. Its legacy stage names identify readback formats only.
    RawDevelopmentGraphScopeStage inputAnalysis{};
    bool hasFloatingSurface = true;
};

// Absence of a custom primary is intentional: borrow the existing graph
// controls until this definition receives its individual image-editor design.
const NodeEditorDefinition& FindNodeEditor(const EditorNodeGraph::Node& node);
const NodeEditorDefinition* FindNodeEditorForTool(EditorModuleTypes::RawLabTool tool, int toneView);
const std::vector<NodeEditorDefinition>& NodeEditorDefinitions();
struct NodeEditorRegistration;
}
