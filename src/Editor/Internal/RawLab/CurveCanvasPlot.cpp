#include "Editor/Internal/RawLab/CurveCanvasPlot.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Utils/GlassControls.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
using namespace RawRecipe;
namespace {
const std::vector<RawBezierSegment>& Geometry(CurveCanvasGeometryCache& cache,
    const std::vector<RawBezierCurvePoint>& points) {
    if(!cache.valid || !SameCurvePoints(cache.source,points)) {
        cache.source=points;cache.segments=BuildRawBezierSegments(points);cache.valid=true;
    }
    return cache.segments;
}
}
void DrawCurveCanvasPlot(const CurveCanvasPlotContext& context,
    const std::vector<RawBezierCurvePoint>& points,const CurveAmountState& amount,
    CurveCanvasState& s,const RawLabGraphHistogram& histogram,std::uint64_t histogramRevision) {
    const auto& box=context.box;const auto& style=context.style;const auto& view=context.view;
    const auto& input=context.input;const auto mouse=context.mouse;
    const bool diagonal=context.diagonal,amountDriven=context.amountDriven,hovered=context.hovered;
    const float offset=context.offset,yMinimum=context.yMinimum,yMaximum=context.yMaximum;
    const float span=std::max(.0001f,view.maximum-view.minimum),ySpan=std::max(.0001f,yMaximum-yMinimum);
    const auto screen=[&](float x,float y){return ImVec2(box.Min.x+(x-view.minimum)/span*box.GetWidth(),box.Max.y-(y-offset-yMinimum)/ySpan*box.GetHeight());};
    const auto plotScreen=[&](float x,float y){return ImVec2(box.Min.x+(x-view.minimum)/span*box.GetWidth(),box.Max.y-(y-yMinimum)/ySpan*box.GetHeight());};
    auto* d=ImGui::GetWindowDrawList();
    const auto white=style.curve,muted=style.reference,accent=style.selected;
    ImGuiExtras::GlassBackgroundScope plotBackground(*d);
    d->PushClipRect(box.Min,box.Max,true);
    GraphUI::DrawGraphBackdrop(*d,box.Min,box.Max,style);
    if(!s.histogramCached || s.histogramRevision!=histogramRevision || s.histogramValid!=histogram.valid ||
        s.histogramView.minimum!=view.minimum || s.histogramView.maximum!=view.maximum ||
        s.histogramSource!=histogram.luma) {
        s.histogramSource=histogram.luma;s.histogramRevision=histogramRevision;
        s.histogramView=view;s.histogramValid=histogram.valid;s.histogramCached=true;
        s.histogramBins=view.minimum==0.f && view.maximum==1.f
            ? histogram.luma : CropRawLabGraphHistogram(histogram,view).luma;
        s.histogramPeak=*std::max_element(s.histogramBins.begin(),s.histogramBins.end());
    }
    if(s.histogramValid) {
        for(int i=0;i<int(s.histogramBins.size());++i) {
            const float x=float(i)/(s.histogramBins.size()-1);
            d->AddLine({box.Min.x+x*box.GetWidth(),box.Max.y},{box.Min.x+x*box.GetWidth(),box.Max.y-(s.histogramPeak>0?.75f*s.histogramBins[i]/s.histogramPeak:0)*box.GetHeight()},style.histogram,style.scale);
        }
    }
    if(!style.liquidPoints) GraphUI::DrawGraphReference(*d,plotScreen(0,diagonal?0:.5f),plotScreen(1,diagonal?1:.5f),style);
    if(s.cachedSamplesRevision!=s.samplesRevision || s.cachedSamplesSize!=s.samples.size()) {
        s.sampleBins.fill(0);
        for(float x:s.samples)if(x>=0 && x<=1)++s.sampleBins[std::min(127,int(x*128))];
        s.samplePeak=*std::max_element(s.sampleBins.begin(),s.sampleBins.end());
        s.cachedSamplesRevision=s.samplesRevision;s.cachedSamplesSize=s.samples.size();
    }
    if(s.samplePeak)for(int i=0;i<128;++i)d->AddLine(plotScreen((i+.5f)/128,yMinimum),plotScreen((i+.5f)/128,yMinimum+.18f*ySpan*s.sampleBins[i]/s.samplePeak),style.guide,style.scale);
    const auto paint=[&](const auto& curve,ImU32 color,float thickness,int cacheIndex) {
        auto curveStyle=style;curveStyle.curveWidth=thickness;
        for(const auto& seg:Geometry(s.geometry[cacheIndex],curve)) {
            GraphUI::DrawGraphBezier(*d,screen(seg.left.x,seg.left.y),screen(seg.leftHandleX,seg.leftHandleY),
                screen(seg.rightHandleX,seg.rightHandleY),screen(seg.right.x,seg.right.y),color,curveStyle);
        }
    };
    if(s.gesture) {
        paint(s.before,muted,style.scale,1);
        if(s.mode==CurveGestureMode::Bend) for(float x:{s.pivot-s.radius,s.pivot,s.pivot+s.radius})d->AddLine(plotScreen(x,yMinimum),plotScreen(x,yMaximum),muted,style.scale);
    }
    if(amount.locked && amount.amount<1 && !amountDriven)paint(amount.reference,muted,style.scale,2);
    const auto& source=amount.locked?amount.reference:points;
    if(amountDriven && (s.effectiveAmount!=amount.amount || s.effectiveDiagonal!=diagonal ||
        !SameCurvePoints(s.effectiveSource,source))) {
        s.effectiveSource=source;s.effectiveAmount=amount.amount;s.effectiveDiagonal=diagonal;
        s.effectivePoints=AttenuateCurve(source,amount.amount,diagonal);
    }
    const auto& effective=amountDriven?s.effectivePoints:points;
    if(amountDriven)paint(source,muted,style.scale,2);
    paint(effective,white,style.curveWidth,0);
    if(s.probe && *s.probe>=0 && *s.probe<=1) {
        const float x=*s.probe,y=EvaluateRawBezierCurve(effective,x);const auto p=screen(x,y);
        GraphUI::DrawGraphProbe(*d,box.Min,box.Max,p.x,style);
        const float radius=5.f*style.scale;
        d->AddQuad({p.x,p.y-radius},{p.x+radius,p.y},{p.x,p.y+radius},{p.x-radius,p.y},accent,1.5f*style.scale);
    }
    d->PopClipRect();
    plotBackground.End();
    const float pointApron=style.liquidPoints?style.selectedPointRadius*1.25f:0.f;
    d->PushClipRect({box.Min.x-pointApron,box.Min.y-pointApron},{box.Max.x+pointApron,box.Max.y+pointApron},true);
    int nearest=context.geometryChanged?-1:context.hoveredPoint;
    float best=16.f*16.f*style.scale*style.scale;
    for(int i=0;context.geometryChanged && hovered && i<int(points.size());++i) {
        const auto p=screen(points[i].x,points[i].y);
        const float dx=mouse.x-p.x,dy=mouse.y-p.y,dist=dx*dx+dy*dy;
        if(dist<best){best=dist;nearest=i;}
    }
    const int pointCount=int(points.size());
    const auto pointGroup=ImHashData(&pointCount,sizeof(pointCount),input.identity);
    for(int i=0;i<int(points.size());++i) {
        const auto p=screen(points[i].x,points[i].y);const float dist=std::hypot(mouse.x-p.x,mouse.y-p.y);
        if(hovered)GraphUI::DrawGraphPointHalo(*d,p,dist,style);
        GraphUI::DrawGraphPoint(*d,p,style,s.selected[i],false,ImHashData(&i,sizeof(i),pointGroup),
            hovered && nearest==i,s.gesture && (s.dragged==i || (s.mode==CurveGestureMode::Select && s.selected[i])),&input);
    }
    d->PopClipRect();
}
}
