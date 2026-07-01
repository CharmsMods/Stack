#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace Stack::Editor::DevelopPayloadComparison {

bool SameRawDevelopPayload(
    const EditorNodeGraph::RawDevelopPayload& a,
    const EditorNodeGraph::RawDevelopPayload& b);

} // namespace Stack::Editor::DevelopPayloadComparison
