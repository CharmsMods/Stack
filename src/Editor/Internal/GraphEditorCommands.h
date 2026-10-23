#pragma once

#include "Editor/EditorModule.h"
#include "Graph/GraphDocumentRules.h"
#include "Editor/NodeGraph/CompoundGroupMembership.h"

#include <stdexcept>
#include <unordered_set>

namespace Stack::Editor {

inline bool ApplyGraphCommand(EditorModule& editor, const std::function<void(EditorNodeGraph::Graph&)>& edit,
    std::string* error = nullptr) {
    auto context = editor.GetGraphEditorContext();
    std::string reason;
    const auto ownedEdit=[&](EditorNodeGraph::Graph& graph) {
        std::unordered_set<int> existing;
        for(const auto& node:graph.GetNodes())existing.insert(node.id);
        std::unordered_map<int,EditorNodeGraph::GraphRect> frames;
        for(const auto& group:graph.GetGroups())if(group.compoundBoundary.is_object())
            frames.emplace(group.id,EditorNodeGraph::GraphRect{group.position,{group.position.x+group.size.x,group.position.y+group.size.y}});
        edit(graph);
        std::vector<int> added;
        for(const auto& node:graph.GetNodes())if(!existing.count(node.id)) {
            // Decompile/repack already establish authoritative ownership. Only
            // ordinary new additions need spatial admission; old frame extents
            // must not detach newly expanded children from their ancestors.
            const bool owned=std::any_of(graph.GetGroups().begin(),graph.GetGroups().end(),[&](const auto& group) {
                return group.compoundBoundary.is_object() &&
                    std::find(group.compoundMembers.begin(),group.compoundMembers.end(),node.id)!=group.compoundMembers.end();
            });
            if(!owned)added.push_back(node.id);
        }
        if(!added.empty() && !frames.empty())EditorNodeGraph::UpdateCompoundGroupMembership(graph,added,frames,[](const auto&,const auto& n) {
            return EditorNodeGraph::GraphRect{{-12,0},{292,n.expanded?240.f:52.f}};
        },false);
    };
    const bool applied = context.applyCommand ? context.applyCommand(ownedEdit,reason) :
        context.applyEdit(Stack::GraphModel::ProposeEdit(*context.graph,context.revision,ownedEdit),reason);
    if (error) *error = std::move(reason);
    return applied;
}

template<class Factory>
int AddGraphNode(EditorModule& editor, Factory&& create) {
    int nodeId = 0;
    std::string error;
    if (!ApplyGraphCommand(editor,[&](auto& graph) {
            auto* node = create(graph);
            if (!node) throw std::runtime_error("The operation could not be created.");
            nodeId = node->id;
            graph.SelectNode(nodeId);
        },&error)) {
        editor.ShowUiNotification(UiNotificationSeverity::Error,error,"graph-add-operation");
        return 0;
    }
    editor.SelectGraphNode(nodeId);
    return nodeId;
}

} // namespace Stack::Editor
