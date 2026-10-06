#pragma once
#include "Editor/GraphScopeData.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <cstdint>
#include <vector>
#include <memory>

class EditorModule;

class EditorScopes {
public:
    EditorScopes();
    ~EditorScopes();

    void Initialize();
    void RenderScopeNode(EditorModule* editor, EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId);

private:
    
    // UI Drawing Helpers
    void DrawHistogram();
    void DrawVectorscope();
    void DrawRGBParade();

    std::shared_ptr<const GraphScopeData> m_Data;
};
