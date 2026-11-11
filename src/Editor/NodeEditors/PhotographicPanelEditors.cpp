#include "Editor/NodeEditors/PhotographicPanelEditors.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include "Editor/NodeEditors/LightBandEditor.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Raw/Curves/TonalCurvePoints.h"
#include "Raw/ColorWarpMath.h"
#include "Raw/RawColorCalibration.h"
#include "Editor/Internal/RawLab/RawCalibrationEditor.h"
#include <imgui_internal.h>
#include <algorithm>
#include <exception>
#include <limits>

namespace Stack::Editor {
namespace {
constexpr const char* tones[]{"Blacks","Shadows","Midtones","Highlights","Whites"};
constexpr const char* primaries[]{"Red","Green","Blue"};
constexpr const char* hueIds[]{"hue-0","hue-1","hue-2"};
constexpr const char* saturationIds[]{"saturation-0","saturation-1","saturation-2"};
bool Field(RawWorkspaceEditContext& c,const char* label,const char* id,float& value,
    float low,float high,float speed,float reset,const char* format) {
    ImGui::BeginDisabled(c.IsParameterDriven(id));
    const bool changed=PanelRoller(label,id,value,low,high,speed,reset,format,c.State<PanelControlState>());
    ImGui::EndDisabled();return changed;
}
}
bool DrawPanelTonalRollers(RawWorkspaceEditContext& c,std::vector<RawRecipe::RawBezierCurvePoint>& points,
    const RawLabInternal::CurveTonalSliderDomain& domain,int maximumPoints,bool locked) {
    bool changed=false;
    for(int i=0;i<5;++i) {
        const bool owned=std::any_of(points.begin(),points.end(),[i](const auto& p){return p.tonal.region==i;});
        const bool available=owned || (domain.positions[i]>0 && domain.positions[i]<1);
        float value=100.f*RawRecipe::TonalCurveSliderValue(points,i);
        ImGui::BeginDisabled(!available || locked);
        if(Field(c,tones[i],tones[i],value,-100,100,.4f,0,"%+.1f")) {
            const auto result=RawRecipe::SetTonalCurveSlider(points,i,domain.positions[i],value*.01f,maximumPoints);
            if(result==RawRecipe::TonalCurveEditResult::Changed) {
                changed=true;
            } else if(result==RawRecipe::TonalCurveEditResult::CapacityReached || result==RawRecipe::TonalCurveEditResult::Conflict)
                c.error="This tonal control needs room for a curve point. Adjust the custom points in the floating graph.";
        }
        ImGui::EndDisabled();
    }
    return changed;
}
bool DrawLightPanel(EditorModule&,RawWorkspaceEditContext& c) {
    bool changed=Field(c,"Exposure","ev",c.recipe.preToneExposureEv,-16,16,.02f,0,"%+.2f EV");
    auto& local=c.recipe.localRange;
    for(int i=0;i<5;++i) {
        if(Field(c,tones[i],tones[i],local.light.amounts[i],-100,100,.4f,0,"%+.1f")) {
            local.enabled=true;changed=true;
        }
    }
    return changed;
}
bool DrawCalibrationPanel(EditorModule&,RawWorkspaceEditContext& c) {
    bool changed=false;
    ImGui::BeginDisabled(!c.recipe.colorCalibration.enabled);
    const float scale=ImGui::GetFontSize()/13.f;
    const float width=ImGui::GetContentRegionAvail().x;
    const bool paired=width>=260.f*scale;
    const float nameWidth=48.f*scale,gap=6.f*scale;
    const float column=(width-nameWidth-gap)*.5f;
    const float start=ImGui::GetCursorScreenPos().x;
    if(paired) {
        const auto origin=ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({start+nameWidth,origin.y});ImGui::TextDisabled("Hue");
        ImGui::SetCursorScreenPos({start+nameWidth+column+gap,origin.y});ImGui::TextDisabled("Saturation");
        ImGui::SetCursorScreenPos({start,ImGui::GetCursorScreenPos().y});
    }
    for(int i=0;i<3;++i) {
        ImGui::PushID(i);
        const auto origin=ImGui::GetCursorScreenPos();
        auto& primary=c.recipe.colorCalibration.primaries[i];
        constexpr float limit=std::numeric_limits<float>::max();
        if(paired) {
            ImGui::SetCursorScreenPos({start,origin.y+5.f*scale});ImGui::TextUnformatted(primaries[i]);
            ImGui::SetCursorScreenPos({start+nameWidth,origin.y});
            ImGui::BeginDisabled(c.IsParameterDriven(hueIds[i]));
            changed|=PanelRoller("",hueIds[i],primary.hue,-limit,limit,.25f,0,"%+.1f",c.State<PanelControlState>(),column);
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos({start+nameWidth+column+gap,origin.y});
            ImGui::BeginDisabled(c.IsParameterDriven(saturationIds[i]));
            changed|=PanelRoller("",saturationIds[i],primary.saturation,-limit,limit,.4f,0,"%+.1f",c.State<PanelControlState>(),column);
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos({start,ImGui::GetCursorScreenPos().y});
        } else {
            ImGui::TextDisabled("%s primary",primaries[i]);
            changed|=Field(c,"Hue",hueIds[i],primary.hue,-limit,limit,.25f,0,"%+.1f");
            changed|=Field(c,"Saturation",saturationIds[i],primary.saturation,-limit,limit,.4f,0,"%+.1f");
        }
        if(ImGui::IsMouseHoveringRect(origin,{origin.x+ImGui::GetContentRegionAvail().x,ImGui::GetCursorScreenPos().y}) &&
            (ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(2)))c.State<RawLabInternal::CalibrationEditorState>().selected=i;
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    return changed;
}
bool DrawHslPanel(EditorModule&,RawWorkspaceEditContext& c) {
    auto& state=c.State<PanelControlState>();
    const char* channels[]{"Hue","Saturation","Brightness"};
    static const char* names[]{"   Red","   Orange","   Yellow","   Green","   Aqua","   Blue","   Purple","   Magenta"};
    static const ImVec4 colors[]={{.9f,.3f,.3f,1},{.9f,.55f,.25f,1},{.85f,.8f,.3f,1},{.3f,.75f,.4f,1},{.3f,.75f,.75f,1},{.35f,.55f,.9f,1},{.65f,.4f,.85f,1},{.85f,.4f,.7f,1}};
    const float width=ImGui::GetContentRegionAvail().x;
    for(int channel=0;channel<3;++channel) {
        if(channel)ImGui::SameLine(0,4);
        ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(state.hslChannel==channel?ImGuiCol_ButtonActive:ImGuiCol_ChildBg));
        if(ImGui::Button(channels[channel],{(width-8)/3,0}))state.hslChannel=channel;
        ImGui::PopStyleColor();
    }
    bool changed=false;
    ImGui::BeginDisabled(!c.recipe.colorWarp.enabled);
    for(int family=0;family<8;++family) {
        const auto found=std::find_if(c.recipe.colorWarp.pins.begin(),c.recipe.colorWarp.pins.end(),
            [family](const auto& pin){return pin.family==family;});
        ImGui::PushID(family);
        if(found==c.recipe.colorWarp.pins.end()) {
            ImGui::BeginDisabled();
            PanelRollerPlaceholder(RawRecipe::ColorWarpFamilyName(family));
            ImGui::EndDisabled();ImGui::PopID();continue;
        }
        auto& pin=*found;
        const auto origin=ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!pin.enabled || pin.protectColor);
        float value=state.hslChannel==0?pin.hueShift:state.hslChannel==1?pin.saturation*100:pin.brightness*100;
        if(PanelRoller(names[pin.family],"family",value,
            state.hslChannel==0?-180.f:-100.f,
            state.hslChannel==0?180.f:state.hslChannel==1?500.f:100.f,
            .4f,0,"%+.1f",state)) {
            if(state.hslChannel==0)pin.hueShift=value;
            else if(state.hslChannel==1)pin.saturation=value*.01f;
            else pin.brightness=value*.01f;
            changed=true;
        }
        ImGui::GetWindowDrawList()->AddCircleFilled({origin.x+4,origin.y+12.f*ImGui::GetFontSize()/13.f},3,ImGui::GetColorU32(colors[family]));
        if(ImGui::IsMouseHoveringRect(origin,{origin.x+ImGui::GetContentRegionAvail().x,ImGui::GetCursorScreenPos().y}) &&
            (ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(2)))state.selectedFamily=pin.family;
        ImGui::EndDisabled();ImGui::PopID();
    }
    ImGui::EndDisabled();
    return changed;
}
bool DrawCalibrationPanelSettings(EditorModule&,RawWorkspaceEditContext& c) {
    bool changed=ImGui::Checkbox("Apply calibration",&c.recipe.colorCalibration.enabled);
    auto& state=c.State<RawLabInternal::CalibrationEditorState>();
    const auto& recipe=c.recipe.colorCalibration;
    bool stale=!state.transformChecked || state.checkedSpace!=c.recipe.technical.workingSpace || state.checkedRecipe.enabled!=recipe.enabled;
    for(int i=0;i<3;++i)stale|=state.checkedRecipe.primaries[i].hue!=recipe.primaries[i].hue ||
        state.checkedRecipe.primaries[i].saturation!=recipe.primaries[i].saturation;
    if(stale) {
        state.checkedRecipe=recipe;state.checkedSpace=c.recipe.technical.workingSpace;state.transformChecked=true;state.transformError.clear();
        try { RawRecipe::BuildColorCalibrationTransform(recipe,state.checkedSpace); }
        catch(const std::exception& error) { state.transformError=error.what(); }
    }
    if(!state.transformError.empty())ImGui::TextWrapped("%s",state.transformError.c_str());
    return changed;
}
bool DrawLightPanelSettings(EditorModule&,RawWorkspaceEditContext& c) {
    auto& local=c.recipe.localRange;
    bool changed=DrawLightRangeSettings(c);
    changed|=Field(c,"Smoothness","smoothness",local.smoothness,0,1,.004f,.65f,"%.2f");
    changed|=Field(c,"Edge protection","edgeProtection",local.edgeProtection,0,1,.004f,.75f,"%.2f");
    changed|=Field(c,"Detail protection","detailProtection",local.detailProtection,0,1,.004f,.8f,"%.2f");
    changed|=Field(c,"Highlight protection","highlightProtection",local.highlightProtection,0,1,.004f,.5f,"%.2f");
    changed|=ImGui::Checkbox("Use color target",&local.colorMaskEnabled);
    changed|=ImGui::Checkbox("Use region mask",&local.regionMaskEnabled);
    return changed;
}
void DrawMissingPanelControls(int order) {
    ImGui::BeginDisabled();
    const float scale=ImGui::GetFontSize()/13.f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,2.f*scale});
    const auto row=[](const char* label) {PanelRollerPlaceholder(label);};
    if(order==0) {row("Exposure");for(const auto* tone:tones)row(tone);}
    else if(order==1) {ImGui::TextUnformatted("Luminance   R   G   B");ImGui::Dummy({0,180.f*scale});}
    else if(order==3) {
        const float width=ImGui::GetContentRegionAvail().x,nameWidth=48.f*scale,gap=6.f*scale;
        const float column=(width-nameWidth-gap)*.5f,start=ImGui::GetCursorScreenPos().x;
        if(width>=260.f*scale) {
            const auto origin=ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos({start+nameWidth,origin.y});ImGui::TextUnformatted("Hue");
            ImGui::SetCursorScreenPos({start+nameWidth+column+gap,origin.y});ImGui::TextUnformatted("Saturation");
            ImGui::SetCursorScreenPos({start,ImGui::GetCursorScreenPos().y});
            for(const auto* primary:primaries) {
                const auto at=ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos({start,at.y+5.f*scale});ImGui::TextUnformatted(primary);
                ImGui::SetCursorScreenPos({start+nameWidth,at.y});PanelRollerPlaceholder("",column);
                ImGui::SetCursorScreenPos({start+nameWidth+column+gap,at.y});PanelRollerPlaceholder("",column);
                ImGui::SetCursorScreenPos({start,ImGui::GetCursorScreenPos().y});
            }
        } else for(const auto* primary:primaries) {ImGui::TextUnformatted(primary);row("Hue");row("Saturation");}
    }
    else {ImGui::TextUnformatted("Hue   Saturation   Brightness");for(int i=0;i<8;++i)row(RawRecipe::ColorWarpFamilyName(i));}
    ImGui::Spacing();ImGui::TextUnformatted("> Settings");
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    ImGui::TextDisabled("Connect this node in the graph to edit.");
}
}
