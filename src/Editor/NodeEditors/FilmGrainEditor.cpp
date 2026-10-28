#include "FilmGrainEditor.h"
#include "RawWorkspaceEditContext.h"
#include "Editor/EditorModule.h"
#include "Editor/Layers/NoiseLayer.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include "NodeMath/ImageOperationInput.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Renderer/FilmGrain/FilmGrainPreview.h"
#include "Utils/ImGuiExtras.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <exception>

namespace Stack::Editor {
namespace {
using Parameters=Renderer::FilmGrain::Parameters;
struct GrainViewState {
    void EndInteraction() {
        curve.draggingExposure=false;curve.draggingPoint=curve.draggingSegment=curve.draggingSide=-1;
    }
    EditorModuleTypes::RawCurveGraphUiState curve;
    int selected=-1;
    float tone=.18f;
    bool loupeDragging=false;
    ImVec2 loupePress{};
    float radiusBefore=0,densityBefore=0;
    std::vector<RawRecipe::RawBezierCurvePoint> expectedCurve;
    Renderer::FilmGrain::ReferencePreview preview;
    nlohmann::json previewKey;
    std::string error,maskError;
    int lastFrame=-1;
};
struct Number {
    const char* key;const char* label;
    float Parameters::* value;
    float low,high,initial;
};
constexpr Number numbers[]{
    {"grainDensityDeviation","Density (OD)",&Parameters::densityDeviation,0,.3f,.035f},
    {"grainRadiusMicrons","Radius (um)",&Parameters::radiusMicrons,.25f,25,3},
    {"grainRadiusVariation","Size variation",&Parameters::radiusVariation,0,.8f,.35f},
    {"grainMaximumRadiusFactor","Radius cap (x)",&Parameters::maximumRadiusFactor,1,4,3},
    {"grainFilmWidthMm","Film width (mm)",&Parameters::filmWidthMm,8,70,36},
    {"grainCoverage","Coverage",&Parameters::coverage,.05f,.9f,.5f},
    {"grainOpticalBlurMicrons","Optics (um)",&Parameters::opticalBlurMicrons,0,20,1.5f},
    {"grainMix","Mix",&Parameters::mix,0,1,1},
    {"grainColorAmount","Color amount",&Parameters::colorAmount,0,1,.25f},
    {"grainChannelCorrelation","RGB correlation",&Parameters::channelCorrelation,0,1,.65f},
    {"grainShadows","Shadows",&Parameters::shadows,0,3,1.2f},
    {"grainMidtones","Midtones",&Parameters::midtones,0,3,1},
    {"grainHighlights","Highlights",&Parameters::highlights,0,3,.6f},
    {"grainTonalPivot","Pivot (linear)",&Parameters::tonalPivot,.01f,.9f,.18f},
    {"grainExposureFloor","Shadow floor",&Parameters::exposureFloor,0,.1f,.005f}
};
std::string ParameterId(const RawWorkspaceEditContext& c,const char* key) {
    if(c.nodeDraft)if(const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(*c.nodeDraft))
        for(const auto& parameter:definition->parameters)if(parameter.storageKey==key)return parameter.id;
    return std::string("layer.")+key;
}
bool Driven(const RawWorkspaceEditContext& c,const char* key) {return c.IsParameterDriven(ParameterId(c,key));}
float DisplayValue(const RawWorkspaceEditContext& c,const char* key,float authored) {
    if(!Driven(c,key))return authored;
    const auto found=c.uniformParameters.find(ParameterId(c,key));
    return found!=c.uniformParameters.end() && found->second.available && std::isfinite(found->second.value)
        ? float(found->second.value) : authored;
}
void MergeParameters(nlohmann::json& destination,const nlohmann::json& source) {
    for(auto it=source.begin();it!=source.end();++it)
        if(it.key().rfind("grain",0)==0 || it.key()=="filmGrainVersion" || it.key()=="noiseType")destination[it.key()]=it.value();
}
void DrawLoupe(RawWorkspaceEditContext& c,Parameters& p,const Parameters& resolved,
    GrainViewState& state,Renderer::FilmGrain::Input input,float width,float scale,bool& changed) {
    NoiseLayer snapshot;snapshot.SetFilmParameters(resolved);
    auto key=snapshot.Serialize();key["tone"]=state.tone;
    key["native"]={input.nativeWidth,input.nativeHeight};key["scalar"]=input.scalar;
    key["luma"]=input.luma;key["components"]=input.components;
    if(key!=state.previewKey) {
        state.previewKey=key;state.error.clear();
        try {state.preview.Render(resolved,input,state.tone);}
        catch(const std::exception& error){state.error=error.what();}
    }
    const float diameter=std::min(132.f*scale,width);
    ImGui::BeginGroup();
    ImGui::TextDisabled("Grain loupe");
    ImGui::InvisibleButton("##GrainLoupe",{width,diameter},ImGuiButtonFlags_MouseButtonLeft);
    const ImVec2 minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
    const ImVec2 a(minimum.x+(width-diameter)*.5f,minimum.y),b(a.x+diameter,a.y+diameter);
    auto* draw=ImGui::GetWindowDrawList();
    if(state.preview.Texture() && state.error.empty())
        draw->AddImageRounded((ImTextureID)(intptr_t)state.preview.Texture(),a,b,{0,1},{1,0},IM_COL32_WHITE,diameter*.5f);
    else draw->AddCircleFilled({(a.x+b.x)*.5f,(a.y+b.y)*.5f},diameter*.5f,ImGui::GetColorU32(ImGuiCol_FrameBg));
    draw->AddCircle({(a.x+b.x)*.5f,(a.y+b.y)*.5f},diameter*.5f,ImGui::GetColorU32(ImGuiCol_TextDisabled,.6f),64,scale);
    const bool canEdit=c.canEdit && !Driven(c,"grainRadiusMicrons") && !Driven(c,"grainDensityDeviation");
    if(!canEdit)state.loupeDragging=false;
    if(canEdit && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        state.loupeDragging=true;state.loupePress=ImGui::GetIO().MousePos;
        state.radiusBefore=p.radiusMicrons;state.densityBefore=p.densityDeviation;
    }
    if(state.loupeDragging) {
        const bool cancel=ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
            (ImGui::IsMouseDown(0) && (!ImGui::IsItemActive() || ImGui::IsMouseDown(1)));
        if(cancel){p.radiusMicrons=state.radiusBefore;p.densityDeviation=state.densityBefore;changed=true;state.loupeDragging=false;}
        else if(ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0)) {
            const auto mouse=ImGui::GetIO().MousePos;
            const float r=std::clamp(state.radiusBefore+(mouse.x-state.loupePress.x)*.04f/scale,.25f,25.f);
            const float d=std::clamp(state.densityBefore-(mouse.y-state.loupePress.y)*.001f/scale,0.f,.3f);
            changed|=p.radiusMicrons!=r || p.densityDeviation!=d;p.radiusMicrons=r;p.densityDeviation=d;
            if(ImGui::IsMouseReleased(0))state.loupeDragging=false;
        } else state.loupeDragging=false;
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Reference gray at %.3f linear brightness, native pixels magnified 8x. Drag horizontally for radius, vertically for density.%s",state.tone,canEdit?"":" Values are driven from Graph.");
    ImGui::TextDisabled("8x native pixels");
    ImGui::TextDisabled("Response %.2fx",Renderer::FilmGrain::EvaluateResponse(resolved,state.tone));
    if(!state.error.empty())ImGui::TextWrapped("%s",state.error.c_str());
    if(input.nativeWidth<=0)ImGui::TextDisabled("6000 px reference");
    ImGui::EndGroup();
}
bool AddPoint(Parameters& p,GrainViewState& state) {
    size_t at=1;float gap=0;
    for(size_t i=1;i<p.responseCurve.size();++i) {
        const float width=p.responseCurve[i].x-p.responseCurve[i-1].x;
        if(width>gap){gap=width;at=i;}
    }
    const float x=(p.responseCurve[at-1].x+p.responseCurve[at].x)*.5f;
    if(x<=p.responseCurve[at-1].x || x>=p.responseCurve[at].x)return false;
    const float y=RawRecipe::EvaluateRawBezierCurve(p.responseCurve,x);
    p.responseCurve.insert(p.responseCurve.begin()+at,{x,y,{1},{1}});
    state.curve={};state.selected=int(at);state.tone=x;return true;
}
}

bool DrawFilmGrain(EditorModule& editor,RawWorkspaceEditContext& c) {
    if(!c.nodeTarget || !c.nodeDraft || !c.editorState)return false;
    auto& state=c.State<GrainViewState>();
    const int frame=ImGui::GetFrameCount();
    if(state.lastFrame>=0 && state.lastFrame+1<frame){state.curve={};state.loupeDragging=false;}
    if(!c.canEdit){state.curve={};state.loupeDragging=false;}
    state.lastFrame=frame;
    NoiseLayer layer;layer.Deserialize(c.nodeSettings);
    const auto before=layer.Serialize();
    if(before.value("noiseType",13)!=13) {
        state.curve={};state.loupeDragging=false;state.expectedCurve.clear();
        ImGui::BeginDisabled(!c.drivenParameters.empty());layer.RenderUI();ImGui::EndDisabled();
        const auto after=layer.Serialize();if(before==after)return false;
        c.nodeSettings.update(after);return true;
    }
    auto p=layer.FilmParameters();auto resolved=p;
    for(const auto& n:numbers)resolved.*(n.value)=DisplayValue(c,n.key,p.*(n.value));
    for(int i=0;i<3;++i){const auto size=std::string("grainRadius")+"RGB"[i],strength=std::string("grainStrength")+"RGB"[i];
        resolved.channelSize[i]=DisplayValue(c,size.c_str(),p.channelSize[i]);resolved.channelStrength[i]=DisplayValue(c,strength.c_str(),p.channelStrength[i]);}
    resolved.Sanitize();
    if(!Renderer::FilmGrain::SameResponseCurve(state.expectedCurve,p.responseCurve)){state.curve={};state.selected=-1;}
    const float scale=std::max(.01f,ImGui::GetFontSize()/ImGui::GetStyle().FontSizeBase);
    const float width=std::max(1.f,ImGuiExtras::IsGraphNodeControlScopeActive()?ImGui::CalcItemWidth():ImGui::GetContentRegionAvail().x);
    const bool ownNumericScope=!ImGuiExtras::IsGraphNodeControlScopeActive();
    if(ownNumericScope){ImGuiExtras::GraphNodeControlScopeConfig config;config.scale=scale;
        config.interactionScale=scale;config.useScrubHandles=true;config.allowSliderTextEntry=true;ImGuiExtras::BeginGraphNodeControlScope(config);}
    bool changed=false;
    const char* models[]{"Color White","Grayscale White","Blend (Sat)","Blue Noise","Blue Noise (Color)",
        "Perlin (Cloudy)","Worley (Cellular)","Scanlines","Speckle (Dust)","Glitch","Anisotropic (Fiber)",
        "Legacy White (type 11)","Legacy White (type 12)","Film Grain (Particles)"};
    int mode=13;
    if(ImGuiExtras::NodeCombo("Model","##GrainModel",&mode,models,14,width)) {
        state.curve={};state.loupeDragging=false;
        c.nodeSettings["noiseType"]=mode;
        if(ownNumericScope)ImGuiExtras::EndGraphNodeControlScope();
        return true;
    }
    const auto availability=editor.QueryRawAssetCaptureAvailability();
    const bool maskAvailable=availability.target && availability.target->documentId==c.nodeTarget->documentId &&
        availability.target->layerId==c.nodeTarget->layerId && availability.target->operationUuid==c.nodeTarget->nodeUuid;
    ImGui::TextUnformatted("Film grain");ImGui::SameLine();
    ImGui::BeginDisabled(!maskAvailable);
    if(ImGui::SmallButton("Image mask"))editor.BeginRawAssetCapture(*availability.target,RawAssetCapture::Shape::Stroke,RawAssetCapture::Channels::Both,state.maskError);
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Draw an area in the image, then choose Create and apply mask from the capture actions. In Graph, connect the Mask input.%s",maskAvailable?"": " Select this operation in the image view to draw.");
    if(!state.maskError.empty())ImGui::TextWrapped("%s",state.maskError.c_str());
    const float gap=18.f*scale,loupeWidth=150.f*scale;
    const bool beside=width>=490.f*scale;
    const float graphWidth=beside ? width-loupeWidth-gap : width;
    const float graphHeight=220.f*scale;
    ImGui::BeginGroup();ImGui::TextDisabled("Tonal response multiplier");
    ImVec2 plotMinimum,plotMaximum;
    changed|=RawLabInternal::DrawFramelessResponseBezier("##GrainCurve",p.responseCurve,state.curve,state.selected,
        c.inputHistogram,{graphWidth,graphHeight},scale,plotMinimum,plotMaximum,state.tone);
    if(ImGui::IsItemHovered())state.tone=std::clamp((ImGui::GetIO().MousePos.x-plotMinimum.x)/(plotMaximum.x-plotMinimum.x),0.f,1.f);
    else if(state.selected>=0 && state.selected<int(p.responseCurve.size()))state.tone=p.responseCurve[state.selected].x;
    ImGui::TextDisabled("Linear brightness 0..1 / response 0..3x");
    if(ImGui::SmallButton("Add point"))changed|=AddPoint(p,state);
    ImGui::SameLine();if(ImGui::SmallButton("Reset curve")){p.responseCurve=Parameters{}.responseCurve;state.curve={};state.selected=-1;changed=true;}
    ImGui::SameLine();ImGui::TextDisabled("%zu points",p.responseCurve.size());
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Right-click empty space to add; right-click an interior point to remove. Drag points or segment handles like Local EV. Add point has no fixed count limit.");
    ImGui::EndGroup();
    if(beside)ImGui::SameLine(0,gap);
    Renderer::FilmGrain::Input input;
    const auto capability=NodeMath::DescribeImageOperationInput(c.inputDescriptor);input.scalar=capability.scalar;input.luma=capability.lumaWeights;
    if(!capability.scalar && capability.components.bits!=0)
        for(int i=0;i<3;++i)input.components[i]=NodeMath::HasImageComponent(capability.components,static_cast<NodeMath::ImageComponent>(1u<<i))?1.f:0.f;
    if(c.inputDescriptor.spatial.state==NodeMath::KnowledgeState::Known){input.nativeWidth=c.inputDescriptor.spatial.value.nativeWidth;input.nativeHeight=c.inputDescriptor.spatial.value.nativeHeight;}
    resolved.responseCurve=p.responseCurve;
    DrawLoupe(c,p,resolved,state,input,beside?loupeWidth:width,scale,changed);
    if(state.selected>=0 && state.selected<int(p.responseCurve.size())) {
        auto& point=p.responseCurve[state.selected];
        float x=point.x,strength=point.y*3;
        const bool endpoint=state.selected==0 || state.selected+1==int(p.responseCurve.size());
        ImGui::BeginDisabled(endpoint);
        if(ImGuiExtras::NodeSliderFloat("Selected brightness","##GrainPointX",&x,0,1,"%.4f",width) && !endpoint) {
            const float lo=std::nextafter(p.responseCurve[state.selected-1].x,1.f),hi=std::nextafter(p.responseCurve[state.selected+1].x,0.f);
            if(lo<=hi){point.x=std::clamp(x,lo,hi);changed=true;}
        }
        ImGui::EndDisabled();
        if(ImGuiExtras::NodeSliderFloat("Selected response","##GrainPointY",&strength,0,3,"%.3fx",width)){point.y=strength/3;changed=true;}
        ImGui::BeginDisabled(endpoint);
        if(ImGui::SmallButton("Remove selected point")){p.responseCurve.erase(p.responseCurve.begin()+state.selected);state.curve={};state.selected=-1;changed=true;}
        ImGui::EndDisabled();
    }
    const int columns=width>=640.f*scale?3:width>=430.f*scale?2:1;
    const float lane=(width-gap*(columns-1))/columns;
    const auto value=[&](const char* key,const char* label,float& v,float lo,float hi,float initial) {
        float shown=DisplayValue(c,key,v);const bool driven=Driven(c,key);
        ImGui::BeginDisabled(driven);ImGuiExtras::SetNextNodeNumericDefault(initial);
        if(ImGuiExtras::NodeSliderFloat(label,key,&shown,lo,hi,"%.4g",lane) && !driven){v=shown;changed=true;}
        ImGui::EndDisabled();
        if(driven && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Driven by the connected Graph input.");
    };
    const auto integer=[&](const char* label,const char* id,int& v,int lo,int hi,int initial){
        ImGuiExtras::SetNextNodeNumericDefault(initial);changed|=ImGuiExtras::NodeSliderInt(label,id,&v,lo,hi,"%d",lane);};
    ImGui::Dummy({0,10*scale});
    for(int group=0;group<3;++group) {
        if(group%columns)ImGui::SameLine(0,gap);
        ImGui::BeginGroup();ImGui::PushItemWidth(lane);ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+lane);
        const char* titles[]{"Emulsion","Color layers","Response and rendering"};ImGui::TextDisabled("%s",titles[group]);
        ImGui::BeginDisabled(group==1 && capability.scalar);
        const int first=group==0?0:group==1?8:10,last=group==0?8:group==1?10:15;
        for(int i=first;i<last;++i){const auto& n=numbers[i];value(n.key,n.label,p.*(n.value),n.low,n.high,n.initial);}
        if(group==1)for(int i=0;i<3;++i){const auto key=std::string("grainRadius")+"RGB"[i],label=std::string(1,"RGB"[i])+" radius x";
            value(key.c_str(),label.c_str(),p.channelSize[i],.5f,2,1);}
        if(group==1)for(int i=0;i<3;++i){const auto key=std::string("grainStrength")+"RGB"[i],label=std::string(1,"RGB"[i])+" strength x";
            value(key.c_str(),label.c_str(),p.channelStrength[i],0,3,1);}
        if(group==2){
            integer("Samples","##GrainSamples",p.samples,4,64,16);integer("Seed","##GrainSeed",p.seed,0,16777215,125775);integer("Frame","##GrainFrame",p.frame,0,1000000,0);
            const char* encodings[]{"Auto (metadata)","Linear light","sRGB encoded"};const char* outputs[]{"Density","Additive","Grain only"};
            changed|=ImGuiExtras::NodeCombo("Encoding","##GrainEncoding",&p.encoding,encodings,3,lane);
            changed|=ImGuiExtras::NodeCombo("Output","##GrainOutput",&p.output,outputs,3,lane);
            changed|=ImGuiExtras::NodeCheckbox("Mean bias","##GrainMean",&p.compensateMean,lane);
        }
        ImGui::EndDisabled();ImGui::PopTextWrapPos();ImGui::PopItemWidth();ImGui::EndGroup();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("The curve multiplies the shadow / midtone / highlight response. 1x is neutral.");
    ImGui::PopStyleColor();ImGui::PopTextWrapPos();
    if(ownNumericScope)ImGuiExtras::EndGraphNodeControlScope();
    p.Sanitize();state.expectedCurve=p.responseCurve;
    if(changed){layer.SetFilmParameters(p);MergeParameters(c.nodeSettings,layer.Serialize());}
    return changed;
}
bool ResetFilmGrain(EditorModule&,RawWorkspaceEditContext& c) {
    NoiseLayer layer;const auto defaults=layer.Serialize();bool changed=false;
    const bool film=c.nodeSettings.value("noiseType",13)==13;
    for(auto it=defaults.begin();it!=defaults.end();++it)if(
        (film ? it.key().rfind("grain",0)==0 : it.key().rfind("grain",0)!=0 && it.key()!="type" && it.key()!="noiseType" && it.key()!="filmGrainVersion") &&
        !Driven(c,it.key().c_str())) {
        if(c.nodeSettings[it.key()]!=it.value()){c.nodeSettings[it.key()]=it.value();changed=true;}
    }
    return changed;
}
}
