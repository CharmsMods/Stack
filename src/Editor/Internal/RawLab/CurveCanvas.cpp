#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Internal/RawLab/CurveCanvasPlot.h"
#include "App/ControlMotionAdapter.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Raw/Curves/LocalEvCurveNormalization.h"
#include "Raw/Curves/TonalCurvePoints.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include "Utils/GlassControls.h"
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
    const auto plotInput=UiMotion::LastItemInput();
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
    const auto mouseX=[&]{return std::clamp(view.minimum+(mouse.x-box.Min.x)/box.GetWidth()*span,0.f,1.f);};
    const auto mouseY=[&]{return std::clamp(offset+yMinimum+(box.Max.y-mouse.y)/box.GetHeight()*ySpan,0.f,1.f);};
    auto* d=ImGui::GetWindowDrawList();
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
    if(plotVisible) {
        // Authoring is complete, including tonal sliders, amount and cancel.
        // Recompute only the normalization mapping affected by this edit.
        const float displayOffset=changed && normalization
            ? (*normalization>0?NormalizationMean(s,s.gesture?s.before:points)*std::clamp(*normalization,0.f,1.f):0.f)
            : offset;
        const float displayMin=normalization?std::min(0.f,-displayOffset):view.minimum;
        const float displayMax=normalization?std::max(1.f,1.f-displayOffset):view.maximum;
        DrawCurveCanvasPlot({box,style,view,plotInput,mouse,displayOffset,displayMin,displayMax,
            diagonal,amountDriven,hovered,nearest,changed},points,amount,s,histogram,histogramRevision);
    }
    ImGui::PopID();return changed;
}
}
