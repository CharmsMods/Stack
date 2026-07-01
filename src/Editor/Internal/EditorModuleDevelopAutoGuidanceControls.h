#pragma once

#include "Editor/EditorModuleTypes.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace Stack::Editor::DevelopAutoGuidanceControls {

struct AutoGuidanceControlResult {
    bool changed = false;
    bool forceAutoReanalysis = false;
    bool forceFullAutoReanalysis = false;
    int recordInteractionCount = 0;
};

bool SameDevelopAutoGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b);

AutoGuidanceControlResult RenderDevelopAutoGuidanceControls(
    EditorNodeGraph::DevelopAutoGuidance& guidance,
    EditorNodeGraph::DevelopSubjectImportanceMap& subjectImportance,
    Stack::EditorModuleTypes::DevelopAutoGuidanceDraftState& draftState,
    bool forceAutoReanalysis,
    float controlWidth);

} // namespace Stack::Editor::DevelopAutoGuidanceControls
