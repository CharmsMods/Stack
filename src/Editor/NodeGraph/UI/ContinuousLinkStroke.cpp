#include "ContinuousLinkStroke.h"
#include <algorithm>
#include <cmath>
namespace Stack::Editor::NodeGraphUIVisuals {
namespace {
ImVec2 Mid(ImVec2 a,ImVec2 b) { return {(a.x+b.x)*0.5f,(a.y+b.y)*0.5f}; }
float Length(ImVec2 a,ImVec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }
void Subdivide(std::vector<CurveSample>& out,ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d,float t0,float t1,int depth) {
    const float polygon=Length(a,b)+Length(b,c)+Length(c,d);
    const ImVec2 chord(d.x-a.x,d.y-a.y);
    const float chordSquared=chord.x*chord.x+chord.y*chord.y;
    const auto distanceSquared=[&](ImVec2 point) {
        const float t=std::clamp(((point.x-a.x)*chord.x+(point.y-a.y)*chord.y)/chordSquared,0.0f,1.0f);
        const float x=point.x-a.x-t*chord.x,y=point.y-a.y-t*chord.y;
        return x*x+y*y;
    };
    // Subpixel curvature, with enough vertices to interpolate the edge fade.
    // Straight and nearly straight wires do not need a vertex every four pixels.
    const bool flat=chordSquared>1e-6f
        ? std::max(distanceSquared(b),distanceSquared(c))<=0.0625f
        : polygon<=0.25f;
    if (depth>=16 || (polygon<=16.0f && flat)) { out.push_back({d,t1}); return; }
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
    DrawContinuousLinkStroke(draw,SampleLinkCurve(a,b,c,d),color,thickness,fadeMin,fadeMax,fadeDistance,gapStart,gapEnd);
}
void DrawContinuousLinkStroke(ImDrawList* draw,const std::vector<CurveSample>& samples,
    ImU32 color,float thickness,ImVec2 fadeMin,ImVec2 fadeMax,float fadeDistance,float gapStart,float gapEnd) {
    if (!draw || thickness<=0) return;
    const int firstVertex=draw->VtxBuffer.Size;
    draw->PathClear();
    auto flush=[&] { draw->PathStroke(color,thickness,ImDrawFlags_None); };
    const bool hasGap=gapStart>=0 && gapEnd>gapStart;
    for (std::size_t i=0;i<samples.size();++i) {
        const auto& sample=samples[i];
        if (hasGap && i>0) {
            const auto& previous=samples[i-1];
            const auto boundary=[&](float t) {
                const float amount=(t-previous.t)/(sample.t-previous.t);
                draw->PathLineTo({previous.point.x+(sample.point.x-previous.point.x)*amount,
                    previous.point.y+(sample.point.y-previous.point.y)*amount});
            };
            if (previous.t<gapStart && sample.t>=gapStart) { boundary(gapStart); flush(); }
            if (previous.t<=gapEnd && sample.t>gapEnd) boundary(gapEnd);
        }
        if (!hasGap || sample.t<gapStart || sample.t>gapEnd) draw->PathLineTo(sample.point);
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
