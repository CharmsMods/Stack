#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/CompoundGroupMembership.h"
#include "Notifications/NotificationPresenter.h"
#include "GraphLayoutMotionTiming.h"
#include <imgui_internal.h>

#include <algorithm>
#include <limits>

namespace {
using namespace EditorNodeGraph;
bool Equal(Vec2 a,Vec2 b) {return a.x==b.x && a.y==b.y;}
Vec2 Blend(Vec2 a,Vec2 b,float t) {return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
}

bool EditorNodeGraphUI::DecompileNode(EditorModule* editor,int nodeId,std::string* error) {
    m_ActiveEditor=editor;
    const auto before=editor->GetGraphEditorContext();
    const auto* original=before.graph->FindNode(nodeId);
    const auto expansionStart=original?DisplayNodePosition(*original):Vec2{};
    AdvanceLayoutMotion(before);
    auto expansion=std::make_unique<ExpansionMotion>();
    if(original && ImGui::GetCurrentContext()->WithinFrameScope) {
        expansion->origin=CaptureNodeAppearance(editor,nodeId,{m_CanvasMin.x,m_CanvasMin.y},{m_CanvasMax.x,m_CanvasMax.y},true);
        expansion->originRect={ScreenToGraph({expansion->origin.minimum.x,expansion->origin.minimum.y}),
            ScreenToGraph({expansion->origin.maximum.x,expansion->origin.maximum.y})};
    }
    std::unordered_map<std::string,Vec2> starts;
    std::unordered_map<int,GraphRect> groupStarts;
    for(const auto& node:before.graph->GetNodes())starts.emplace(node.instanceUuid,DisplayNodePosition(node));
    for(const auto& group:before.graph->GetGroups())groupStarts.emplace(group.id,DisplayGroupRect(group));
    std::unordered_map<int,Stack::Editor::NodeGraphUILayout::NodeLogicalLayout> measured;
    const auto logical=[&](const Graph& graph,const Node& node)->const Stack::Editor::NodeGraphUILayout::NodeLogicalLayout& {
        auto it=measured.find(node.id);
        if(it==measured.end())it=measured.emplace(node.id,BuildLogicalNodeLayout(graph,node)).first;
        return it->second;
    };
    const auto measure=[&](const Graph& graph,const Node& node) {
        // Existing measured content heights belong to this native canvas. New
        // nodes use the same declared control-height contracts as their first draw.
        const auto& r=logical(graph,node).bounds.persistentVisual;
        return GraphRect{r.min,r.max};
    };
    const auto measureSocket=[&](const Graph& graph,const Node& node,const std::string& socket,SocketDirection direction)->std::optional<Vec2> {
        for(const auto& p:logical(graph,node).sockets)if(p.socketId==socket && p.direction==direction)return p.center;
        return {};
    };
    if(!editor->UnpackCompoundNode(nodeId,error,measure,measureSocket))return false;
    const auto after=editor->GetGraphEditorContext();
    ClearLayoutMotion();
    m_LayoutRestingNodes.clear();m_LayoutRestingGroups.clear();
    AdoptLayoutMotionOwner(after);
    if(!Stack::Notifications::SystemReducedMotion()) {
        for(const auto& node:after.graph->GetNodes()) {
            const auto it=starts.find(node.instanceUuid);
            AnimateNodePlacement(node,it!=starts.end()?it->second:expansionStart);
            if(it==starts.end()) {
                expansion->nodes.insert(node.id);
                if(auto motion=m_LayoutNodeMotion.find(node.id);motion!=m_LayoutNodeMotion.end()) {
                    motion->second.delay=Stack::Editor::GraphLayoutMotionTiming::Room;
                    motion->second.duration=Stack::Editor::GraphLayoutMotionTiming::Reveal;
                }
            } else if(auto motion=m_LayoutNodeMotion.find(node.id);motion!=m_LayoutNodeMotion.end())
                motion->second.duration=Stack::Editor::GraphLayoutMotionTiming::Room;
        }
        for(const auto& group:after.graph->GetGroups()) {
            const auto it=groupStarts.find(group.id);
            AnimateGroupPlacement(group,it!=groupStarts.end()?it->second:
                GraphRect{expansionStart,{expansionStart.x+32,expansionStart.y+32}});
            if(it==groupStarts.end()) {
                expansion->groups.insert(group.id);
                if(auto motion=m_LayoutGroupMotion.find(group.id);motion!=m_LayoutGroupMotion.end()) {
                    motion->second.delay=Stack::Editor::GraphLayoutMotionTiming::Room;
                    motion->second.duration=Stack::Editor::GraphLayoutMotionTiming::Reveal;
                }
            } else if(auto motion=m_LayoutGroupMotion.find(group.id);motion!=m_LayoutGroupMotion.end())
                motion->second.duration=Stack::Editor::GraphLayoutMotionTiming::Room;
        }
        float left=std::numeric_limits<float>::max(),right=std::numeric_limits<float>::lowest();
        for(int id:expansion->nodes) {const float x=after.graph->FindNode(id)->position.x;left=std::min(left,x);right=std::max(right,x);}
        for(int id:expansion->nodes)expansion->columns.emplace(id,(after.graph->FindNode(id)->position.x-left)/std::max(1.f,right-left));
        m_ExpansionMotion=std::move(expansion);
    }
    return true;
}

GraphRect EditorNodeGraphUI::MeasureNodeForLayout(const Graph& graph,const Node& node) const {
    if(m_GraphContext.graph==&graph)if(const auto* cache=FindNodeLayoutCache(node.id);
        cache && cache->hasLogicalLayout && cache->instanceUuid==node.instanceUuid &&
        cache->logicalInputRevision==LogicalNodeInputRevision(graph,node))
        return {cache->logicalLayout.bounds.persistentVisual.min,cache->logicalLayout.bounds.persistentVisual.max};
    const auto logical=BuildLogicalNodeLayout(graph,node);
    return {logical.bounds.persistentVisual.min,logical.bounds.persistentVisual.max};
}

void EditorNodeGraphUI::ClearLayoutMotion() {
    if(m_ExpansionMotion)++m_WireWorldRevision;
    m_LayoutNodeMotion.clear();m_LayoutGroupMotion.clear();m_ExpansionMotion.reset();
}

void EditorNodeGraphUI::AdoptLayoutMotionOwner(const Stack::Editor::GraphEditorContext& context) {
    m_LayoutDocumentId=context.documentId;m_LayoutGraphId=context.graphId;m_LayoutDocumentRevision=context.documentRevision;
    m_LayoutLastFrame=ImGui::GetTime();
}

void EditorNodeGraphUI::ObserveLayoutRevision(const Stack::Editor::GraphEditorContext& context) {
    const bool sameOwner=context.documentId==m_GraphContext.documentId && context.graphId==m_GraphContext.graphId;
    if(!sameOwner){m_CompoundEntryGraph=nullptr;m_CompoundEntryRevision=0;m_LayoutGroupPresentation.clear();m_LayoutRestingNodes.clear();m_LayoutRestingGroups.clear();}
    const bool changed=!sameOwner || context.documentRevision!=m_GraphContext.documentRevision ||
        (context.graph && context.graph->GetStructureRevision()!=m_LastGraphStructureRevision);
    if(!changed)return;
    if(m_DragLayout && (!sameOwner || context.documentRevision!=m_DragLayout->revision)) {
        m_DragLayout.reset();m_DragNodeId=-1;
    }
    // Commands outside the canvas (including undo/redo) may restore different
    // placements. Reuse displayed geometry once at that revision boundary;
    // RAW canvas IDs may change, so match nodes by their authored UUIDs.
    if(sameOwner && context.documentRevision!=m_LayoutDocumentRevision) {
        m_ExpansionMotion.reset();m_LayoutRestingNodes.clear();m_LayoutRestingGroups.clear();
        std::unordered_map<std::string,Vec2> previous;
        for(const auto& [id,cache]:m_NodeLayoutCache)if(cache.hasProjection && !cache.instanceUuid.empty())
            previous.emplace(cache.instanceUuid,cache.projectedPosition);
        for(const auto& node:context.graph->GetNodes())if(const auto found=previous.find(node.instanceUuid);found!=previous.end())
            AnimateNodePlacement(node,found->second);
        for(const auto& group:context.graph->GetGroups()) {
            const auto prior=m_LayoutGroupPresentation.find(group.id);
            const auto uuid=group.compoundBoundary.is_object()?group.compoundBoundary.value("instanceUuid",std::string{}):std::string{};
            if(prior==m_LayoutGroupPresentation.end() || prior->second.title!=group.title || prior->second.uuid!=uuid)continue;
            const auto motion=m_LayoutGroupMotion.find(group.id);
            AnimateGroupPlacement(group,motion!=m_LayoutGroupMotion.end()?motion->second.current:prior->second.rect);
        }
        AdoptLayoutMotionOwner(context);
    }
    const auto measuredFrames=MeasureCompoundGroupFrames(*context.graph,[&](const Graph& g,const Node& n){return MeasureNodeForLayout(g,n);});
    m_LayoutGroupPresentation.clear();
    for(const auto& group:context.graph->GetGroups()) {
        auto fitted=group;
        if(const auto found=measuredFrames.find(group.id);found!=measuredFrames.end()) {
            fitted.position=found->second.min;
            fitted.size={found->second.max.x-found->second.min.x,found->second.max.y-found->second.min.y};
            AnimateGroupPlacement(fitted,DisplayGroupRect(group));
        }
        m_LayoutGroupPresentation.emplace(group.id,GroupPresentation{
            group.title,group.compoundBoundary.is_object()?group.compoundBoundary.value("instanceUuid",std::string{}):std::string{},
            {fitted.position,{fitted.position.x+fitted.size.x,fitted.position.y+fitted.size.y}}});
    }
}

void EditorNodeGraphUI::AnimateNodePlacement(const Node& node,Vec2 start,bool immediate) {
    if(immediate || Equal(start,node.position) || Stack::Notifications::SystemReducedMotion()) {
        m_LayoutNodeMotion.erase(node.id);return;
    }
    const auto previous=m_LayoutNodeMotion.find(node.id);
    if(previous!=m_LayoutNodeMotion.end() && previous->second.uuid==node.instanceUuid && Equal(previous->second.target,node.position))return;
    m_LayoutNodeMotion[node.id]={node.instanceUuid,start,node.position,start};
}

void EditorNodeGraphUI::AnimateGroupPlacement(const NodeGroup& group,GraphRect start) {
    const GraphRect target{group.position,{group.position.x+group.size.x,group.position.y+group.size.y}};
    if((Equal(start.min,target.min) && Equal(start.max,target.max)) || Stack::Notifications::SystemReducedMotion()) {
        m_LayoutGroupMotion.erase(group.id);return;
    }
    const auto previous=m_LayoutGroupMotion.find(group.id);
    if(previous!=m_LayoutGroupMotion.end() && Equal(previous->second.target.min,target.min) && Equal(previous->second.target.max,target.max))return;
    m_LayoutGroupMotion[group.id]={start,target,start};
}

void EditorNodeGraphUI::AdvanceLayoutMotion(const Stack::Editor::GraphEditorContext& context) {
    if(!HasLayoutMotion())return;
    const double now=ImGui::GetTime();
    if(context.documentId!=m_LayoutDocumentId || context.graphId!=m_LayoutGraphId ||
        context.documentRevision!=m_LayoutDocumentRevision || Stack::Notifications::SystemReducedMotion()) {
        ClearLayoutMotion();return;
    }
    // Presentation time advances by bounded visible steps. A slow command or
    // first frame cannot skip the choreography or cancel its own animation.
    double step=std::clamp(now-m_LayoutLastFrame,0.0,.04);
    m_LayoutLastFrame=now;
    if(m_ExpansionMotion) {
        if(!m_ExpansionMotion->presented){m_ExpansionMotion->presented=true;step=0;}
        const double ready=Stack::Editor::GraphLayoutMotionTiming::Room+Stack::Editor::GraphLayoutMotionTiming::Reveal;
        const bool wasReady=m_ExpansionMotion->elapsed>=ready;
        m_ExpansionMotion->elapsed+=step;m_LinkHitTestFrame=-1;
        // Hidden children initially coincide at their parent's position. They
        // enter routing together once revealed; even a stationary child must
        // invalidate the obstacle scene at that presentation boundary.
        if(!wasReady && m_ExpansionMotion->elapsed>=ready)++m_WireWorldRevision;
        if(m_ExpansionMotion->channelFold) {
            ++m_WireWorldRevision;
            if(m_ExpansionMotion->elapsed>=.22) {
                for(const auto& [id,column]:m_ExpansionMotion->columns)
                    if(auto cache=m_NodeLayoutCache.find(id);cache!=m_NodeLayoutCache.end())cache->second.hasProjection=false;
                m_ExpansionMotion.reset();++m_WireWorldRevision;
            }
        } else if(m_ExpansionMotion->elapsed>=Stack::Editor::GraphLayoutMotionTiming::Total)m_ExpansionMotion.reset();
    }
    for(auto it=m_LayoutNodeMotion.begin();it!=m_LayoutNodeMotion.end();) {
        const auto* node=context.graph->FindNode(it->first);auto& motion=it->second;
        motion.elapsed+=step;
        const float t=std::clamp(float((motion.elapsed-motion.delay)/motion.duration),0.f,1.f),ease=t*t*(3-2*t);
        if(!node || node->instanceUuid!=motion.uuid || !Equal(node->position,motion.target) || t>=1)it=m_LayoutNodeMotion.erase(it);
        else {motion.current=Blend(motion.start,motion.target,ease);++it;}
    }
    for(auto it=m_LayoutGroupMotion.begin();it!=m_LayoutGroupMotion.end();) {
        const auto* group=context.graph->FindGroup(it->first);auto& motion=it->second;
        motion.elapsed+=step;
        const float t=std::clamp(float((motion.elapsed-motion.delay)/motion.duration),0.f,1.f),ease=t*t*(3-2*t);
        const auto presentation=m_LayoutGroupPresentation.find(it->first);
        const bool matchesPresentation=presentation!=m_LayoutGroupPresentation.end() &&
            Equal(presentation->second.rect.min,motion.target.min) && Equal(presentation->second.rect.max,motion.target.max);
        const bool matchesAuthored=group && Equal(group->position,motion.target.min) &&
            Equal(Vec2{group->position.x+group->size.x,group->position.y+group->size.y},motion.target.max);
        if(!group || (!matchesAuthored && !matchesPresentation) || t>=1)it=m_LayoutGroupMotion.erase(it);
        else {motion.current={Blend(motion.start.min,motion.target.min,ease),Blend(motion.start.max,motion.target.max,ease)};++it;}
    }
}

Vec2 EditorNodeGraphUI::DisplayNodePosition(const Node& node) const {
    if(!m_RenderPreviewOnly && !m_RenderCaptureOnly && m_DragLayout) {
        const auto held=m_DragLayout->displayed.find(node.id);
        if(held!=m_DragLayout->displayed.end())return held->second;
    }
    if(!m_RenderPreviewOnly && !m_RenderCaptureOnly && !m_LayoutNodeMotion.empty()) {
        const auto it=m_LayoutNodeMotion.find(node.id);
        if(it!=m_LayoutNodeMotion.end() && it->second.uuid==node.instanceUuid && Equal(node.position,it->second.target))return it->second.current;
    }
    return node.position;
}

GraphRect EditorNodeGraphUI::DisplayGroupRect(const NodeGroup& group) const {
    if(!m_RenderPreviewOnly && !m_RenderCaptureOnly && !m_LayoutGroupMotion.empty()) {
        const auto it=m_LayoutGroupMotion.find(group.id);
        if(it!=m_LayoutGroupMotion.end())return it->second.current;
    }
    if(group.compoundBoundary.is_object())if(const auto cached=m_LayoutGroupPresentation.find(group.id);cached!=m_LayoutGroupPresentation.end())
        return cached->second.rect;
    return {group.position,{group.position.x+group.size.x,group.position.y+group.size.y}};
}

void EditorNodeGraphUI::TakeOverNodeMotion(Node& node) {
    node.position=DisplayNodePosition(node);m_LayoutNodeMotion.erase(node.id);
}

void EditorNodeGraphUI::TakeOverGroupMotion(Graph& graph,NodeGroup& group) {
    if(!HasLayoutMotion())return;
    // A new placement gesture adopts what is actually under the pointer. Its
    // normal history route records this edit; the prior decompile remains atomic.
    const GraphRect target{group.position,{group.position.x+group.size.x,group.position.y+group.size.y}};
    const auto contained=[&](Vec2 p){return p.x>=target.min.x && p.y>=target.min.y && p.x<=target.max.x && p.y<=target.max.y;};
    for(auto& node:graph.GetNodes()) {
        const bool member=group.compoundBoundary.is_object()
            ? std::find(group.compoundMembers.begin(),group.compoundMembers.end(),node.id)!=group.compoundMembers.end()
            : contained(node.position);
        if(member)TakeOverNodeMotion(node);
    }
    for(auto& item:graph.GetGroups()) {
        if(item.id==group.id || (contained(item.position) && contained({item.position.x+item.size.x,item.position.y+item.size.y}))) {
            const auto r=DisplayGroupRect(item);item.position=r.min;item.size={r.max.x-r.min.x,r.max.y-r.min.y};m_LayoutGroupMotion.erase(item.id);
        }
    }
}
