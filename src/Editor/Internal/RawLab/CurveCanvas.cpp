#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Internal/RawLab/CurvePointInsertion.h"
#include "Editor/Internal/RawLab/CurveCanvasPlot.h"
#include "Editor/Internal/RawLab/CurveCanvasNavigation.h"
#include "App/ControlMotionAdapter.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Raw/Curves/LocalEvCurveNormalization.h"
#include "Raw/Curves/TonalCurvePoints.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include "Utils/GlassControls.h"
#include "Utils/ImGuiExtras.h"
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
float NormalizationMean(CurveCanvasState& state,const std::vector<RawBezierCurvePoint>& points) {
    if(!state.normalizationCached || !SameCurvePoints(state.normalizationSource,points)) {
        state.normalizationSource=points;
        state.normalizationMean=LocalEvCurveOffset(points,1.f);
        state.normalizationCached=true;
    }
    return state.normalizationMean;
}

}
bool DrawCurveCanvas(const char* id,std::vector<RawBezierCurvePoint>& points,
    CurveAmountState& amount,CurveCanvasState& s,bool diagonal,
    const RawLabGraphHistogram& histogram,ImVec2 size,int maximumPoints,float* exposure,
    RawLabToneGraphViewRange view,bool amountDriven,float* normalization,bool normalizationDriven,bool normalizationAvailable,
    const CurveTonalSliderDomain* tonalSliders,std::uint64_t histogramRevision,CurveCanvasViewOptions viewOptions) {
    if(points.size()<2) return false;
    const int frame=ImGui::GetFrameCount();
    if(s.lastFrame>=0 && s.lastFrame+1<frame) {
        s.gesture=s.exposureGesture=s.amountGesture=s.normalizationGesture=s.tonalSliderGesture=false;
        s.activeTonalSlider=0;
        s.viewPanning=false;
    }
    s.lastFrame=frame;
    ImGui::PushID(id);
    const float surfaceStartX=ImGui::GetCursorPosX();
    const float surfaceScreenX=ImGui::GetCursorScreenPos().x;
    bool changed=false;
    const auto style=GraphUI::ResolveGraphPlotStyle();
    const float axisHeight=viewOptions.stableScale ? ImGui::GetFontSize()+style.controlGap : 0;
    const float controlsHeight=style.controlHeight+ImGui::GetStyle().ItemSpacing.y;
    size.x=std::max(1.f,size.x);size.y=std::max(12.f*style.scale,size.y-controlsHeight);
    ImGui::InvisibleButton("curve",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|
        (viewOptions.stableScale?ImGuiButtonFlags_MouseButtonMiddle:0));
    const float inset=std::min(style.inset,std::min(size.x,size.y)*.2f);
    const ImRect box(ImVec2(ImGui::GetItemRectMin().x+inset,ImGui::GetItemRectMin().y+inset),
        ImVec2(ImGui::GetItemRectMax().x-inset,ImGui::GetItemRectMax().y-inset-std::min(axisHeight,size.y*.25f)));
    const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    const bool plotVisible=ImGui::IsItemVisible();
    const auto plotInput=UiMotion::LastItemInput();
    const bool inputEnabled=!(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled);
    const bool editable=!amount.locked && inputEnabled;
    if(s.tonalSliderGesture && !editable) {
        if(ImGui::GetActiveID()==s.activeTonalSlider)ImGui::ClearActiveID();
        s.tonalSliderGesture=false;s.activeTonalSlider=0;
    }
    const auto mouse=ImGui::GetIO().MousePos;
    const bool interacting=s.gesture || s.amountGesture || s.tonalSliderGesture ||
        ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0);
    auto xAxis=ResolveCurveCanvasAxis(s.framing,viewOptions,view);
    if(viewOptions.stableScale) {
        if(plotVisible && !interacting && !ImGui::IsMouseDown(2) && !s.viewPanning) {
            if(s.framing.request==CurveCanvasFraming::Request::Full) {
                SetCurveCanvasView(s.framing,{},{});s.framing.fitPending=false;
            } else if(s.framing.request==CurveCanvasFraming::Request::Fit) {
                s.framing.fitPending=true;s.framing.fitAttempted=false;
            }
            s.framing.request=CurveCanvasFraming::Request::None;
            FitCurveCanvasView(s.framing,histogram,histogramRevision,points,diagonal);
        }
        NavigateCurveCanvas(s,box,hovered,active,inputEnabled && plotVisible,interacting,diagonal);
        xAxis=s.framing.axis;
    }
    // All gestures share one fixed view, including amount and tonal sliders.
    // Release/cancel completes in those same coordinates.
    if(normalization && (plotVisible || s.gesture) && (!s.viewOffsetReady || !interacting)) {
        s.viewOffset=NormalizationMean(s,points);s.viewOffsetReady=true;
    }
    const float offset=normalization?s.viewOffset:0.f;
    const float yMinimum=normalization ? std::min(0.f,-offset) : view.minimum;
    const float yMaximum=normalization ? std::max(1.f,1.f-offset) : view.maximum;
    const CurveAxisMapping yAxis=diagonal && !normalization ? (viewOptions.stableScale?s.framing.outputAxis:xAxis) :
        CurveAxisMapping{yMinimum,yMaximum,false};
    const auto screen=[&](float x,float y){return ImVec2(box.Min.x+xAxis.ToView(x)*box.GetWidth(),box.Max.y-yAxis.ToView(y-offset)*box.GetHeight());};
    const auto mouseX=[&]{return std::clamp(xAxis.FromView((mouse.x-box.Min.x)/box.GetWidth()),0.f,1.f);};
    const auto mouseY=[&]{return std::clamp(offset+yAxis.FromView((box.Max.y-mouse.y)/box.GetHeight()),0.f,1.f);};
    auto* d=ImGui::GetWindowDrawList();
    s.selected.resize(points.size(),false);
    int nearest=-1;float best=16*16.f*style.scale*style.scale;
    for(int i=0;hovered && i<int(points.size());++i) {
        const auto p=screen(points[i].x,points[i].y);const auto delta=ImVec2(mouse.x-p.x,mouse.y-p.y);
        if(p.x<box.Min.x || p.x>box.Max.x || p.y<box.Min.y || p.y>box.Max.y)continue;
        const float dist=delta.x*delta.x+delta.y*delta.y;
        if(dist<best) {nearest=i;best=dist;}
    }
    if(editable && hovered && !ImGui::GetIO().MouseDown[1] && !ImGui::IsMouseDown(2) && ImGui::IsMouseClicked(0)) {
        if(s.mode==CurveGestureMode::Select && nearest>=0) s.selected[nearest]=!s.selected[nearest];
        else {
            s.gesture=true;s.gestureMoved=false;s.gestureApplied=false;s.insertionDrag=false;s.press=mouse;s.before=points;s.dragged=nearest;
            s.pivot=mouseX();
            s.exposureGesture=s.mode==CurveGestureMode::Points && nearest<0 && exposure;
            s.exposureBefore=exposure?*exposure:0;
            if(s.mode==CurveGestureMode::Points && nearest>=0) {
                std::fill(s.selected.begin(),s.selected.end(),false);s.selected[nearest]=true;
            }
        }
    }
    if(editable && hovered && !ImGui::IsMouseDown(2) && s.mode==CurveGestureMode::Points &&
       (ImGui::IsMouseDoubleClicked(0) || (ImGui::IsMouseClicked(1) && !ImGui::IsMouseDown(0)))) {
        if(nearest>0 && nearest+1<int(points.size()) && ImGui::IsMouseClicked(1)) {
            if(IsTonalCurvePoint(points[nearest])) changed|=RemoveTonalCurvePoint(points,nearest);
            else {points.erase(points.begin()+nearest);Smooth(points);changed=true;}
        } else if(nearest<0 && ImGui::IsMouseDoubleClicked(0) && int(points.size())<maximumPoints) {
            RawBezierCurvePoint p{std::clamp(mouseX(),.001f,.999f),
                mouseY(),{1},{1}};
            const auto at=std::lower_bound(points.begin(),points.end(),p.x,[](auto a,float x){return a.x<x;});
            if((at==points.begin() || p.x-(at-1)->x>.001f) && (at==points.end() || at->x-p.x>.001f)) {
                const int inserted=int(at-points.begin());
                InsertCurvePointOnLine(points,p.x,BuildRawBezierSegments(points));changed=true;
                s.gesture=true;s.exposureGesture=false;s.gestureMoved=false;s.gestureApplied=false;
                s.press=mouse;s.before=points;s.dragged=inserted;s.insertionDrag=true;
                s.selected.assign(points.size(),false);s.selected[inserted]=true;
            }
        }
        if(changed && !ImGui::IsMouseDoubleClicked(0)) {s.gesture=false;s.selected.assign(points.size(),false);}
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
            // Invert the same mapping used by the plot. Anchor a selected point
            // at its original position so an off-center grab cannot jump it.
            const float anchorX=s.dragged>=0?s.before[s.dragged].x:
                xAxis.FromView((s.press.x-box.Min.x)/box.GetWidth());
            const float anchorY=s.dragged>=0?s.before[s.dragged].y:
                offset+yAxis.FromView((box.Max.y-s.press.y)/box.GetHeight());
            const float dx=xAxis.FromView(xAxis.ToView(anchorX)+.15f*(mouse.x-s.press.x)/box.GetWidth())-anchorX;
            const float dy=offset+yAxis.FromView(yAxis.ToView(anchorY-offset)+.15f*(s.press.y-mouse.y)/box.GetHeight())-anchorY;
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
                    const float pivotView=xAxis.ToView(s.pivot);
                    for(float x:{std::max(.001f,xAxis.FromView(pivotView-s.radius)),s.pivot,
                        std::min(.999f,xAxis.FromView(pivotView+s.radius))}) {
                        if(int(next.size())>=maximumPoints) break;
                        if(std::none_of(next.begin(),next.end(),[&](const auto& p){return std::abs(p.x-x)<.015f;})) {
                            const float y=EvaluateRawBezierCurve(s.before,x);
                            next.push_back({x,y,{1},{1}});
                        }
                    }
                    std::sort(next.begin(),next.end(),[](const auto& a,const auto& b){return a.x<b.x;});
                    for(auto& p:next) {
                        const float distance=std::abs(xAxis.ToView(p.x)-pivotView)/s.radius;
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
    if(s.gesture && editable && ImGui::IsMouseDown(0) && !ImGui::GetIO().AppFocusLost) {
        ImGuiExtras::CursorCaptureRequest capture;
        capture.mode=ImGuiExtras::CursorCaptureMode::LockedPan;
        capture.anchorScreenPos=s.press;
        capture.restoreScreenPos=s.press;
        ImGuiExtras::SubmitCursorCaptureRequest(capture);
    }
    if(hovered && int(s.mode)>=2 && editable && (!viewOptions.stableScale || !ImGui::GetIO().KeyCtrl)) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        s.radius=std::clamp(s.radius+ImGui::GetIO().MouseWheel*.025f,.025f,1.f);
    }
    ImGui::BeginDisabled(amount.locked);
    int selectedMode=int(s.mode);
    if(DrawCurveModeSelector(selectedMode,s.modeSelector,style)) {
        s.mode=CurveGestureMode(selectedMode);s.gesture=false;
    }
    ImGui::EndDisabled();
    if(tonalSliders) {
        ImGui::SameLine(0,style.controlGap);
        if(DrawCurveControlGlyph(4,s.showTonalSliders,s.showTonalSliders ? "Hide tonal sliders." :
            "Show tonal sliders. Slider points keep their brightness positions while moving vertically.",style))
            s.showTonalSliders=!s.showTonalSliders;
    }
    if(viewOptions.stableScale) {
        ImGui::SameLine(0,style.controlGap);
        DrawCurveCanvasViewControl(s.framing,style);
    }
    ImGui::SameLine(0,style.controlGap);
    ImGui::BeginDisabled(amountDriven);
    if(DrawCurveControlGlyph(3,amount.locked,"Lock this curve, then vary its amount. Unlock keeps the current shape.",style)) {
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
    s.normalizationGesture=false;
    if(tonalSliders && s.showTonalSliders)
        changed|=DrawCurveTonalSliders(points,s,*tonalSliders,maximumPoints,!editable);
    else if(s.tonalSliderGesture) {
        if(editable && !SameCurvePoints(points,s.beforeTonalSliders)) {points=s.beforeTonalSliders;changed=true;}
        if(ImGui::GetActiveID()==s.activeTonalSlider)ImGui::ClearActiveID();
        s.tonalSliderGesture=false;s.activeTonalSlider=0;
    }
    if(plotVisible) {
        DrawCurveCanvasPlot({box,style,xAxis,yAxis,viewOptions,plotInput,mouse,offset,
            diagonal,amountDriven,hovered,nearest,changed},points,amount,s,histogram,histogramRevision);
    }
    ImGui::PopID();return changed;
}
}
