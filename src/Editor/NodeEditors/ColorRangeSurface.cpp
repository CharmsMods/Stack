#include "Editor/NodeEditors/ColorGradeHover.h"
#include "Editor/NodeEditors/HslSelectionEditor.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Notifications/NotificationPresenter.h"
#include <cmath>
#include <cstdio>

namespace Stack::Editor {
using namespace RawRecipe;
namespace {
constexpr float radians=3.14159265359f/180.f;
constexpr float tintRadius=.26f;
constexpr int sectors=72;

struct WheelFeedback {
    enum class Handle { Result, Source, Body, Hue, Chroma };
    float hueOffset=0, radiusOffset=0;
    Handle handle=Handle::Result;
    std::array<ImVec2,49> arc{};
    std::array<float,49> hueWeight{};
    float arcHue=999,arcWidth=-1,arcSoftness=-1;
    Animation::ScalarChannel emphasis{0};
    void EndInteraction() { emphasis.Reset(0); }
};
struct Swatch {ImVec2 unit; ImU32 color;};
const auto& Palette() {
    static const auto palette=[] {
        std::array<Swatch,sectors+1> swatches{};
        for(int i=0;i<=sectors;++i) {
            const float angle=(ColorWarpHslAnchorHues()[0]+360.f*i/sectors)*radians;
            const float x=std::cos(angle),y=std::sin(angle);
            const auto rgb=ColorWarpCoordinateToWorkingRgb({.73f,.16f*x,.16f*y,0},Raw::RawWorkingSpace::LinearSrgbD65);
            const auto display=[](float v){v=std::clamp(v,0.f,1.f);return v<=.0031308f?12.92f*v:1.055f*std::pow(v,1.f/2.4f)-.055f;};
            swatches[i]={{x,-y},ImGui::ColorConvertFloat4ToU32({display(rgb[0]),display(rgb[1]),display(rgb[2]),1})};
        }
        return swatches;
    }();
    return palette;
}
HslSelection* FindRange(RawColorWarpRecipe& recipe,const std::string& id) {
    for(auto& s:recipe.hslSelections)if(s.id==id)return &s;
    return nullptr;
}
HslSelection* AddRange(RawColorWarpRecipe& recipe,HslSelectionEditorState& state,bool grade) {
    if(recipe.hslSelections.size()>=kMaxHslSelections)return nullptr;
    int n=1;
    while(FindRange(recipe,"range-"+std::to_string(n)))++n;
    HslSelection s;s.id="range-"+std::to_string(n);s.name=(grade?"Range ":"Pick ")+std::to_string(n);s.allColors=grade;s.independentChannels=true;
    recipe.hslSelections.push_back(s);recipe.version=6;
    state.EndInteraction();state.selected=s.id;
    return &recipe.hslSelections.back();
}
float HueFraction(float hue) {
    float t=(hue-ColorWarpHslAnchorHues()[0])/360.f;
    return t-std::floor(t);
}
struct WheelFamilySector {
    ImVec2 handle;
    std::array<ImVec2,13> arc;
    ImU32 color;
};

const auto& WheelFamilySectors() {
    static const auto sectorsByFamily=[] {
        std::array<WheelFamilySector,8> result{};
        const auto& hues=ColorWarpHslAnchorHues();
        const auto unit=[](float hue) {
            return ImVec2(std::cos(hue*radians),-std::sin(hue*radians));
        };
        for(size_t i=0;i<hues.size();++i) {
            // Adjacent slider weights cross at half the distance between their peaks.
            const float previous=ColorWarpHueDelta(hues[i]-hues[(i+7)%8]);
            const float next=ColorWarpHueDelta(hues[(i+1)%8]-hues[i]);
            auto& sector=result[i];
            sector.handle=unit(hues[i]);
            sector.color=Palette()[std::lround(HueFraction(hues[i])*sectors)%sectors].color;
            for(size_t j=0;j<sector.arc.size();++j) {
                const float t=float(j)/float(sector.arc.size()-1);
                sector.arc[j]=unit(hues[i]+(t*(previous+next)-previous)*.5f);
            }
        }
        return result;
    }();
    return sectorsByFamily;
}
bool DrawMap(RawColorWarpRecipe& recipe,HslSelectionEditorState& state,bool grade,int activeFamily,float width,ColorGradeHover* probe) {
    const auto origin=ImGui::GetCursorScreenPos();
    const float font=ImGui::GetFontSize();
    const float height=font*5.6f,neutralY=origin.y+height+font*.68f;
    ImGui::InvisibleButton("Source brightness and hue",{width,height+font*1.35f});
    if(!ImGui::IsItemVisible() && !ImGui::IsItemActive())return false;
    const bool activated=ImGui::IsItemActivated();
    const bool hovered=ImGui::IsItemHovered();
    const auto mouse=ImGui::GetIO().MousePos;
    const float span=state.viewHigh-state.viewLow;
    const auto x=[&](float ev){return origin.x+(ev-state.viewLow)/span*width;};
    const auto ev=std::clamp(state.viewLow+(mouse.x-origin.x)/width*span,state.viewLow,state.viewHigh);
    const float hue=ColorWarpHueDelta(ColorWarpHslAnchorHues()[0]+360*(mouse.y-origin.y)/height);
    auto* draw=ImGui::GetWindowDrawList();
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
    draw->PushClipRect(origin,{origin.x+width,neutralY+font*.72f},true);
    draw->AddRectFilled(origin,{origin.x+width,neutralY+font*.64f},IM_COL32(30,31,33,255),font*.18f);
    for(int i=0;i<12;++i) {
        const auto color=Palette()[i*6].color,next=Palette()[(i+1)*6].color;
        draw->AddRectFilledMultiColor({origin.x,origin.y+height*i/12},{origin.x+width,origin.y+height*(i+1)/12},
            (color&0xffffff)|0x22000000,(color&0xffffff)|0x70000000,(next&0xffffff)|0x70000000,(next&0xffffff)|0x22000000);
    }
    draw->AddText({origin.x+font*.35f,neutralY-font*.5f},ImGui::GetColorU32(ImGuiCol_Text,.6f),"Neutral");
    if(!grade && state.selected.empty()) {
        const int family=std::clamp(activeFamily,0,7);
        const auto& anchor=ColorWarpHslAnchors()[family];
        const float center=HueFraction(ColorWarpHslAnchorHues()[family]);
        for(int wrap=-1;wrap<=1;++wrap) {
            const float low=std::max(0.f,center+wrap-anchor.hueRange.featherLow/360);
            const float high=std::min(1.f,center+wrap+anchor.hueRange.featherHigh/360);
            if(low>=high)continue;
            draw->AddRect({origin.x,origin.y+height*low},{origin.x+width,origin.y+height*high},ImGui::GetColorU32(ImGuiCol_Text,.32f));
        }
    }
    draw->AddLine({x(0),origin.y},{x(0),origin.y+height},ImGui::GetColorU32(ImGuiCol_Text,.25f));
    HslSelection* hit=nullptr;int hitBand=-1,handle=-1;float best=font*.85f;
    for(auto& s:recipe.hslSelections) {
        const bool selected=s.id==state.selected;
        const float cy=s.neutralOnly?neutralY:origin.y+(s.allColors?.5f:HueFraction(s.hue))*height;
        const float half=s.neutralOnly?font*.45f:s.allColors?height*.5f:s.hueWidth/360*height;
        for(int i=0;i<std::max(1,int(s.bands.size()));++i) {
            const float left=s.bands.empty()?origin.x:x(s.bands[i].low),right=s.bands.empty()?origin.x+width:x(s.bands[i].high);
            const float cx=(left+right)*.5f;
            for(int wrap=-1;wrap<=1;++wrap) {
                if(wrap && (s.neutralOnly || s.allColors))continue;
                const float y=cy+height*wrap,top=std::max(origin.y,y-half),bottom=std::min(s.neutralOnly?neutralY+font*.5f:origin.y+height,y+half);
                if(top>bottom)continue;
                draw->AddRectFilled({left,top},{right,bottom},ImGui::GetColorU32(ImGuiCol_Text,selected?.13f:.05f));
                draw->AddRect({left,top},{right,bottom},ImGui::GetColorU32(ImGuiCol_Text,selected?.78f:.32f));
                if(selected && !s.neutralOnly && !s.bands.empty()) {
                    const auto& band=s.bands[i];
                    const float dy=s.allColors?0:s.hueSoftness*height/360;
                    draw->AddRect({x(band.low-band.softness),std::max(origin.y,y-half-dy)},
                                  {x(band.high+band.softness),std::min(origin.y+height,y+half+dy)},
                                  ImGui::GetColorU32(ImGuiCol_Text,.24f));
                }
                if(wrap)continue;
                draw->AddCircleFilled({cx,y},selected?4.f:3.f,ink);
                const ImVec2 anchors[]={{cx,y},{left,y},{right,y},{cx,top},{cx,bottom}};
                for(int h=0;h<5;++h) {
                    if(h && (!selected || (h>=3 && (s.neutralOnly || s.allColors))))continue;
                    const auto p=anchors[h];if(h)draw->AddCircleFilled(p,2,ink);
                    if(!activated && !hovered)continue;
                    const float distance=std::hypot(p.x-mouse.x,p.y-mouse.y);
                    if(distance<best){best=distance;hit=&s;hitBand=i;handle=h;}
                }
            }
        }
    }
    if(probe) {
        probe->mapFrame=ImGui::GetFrameCount();
        probe->overMap=hovered && !ImGui::IsAnyMouseDown();
        probe->mapRect={origin,{origin.x+width,neutralY+font*.68f}};
        probe->mapRect.ClipWith(ImRect(draw->GetClipRectMin(),draw->GetClipRectMax()));
        probe->query={ev,hue,mouse.y>=origin.y+height};
        if(!hovered && probe->photoFrame>=probe->mapFrame-1) {
            const auto& sample=probe->photo;
            const ImVec2 marker{std::clamp(x(sample.ev),origin.x,origin.x+width),
                sample.neutral?neutralY:origin.y+height*HueFraction(sample.hue)};
            draw->AddCircle(marker,font*.36f,IM_COL32(16,16,16,255),20,3.5f);
            draw->AddCircle(marker,font*.36f,ink,20,1.6f);
        }
    }
    draw->PopClipRect();
    bool changed=false;
    if(hovered && hit) {
        ImGui::SetMouseCursor(handle==0?ImGuiMouseCursor_ResizeAll:handle<=2?ImGuiMouseCursor_ResizeEW:ImGuiMouseCursor_ResizeNS);
    }
    if(activated) {
        state.EndGesture();state.graphHandle=-1;
        if(!hit) {
            hit=AddRange(recipe,state,grade);
            if(hit) {
                hit->neutralOnly=mouse.y>origin.y+height;hit->allColors=grade && !ImGui::GetIO().KeyShift && !hit->neutralOnly;
                hit->hue=hue;hit->bands={{ev-.55f,ev+.55f,.65f}};hitBand=0;handle=0;changed=true;
            }
        }
        if(hit){state.selected=hit->id;state.graphBand=hitBand;state.graphHandle=handle;}
    }
    if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0) && state.graphHandle>=0) {
        if(auto* s=FindRange(recipe,state.selected)) {
            const auto delta=ImGui::GetIO().MouseDelta;
            if(delta.x!=0 || delta.y!=0) {
                const int h=state.graphHandle;
                if(s->bands.empty() && (h==1 || h==2))s->bands.push_back({state.viewLow,state.viewHigh,.5f});
                if(state.graphBand>=0 && state.graphBand<int(s->bands.size())) {
                    auto& band=s->bands[state.graphBand];
                    if(h==0){const float dx=std::clamp(delta.x/width*span,-32-band.low,32-band.high);band.low+=dx;band.high+=dx;}
                    if(h==1)band.low=std::clamp(ev,-32.f,band.high);
                    if(h==2)band.high=std::clamp(ev,band.low,32.f);
                }
                if(h==0 && !s->allColors && !s->neutralOnly)s->hue=ColorWarpHueDelta(s->hue+delta.y/height*360);
                if(h>=3)s->hueWidth=std::abs(ColorWarpHueDelta(hue-s->hue));
                changed=true;
            }
        }
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))ImGui::SetTooltip(grade
        ?"Click for all hues at this brightness. Shift-click for a hue range. Drag handles to edit."
        :"Click to add a hue and brightness range. Drag its center to move it or its edges to resize.");
    return changed;
}

bool Wheel(float source,float& rotation,float& sat,bool grade,std::array<float,3>* tint,int& family,bool& pickedFamily,float width,float maxSize,WheelFeedback& feedback,HslSelection*& pick,bool& selectionChanged,RawColorWarpRecipe& recipe,HslSelectionEditorState& state) {
    const float size=std::max(1.f,std::min(width,maxSize));
    const float radius=size*.40f;
    const auto origin=ImGui::GetCursorScreenPos();const ImVec2 center{origin.x+width*.5f,origin.y+size*.5f};
    ImGui::InvisibleButton("Color wheel",{width,size});
    if(!ImGui::IsItemVisible() && !ImGui::IsItemActive())return false;
    auto* draw=ImGui::GetWindowDrawList();const auto& palette=Palette();
    const auto at=[&](ImVec2 direction,float r){return ImVec2(center.x+direction.x*r,center.y+direction.y*r);};
    const auto pos=[&](float h,float r){return at({std::cos(h*radians),-std::sin(h*radians)},r);};
    constexpr float fadeStart = .96f;
    const float innerRadius = radius * fadeStart;
    draw->PrimReserve(sectors * 9, sectors * 9);
    const auto uv=ImGui::GetFontTexUvWhitePixel();
    const auto neutral=ImGui::GetColorU32(IM_COL32(128,128,128,255));
    const auto innerColor = [fadeStart](ImU32 color) {
        auto rgba = ImGui::ColorConvertU32ToFloat4(color);
        constexpr float gray = 128.f / 255.f;
        rgba.x = gray + (rgba.x - gray) * fadeStart;
        rgba.y = gray + (rgba.y - gray) * fadeStart;
        rgba.z = gray + (rgba.z - gray) * fadeStart;
        return ImGui::GetColorU32(rgba);
    };
    for(int i=0;i<sectors;++i) {
        const auto& a=palette[i];const auto& b=palette[i+1];
        const auto innerA = at(a.unit, innerRadius), innerB = at(b.unit, innerRadius);
        const auto outerA = at(a.unit, radius), outerB = at(b.unit, radius);
        const auto colorA = innerColor(a.color), colorB = innerColor(b.color);
        const auto clearA = a.color & ~IM_COL32_A_MASK, clearB = b.color & ~IM_COL32_A_MASK;
        draw->PrimVtx(center, uv, neutral);
        draw->PrimVtx(innerB, uv, colorB);
        draw->PrimVtx(innerA, uv, colorA);
        draw->PrimVtx(innerA, uv, colorA);
        draw->PrimVtx(innerB, uv, colorB);
        draw->PrimVtx(outerB, uv, clearB);
        draw->PrimVtx(innerA, uv, colorA);
        draw->PrimVtx(outerB, uv, clearB);
        draw->PrimVtx(outerA, uv, clearA);
    }
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 mouse=ImGui::GetIO().MousePos;
    const float dx=(mouse.x-center.x)/radius,dy=(center.y-mouse.y)/radius;
    const float distance=std::hypot(dx,dy),direction=ColorWarpHue(dx,dy);
    if(!grade) {
        int hoveredFamily=-1;
        if(ImGui::IsItemHovered() && distance>1.f && distance<1.25f) {
            float nearest=360.f;
            for(int i=0;i<8;++i) {
                const float delta=std::abs(ColorWarpHueDelta(direction-ColorWarpHslAnchorHues()[i]));
                if(delta<nearest){nearest=delta;hoveredFamily=i;}
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if(ImGui::IsItemActivated()){family=hoveredFamily;pickedFamily=true;}
        }
        const auto& families=WheelFamilySectors();
        for(int i=0;i<8;++i) {
            const auto& sector=families[i];
            const auto color=ImGui::GetColorU32(sector.color);
            for(auto unit:sector.arc)draw->PathLineTo(at(unit,radius*1.065f));
            draw->PathStroke(color,i==family?3.f:1.5f);
            const auto handle=at(sector.handle,radius*1.14f);
            const float dot=size*.031f;
            draw->AddCircleFilled(handle,dot+1.f,ImGui::GetColorU32(IM_COL32(26,26,26,255)),16);
            draw->AddCircleFilled(handle,dot,color,16);
            if(i==family || i==hoveredFamily)
                draw->AddCircle(handle,dot+1.5f,ImGui::GetColorU32(ImGuiCol_Text,i==family?1.f:.6f),20,1.3f);
        }
    }
    const bool wedge=!grade && pick && pick->sourceChroma>=0;
    // A monotonic radius keeps saturated and scene-linear outliers on the wheel.
    const auto radial=[](float chroma){return chroma/(.28f+chroma);};
    const auto chromaAt=[](float r){return .28f*r/std::max(.001f,1.f-r);};
    float sourceChroma=wedge?pick->sourceChroma:.28f;
    auto sourceDot=pos(source,radius*radial(sourceChroma));
    auto resultDot=pos(source+rotation,radius*radial(sourceChroma*(1+sat)));
    const auto press=ImGui::GetIO().MouseClickedPos[0];
    const bool inside=std::hypot(press.x-center.x,press.y-center.y)<=radius;
    const auto separation=[&](ImVec2 p){return std::hypot(p.x-mouse.x,p.y-mouse.y);};
    const float grip=ImGui::GetFontSize()*.34f;
    if(ImGui::IsItemActivated())feedback.handle=WheelFeedback::Handle::Result;
    const auto contains=[&](const HslSelection& item) {
        return std::abs(ColorWarpHueDelta(direction-item.hue))<=item.hueWidth &&
            std::abs(chromaAt(std::min(distance,1.f))-item.sourceChroma)<=item.chromaReach;
    };
    const auto grabBody=[&](const HslSelection& item) {
        feedback.handle=WheelFeedback::Handle::Body;
        feedback.hueOffset=ColorWarpHueDelta(item.hue-direction);
        feedback.radiusOffset=radial(item.sourceChroma)-distance;
    };
    HslSelection* otherHit=nullptr;
    if(!grade) for(auto& item:recipe.hslSelections) {
        if(&item==pick || item.sourceChroma<0 || !item.enabled)continue;
        const float inner=radius*radial(std::max(0.f,item.sourceChroma-item.chromaReach));
        const float outer=radius*radial(item.sourceChroma+item.chromaReach);
        const float start=(-item.hue-item.hueWidth)*radians,end=(-item.hue+item.hueWidth)*radians;
        draw->PathArcTo(center,outer,start,end);
        draw->PathArcTo(center,inner,end,start);
        draw->PathStroke(ImGui::GetColorU32(ImGuiCol_Text,.46f),1.f,ImDrawFlags_Closed);
        if(ImGui::IsItemActivated() && inside && contains(item))otherHit=&item;
    }
    bool hitSelected=false;
    if(wedge) {
        if(feedback.arcHue!=source || feedback.arcWidth!=pick->hueWidth || feedback.arcSoftness!=pick->hueSoftness) {
            feedback.arcHue=source;feedback.arcWidth=pick->hueWidth;feedback.arcSoftness=pick->hueSoftness;
            for(size_t i=0;i<feedback.arc.size();++i) {
                const float angle=(source-pick->hueWidth+2*pick->hueWidth*i/(feedback.arc.size()-1))*radians;
                feedback.arc[i]={std::cos(angle),-std::sin(angle)};
                const float fromCenter=std::abs(2.f*float(i)/float(feedback.arc.size()-1)-1.f);
                const float softness=std::min(pick->hueWidth,pick->hueSoftness);
                feedback.hueWeight[i]=softness>0?HslSmooth((1-fromCenter)*pick->hueWidth/softness):1.f;
            }
        }
        const float inner=radius*radial(std::max(0.f,sourceChroma-pick->chromaReach));
        const float outer=radius*radial(sourceChroma+pick->chromaReach);
        const auto edge=ImGui::GetColorU32(ImGuiCol_Text,.73f);
        std::array<ImVec2,49> innerEdge,outerEdge;
        const float feather=std::min(.035f,pick->chromaReach);
        const float core=pick->chromaReach-feather;
        const std::array<float,4> rings{inner,radius*radial(std::max(0.f,sourceChroma-core)),
            radius*radial(sourceChroma+core),outer};
        draw->PrimReserve(int((feedback.arc.size()-1)*18),int(feedback.arc.size()*4));
        const auto first=draw->_VtxCurrentIdx;
        for(size_t i=0;i<feedback.arc.size();++i) {
            const auto fill=ImGui::GetColorU32(ImGuiCol_Text,.35f*feedback.hueWeight[i]);
            innerEdge[i]=at(feedback.arc[i],inner);outerEdge[i]=at(feedback.arc[i],outer);
            draw->PrimWriteVtx(innerEdge[i],uv,sourceChroma<=core?fill:fill&0x00ffffff);
            draw->PrimWriteVtx(at(feedback.arc[i],rings[1]),uv,fill);
            draw->PrimWriteVtx(at(feedback.arc[i],rings[2]),uv,fill);
            draw->PrimWriteVtx(outerEdge[i],uv,fill&0x00ffffff);
            if(!i)continue;
            for(int band=0;band<3;++band) {
                const auto k=ImDrawIdx(first+4*i+band);
                draw->PrimWriteIdx(k-4);draw->PrimWriteIdx(k);draw->PrimWriteIdx(k-3);
                draw->PrimWriteIdx(k-3);draw->PrimWriteIdx(k);draw->PrimWriteIdx(k+1);
            }
        }
        draw->AddPolyline(innerEdge.data(),int(innerEdge.size()),edge,1.f);
        draw->AddPolyline(outerEdge.data(),int(outerEdge.size()),edge,1.f);
        for(auto end:{feedback.arc.front(),feedback.arc.back()})draw->AddLine(at(end,inner),at(end,outer),edge);
        const auto hueGrip=at(feedback.arc.back(),(inner+outer)*.5f);
        const auto radialGrip=pos(source,outer);
        draw->AddCircleFilled(hueGrip,2.8f,ink);draw->AddCircleFilled(radialGrip,2.8f,ink);
        if(ImGui::IsItemActivated() && inside && separation(resultDot)>grip) {
            if(separation(sourceDot)<=grip)feedback.handle=WheelFeedback::Handle::Source;
            else if(separation(hueGrip)<=grip)feedback.handle=WheelFeedback::Handle::Hue;
            else if(separation(radialGrip)<=grip)feedback.handle=WheelFeedback::Handle::Chroma;
            else if(contains(*pick))grabBody(*pick);
        }
        hitSelected=separation(resultDot)<=grip || feedback.handle!=WheelFeedback::Handle::Result;
    }
    if(!grade && inside && ImGui::IsItemActivated() && !hitSelected &&
        !(pick==nullptr && separation(pos(source+rotation,radius*std::clamp((1+sat)*.5f,0.f,1.f)))<=grip)) {
        if(otherHit) {
            state.EndInteraction();
            state.selected=otherHit->id;
            pick=otherHit;
            grabBody(*pick);
        } else {
            auto* added=state.tool==HslImageTool::Sample && pick && pick->bands.empty()?pick:AddRange(recipe,state,false);
            if(!added){state.status="Remove a pick to add another.";return false;}
            state.EndGesture();
            pick=added;
            pick->areaEnabled=false;
            pick->hue=direction;
            pick->sourceChroma=std::clamp(chromaAt(std::min(distance,1.f)),.003f,4.f);
            pick->chromaReach=std::max(.09f,pick->sourceChroma*.3f);
            pick->bands={{-32.f,32.f,.45f}};
            state.localPick=false;
            selectionChanged=true;
        }
        return false;
    }
    if(!grade && !pick && family>=0 && family<8) {
        const auto& arc=WheelFamilySectors()[family].arc;
        for(size_t i=1;i<arc.size();++i)
            draw->AddTriangleFilled(center,at(arc[i],radius),at(arc[i-1],radius),ImGui::GetColorU32(ImGuiCol_Text,.12f));
        for(auto end:{arc.front(),arc.back()})draw->AddLine(center,at(end,radius),ImGui::GetColorU32(ImGuiCol_Text,.45f));
    }
    bool changed=false;
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))ImGui::SetTooltip(grade?"Drag toward the tint to add. Center clears the tint.":
        wedge?"Drag the dot to warp, the wedge to move the match, or its grips to resize. Click empty space to add a pick.":
        "Click to add a pick. Drag its dot to warp. The rim selects color families.");
    if(ImGui::IsItemActive() && inside && (ImGui::IsItemActivated() || ImGui::GetIO().MouseDelta.x || ImGui::GetIO().MouseDelta.y) && (!wedge || ImGui::IsMouseDragging(0,1.f))) {
        const float amount=distance<.04f?0.f:std::min(distance,1.f);
        if(grade && tint) {(*tint)[0]=amount*tintRadius*std::cos(direction*radians);(*tint)[1]=amount*tintRadius*std::sin(direction*radians);changed=true;}
        else if(wedge && feedback.handle==WheelFeedback::Handle::Hue) {
            pick->hueWidth=std::clamp(std::abs(ColorWarpHueDelta(direction-source)),1.f,180.f);selectionChanged=true;
        }else if(wedge && feedback.handle==WheelFeedback::Handle::Chroma) {
            pick->chromaReach=std::clamp(std::abs(chromaAt(amount)-sourceChroma),.002f,4.f);selectionChanged=true;
        }else if(wedge && feedback.handle==WheelFeedback::Handle::Body) {
            source=pick->hue=ColorWarpHueDelta(direction+feedback.hueOffset);
            sourceChroma=pick->sourceChroma=std::clamp(chromaAt(std::clamp(distance+feedback.radiusOffset,0.f,1.f)),.003f,4.f);
            pick->neutralOnly=false;
            selectionChanged=true;
        }else if(wedge && feedback.handle==WheelFeedback::Handle::Source) {
            // Retain the destination while moving the source.
            const float destHue=source+rotation,destChroma=sourceChroma*(1+sat);
            source=pick->hue=direction;sourceChroma=pick->sourceChroma=std::clamp(chromaAt(amount),.003f,4.f);
            pick->neutralOnly=false;pick->allColors=false;
            rotation=ColorWarpHueDelta(destHue-source);sat=std::clamp(destChroma/sourceChroma-1.f,-1.f,10.f);
            selectionChanged=changed=true;
        }else if(!grade) {
            if(amount>0)rotation=ColorWarpHueDelta(direction-source);
            sat=wedge?std::clamp(chromaAt(amount)/std::max(.003f,sourceChroma)-1.f,-1.f,10.f):amount*2-1;
            changed=true;
        }
    }
    draw->AddCircle(center,3,IM_COL32(240,240,240,180));
    sourceDot=pos(source,radius*radial(sourceChroma));
    if(!grade)draw->AddCircle(sourceDot,wedge?7.f:5.f,ink,20,1.5f);
    const auto result=grade?(tint?at({(*tint)[0]/tintRadius,-(*tint)[1]/tintRadius},radius):center):
        pos(source+rotation,radius*(wedge?radial(sourceChroma*(1+sat)):std::clamp((1+sat)*.5f,0.f,1.f)));
    const float target=ImGui::IsItemActive() && inside?1.f:ImGui::IsItemHovered() && distance<=1.2f?.55f:0.f;
    if(feedback.emphasis.Target()!=target)feedback.emphasis.Retarget(target,Animation::MotionSpec::Tween(.12f));
    if(feedback.emphasis.IsActive())feedback.emphasis.Advance(ImGui::GetIO().DeltaTime,Notifications::SystemReducedMotion());
    const float marker=ImGui::GetFontSize()*.29f;
    const auto from=grade?center:sourceDot;
    if(std::hypot(result.x-from.x,result.y-from.y)>2*marker)
        draw->AddLine(from,result,ImGui::GetColorU32(ImGuiCol_Text,.55f));
    if(feedback.emphasis.Value()>.01f)
        draw->AddCircle(result,marker*(1.3f+.55f*feedback.emphasis.Value()),ImGui::GetColorU32(ImGuiCol_Text,.45f*feedback.emphasis.Value()),24);
    if(wedge) {
        const auto hueColor=ImGui::ColorConvertU32ToFloat4(palette[std::lround(HueFraction(source+rotation)*sectors)%sectors].color);
        const float chroma=radial(sourceChroma*(1+sat));
        const ImVec4 fill{.5f+(hueColor.x-.5f)*chroma,.5f+(hueColor.y-.5f)*chroma,.5f+(hueColor.z-.5f)*chroma,1};
        draw->AddCircleFilled(result,marker+1.5f,IM_COL32(22,22,22,255));
        draw->AddCircleFilled(result,marker,ImGui::GetColorU32(fill));
    }else draw->AddCircleFilled(result,marker,IM_COL32(30,30,30,255));
    draw->AddCircle(result,marker,ink,20,1.6f);
    return changed;
}
} // namespace

bool DrawColorRangeSurface(RawWorkspaceEditContext& c,bool grade) {
    auto& recipe=c.recipe.colorWarp;auto& state=c.State<HslSelectionEditorState>();
    bool changed=CommitHslImagePick(c);
    ImGui::BeginDisabled(!c.canEdit || !recipe.enabled);
    const float font=ImGui::GetFontSize();
    const float width=std::max(1.f,ImGui::GetContentRegionAvail().x);
    const float gap=font*.8f;
    const bool beside=grade && width>=font*19.f;
    const float wheelWidth=beside?std::min(width*.35f,font*10.f):width;
    const float mapWidth=beside?width-wheelWidth-gap:width;
    auto& controls=c.State<PanelControlState>();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,font*.35f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{font*.52f,font*.25f});
    const int localMasks=grade?0:int(std::count_if(recipe.hslSelections.begin(),recipe.hslSelections.end(),[](const auto& pick){return pick.warpMask;}));
    ImGui::BeginDisabled((!grade && state.localPick && localMasks>=8) || recipe.hslSelections.size()>=kMaxHslSelections && state.tool!=HslImageTool::Sample);
    const bool sampling=state.tool==HslImageTool::Sample;
    if(ImGui::Button(sampling?"Finish picking":"Pick from photo")) {
        if(sampling)state.EndGesture();
        else if(auto* added=AddRange(recipe,state,grade)){added->allColors=false;if(!grade){added->sourceChroma=.28f;added->areaEnabled=state.localPick;}state.tool=HslImageTool::Sample;changed=true;}
    }
    ImGui::EndDisabled();
    auto* s=FindRange(recipe,state.selected);
    if(!recipe.hslSelections.empty()) {
        if(width>=font*15.f)ImGui::SameLine(0,font*.45f);
        ImGui::SetNextItemWidth(std::max(1.f,std::min(font*10.f,ImGui::GetContentRegionAvail().x)));
        if(ImGui::BeginCombo("##range",s?s->name.c_str():(grade?"Select range":"Select pick"))) {
            for(auto& item:recipe.hslSelections) {
                ImGui::PushID(item.id.c_str());
                if(ImGui::Selectable(item.name.c_str(),s==&item)){state.EndInteraction();state.selected=item.id;s=&item;}
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    } else if(!grade) {
        const int family=std::clamp(controls.selectedFamily,0,7);
        ImGui::SameLine(0,gap);
        const auto dot=ImGui::GetCursorScreenPos();
        ImGui::Dummy({font*.55f,ImGui::GetFrameHeight()});
        ImGui::GetWindowDrawList()->AddCircleFilled({dot.x+font*.25f,dot.y+ImGui::GetFrameHeight()*.5f},font*.22f,Palette()[int(HueFraction(ColorWarpHslAnchorHues()[family])*sectors)%sectors].color);
        ImGui::SameLine(0,font*.2f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s",ColorWarpFamilyName(family));
    } else if(width>ImGui::CalcTextSize("Pick from photoSelect on graph").x+gap*4) {
        ImGui::SameLine(0,gap);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Select on graph");
    }
    if(!grade && (!s || s->sourceChroma>=0)) {
        bool local=s?s->areaEnabled:state.localPick;
        ImGui::BeginDisabled(!local && localMasks>=8 && (!s || !s->warpMask));
        if(ImGui::RadioButton("Global",!local) && local) {
            state.localPick=false;if(s){s->areaEnabled=false;changed=true;}
        }
        ImGui::SameLine();
        if(ImGui::RadioButton("Local",local) && !local) {
            state.localPick=true;
            if(s && s->outline.size()>=3){s->areaEnabled=s->warpMask=true;changed=true;}
            else if(s && state.tool!=HslImageTool::Sample){state.EndGesture();state.tool=HslImageTool::Outline;}
            else if(s){s->areaEnabled=true;changed=true;}
        }
        ImGui::EndDisabled();
        if(s && state.tool!=HslImageTool::Sample)state.localPick=s->areaEnabled;
    }
    if(state.tool==HslImageTool::Sample)ImGui::TextDisabled(grade?"Click or stroke the photo":"Click a color or drag a sampling circle");
    ImGui::Dummy({0,font*.15f});
    if(grade) {
    ImGui::BeginGroup();
    const auto axis=ImGui::GetCursorScreenPos();
    ImGui::Dummy({mapWidth,font*1.2f});
    if(ImGui::IsItemVisible()) {
        auto* draw=ImGui::GetWindowDrawList();
        const auto muted=ImGui::GetColorU32(ImGuiCol_Text,.64f);
        char low[24],high[24];
        std::snprintf(low,sizeof(low),"%+.0f EV",state.viewLow);
        std::snprintf(high,sizeof(high),"%+.0f EV",state.viewHigh);
        draw->AddText(axis,muted,low);
        const float zero=-state.viewLow/(state.viewHigh-state.viewLow)*mapWidth;
        if(zero>font*3.8f && zero<mapWidth-font*3.8f)
            draw->AddText({axis.x+zero-ImGui::CalcTextSize("0").x*.5f,axis.y},muted,"0");
        draw->AddText({axis.x+mapWidth-ImGui::CalcTextSize(high).x,axis.y},muted,high);
    }
    auto* probe=grade && c.canEdit && recipe.enabled?&c.State<ColorGradeHover>():nullptr;
    if(probe){probe->crop=c.recipe.cropRotation;probe->space=c.recipe.technical.workingSpace;}
    changed|=DrawMap(recipe,state,grade,controls.selectedFamily,mapWidth,probe);
    ImGui::EndGroup();
    if(beside)ImGui::SameLine(0,gap);
    }
    s=FindRange(recipe,state.selected);
    if(!s && !state.selected.empty()){state.EndInteraction();state.selected.clear();}
    int family=std::clamp(controls.selectedFamily,0,7);
    RawColorWarpPin* pin=nullptr;
    for(auto& candidate:recipe.pins)if(candidate.family==family){pin=&candidate;break;}
    const float source=s?s->hue:ColorWarpHslAnchorHues()[family];
    if(s && (state.referenceRevision!=c.nodeRevision || state.referenceSelection!=s->id || changed)) {
        state.globalReference=EvaluateColorWarpHsl(PrepareColorWarpHsl(recipe),source);
        state.ownReference=EvaluateColorWarpHsl(HslOwnCoefficients(*s),source);
        state.referenceRevision=c.nodeRevision;state.referenceSelection=s->id;
    }
    const auto channel=[&](int i){return s?(s->own[i]?state.ownReference[i]:state.globalReference[i]*s->influence):0.f;};
    float rotation=s?channel(0):pin?pin->hueShift:0;
    float sat=s?channel(1):pin?pin->saturation:0;

    bool picked=false;
    int wheelFamily=s?-1:family;
    ImGui::BeginDisabled((grade && !s) || (s && !s->enabled) || (!grade && !s && (!pin || !pin->enabled || pin->protectColor)));
    bool selectionChanged=false;
    if(Wheel(source,rotation,sat,grade,s?&s->grade:nullptr,wheelFamily,picked,wheelWidth,grade?(beside?font*8.25f:font*9.5f):font*15.f,c.State<WheelFeedback>(),s,selectionChanged,recipe,state)) {
        if(!grade && s) {
            const float previousHue=channel(0),previousSat=channel(1);
            for(int i=0;i<2;++i)if(!s->own[i])HslDetachChannel(*s,i,recipe);
            for(auto& v:s->values) {
                v[0]=s->independentChannels?rotation:ColorWarpHueDelta(v[0]+rotation-previousHue);
                v[1]=s->independentChannels || sat==-1.f?sat:std::clamp(v[1]+sat-previousSat,-1.f,10.f);
            }
        } else if(!grade && pin) {pin->hueShift=rotation;pin->saturation=sat;}
        changed=true;
    }
    if(grade && !s && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Select a range on the graph to grade it.");
    changed|=selectionChanged;
    float brightness=100*(s?(grade?s->grade[2]:channel(2)):pin?pin->brightness:0);
    if(PanelRoller("Brightness","range-brightness",brightness,-100,100,.3f,0,"%.1f%%",controls)) {
        if(grade && s)s->grade[2]=brightness*.01f;
        else if(s){
            const float previous=channel(2);
            if(!s->own[2])HslDetachChannel(*s,2,recipe);
            for(auto& v:s->values)v[2]=s->independentChannels?brightness*.01f:std::clamp(v[2]+brightness*.01f-previous,-2.f,2.f);
        }
        else if(pin)pin->brightness=brightness*.01f;
        changed=true;
    }
    ImGui::EndDisabled();
    if(picked){controls.selectedFamily=wheelFamily;state.EndInteraction();state.selected.clear();s=nullptr;}
    if(!grade && s && s->sourceChroma>=0) {
        changed|=PanelRoller("Hue reach","warpHue",s->hueWidth,1,180,.35f,25,"%.0f deg",controls);
        float reach=100*s->chromaReach;
        if(PanelRoller("Sat. reach","warpChroma",reach,.2f,400,.22f,9,"%.1f",controls)){s->chromaReach=reach*.01f;changed=true;}
        if(!s->bands.empty()) {
            auto& band=s->bands.front();
            changed|=PanelRoller("Input minimum","warpLow",band.low,-32,band.high,.06f,std::min(-12.f,band.high),"%.2f EV",controls);
            changed|=PanelRoller("Input maximum","warpHigh",band.high,band.low,32,.06f,std::max(8.f,band.low),"%.2f EV",controls);
        }
    }
    if(s) {
        if(ImGui::Button(state.preview?"Hide coverage":"Show coverage"))state.preview=state.preview?0:1;
        ImGui::SameLine(0,font*.45f);
        if(ImGui::Button(grade?"Range options":"Pick options"))ImGui::OpenPopup("Range options");
        if(ImGui::BeginPopup("Range options")) {
            if(grade || s->sourceChroma<0){
            if(ImGui::MenuItem("All hues",nullptr,&s->allColors)){s->neutralOnly=false;changed=true;}
            if(ImGui::MenuItem("Draw area on photo")){state.EndGesture();state.tool=HslImageTool::Outline;}
            if(s->areaEnabled && ImGui::MenuItem("Apply everywhere")){s->areaEnabled=false;s->outline.clear();state.EndGesture();changed=true;}
            }
            ImGui::Separator();
            if(ImGui::MenuItem(grade?"Remove range":"Remove pick")) {
                recipe.hslSelections.erase(recipe.hslSelections.begin()+(s-recipe.hslSelections.data()));
                state.EndInteraction();state.selected.clear();changed=true;
            }
            ImGui::EndPopup();
        }
    }
    if(state.tool==HslImageTool::Outline)ImGui::TextDisabled("Trace the area on the photo. Release to close.");
    if(!state.status.empty())ImGui::TextWrapped("%s",state.status.c_str());
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    if(changed)state.referenceRevision=~std::uint64_t(0);
    return changed;
}
} // namespace Stack::Editor
