#include "Raw/HslSelections.h"
#include "Raw/HslSelectionSerialization.h"
#include "Raw/RawGraphOperation.h"
#include "Project/RawLayerStack.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include "Editor/NodeEditors/HslSelectionEditor.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Editor/NodeEditors/PhotographicPanelEditors.h"
#include "ThirdParty/stb_image_write.h"
#include <backends/imgui_impl_opengl3.h>
#include <filesystem>
#include <iostream>

namespace Stack::Validation {
bool ValidateHslSelectionMath(unsigned program) {
    using namespace RawRecipe;bool ok=true;float worst=0;
    const auto require=[&](bool pass,const char* label){if(!pass)std::cerr<<"HSL selection FAILED: "<<label<<'\n';ok&=pass;};
    RawColorWarpRecipe recipe;recipe.version=6;
    for(auto& p:recipe.pins){p.hueShift=12;p.saturation=.4f;p.brightness=.1f;}
    HslSelection selection;selection.id="reds";selection.name="Bright + dark reds";selection.allColors=true;
    selection.bands={{-8,-3,.25f},{2,6,.25f}};recipe.hslSelections={selection};recipe.limitHslToSelections=true;
    RawColorWarpCoordinate coordinate{.6f,.15f,.05f,0};
    const auto global=EvaluateColorWarpHsl(PrepareColorWarpHsl(recipe),ColorWarpHue(coordinate.a,coordinate.b));
    require(ResolveHslSelections(recipe,coordinate,global,{.5f,.5f})==std::array<float,3>{},"middle brightness gap");
    for(float ev:{-5.f,4.f}){coordinate.sceneEv=ev;require(ResolveHslSelections(recipe,coordinate,global,{.5f,.5f})==global,"separate included brightnesses");}
    auto& s=recipe.hslSelections[0];coordinate.sceneEv=4;s.influence=.5f;
    const auto linked=ResolveHslSelections(recipe,coordinate,global,{.5f,.5f});HslDetachChannel(s,1,recipe);
    require(ResolveHslSelections(recipe,coordinate,global,{.5f,.5f})==linked,"detach preserves current settings");
    for(auto& p:recipe.pins)p.saturation=2;
    auto newGlobal=global;newGlobal[1]=2;
    require(std::abs(ResolveHslSelections(recipe,coordinate,newGlobal,{.5f,.5f})[1]-linked[1])<1e-6f,"own values stop following global");
    s.excludedBands={{3.5f,4.5f,.1f}};
    require(HslSelectionWeight(s,coordinate,{.5f,.5f})==0,"excluded tones");s.excludedBands.clear();
    s.areaEnabled=true;s.outline={{.1f,.1f},{.9f,.1f},{.1f,.9f}};s.areaSoftness=.05f;
    require(HslAreaWeight(s,{.2f,.2f})==1 && HslAreaWeight(s,{.85f,.85f})==0,"spatial intersection");
    s.outline.clear();require(HslAreaWeight(s,{.5f,.5f})==0,"unfinished area has zero coverage");
    s.areaEnabled=false;
    auto later=s;later.id="later";later.own={};later.influence=0;recipe.hslSelections.push_back(later);
    require(ResolveHslSelections(recipe,coordinate,newGlobal,{.5f,.5f})==std::array<float,3>{},"later selection wins");
    const auto saved=SerializeColorWarpRecipe(recipe);const auto restored=DeserializeColorWarpRecipe(saved);
    require(SerializeColorWarpRecipe(restored)==saved,"saved selection round trip");
    {
        Project::RawLayerStackDocument doc;auto initial=doc.State();std::string error;
        auto* node=Project::FindRawOperation(initial.background,GraphOperationKind::ColorWarp);
        if(node){auto r=ReadGraphOperation(node->rawOperation);r.colorWarp=recipe;WriteGraphOperation(node->rawOperation,r);require(doc.Apply(initial,error),"graph selection edit");
            require(doc.Undo(&error),"selection undo");require(ReadGraphOperation(Project::FindRawOperation(doc.State().background,GraphOperationKind::ColorWarp)->rawOperation).colorWarp.hslSelections.empty(),"undo restores prior state");
            require(doc.Redo(&error),"selection redo");require(SerializeColorWarpRecipe(ReadGraphOperation(Project::FindRawOperation(doc.State().background,GraphOperationKind::ColorWarp)->rawOperation).colorWarp)==saved,"redo restores all selection fields");}
        else require(false,"graph HSL target");
    }
    {
        auto node=MakeGraphOperation(GraphOperationKind::ColorGrade);
        auto value=ReadGraphOperation(node);auto& warp=value.colorWarp;
        const auto space=Raw::RawWorkingSpace::LinearSrgbD65;
        const std::array<float,3> glint{4,4,4};
        require(ApplyPreparedColorWarp(warp,glint,space)==glint,"neutral grade preserves HDR exactly");
        HslSelection grade;grade.id="reflections";grade.allColors=true;grade.grade={.06f,.09f,0};
        grade.bands={{1,8,.5f}};warp.hslSelections={grade};
        WriteGraphOperation(node,value);
        const auto restoredGrade=ReadGraphOperation(DeserializeGraphOperation(SerializeGraphOperation(node)));
        const auto bright=ApplyPreparedColorWarp(restoredGrade.colorWarp,glint,space);
        const auto brighter=ApplyPreparedColorWarp(restoredGrade.colorWarp,{8,8,8},space);
        require(bright[0]>bright[2] && bright[0]>1 && brighter[0]>bright[0],"saved highlight grade tints gray and retains HDR spacing");
        grade.grade={};grade.own[1]=true;grade.bands.clear();
        for(auto& v:grade.values)v[1]=-1;
        warp.hslSelections={grade};
        HslSelection empty;empty.id="second";empty.allColors=true;empty.independentChannels=true;
        warp.hslSelections.push_back(empty);
        const auto paint=ApplyPreparedColorWarp(warp,{.12f,.2f,.8f},space);
        require(paint[0]==paint[1] && paint[1]==paint[2],"new neutral range leaves exact gray intact");
    }
    {
        Project::RawLayerStackDocument document;
        auto state=document.State();
        auto& layer=state.background;
        const auto* node=Project::FindRawOperation(layer,GraphOperationKind::ColorWarp);
        if(!node)return false;
        const int id=node->id;
        const auto uuid=node->instanceUuid;
        auto edit=ReadGraphOperation(node->rawOperation);
        HslSelection pick;pick.id="pick-test";pick.name="Green reflection";pick.sourceChroma=.3f;
        pick.chromaReach=.05f;pick.areaEnabled=pick.warpMask=true;pick.outline={{.1f,.1f},{.8f,.1f},{.1f,.8f}};
        edit.colorWarp.hslSelections={pick};
        std::string error;
        require(Project::WriteRawLayerOperation(layer,uuid,edit,error),"Warp mask creation");
        const auto* link=layer.graph.FindInputLink(id,"area:warp-pick-test");
        require(link && layer.graph.FindNode(link->fromNodeId)->kind==EditorNodeGraph::NodeKind::CustomMask,"normal mask connected to pick");
        require(document.Apply(state,error),"masked Warp graph validation");
        const auto read=Project::ReadRawLayerOperation(layer,uuid);
        require(read.colorWarp.hslSelections.front().sourceChroma==pick.sourceChroma,"Warp wedge retained");
        if(link) {
            const int generator=link->fromNodeId;
            edit.colorWarp.hslSelections.front().areaEnabled=false;
            require(Project::WriteRawLayerOperation(layer,uuid,edit,error),"switch pick to Global");
            const auto* globalLink=layer.graph.FindInputLink(id,"area:warp-pick-test");
            require(globalLink && globalLink->fromNodeId==generator,"Global retains the local mask wire");
        }
    }
    glUseProgram(program);
    const auto integer=[&](const char* name,int value){glUniform1i(glGetUniformLocation(program,name),value);};
    integer("uPinCount",0);integer("uHslModel",1);integer("uUseSpatialMasks",0);integer("uHslSelections",5);
    glUniform1f(glGetUniformLocation(program,"uStrength"),1);
    int cases=0;
    for(bool rec2020:{false,true})for(int mode=0;mode<10;++mode) {
        RawColorWarpRecipe r;r.version=6;for(auto& p:r.pins){p.hueShift=18;p.saturation=.4f;p.brightness=.15f;}
        HslSelection a;a.id="a";a.allColors=true;
        if(mode==0)a.influence=1;
        if(mode==1){r.limitHslToSelections=true;a.bands={{-8,-3,.3f},{2,8,.3f}};}
        if(mode==2){a.influence=0;}
        if(mode==3){HslDetachChannel(a,1,r);for(auto& v:a.values)v[1]=-.6f;a.own[2]=true;for(auto& v:a.values)v[2]=.25f;}
        if(mode==4){a.areaEnabled=true;a.outline={{.05f,.05f},{.85f,.05f},{.05f,.85f}};a.areaSoftness=.15f;a.influence=0;}
        if(mode>=6){r.pins=RawColorWarpRecipe{}.pins;a.grade={.10f,-.07f,.12f};a.neutralOnly=mode==7;a.independentChannels=true;}
        if(mode>=8){a.sourceChroma=.32f;a.chromaReach=.16f;a.grade={.1f,.04f,0};}
        r.hslSelections={a};
        if(mode==5){a.id="b";a.influence=.2f;a.excludedBands={{-.5f,.5f,.3f}};r.hslSelections.push_back(a);}
        const auto coefficients=PrepareColorWarpHsl(r);glUniform4fv(glGetUniformLocation(program,"uHsl[0]"),8,coefficients.front().data());
        integer("uInputIsRec2020",rec2020);integer("uHslSelectionCount",int(r.hslSelections.size()));integer("uHslLimit",r.limitHslToSelections);
        auto packed=PackHslSelections(r);
        Renderer::ScopedGLTexture pickMask;
        if(mode==9) {
            const std::array<float,4> zero{};
            pickMask.Reset(GLHelpers::CreateTextureFromData(zero.data(),1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT));
            glActiveTexture(GL_TEXTURE0+6);glBindTexture(GL_TEXTURE_2D,pickMask.Get());
            integer("uWarpPickMasks[0]",6);packed[109][2]=1;
            r.hslSelections.front().enabled=false; // Black graph mask should remove this pick only.
        }
        Renderer::ScopedGLTexture data(GLHelpers::CreateTextureFromData(packed.data(),kHslDataWidth,int(r.hslSelections.size()),GL_RGBA32F,GL_RGBA,GL_FLOAT));
        glActiveTexture(GL_TEXTURE0+5);glBindTexture(GL_TEXTURE_2D,data.Get());
        const auto space=rec2020?Raw::RawWorkingSpace::LinearRec2020D65:Raw::RawWorkingSpace::LinearSrgbD65;
        for(const auto input:{std::array<float,4>{.004f,.0012f,.0008f,1},std::array<float,4>{.4f,.12f,.08f,1},std::array<float,4>{8,2,.3f,.5f},std::array<float,4>{4,4,4,1}})for(bool regional:{false,true}) {
            const std::array<float,4> region=regional?std::array<float,4>{.15f,.2f,.5f,.6f}:std::array<float,4>{0,0,1,1};
            glUniform4fv(glGetUniformLocation(program,"uHslImageRegion"),1,region.data());
            Renderer::ScopedGLTexture source(GLHelpers::CreateTextureFromData(input.data(),1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT));
            Renderer::ScopedGLTexture output(GLHelpers::CreateTextureFromData(nullptr,4,4,GL_RGBA32F,GL_RGBA,GL_FLOAT));
            const auto fbo=GLHelpers::CreateFBO(output.Get());glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,4,4);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.Get());glDrawArrays(GL_TRIANGLES,0,3);
            std::array<float,64> actual{};glReadPixels(0,0,4,4,GL_RGBA,GL_FLOAT,actual.data());
            for(int y=0;y<4;++y)for(int x=0;x<4;++x) {
                const std::array<float,2> uv{region[0]+(x+.5f)/4*region[2],region[1]+(1-(y+.5f)/4)*region[3]};
                const auto expected=ApplyPreparedColorWarp(r,{input[0],input[1],input[2]},space,nullptr,uv);
                for(int c=0;c<3;++c){const float delta=std::abs(actual[(y*4+x)*4+c]-expected[c]);worst=std::max(worst,delta);require(std::isfinite(delta) && delta<.0001f*std::max(1.f,std::abs(expected[c])),"CPU/GPU and regional agreement");}
                require(actual[(y*4+x)*4+3]==input[3],"alpha preserved");
            }
            require(glGetError()==GL_NO_ERROR,"GL selection execution");glDeleteFramebuffers(1,&fbo);++cases;
        }
    }
    integer("uHslSelectionCount",0);integer("uHslLimit",0);
    std::cout<<"HSL selection checks: "<<cases<<" GPU cases; worst="<<worst<<"; passed="<<ok<<'\n';return ok;
}
bool CaptureHslSelectionControls() {
    Editor::NodeEditorState store;Editor::RawWorkspaceEditContext c;c.editorState=&store;c.nodeTarget=Project::NodeEditTarget{"hsl-validation","background","color"};c.nodeRevision=1;c.canEdit=true;
    auto& recipe=c.recipe.colorWarp;recipe.version=6;recipe.pins[0].saturation=.4f;
    RawRecipe::HslSelection selection;selection.id="reds";selection.name="Bright + dark reds";selection.bands={{-8,-3,.5f},{2,6,.5f}};recipe.hslSelections={selection};recipe.limitHslToSelections=true;
    auto& state=c.State<Editor::HslSelectionEditorState>();state.selected=selection.id;state.reveal.Reset(1);
    c.State<Editor::PanelControlState>().hslChannel=1;
    c.State<Editor::PanelControlState>().hslChannelMotion.Reset(1);
    bool ok=true;
    for(int capture=0;capture<3;++capture) {
    for(int frame=0;frame<3;++frame) {
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({12,12});ImGui::SetNextWindowSize({430,830});
        ImGui::Begin(capture==2?"Warp":"HSL selections",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
        if(capture==1 && frame==0)ImGui::SetScrollY(ImGui::GetScrollMaxY());
        if(capture==2){
            if(frame==0) {
                auto& pick=recipe.hslSelections.front();
                pick.name="Green paint";pick.sourceChroma=.38f;pick.chromaReach=.16f;
                pick.allColors=false;pick.independentChannels=true;pick.hue=145;pick.hueWidth=32;
                pick.bands={{-2,1,.5f}};pick.own[0]=true;
                for(auto& channel:pick.values)channel[0]=105;
                ++c.nodeRevision;
            }
            Editor::DrawColorRangeSurface(c,false);
        }
        else {Editor::DrawHslPanelControls(c);Editor::DrawHslSelections(c); }
        if(capture==1 && frame==2)ok &= ImGui::IsItemVisible();
        ImGui::End();ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,720,860);glClearColor(.08f,.08f,.08f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    std::vector<unsigned char> pixels(720*860*4);glReadPixels(0,0,720,860,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for(int y=0;y<430;++y)for(int x=0;x<720*4;++x)std::swap(pixels[y*720*4+x],pixels[(859-y)*720*4+x]);
    std::filesystem::create_directories("outputs/validation/hsl-selections");
    ok &= stbi_write_png(capture==2?"outputs/validation/hsl-selections/range-wheel.png":capture==0?"outputs/validation/hsl-selections/controls.png":"outputs/validation/hsl-selections/selection.png",720,860,4,pixels.data(),720*4)!=0;
    }
    // Replay gestures in the native window used for the wheel capture above.
    auto wheelFrame=[&](float x,float y,bool down) {
        auto& io=ImGui::GetIO();
        io.AddMousePosEvent(x,y);io.AddMouseButtonEvent(0,down);
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos({12,12});ImGui::SetNextWindowSize({430,830});
        ImGui::Begin("Warp",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
        if(Editor::DrawColorRangeSurface(c,false))++c.nodeRevision;
        ImGui::End();ImGui::Render();
    };
    wheelFrame(270,220,false);wheelFrame(270,220,true);
    const auto first=recipe.hslSelections.back();
    wheelFrame(175,254,true);wheelFrame(175,254,false);
    const auto warped=recipe.hslSelections.back();
    const bool created=recipe.hslSelections.size()==2 && first.sourceChroma>=0;
    const bool movedResult=warped.hue==first.hue && warped.own[0] && std::abs(warped.values[0][0])>10;
    wheelFrame(264,210,false);wheelFrame(264,210,true);
    wheelFrame(244,191,true);wheelFrame(244,191,false);
    const auto moved=recipe.hslSelections.back();
    const bool movedBody=recipe.hslSelections.size()==2 && std::abs(moved.hue-warped.hue)>8 &&
        moved.values==warped.values && moved.hueWidth==warped.hueWidth && moved.chromaReach==warped.chromaReach;
    std::cout<<"Warp wheel gestures: create="<<created<<" result="<<movedResult<<" move wedge="<<movedBody<<'\n';
    ok &= created && movedResult && movedBody;
    std::cout<<"HSL native controls and lower-control access passed="<<ok<<'\n';return ok;
}
}
