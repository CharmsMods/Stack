#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace Stack::Editor::DevelopSubjectControls {

struct SubjectImportanceControlResult {
    bool changed = false;
    bool forceAutoReanalysis = false;
    bool recordInteraction = false;
};

SubjectImportanceControlResult RenderDevelopSubjectImportanceControls(
    EditorNodeGraph::DevelopSubjectImportanceMap& importance,
    float controlWidth,
    float buttonGap);

} // namespace Stack::Editor::DevelopSubjectControls