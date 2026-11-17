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
    glUseProgram(program);
    const auto integer=[&](const char* name,int value){glUniform1i(glGetUniformLocation(program,name),value);};
    integer("uPinCount",0);integer("uHslModel",1);integer("uUseSpatialMasks",0);integer("uHslSelections",5);
    glUniform1f(glGetUniformLocation(program,"uStrength"),1);
    int cases=0;
    for(bool rec2020:{false,true})for(int mode=0;mode<6;++mode) {
        RawColorWarpRecipe r;r.version=6;for(auto& p:r.pins){p.hueShift=18;p.saturation=.4f;p.brightness=.15f;}
        HslSelection a;a.id="a";a.allColors=true;
        if(mode==0)a.influence=1;
        if(mode==1){r.limitHslToSelections=true;a.bands={{-8,-3,.3f},{2,8,.3f}};}
        if(mode==2){a.influence=0;}
        if(mode==3){HslDetachChannel(a,1,r);for(auto& v:a.values)v[1]=-.6f;a.own[2]=true;for(auto& v:a.values)v[2]=.25f;}
        if(mode==4){a.areaEnabled=true;a.outline={{.05f,.05f},{.85f,.05f},{.05f,.85f}};a.areaSoftness=.15f;a.influence=0;}
        r.hslSelections={a};
        if(mode==5){a.id="b";a.influence=.2f;a.excludedBands={{-.5f,.5f,.3f}};r.hslSelections.push_back(a);}
        const auto coefficients=PrepareColorWarpHsl(r);glUniform4fv(glGetUniformLocation(program,"uHsl[0]"),8,coefficients.front().data());
        integer("uInputIsRec2020",rec2020);integer("uHslSelectionCount",int(r.hslSelections.size()));integer("uHslLimit",r.limitHslToSelections);
        const auto packed=PackHslSelections(r);Renderer::ScopedGLTexture data(GLHelpers::CreateTextureFromData(packed.data(),kHslDataWidth,int(r.hslSelections.size()),GL_RGBA32F,GL_RGBA,GL_FLOAT));
        glActiveTexture(GL_TEXTURE0+5);glBindTexture(GL_TEXTURE_2D,data.Get());
        const auto space=rec2020?Raw::RawWorkingSpace::LinearRec2020D65:Raw::RawWorkingSpace::LinearSrgbD65;
        for(const auto input:{std::array<float,4>{.004f,.0012f,.0008f,1},std::array<float,4>{.4f,.12f,.08f,1},std::array<float,4>{8,2,.3f,.5f}})for(bool regional:{false,true}) {
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
    for(int capture=0;capture<2;++capture) {
    for(int frame=0;frame<3;++frame) {
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({12,12});ImGui::SetNextWindowSize({430,830});
        ImGui::Begin("HSL selections",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
        if(capture==1 && frame==0)ImGui::SetScrollY(ImGui::GetScrollMaxY());
        Editor::DrawHslPanelControls(c);Editor::DrawHslSelections(c);
        if(capture==1 && frame==2)ok &= ImGui::IsItemVisible();
        ImGui::End();ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,720,860);glClearColor(.08f,.08f,.08f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    std::vector<unsigned char> pixels(720*860*4);glReadPixels(0,0,720,860,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for(int y=0;y<430;++y)for(int x=0;x<720*4;++x)std::swap(pixels[y*720*4+x],pixels[(859-y)*720*4+x]);
    std::filesystem::create_directories("outputs/validation/hsl-selections");
    ok &= stbi_write_png(capture==0?"outputs/validation/hsl-selections/controls.png":"outputs/validation/hsl-selections/selection.png",720,860,4,pixels.data(),720*4)!=0;
    }
    std::cout<<"HSL native controls and lower-control access passed="<<ok<<'\n';return ok;
}
}
