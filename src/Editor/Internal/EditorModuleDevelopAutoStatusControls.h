#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace Stack::Editor::DevelopAutoStatusControls {

struct CandidateFeedbackStatus {
    bool deferred = false;
    double quietRemainingSeconds = 0.0;
};

void RenderDevelopAutoStatusReadouts(
    const EditorNodeGraph::RawDevelopPayload& payload,
    const CandidateFeedbackStatus& candidateFeedback);

} // namespace Stack::Editor::DevelopAutoStatusControls