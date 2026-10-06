#include "ContinuousLinkStroke.h"
#include <algorithm>
#include <cmath>
namespace Stack::Editor::NodeGraphUIVisuals {
namespace {
ImVec2 Mid(ImVec2 a,ImVec2 b) { return {(a.x+b.x)*0.5f,(a.y+b.y)*0.5f}; }
float Length(ImVec2 a,ImVec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }
void Subdivide(std::vector<CurveSample>& out,ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d,float t0,float t1,int depth) {
    const float polygon=Length(a,b)+Length(b,c)+Length(c,d);
    if (depth>=16 || (polygon<=4.0f && polygon-Length(a,d)<=0.12f)) { out.push_back({d,t1}); return; }
    const auto ab=Mid(a,b),bc=Mid(b,c),cd=Mid(c,d),abc=Mid(ab,bc),bcd=Mid(bc,cd),center=Mid(abc,bcd);
    const float t=(t0+t1)*0.5f;
    Subdivide(out,a,ab,abc,center,t0,t,depth+1); Subdivide(out,center,bcd,cd,d,t,t1,depth+1);
}
}
std::vector<CurveSample> SampleLinkCurve(ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d) {
    std::vector<CurveSample> out; out.reserve(128); out.push_back({a,0}); Subdivide(out,a,b,c,d,0,1,0); return out;
}
void DrawContinuousLinkStroke(ImDrawList* draw,ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d,
    ImU32 color,float thickness,bool straight,ImVec2 fadeMin,ImVec2 fadeMax,float fadeDistance,float gapStart,float gapEnd) {
    if (!draw || thickness<=0) return;
    if (straight) { b=Mid(a,d); c=b; }
    const auto samples=SampleLinkCurve(a,b,c,d);
    std::vector<ImVec2> run; run.reserve(samples.size());
    const int firstVertex=draw->VtxBuffer.Size;
    auto flush=[&] { if (run.size()>1) draw->AddPolyline(run.data(),static_cast<int>(run.size()),color,thickness,ImDrawFlags_None); run.clear(); };
    for (const auto& sample : samples) {
        if (gapStart>=0 && gapEnd>gapStart && sample.t>=gapStart && sample.t<=gapEnd) flush();
        else run.push_back(sample.point);
    }
    flush();
    // Fade the joined mesh rather than drawing disconnected per-segment strokes.
    if (fadeDistance>0) for (int i=firstVertex;i<draw->VtxBuffer.Size;++i) {
        auto& v=draw->VtxBuffer[i];
        float alpha=std::clamp(std::min({v.pos.x-fadeMin.x,fadeMax.x-v.pos.x,v.pos.y-fadeMin.y,fadeMax.y-v.pos.y})/fadeDistance,0.0f,1.0f);
        alpha=alpha*alpha*(3-2*alpha); alpha*=alpha;
        auto rgba=ImGui::ColorConvertU32ToFloat4(v.col); rgba.w*=alpha; v.col=ImGui::ColorConvertFloat4ToU32(rgba);
    }
}
}
