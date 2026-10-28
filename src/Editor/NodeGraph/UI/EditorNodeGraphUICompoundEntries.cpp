#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "EditorNodeGraphUIVisuals.h"
#include <unordered_set>

void EditorNodeGraphUI::PrepareCompoundEntries(const EditorNodeGraph::Graph& graph) {
    // Authored bindings are resolved only on topology change. Motion updates
    // just the small set of gateway row positions, never processing metadata.
    if(m_CompoundEntryGraph!=&graph || m_CompoundEntryRevision!=graph.GetStructureRevision()) {
        m_CompoundEntryGraph=&graph;m_CompoundEntryRevision=graph.GetStructureRevision();
        m_CompoundEntryRows.clear();m_CompoundEntryLinks.clear();
        m_CompoundEntryDragInput=m_CompoundEntryDragOutput=-1;
        std::unordered_map<std::string,int> identities;
        for(const auto& node:graph.GetNodes())identities.emplace(node.instanceUuid,node.id);
        for(const auto& group:graph.GetGroups()) {
            if(!group.compoundBoundary.is_object())continue;
            const std::unordered_set<int> members(group.compoundMembers.begin(),group.compoundMembers.end());
            for(const auto& port:group.compoundBoundary["ports"]) {
                if(port.value("direction",std::string{})!="input")continue;
                const auto target=identities.find(port.value("internalInstanceUuid",std::string{}));
                if(target==identities.end() || !members.count(target->second))continue;
                const auto socket=port.value("internalSocketId",std::string{});
                const auto* link=graph.FindAnyInputLink(target->second,socket);
                if(link && members.count(link->fromNodeId))continue;
                CompoundEntryRow row;row.group=group.id;row.target=target->second;row.socket=socket;
                row.label=port.value("label",port.value("id",std::string{}));
                if(link) {
                    row.source=link->fromNodeId;row.sourceSocket=link->fromSocketId;
                    row.linkKey=Stack::Editor::NodeGraphUIVisuals::LinkAnimationKey(*link);
                    m_CompoundEntryLinks[row.linkKey]=m_CompoundEntryRows.size();
                }
                m_CompoundEntryRows.push_back(std::move(row));
            }
        }
    }
    int groupId=-1,index=0;
    for(auto& row:m_CompoundEntryRows) {
        if(row.group!=groupId){groupId=row.group;index=0;}
        const auto* group=graph.FindGroup(groupId);if(!group)continue;
        const auto bounds=DisplayGroupRect(*group);
        const auto left=GraphToScreen({bounds.min.x+20,bounds.min.y+62+24.f*index++});
        const ImVec2 input{left.x,left.y}, output{left.x+160*m_Zoom,left.y};
        // A fitted container can animate without moving any of its nodes.
        // Its gateway still bends the visible wire, so a stationary pointer's
        // cached link hit must follow these anchors, not only node projection.
        if(row.input.x!=input.x || row.input.y!=input.y ||
            row.output.x!=output.x || row.output.y!=output.y)m_LinkHitTestFrame=-1;
        row.input=input;row.output=output;
    }
}

void EditorNodeGraphUI::RenderCompoundEntries(const EditorNodeGraph::Graph& graph) {
    auto* draw=ImGui::GetWindowDrawList();
    for(std::size_t first=0;first<m_CompoundEntryRows.size();) {
        std::size_t end=first+1;
        while(end<m_CompoundEntryRows.size() && m_CompoundEntryRows[end].group==m_CompoundEntryRows[first].group)++end;
        if(!LayoutGroupVisible(m_CompoundEntryRows[first].group)){first=end;continue;}
        const auto& start=m_CompoundEntryRows[first];const auto& last=m_CompoundEntryRows[end-1];
        const ImVec2 min{start.input.x,start.input.y-28*m_Zoom};
        const ImVec2 max{start.output.x,last.output.y+14*m_Zoom};
        draw->AddRectFilled(min,max,IM_COL32(37,39,43,235),5*m_Zoom);
        draw->AddRect(min,max,IM_COL32(105,110,120,170),5*m_Zoom);
        draw->AddText(ImGui::GetFont(),11*m_Zoom,{min.x+8*m_Zoom,min.y+5*m_Zoom},IM_COL32(220,220,225,255),"Inputs");
        for(std::size_t i=first;i<end;++i) {
            const auto& row=m_CompoundEntryRows[i];
            draw->AddCircleFilled(row.input,3*m_Zoom,IM_COL32(190,195,205,255));
            draw->AddCircleFilled(row.output,3*m_Zoom,IM_COL32(190,195,205,255));
            const ImVec4 clip{min.x+7*m_Zoom,min.y,max.x-7*m_Zoom,max.y};
            draw->AddText(ImGui::GetFont(),10*m_Zoom,{min.x+8*m_Zoom,row.input.y-5*m_Zoom},IM_COL32(205,210,220,255),row.label.c_str(),nullptr,0,&clip);
        }
        first=end;
    }
}
