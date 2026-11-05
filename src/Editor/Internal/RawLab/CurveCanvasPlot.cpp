#include "Editor/Internal/RawLab/CurveCanvasPlot.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Utils/GlassControls.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
using namespace RawRecipe;
namespace {
void Geometry(CurveCanvasGeometryCache& cache,
    const std::vector<RawBezierCurvePoint>& points,const CurveAxisMapping& xAxis,
    const CurveAxisMapping& yAxis,float offset) {
    if(!cache.valid || !SameCurvePoints(cache.source,points)) {
        cache.source=points;cache.segments=BuildRawBezierSegments(points);cache.valid=true;cache.mapped=false;
    }
    if(cache.mapped && cache.xAxis==xAxis && cache.yAxis==yAxis && cache.offset==offset) return;
    cache.xAxis=xAxis;cache.yAxis=yAxis;cache.offset=offset;cache.mapped=true;
    cache.mappedPoints.clear();
    const auto mapped=[&](const RawBezierSegment& segment,float t) {
        const float u=1-t,a=u*u*u,b=3*u*u*t,c=3*u*t*t,e=t*t*t;
        const float x=a*segment.left.x+b*segment.leftHandleX+c*segment.rightHandleX+e*segment.right.x;
        const float y=a*segment.left.y+b*segment.leftHandleY+c*segment.rightHandleY+e*segment.right.y;
        return ImVec2{xAxis.ToView(x),yAxis.ToView(y-offset)};
    };
    const auto error=[](ImVec2 p,ImVec2 a,ImVec2 b) {
        const float dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
        const float t=length>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.f,1.f):0.f;
        return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
    };
    for(const auto& segment:cache.segments) {
        const auto left=mapped(segment,0),right=mapped(segment,1);
        if(cache.mappedPoints.empty())cache.mappedPoints.push_back(left);
        // Transform evaluated geometry, never the cubic's handles alone.
        // Quarter samples catch an S-shaped segment whose midpoint is collinear.
        const auto subdivide=[&](auto&& self,float low,float high,ImVec2 a,ImVec2 b,int depth)->void {
            const float middle=(low+high)*.5f;
            const auto mid=mapped(segment,middle);
            const float deviation=std::max({error(mid,a,b),error(mapped(segment,(low+middle)*.5f),a,b),
                error(mapped(segment,(middle+high)*.5f),a,b)});
            if(depth<10 && deviation>1.f/2048) {
                self(self,low,middle,a,mid,depth+1);self(self,middle,high,mid,b,depth+1);
            } else cache.mappedPoints.push_back(b);
        };
        subdivide(subdivide,0,1,left,right,0);
    }
    cache.screenPoints.resize(cache.mappedPoints.size());
}
}
void DrawCurveCanvasPlot(const CurveCanvasPlotContext& context,
    const std::vector<RawBezierCurvePoint>& points,const CurveAmountState& amount,
    CurveCanvasState& s,const RawLabGraphHistogram& histogram,std::uint64_t histogramRevision) {
    const auto& box=context.box;const auto& style=context.style;
    const auto& xAxis=context.xAxis;const auto& yAxis=context.yAxis;
    const auto& input=context.input;const auto mouse=context.mouse;
    const bool diagonal=context.diagonal,amountDriven=context.amountDriven,hovered=context.hovered;
    const float offset=context.offset;
    const auto screen=[&](float x,float y){return ImVec2(box.Min.x+xAxis.ToView(x)*box.GetWidth(),box.Max.y-yAxis.ToView(y-offset)*box.GetHeight());};
    const auto plotScreen=[&](float x,float y){return ImVec2(box.Min.x+xAxis.ToView(x)*box.GetWidth(),box.Max.y-yAxis.ToView(y)*box.GetHeight());};
    auto* d=ImGui::GetWindowDrawList();
    const auto white=style.curve,muted=style.reference,accent=style.selected;
    ImGuiExtras::GlassBackgroundScope plotBackground(*d);
    d->PushClipRect(box.Min,box.Max,true);
    GraphUI::DrawGraphBackdrop(*d,box.Min,box.Max,style);
    if(!s.histogramCached || s.histogramRevision!=histogramRevision || s.histogramValid!=histogram.valid ||
        s.histogramAxis!=xAxis || (!histogramRevision && s.histogramSource!=histogram.luma)) {
        s.histogramSource=histogram.luma;s.histogramRevision=histogramRevision;
        s.histogramAxis=xAxis;s.histogramValid=histogram.valid;s.histogramCached=true;
        s.histogramBins=MapCurveHistogram(histogram.luma,xAxis);
        s.histogramPeak=*std::max_element(s.histogramBins.begin(),s.histogramBins.end());
    }
    if(s.histogramValid) {
        for(int i=0;i<int(s.histogramBins.size());++i) {
            const float x=(float(i)+.5f)/s.histogramBins.size();
            d->AddLine({box.Min.x+x*box.GetWidth(),box.Max.y},{box.Min.x+x*box.GetWidth(),box.Max.y-(s.histogramPeak>0?.75f*s.histogramBins[i]/s.histogramPeak:0)*box.GetHeight()},style.histogram,style.scale);
        }
    }
    if(!style.liquidPoints || !diagonal) GraphUI::DrawGraphReference(*d,plotScreen(0,diagonal?0:.5f),plotScreen(1,diagonal?1:.5f),style);
    if(s.cachedSamplesRevision!=s.samplesRevision || s.cachedSamplesSize!=s.samples.size()) {
        s.sampleBins.fill(0);
        for(float x:s.samples)if(x>=0 && x<=1)++s.sampleBins[std::min(127,int(x*128))];
        s.samplePeak=*std::max_element(s.sampleBins.begin(),s.sampleBins.end());
        s.cachedSamplesRevision=s.samplesRevision;s.cachedSamplesSize=s.samples.size();
    }
    if(s.samplePeak)for(int i=0;i<128;++i) {
        const float x=box.Min.x+xAxis.ToView((i+.5f)/128)*box.GetWidth();
        d->AddLine({x,box.Max.y},{x,box.Max.y-.18f*box.GetHeight()*s.sampleBins[i]/s.samplePeak},style.guide,style.scale);
    }
    const auto paint=[&](const auto& curve,ImU32 color,float thickness,int cacheIndex) {
        auto& cache=s.geometry[cacheIndex];Geometry(cache,curve,xAxis,yAxis,offset);
        for(size_t i=0;i<cache.mappedPoints.size();++i) {
            const auto p=cache.mappedPoints[i];
            cache.screenPoints[i]={box.Min.x+p.x*box.GetWidth(),box.Max.y-p.y*box.GetHeight()};
        }
        auto curveStyle=style;curveStyle.curveWidth=thickness;
        GraphUI::DrawGraphPolyline(*d,cache.screenPoints.data(),int(cache.screenPoints.size()),color,curveStyle);
    };
    if(s.gesture) {
        paint(s.before,muted,style.scale,1);
        if(s.mode==CurveGestureMode::Bend) for(float x:{xAxis.ToView(s.pivot)-s.radius,xAxis.ToView(s.pivot),xAxis.ToView(s.pivot)+s.radius})
            d->AddLine({box.Min.x+x*box.GetWidth(),box.Min.y},{box.Min.x+x*box.GetWidth(),box.Max.y},muted,style.scale);
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
    if(context.viewOptions.automatic && s.framing.initialized) {
        // Labels follow the inverse view map, so a fitted graph still exposes
        // real input values. Formatting is cached with the accepted input fit.
        float previousRight=box.Min.x;
        for(int i=0;i<3;++i) {
            const auto* label=s.framing.labels[i].data();
            const auto size=ImGui::CalcTextSize(label);
            const float x=std::clamp(box.Min.x+(.1f+.4f*i)*box.GetWidth()-size.x*.5f,box.Min.x,std::max(box.Min.x,box.Max.x-size.x));
            if(x<previousRight || x+size.x>box.Max.x)continue;
            d->AddText({x,box.Max.y+style.controlGap*.5f},style.label,label);
            previousRight=x+size.x+style.controlGap;
        }
        if(hovered && nearest>=0) {
            const auto& options=context.viewOptions;
            const auto& point=points[nearest];
            const float inputValue=options.domainMinimum+point.x*(options.domainMaximum-options.domainMinimum);
            if(diagonal) {
                const float outputValue=options.domainMinimum+point.y*(options.domainMaximum-options.domainMinimum);
                ImGui::SetTooltip(options.exposureUnits?"Input: %+.2f EV\nOutput: %+.2f EV":"Input: %.5g\nOutput: %.5g",inputValue,outputValue);
            } else ImGui::SetTooltip("Input: %+.2f EV\nAdjustment: %+.2f EV",inputValue,-4.f+8.f*point.y);
        }
    }
}
}
