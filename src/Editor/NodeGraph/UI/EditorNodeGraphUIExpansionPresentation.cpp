#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "GraphLayoutMotionTiming.h"
#include "App/UiPerformanceTrace.h"

namespace {
using namespace Stack::Editor::GraphLayoutMotionTiming;
float Ease(double value) {const float t=float(std::clamp(value,0.0,1.0));return t*t*(3-2*t);}
}

float EditorNodeGraphUI::ExpansionNodeAlpha(int id) const {
    if(m_RenderPreviewOnly || m_RenderCaptureOnly || !m_ExpansionMotion || m_ExpansionMotion->channelFold || !m_ExpansionMotion->nodes.count(id))return 1;
    return Ease((m_ExpansionMotion->elapsed-Room)/Reveal);
}

bool EditorNodeGraphUI::LayoutNodeInteractive(int id) const {return ExpansionNodeAlpha(id)>=1;}

bool EditorNodeGraphUI::LayoutGroupVisible(int id) const {
    return m_RenderPreviewOnly || m_RenderCaptureOnly || !m_ExpansionMotion || !m_ExpansionMotion->groups.count(id) ||
        m_ExpansionMotion->elapsed>Room;
}

float EditorNodeGraphUI::ExpansionLinkProgress(const EditorNodeGraph::Link& link) const {
    if(m_RenderPreviewOnly || m_RenderCaptureOnly || !m_ExpansionMotion || m_ExpansionMotion->channelFold)return 1;
    const auto& expansion=*m_ExpansionMotion;
    if(!expansion.nodes.count(link.fromNodeId) && !expansion.nodes.count(link.toNodeId))return 1;
    const auto from=expansion.columns.find(link.fromNodeId),to=expansion.columns.find(link.toNodeId);
    const float column=from!=expansion.columns.end()?from->second:to!=expansion.columns.end()?to->second:0;
    return TraceProgress(expansion.elapsed,column);
}

void EditorNodeGraphUI::DrawExpansionOrigin(ImDrawList& draw) const {
    if(!m_ExpansionMotion || m_RenderPreviewOnly || m_RenderCaptureOnly)return;
    const auto& expansion=*m_ExpansionMotion;const auto& origin=expansion.origin;
    const float fold=expansion.channelFold?Ease(expansion.elapsed/.22):0;
    const float opacity=expansion.channelFold?1-fold:1-Ease((expansion.elapsed-Room)/(.14));
    if(opacity<=0 || !origin.Valid())return;
    Stack::UiPerformance::Add(Stack::UiPerformance::Counter::LayoutOrigin);
    auto minimum=expansion.originRect.min,maximum=expansion.originRect.max;
    if(expansion.channelFold) {
        minimum={minimum.x+(expansion.foldTarget.min.x-minimum.x)*fold,minimum.y+(expansion.foldTarget.min.y-minimum.y)*fold};
        maximum={maximum.x+(expansion.foldTarget.max.x-maximum.x)*fold,maximum.y+(expansion.foldTarget.max.y-maximum.y)*fold};
    }
    const auto low=GraphToScreen(minimum),high=GraphToScreen(maximum);
    const auto transform=[&](ImVec2 p) {return ImVec2{
        low.x+(p.x-origin.minimum.x)*(high.x-low.x)/(origin.maximum.x-origin.minimum.x),
        low.y+(p.y-origin.minimum.y)*(high.y-low.y)/(origin.maximum.y-origin.minimum.y)};};
    for(const auto& mesh:origin.meshes) {
        draw.PushTextureID(mesh.texture?ImTextureRef((ImTextureID)(intptr_t)mesh.texture->name):ImGui::GetIO().Fonts->TexRef);
        draw.PushClipRect(transform({mesh.clip.x,mesh.clip.y}),transform({mesh.clip.z,mesh.clip.w}),true);
        for(std::size_t begin=0;begin<mesh.vertices.size();begin+=12000) {
            const int count=int(std::min<std::size_t>(12000,mesh.vertices.size()-begin));draw.PrimReserve(count,count);
            for(int i=0;i<count;++i) {
                const auto& v=mesh.vertices[begin+i];const auto color=(v.col&~IM_COL32_A_MASK)|
                    (static_cast<ImU32>((v.col>>IM_COL32_A_SHIFT)*opacity)<<IM_COL32_A_SHIFT);
                draw.PrimVtx(transform(v.pos),v.uv,color);
            }
        }
        draw.PopClipRect();draw.PopTextureID();
    }
}
