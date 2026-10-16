#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Raw/Curves/LocalEvCurveNormalization.h"
#include "Raw/Curves/TonalCurvePoints.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
using namespace RawRecipe;
namespace {
void Smooth(std::vector<RawBezierCurvePoint>& points) {
    for(auto& p:points) {p.incoming={1};p.outgoing={1};}
}
void SmoothMovedPoints(std::vector<RawBezierCurvePoint>& points,const std::vector<RawBezierCurvePoint>& before) {
    for(size_t i=0;i<points.size();++i) {
        if(points[i].x==before[i].x && points[i].y==before[i].y)continue;
        // Rebuild the joins around moved points, preserving distant manual geometry.
        for(size_t j=i?i-1:0;j<=std::min(i+1,points.size()-1);++j)
            points[j].incoming=points[j].outgoing={1};
    }
}
void FreezeHandles(std::vector<RawBezierCurvePoint>& points) {
    const auto segments=BuildRawBezierSegments(points);
    for(size_t i=0;i<segments.size();++i) {
        const auto& segment=segments[i];
        points[i].outgoing={1,true,segment.leftHandleX-segment.left.x,segment.leftHandleY-segment.left.y};
        points[i+1].incoming={1,true,segment.rightHandleX-segment.right.x,segment.rightHandleY-segment.right.y};
    }
}
const std::vector<RawBezierSegment>& Geometry(CurveCanvasGeometryCache& cache,
    const std::vector<RawBezierCurvePoint>& points) {
    if(!cache.valid || !SameCurvePoints(cache.source,points)) {
        cache.source=points;cache.segments=BuildRawBezierSegments(points);cache.valid=true;
    }
    return cache.segments;
}
float NormalizationMean(CurveCanvasState& state,const std::vector<RawBezierCurvePoint>& points) {
    if(!state.normalizationCached || !SameCurvePoints(state.normalizationSource,points)) {
        state.normalizationSource=points;
        state.normalizationMean=LocalEvCurveOffset(points,1.f);
        state.normalizationCached=true;
    }
    return state.normalizationMean;
}
bool Glyph(int index,bool active,const char* tip,const GraphUI::GraphPlotStyle& style) {
    ImGui::PushID(index);
    const bool hit=ImGui::InvisibleButton("mode",{style.controlHeight,style.controlHeight});
    const auto r=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
    const auto c=r.GetCenter(); auto* d=ImGui::GetWindowDrawList();
    const auto color=ImGui::GetColorU32(ImGuiCol_Text,active?1.f:.55f);
    const auto p=[&](float x,float y){return ImVec2(c.x+x*style.scale,c.y+y*style.scale);};
    if(active || ImGui::IsItemHovered()) d->AddCircle(c,10*style.scale,ImGui::GetColorU32(ImGuiCol_Text,active?.45f:.2f),0,style.scale);
    switch(index) {
    case 0:d->AddLine(p(-6,5),p(6,-5),color,style.scale);d->AddCircle(c,3*style.scale,color,0,style.scale);break;
    case 1:for(float x:{-4.f,4.f}) for(float y:{-4.f,4.f})d->AddCircle(p(x,y),2*style.scale,color,0,style.scale);break;
    case 2:d->AddBezierCubic(p(-7,4),p(-4,-8),p(4,-8),p(7,4),color,1.5f*style.scale);break;
    case 3:d->AddRect(p(-5,-1),p(5,6),color,2*style.scale,0,style.scale);d->AddCircle(p(0,-3),4*style.scale,color,0,style.scale);break;
    case 4:for(float y:{-5.f,0.f,5.f}) {d->AddLine(p(-7,y),p(7,y),color,style.scale);const float x=y==0?3.f:-3.f;d->AddCircleFilled(p(x,y),2.f*style.scale,color);}break;
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",tip);
    ImGui::PopID();return hit;
}
}
bool DrawCurveCanvas(const char* id,std::vector<RawBezierCurvePoint>& points,
    CurveAmountState& amount,CurveCanvasState& s,bool diagonal,
    const RawLabGraphHistogram& histogram,ImVec2 size,int maximumPoints,float* exposure,
    RawLabToneGraphViewRange view,bool amountDriven,float* normalization,bool normalizationDriven,bool normalizationAvailable,
    const CurveTonalSliderDomain* tonalSliders,std::uint64_t histogramRevision) {
    if(points.size()<2) return false;
    const int frame=ImGui::GetFrameCount();
    if(s.lastFrame>=0 && s.lastFrame+1<frame) {
        s.gesture=s.exposureGesture=s.amountGesture=s.normalizationGesture=s.tonalSliderGesture=false;
        s.activeTonalSlider=0;
    }
    s.lastFrame=frame;
    ImGui::PushID(id);
    const float surfaceStartX=ImGui::GetCursorPosX();
    const float surfaceScreenX=ImGui::GetCursorScreenPos().x;
    bool changed=false;
    const auto style=GraphUI::ResolveGraphPlotStyle();
    const float controlsHeight=(normalization ? 2.f : 1.f)*(style.controlHeight+ImGui::GetStyle().ItemSpacing.y);
    size.x=std::max(1.f,size.x);size.y=std::max(12.f*style.scale,size.y-controlsHeight);
    ImGui::InvisibleButton("curve",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
    const float inset=std::min(style.inset,std::min(size.x,size.y)*.2f);
    const ImRect box(ImVec2(ImGui::GetItemRectMin().x+inset,ImGui::GetItemRectMin().y+inset),
        ImVec2(ImGui::GetItemRectMax().x-inset,ImGui::GetItemRectMax().y-inset));
    const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    const bool plotVisible=ImGui::IsItemVisible();
    const bool inputEnabled=!(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled);
    const bool editable=!amount.locked && inputEnabled;
    if(s.tonalSliderGesture && !editable) {
        if(ImGui::GetActiveID()==s.activeTonalSlider)ImGui::ClearActiveID();
        s.tonalSliderGesture=false;s.activeTonalSlider=0;
    }
    const auto mouse=ImGui::GetIO().MousePos;
    const float span=std::max(.0001f,view.maximum-view.minimum);
    // Keep the display offset stable while dragging authored points. At release
    // the new curve mean becomes the normalization reference.
    const float offset=(plotVisible || s.gesture) && normalization && *normalization>0
        ? NormalizationMean(s,s.gesture?s.before:points)*std::clamp(*normalization,0.f,1.f) : 0.f;
    const float yMinimum=normalization ? std::min(0.f,-offset) : view.minimum;
    const float yMaximum=normalization ? std::max(1.f,1.f-offset) : view.maximum;
    const float ySpan=std::max(.0001f,yMaximum-yMinimum);
    const auto screen=[&](float x,float y){return ImVec2(box.Min.x+(x-view.minimum)/span*box.GetWidth(),box.Max.y-(y-offset-yMinimum)/ySpan*box.GetHeight());};
    const auto plotScreen=[&](float x,float y){return ImVec2(box.Min.x+(x-view.minimum)/span*box.GetWidth(),box.Max.y-(y-yMinimum)/ySpan*box.GetHeight());};
    const auto mouseX=[&]{return std::clamp(view.minimum+(mouse.x-box.Min.x)/box.GetWidth()*span,0.f,1.f);};
    const auto mouseY=[&]{return std::clamp(offset+yMinimum+(box.Max.y-mouse.y)/box.GetHeight()*ySpan,0.f,1.f);};
    auto* d=ImGui::GetWindowDrawList();
    const auto white=style.curve;
    const auto muted=style.reference;
    const auto accent=style.selected;
    s.selected.resize(points.size(),false);
    int nearest=-1;float best=16*16.f*style.scale*style.scale;
    for(int i=0;hovered && i<int(points.size());++i) {
        const auto p=screen(points[i].x,points[i].y);const auto delta=ImVec2(mouse.x-p.x,mouse.y-p.y);
        const float dist=delta.x*delta.x+delta.y*delta.y;
        if(dist<best) {nearest=i;best=dist;}
    }
    if(editable && hovered && !ImGui::GetIO().MouseDown[1] && ImGui::IsMouseClicked(0)) {
        if(s.mode==CurveGestureMode::Select && nearest>=0) s.selected[nearest]=!s.selected[nearest];
        else {
            s.gesture=true;s.gestureMoved=false;s.gestureApplied=false;s.press=mouse;s.before=points;s.dragged=nearest;
            s.pivot=mouseX();
            s.exposureGesture=s.mode==CurveGestureMode::Points && nearest<0 && exposure;
            s.exposureBefore=exposure?*exposure:0;
            if(s.mode==CurveGestureMode::Points && nearest>=0) {
                std::fill(s.selected.begin(),s.selected.end(),false);s.selected[nearest]=true;
            }
        }
    }
    if(editable && hovered && s.mode==CurveGestureMode::Points &&
       (ImGui::IsMouseDoubleClicked(0) || (ImGui::IsMouseClicked(1) && !ImGui::IsMouseDown(0)))) {
        if(nearest>0 && nearest+1<int(points.size()) && ImGui::IsMouseClicked(1)) {
            if(IsTonalCurvePoint(points[nearest])) changed|=RemoveTonalCurvePoint(points,nearest);
            else {points.erase(points.begin()+nearest);Smooth(points);changed=true;}
        } else if(nearest<0 && ImGui::IsMouseDoubleClicked(0) && int(points.size())<maximumPoints) {
            RawBezierCurvePoint p{std::clamp(mouseX(),.001f,.999f),
                mouseY(),{1},{1}};
            const auto at=std::lower_bound(points.begin(),points.end(),p.x,[](auto a,float x){return a.x<x;});
            if((at==points.begin() || p.x-(at-1)->x>.001f) && (at==points.end() || at->x-p.x>.001f)) {
                points.insert(at,p);Smooth(points);changed=true;
            }
        }
        if(changed) {s.gesture=false;s.selected.assign(points.size(),false);}
    }
    if(s.gesture && !editable) {
        s.gesture=s.exposureGesture=false;
        if(active)ImGui::ClearActiveID();
    }
    if(s.gesture) {
        const bool cancel=ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
            (ImGui::IsMouseDown(0) && (!active || ImGui::IsMouseDown(1)));
        if(cancel) {
            if(!SameCurvePoints(points,s.before)) {points=s.before;changed=true;}
            if(s.exposureGesture && exposure && *exposure!=s.exposureBefore) {*exposure=s.exposureBefore;changed=true;}
            s.gesture=false;s.exposureGesture=false;
            if(active)ImGui::ClearActiveID();
        } else if(ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0)) {
            const float dx=(mouse.x-s.press.x)/box.GetWidth()*span,dy=(s.press.y-mouse.y)/box.GetHeight()*ySpan;
            s.gestureMoved |= std::hypot(mouse.x-s.press.x,mouse.y-s.press.y)>1.5f;
            if(s.gestureMoved && (!s.gestureApplied || dx!=s.appliedGestureDelta.x ||
                dy!=s.appliedGestureDelta.y || s.radius!=s.appliedGestureRadius)) {
                s.gestureApplied=true;s.appliedGestureDelta={dx,dy};s.appliedGestureRadius=s.radius;
                if(s.exposureGesture && exposure) {
                    const float value=std::clamp(s.exposureBefore+(s.press.y-mouse.y)/120.f,-16.f,16.f);
                    changed |= value!=*exposure;*exposure=value;
                }
                else if(s.mode==CurveGestureMode::Points || s.mode==CurveGestureMode::Select) {
                    auto& next=s.gesturePoints;next=s.before;
                    const bool group=s.mode==CurveGestureMode::Select;
                    float moveX=dx,moveY=dy;
                    const auto selected=[&](int i){return group?s.selected[i]:i==s.dragged;};
                    const auto movesHorizontally=[&](int i) {
                        return i>0 && i+1<int(next.size()) && selected(i) && !IsTonalCurvePoint(s.before[i]);
                    };
                    bool any=false;
                    for(int i=0;i<int(next.size());++i) if(selected(i)) {
                        any=true;const auto& p=s.before[i];
                        moveY=std::clamp(moveY,-p.y,1-p.y);
                        if(movesHorizontally(i)) {
                            if(!movesHorizontally(i-1)) moveX=std::max(moveX,s.before[i-1].x+.001f-p.x);
                            if(!movesHorizontally(i+1)) moveX=std::min(moveX,s.before[i+1].x-.001f-p.x);
                        }
                    }
                    if(any) {
                        for(int i=0;i<int(next.size());++i) if(selected(i)) {
                            if(movesHorizontally(i))next[i].x+=moveX;
                            next[i].y+=moveY;
                        }
                        SmoothMovedPoints(next,s.before);
                    } else if(!group && diagonal && dy!=0.f) {
                        for(auto& p:next)p.y=std::clamp(p.y+dy,0.f,1.f);
                        Smooth(next);
                    }
                    if(!SameCurvePoints(points,next)) {points=next;changed=true;}
                } else {
                    auto& next=s.gesturePoints;next=s.before;
                    // Helpers leave ordinary points, with no second effect after release.
                    for(float x:{std::max(.001f,s.pivot-s.radius),s.pivot,std::min(.999f,s.pivot+s.radius)}) {
                        if(int(next.size())>=maximumPoints) break;
                        if(std::none_of(next.begin(),next.end(),[&](const auto& p){return std::abs(p.x-x)<.015f;})) {
                            const float y=EvaluateRawBezierCurve(s.before,x);
                            next.push_back({x,y,{1},{1}});
                        }
                    }
                    std::sort(next.begin(),next.end(),[](const auto& a,const auto& b){return a.x<b.x;});
                    for(auto& p:next) {
                        const float distance=std::abs(p.x-s.pivot)/s.radius;
                        const float w=distance<1?std::pow(1-distance*distance,2.f):0;
                        p.y+=dy*w;
                        p.y=std::clamp(p.y,0.f,1.f);
                    }
                    for(size_t i=1;i+1<next.size();++i) if(!IsTonalCurvePoint(next[i])) {
                        const float lower=next[i-1].x+.001f,upper=next[i+1].x-.001f;
                        if(lower<=upper)next[i].x=std::clamp(next[i].x,lower,upper);
                    }
                    Smooth(next);
                    if(!SameCurvePoints(points,next)) {points=next;s.selected.assign(points.size(),false);changed=true;}
                }
            }
            if(ImGui::IsMouseReleased(0)) {
                if(!s.exposureGesture && s.gestureMoved && RemoveNeutralTonalCurvePoints(points)) {
                    s.selected.assign(points.size(),false);changed=true;
                }
                s.gesture=false;s.exposureGesture=false;
            }
        } else {s.gesture=false;s.exposureGesture=false;}
    }
    if(hovered && int(s.mode)>=2 && editable) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        s.radius=std::clamp(s.radius+ImGui::GetIO().MouseWheel*.025f,.025f,1.f);
    }
    if(plotVisible) {
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
    GraphUI::DrawGraphReference(*d,plotScreen(0,diagonal?0:.5f),plotScreen(1,diagonal?1:.5f),style);
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
    d->PushClipRect(box.Min,box.Max,true);
    for(int i=0;i<int(points.size());++i) {
        const auto p=screen(points[i].x,points[i].y);const float dist=std::hypot(mouse.x-p.x,mouse.y-p.y);
        if(hovered)GraphUI::DrawGraphPointHalo(*d,p,dist,style);
        GraphUI::DrawGraphPoint(*d,p,style,s.selected[i]);
    }
    d->PopClipRect();
    }
    ImGui::BeginDisabled(amount.locked);
    const char* tips[]={"Edit points. Double-click to add; right-click a point to remove.","Select points with clicks; drag empty space to move the group.","Edit a soft brightness band; wheel changes its width."};
    for(int i=0;i<3;++i) {if(i)ImGui::SameLine(0,style.controlGap);if(Glyph(i,int(s.mode)==i,tips[i],style)) {s.mode=CurveGestureMode(i);s.gesture=false;}}
    ImGui::EndDisabled();
    if(tonalSliders) {
        ImGui::SameLine(0,style.controlGap);
        if(Glyph(4,s.showTonalSliders,s.showTonalSliders ? "Hide tonal sliders." :
            "Show tonal sliders. Slider points keep their brightness positions while moving vertically.",style))
            s.showTonalSliders=!s.showTonalSliders;
    }
    ImGui::SameLine(0,style.controlGap);
    ImGui::BeginDisabled(amountDriven);
    if(Glyph(3,amount.locked,"Lock this curve, then vary its amount. Unlock keeps the current shape.",style)) {
        if(!amount.locked) {amount.reference=points;FreezeHandles(amount.reference);amount.amount=1;amount.locked=true;}
        else {amount={};}
        s.gesture=false;changed=true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0,style.controlGap);ImGui::BeginDisabled(!amount.locked || amountDriven);
    const float dialWidth=std::max(1.f,std::min(100.f*style.scale,ImGui::GetContentRegionAvail().x));
    ImGui::InvisibleButton("amount",{dialWidth,style.controlHeight});
    const auto dial=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
    const bool amountEditable=inputEnabled && amount.locked && !amountDriven;
    if(s.amountGesture && !amountEditable) {
        s.amountGesture=false;
        if(ImGui::IsItemActive())ImGui::ClearActiveID();
    }
    if(amountEditable && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {s.amountGesture=true;s.amountBefore=amount.amount;}
    if(s.amountGesture && (ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
       (ImGui::IsMouseDown(0) && (!ImGui::IsItemActive() || ImGui::IsMouseDown(1))))) {
        const auto restored=AttenuateCurve(amount.reference,s.amountBefore,diagonal);
        changed|=amount.amount!=s.amountBefore || !SameCurvePoints(points,restored);
        amount.amount=s.amountBefore;points=restored;
        s.amountGesture=false;
        if(ImGui::IsItemActive())ImGui::ClearActiveID();
    }
    if(amountEditable && ImGui::IsItemActive() && s.amountGesture && ImGui::IsMouseDragging(0,1.f)) {
        const float value=std::clamp(amount.amount+ImGui::GetIO().MouseDelta.x/dialWidth,0.f,1.f);
        if(value!=amount.amount) {amount.amount=value;points=AttenuateCurve(amount.reference,amount.amount,diagonal);changed=true;}
    }
    if(amountEditable && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        changed|=amount.amount!=1.f || !SameCurvePoints(points,amount.reference);
        amount.amount=1;points=amount.reference;
    }
    if(!ImGui::IsMouseDown(0))s.amountGesture=false;
    GraphUI::DrawGraphDial(*d,dial.Min,dial.Max,amount.amount,amount.locked && !amountDriven,style);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemActive())
        ImGui::SetTooltip("Curve amount: %.0f%%. Drag horizontally; double-click restores 100%%. Lock first. Separate exposure and area controls keep their values.",amount.amount*100);
    ImGui::EndDisabled();
    if(normalization) {
        // Align this second dial directly beneath curve amount and lock.
        const float rowY=ImGui::GetCursorScreenPos().y;
        d->PushClipRect({surfaceScreenX,rowY},{dial.Min.x-style.controlGap,rowY+style.controlHeight},true);
        d->AddText({surfaceScreenX,rowY+(style.controlHeight-ImGui::GetFontSize())*.5f},style.label,"Normalize");
        d->PopClipRect();
        ImGui::SetCursorScreenPos({dial.Min.x,rowY});
        ImGui::BeginDisabled(normalizationDriven);
        ImGui::InvisibleButton("normalization",{dialWidth,style.controlHeight});
        const ImRect normalizationDial(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
        if(s.normalizationGesture && !inputEnabled) {
            s.normalizationGesture=false;
            if(ImGui::IsItemActive())ImGui::ClearActiveID();
        }
        if(inputEnabled && !normalizationDriven && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            s.normalizationGesture=true;s.normalizationBefore=*normalization;
        }
        if(s.normalizationGesture && (normalizationDriven || ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
            (ImGui::IsMouseDown(0) && (!ImGui::IsItemActive() || ImGui::IsMouseDown(1))))) {
            if(!normalizationDriven){changed|=*normalization!=s.normalizationBefore;*normalization=s.normalizationBefore;}
            s.normalizationGesture=false;
            if(ImGui::IsItemActive())ImGui::ClearActiveID();
        }
        if(inputEnabled && !normalizationDriven && s.normalizationGesture && ImGui::IsItemActive() && ImGui::IsMouseDragging(0,1.f)) {
            const float value=std::clamp(*normalization+ImGui::GetIO().MouseDelta.x/dialWidth,0.f,1.f);
            changed|=value!=*normalization;*normalization=value;
        }
        if(inputEnabled && !normalizationDriven && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            changed|=*normalization!=0.f;*normalization=0.f;
        }
        if(!ImGui::IsMouseDown(0))s.normalizationGesture=false;
        GraphUI::DrawGraphDial(*d,normalizationDial.Min,normalizationDial.Max,*normalization,!normalizationDriven,style);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemActive()) {
            if(normalizationDriven && !normalizationAvailable)
                ImGui::SetTooltip("Waiting for the connected normalization value from Graph.");
            else ImGui::SetTooltip("Normalization: %.0f%%. Global offset: %+.3f EV. Move the curve's mean into a uniform offset without changing the total correction. Double-click restores 0%%.%s",
                *normalization*100.f,8.f*NormalizationMean(s,points)*std::clamp(*normalization,0.f,1.f),normalizationDriven?" Driven from Graph.":"");
        }
        ImGui::EndDisabled();
        ImGui::SetCursorPosX(surfaceStartX);
    }
    if(tonalSliders && s.showTonalSliders)
        changed|=DrawCurveTonalSliders(points,s,*tonalSliders,maximumPoints,!editable);
    else if(s.tonalSliderGesture) {
        if(editable && !SameCurvePoints(points,s.beforeTonalSliders)) {points=s.beforeTonalSliders;changed=true;}
        if(ImGui::GetActiveID()==s.activeTonalSlider)ImGui::ClearActiveID();
        s.tonalSliderGesture=false;s.activeTonalSlider=0;
    }
    ImGui::PopID();return changed;
}
}
