#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/CompoundGroupMembership.h"
#include "App/UiPerformanceTrace.h"

#include <algorithm>
#include <cmath>

using namespace EditorNodeGraph;

bool EditorNodeGraphUI::BeginDragLayout(EditorModule* editor,Graph& graph) {
    if(m_ExpansionMotion) {
        // Explicit interaction takes over the displayed arrangement. Reveal the
        // remaining geometry and smoothly finish its travel, rather than keep a
        // delayed phase competing with the captured pointer.
        m_ExpansionMotion.reset();
        ++m_WireWorldRevision;
        for(auto& [id,motion]:m_LayoutNodeMotion) {
            motion.start=motion.current;motion.elapsed=motion.delay=0;motion.duration=.26;
        }
        for(auto& [id,motion]:m_LayoutGroupMotion) {
            motion.start=motion.current;motion.elapsed=motion.delay=0;motion.duration=.26;
        }
    }
    const auto context=editor->GetGraphEditorContext();
    auto drag=std::make_unique<DragLayoutState>();
    drag->documentId=context.documentId;drag->graphId=context.graphId;drag->revision=context.documentRevision;
    drag->held=graph.GetSelectedNodeIds();drag->lastFrame=ImGui::GetTime();
    drag->restingNodesBefore=m_LayoutRestingNodes;drag->restingGroupsBefore=m_LayoutRestingGroups;
    std::unordered_map<int,Vec2> rests;
    for(const auto& node:graph.GetNodes())if(const auto found=m_LayoutRestingNodes.find(node.instanceUuid);found!=m_LayoutRestingNodes.end())
        rests.emplace(node.id,found->second);
    const auto press=ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
    drag->press=ScreenToGraph({press.x,press.y});
    std::unordered_map<int,Vec2> anchors;
    const auto* primary=graph.FindNode(m_DragNodeId);
    if(!primary)return false;
    const auto primaryDisplay=DisplayNodePosition(*primary);
    const Vec2 shift{primaryDisplay.x-primary->position.x,primaryDisplay.y-primary->position.y};
    for(int id:drag->held)if(const auto* node=graph.FindNode(id)) {
        drag->displayed.emplace(id,DisplayNodePosition(*node));
        anchors.emplace(id,Vec2{node->position.x+shift.x,node->position.y+shift.y});
        if(id!=m_DragNodeId && m_LayoutNodeMotion.count(id))drag->easedHeld.insert(id);
    }
    std::unordered_map<int,GraphRect> groups;
    for(const auto& group:graph.GetGroups())groups.emplace(group.id,
        GraphRect{{group.position.x+shift.x,group.position.y+shift.y},{group.position.x+group.size.x+shift.x,group.position.y+group.size.y+shift.y}});
    drag->membershipFrames=groups;
    std::string error;
    Stack::UiPerformance::Add(Stack::UiPerformance::Counter::LayoutPrepare);
    if(!drag->room.Begin(graph,drag->held,[&](const Graph& g,const Node& n){return MeasureNodeForLayout(g,n);},error,anchors,groups,false,rests,m_LayoutRestingGroups)) {
        editor->ShowUiNotification(UiNotificationSeverity::Error,error,"graph-drag-layout");return false;
    }
    // Freeze the held geometry at the visible press position. Authored geometry
    // changes only with meaningful pointer movement; clicking creates no edit.
    for(int id:drag->held)if(!drag->easedHeld.count(id))m_LayoutNodeMotion.erase(id);
    m_SmoothZoomActive=false;m_ZoomTarget=m_Zoom;
    m_DragLayout=std::move(drag);return true;
}

bool EditorNodeGraphUI::ApplyAnimatedPlacement(EditorModule* editor,const Stack::GraphLayout::PlacementEdit& edit,
    bool heldImmediate,std::string& error) {
    if(edit.Empty())return true;
    auto& graph=editor->GetNodeGraph();
    std::vector<Vec2> starts;std::vector<GraphRect> frames;
    std::unordered_map<int,GraphRect> ownedFrames;
    for(const auto& group:graph.GetGroups())if(group.compoundBoundary.is_object())
        ownedFrames.emplace(group.id,DisplayGroupRect(group));
    for(const auto& move:edit.nodes) {
        const auto* node=graph.FindNode(move.id);
        if(!node){error="A moving node was replaced.";return false;}
        starts.push_back(DisplayNodePosition(*node));
    }
    for(const auto& move:edit.groups) {
        const auto* group=graph.FindGroup(move.id);
        if(!group){error="A moving group was replaced.";return false;}
        frames.push_back(DisplayGroupRect(*group));
    }
    if(!Stack::GraphLayout::ApplyPlacement(graph,edit,error))return false;
    UpdateCompoundGroupMembership(graph,{}, {},[&](const Graph& g,const Node& n){return MeasureNodeForLayout(g,n);});
    if(heldImmediate && m_DragLayout) {
        m_DragLayout->moved=true;
        for(const auto& move:edit.nodes) {
            const bool held=std::find(m_DragLayout->held.begin(),m_DragLayout->held.end(),move.id)!=m_DragLayout->held.end();
            if(held)m_LayoutRestingNodes.erase(move.uuid);
            else m_LayoutRestingNodes.try_emplace(move.uuid,move.before);
        }
        for(const auto& move:edit.groups) {
            const auto* group=graph.FindGroup(move.id);
            // A frame follows a fully held group deliberately; other translated
            // or fitted frames retain their original resting rectangle.
            const bool allHeld=group && group->compoundBoundary.is_object() && !group->compoundMembers.empty() &&
                std::all_of(group->compoundMembers.begin(),group->compoundMembers.end(),[&](int id) {
                    return std::find(m_DragLayout->held.begin(),m_DragLayout->held.end(),id)!=m_DragLayout->held.end();});
            if(allHeld)m_LayoutRestingGroups.erase(move.id);
            else m_LayoutRestingGroups.try_emplace(move.id,move.before);
        }
    }
    for(std::size_t i=0;i<edit.nodes.size();++i) {
        const auto& move=edit.nodes[i];const bool held=heldImmediate && m_DragLayout &&
            std::find(m_DragLayout->held.begin(),m_DragLayout->held.end(),move.id)!=m_DragLayout->held.end();
        const bool immediate=held && !m_DragLayout->easedHeld.count(move.id);
        if(immediate)m_DragLayout->displayed[move.id]=move.after;
        else if(held)m_DragLayout->displayed.erase(move.id);
        AnimateNodePlacement(*graph.FindNode(move.id),starts[i],immediate);
    }
    for(std::size_t i=0;i<edit.groups.size();++i)AnimateGroupPlacement(*graph.FindGroup(edit.groups[i].id),frames[i]);
    for(const auto& [id,frame]:ownedFrames)if(const auto* group=graph.FindGroup(id)) AnimateGroupPlacement(*group,frame);
    // Keep all moved nodes/frames in the existing placement-only gesture.
    editor->MarkGraphLayoutEdited(true);
    const auto context=editor->GetGraphEditorContext();AdoptLayoutMotionOwner(context);
    if(m_DragLayout)m_DragLayout->revision=context.documentRevision;
    return true;
}

void EditorNodeGraphUI::EndDragLayout(EditorModule* editor,bool cancel) {
    if(!m_DragLayout)return;
    const auto context=editor->GetGraphEditorContext();
    if(context.documentId!=m_DragLayout->documentId || context.graphId!=m_DragLayout->graphId ||
        context.documentRevision!=m_DragLayout->revision) {
        m_DragLayout.reset();m_DragNodeId=-1;return;
    }
    if(cancel) {
        std::string error;
        const auto restored=m_DragLayout->room.Restore();
        if(!ApplyAnimatedPlacement(editor,restored,false,error)) {
            editor->ShowUiNotification(UiNotificationSeverity::Error,error,"graph-drag-cancel");cancel=false;
        }
    }
    // A click interrupted an existing tween without moving the node. Resume
    // toward its authored target smoothly when the pointer is released.
    for(const auto& [id,position]:m_DragLayout->displayed)if(const auto* node=editor->GetNodeGraph().FindNode(id))
        AnimateNodePlacement(*node,position);
    if(cancel) {
        m_LayoutRestingNodes=std::move(m_DragLayout->restingNodesBefore);
        m_LayoutRestingGroups=std::move(m_DragLayout->restingGroupsBefore);
    }
    if (!cancel && m_DragLayout->moved) {
        auto& graph=editor->GetNodeGraph();
        std::unordered_map<int,GraphRect> frameStarts;
        for(const auto& group:graph.GetGroups())if(group.compoundBoundary.is_object())frameStarts.emplace(group.id,DisplayGroupRect(group));
        const bool membershipChanged=UpdateCompoundGroupMembership(graph,m_DragLayout->held,m_DragLayout->membershipFrames,
            [&](const Graph& g,const Node& n){return MeasureNodeForLayout(g,n);});
        for(const auto& group:graph.GetGroups()) if(group.compoundBoundary.is_object()) {
            AnimateGroupPlacement(group,frameStarts.at(group.id)); m_LayoutRestingGroups.erase(group.id);
        }
        if(membershipChanged) editor->MarkGraphEdited(-1,false);
        else editor->MarkGraphLayoutEdited(true);
    }
    m_DragLayout.reset();m_DragNodeId=-1;
    editor->EndGraphLayoutGesture(cancel);AdoptLayoutMotionOwner(editor->GetGraphEditorContext());
}

void EditorNodeGraphUI::UpdateActiveNodeDrag(EditorModule* editor,Graph& graph) {
    if(!m_DragLayout)return;
    if(m_GraphContext.documentId!=m_DragLayout->documentId || m_GraphContext.graphId!=m_DragLayout->graphId ||
        m_GraphContext.documentRevision!=m_DragLayout->revision) {
        m_DragLayout.reset();m_DragNodeId=-1;return;
    }
    const double now=ImGui::GetTime();
    if(now-m_DragLayout->lastFrame>2) {EndDragLayout(editor,false);return;}
    m_DragLayout->lastFrame=now;
    if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {EndDragLayout(editor,true);return;}
    const bool released=!ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if(m_DragNodeId<=0 || m_EditingGroupId>0 || m_DragGroupId>0 || m_ResizingGroupId>0 ||
        m_DragOutputNodeId>0 || m_DragInputNodeId>0 || m_MiddlePanCaptureActive || HasDrawerOpen()) {
        EndDragLayout(editor,false);return;
    }
    const auto delta=ImGui::GetIO().MouseDelta;
    const auto travel=ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    if((travel.x==0 && travel.y==0 && !m_DragLayout->moved) || (delta.x==0 && delta.y==0)) {
        if(released)EndDragLayout(editor,false);return;
    }
    const auto mouse=ImGui::GetMousePos();const auto pointer=ScreenToGraph({mouse.x,mouse.y});
    if(!std::isfinite(pointer.x) || !std::isfinite(pointer.y))return;
    Stack::GraphLayout::PlacementEdit edit;std::string error;
    Stack::UiPerformance::Add(Stack::UiPerformance::Counter::LayoutSolve);
    if(!m_DragLayout->room.Move({pointer.x-m_DragLayout->press.x,pointer.y-m_DragLayout->press.y},edit,error) ||
        !ApplyAnimatedPlacement(editor,edit,true,error)) {
        if(error!=m_DragLayout->lastError)editor->ShowUiNotification(UiNotificationSeverity::Error,error,"graph-drag-layout");
        m_DragLayout->lastError=error;
    } else m_DragLayout->lastError.clear();
    if(released)EndDragLayout(editor,false);
}
