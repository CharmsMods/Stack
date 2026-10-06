#include "Editor/Internal/RawLab/RawLabAreaImage.h"
#include "Editor/Internal/RawLab/RawLabAreaMask.h"
#include "Editor/RawZoneAreaPreview.h"
#include "Raw/RawZoneAreaRasterizer.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <imgui.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Stack;
using namespace Stack::RawRecipe;
using namespace Stack::Editor::RawLabInternal;
void Check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
using Clock=std::chrono::steady_clock;
std::shared_ptr<ImageGuide> Guide(const RawDevelopmentRecipe& recipe,int width=256) {
    auto guide=std::make_shared<ImageGuide>();guide->width=width;guide->height=width/2;
    guide->revision=42;
    guide->recipeFingerprint=Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,0).neutralPlacement;
    guide->pixels.resize(std::size_t(guide->width)*guide->height);
    for (int y=0;y<guide->height;++y) for (int x=0;x<guide->width;++x)
        guide->pixels[std::size_t(y)*guide->width+x]={0,0,x<width/2 ? -2.0f : 2.0f};
    return guide;
}
RawZoneArea Area(int strokes=1) {
    RawZoneArea area;area.id="brush-fixture";area.sourceAspect=2;
    for (int i=0;i<strokes;++i) {
        RawZoneBrushStroke stroke;stroke.radius=.06f;stroke.followEdges=true;
        stroke.path={{.3f,.1f+float(i%10)*.08f},{.45f,.1f+float(i%10)*.08f}};
        if (i%3==1) {stroke.erase=true;stroke.followEdges=false;stroke.radius=.02f;}
        area.strokes.push_back(stroke);
    }
    return area;
}
Editor::RawZoneAreaPreview::Result Await(Editor::RawZoneAreaPreview& worker,std::size_t fingerprint) {
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    while (Clock::now()<deadline) {
        auto result=worker.Latest();
        if (result.mask && result.mask->maskFingerprint==fingerprint && !worker.Pending()) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Mask preview worker did not converge to latest input");
}
void CacheTests() {
    auto recipe=MakeDefaultRecipe("cache.dng");auto guide=Guide(recipe);auto area=Area(40);
    ZoneAreaRasterizer cache;
    auto evaluate=[&] {return cache.Evaluate(area,guide->width,guide->height,{},guide.get());};
    Check(evaluate()!=nullptr,"initial cached mask");
    const auto first=cache.StrokeEvaluations();
    Check(first==40,"initial stroke replay count");
    area.offsetEv=3;Check(evaluate() && cache.StrokeEvaluations()==first,"gain edit rerasterized coverage");
    const auto segments=cache.GeometrySegments();
    area.strokes.back().path.push_back({.49f,.9f});
    const auto* changed=evaluate();
    Check(changed && cache.StrokeEvaluations()==first+1,"growing stroke replayed earlier strokes");
    Check(cache.GeometrySegments()==segments+2,"growing stroke rerasterized earlier path segments");
    Check(*changed==RasterizeZoneArea(area,guide->width,guide->height,{}, {},guide.get()),"cached stroke differs from complete replay");
    auto added=area.strokes.back();added.path={{.4f,.5f}};area.strokes.push_back(added);
    changed=evaluate();Check(changed && cache.StrokeEvaluations()==first+2,"appending stroke replayed earlier strokes");
    Check(*changed==RasterizeZoneArea(area,guide->width,guide->height,{}, {},guide.get()),"cached add/erase order changed mask");
    area.strokes.back().path.push_back({.42f,.5f});
    int checks=0;
    Check(!cache.Evaluate(area,guide->width,guide->height,{},guide.get(),[&]{return ++checks>5;}),"cancelled last stroke published partial coverage");
    const auto afterCancel=cache.StrokeEvaluations();
    changed=evaluate();Check(changed && cache.StrokeEvaluations()==afterCancel+1,"cancel discarded completed stroke cache");
    area.strokes[0].edgeSensitivity=.1f;
    Check(*evaluate()==RasterizeZoneArea(area,guide->width,guide->height,{}, {},guide.get()),"earlier edit reused stale stroke prefix");
    // A long stroke should only rasterize its newly appended capsule.
    area=Area();area.strokes[0].path.clear();
    for (int i=0;i<1200;++i) area.strokes[0].path.push_back({.35f+.03f*std::sin(i*.01f),.5f+.05f*std::cos(i*.01f)});
    const auto start=Clock::now();Check(evaluate()!=nullptr,"long stroke rasterization failed");
    const auto initialMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    const auto beforeSegments=cache.GeometrySegments();
    area.strokes[0].path.push_back({.37f,.53f});const auto appendStart=Clock::now();
    Check(evaluate()!=nullptr && cache.GeometrySegments()==beforeSegments+2,"long stroke append replays existing path");
    std::cout<<"Zones 1,200-point stroke: "<<initialMs<<" ms initial, "
        <<std::chrono::duration<double,std::milli>(Clock::now()-appendStart).count()<<" ms append\n";
    Check(*evaluate()==RasterizeZoneArea(area,guide->width,guide->height,{}, {},guide.get()),"incremental footprint changed long-stroke feather");
    ++guide->revision;
    for (auto& p:guide->pixels) p.sceneEv=-2;
    Check(*evaluate()==RasterizeZoneArea(area,guide->width,guide->height,{}, {},guide.get()),"guide revision reused stale stroke prefix");
}
void WorkerTests() {
    auto recipe=MakeDefaultRecipe("worker.dng");auto area=Area(40);auto guide=Guide(recipe,512);
    Editor::RawZoneAreaPreview worker;
    const auto key=guide->recipeFingerprint;
    worker.Request(area,{},nullptr,key);
    auto result=Await(worker,ZoneAreaMaskFingerprint(area));
    Check(result.provisional,"cold guide did not allow provisional painting");
    worker.Request(area,{},guide,key);
    result=Await(worker,ZoneAreaMaskFingerprint(area));
    Check(!result.provisional,"guide arrival did not refine provisional mask");
    auto accepted=result.mask;
    double maxRequestMs=0;bool progressed=false;
    for (int i=0;i<100;++i) {
        area.strokes.back().path.push_back({.4f+.04f*std::sin(i*.1f),.5f+.04f*std::cos(i*.1f)});
        const auto start=Clock::now();worker.Request(area,{},guide,key);
        maxRequestMs=std::max(maxRequestMs,std::chrono::duration<double,std::milli>(Clock::now()-start).count());
        auto current=worker.Latest();progressed|=current.mask && current.mask!=accepted;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    result=Await(worker,ZoneAreaMaskFingerprint(area));
    Check(progressed,"continuous input starved mask publication");
    Check(result.mask->lastStrokePointCount==area.strokes.back().path.size(),"worker dropped final samples");
    Check(maxRequestMs<200,"UI request performed expensive mask work inline");
    const auto expected=RasterizeZoneArea(area,guide->width,guide->height,{}, {},guide.get());
    Check(result.mask->coverage==expected,"coalesced worker mask differs from final recipe");
    area.strokes.back().path.push_back({.55f,.8f});worker.Request(area,{},guide,key);
    auto restored=Area();worker.Request(restored,{},guide,key+1);
    result=Await(worker,ZoneAreaMaskFingerprint(restored));
    Check(result.referenceKey==key+1,"stale source/mask request replaced newer result");
    worker.Cancel();Check(!worker.Latest().mask,"cancel retained stale overlay");
    worker.Request(area,{},guide,key+2);worker.Cancel();
    const auto deadline=Clock::now()+std::chrono::seconds(5);
    while(worker.Pending() && Clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Check(!worker.Latest().mask,"cancelled in-flight job republished its mask");
    std::cout<<"Zones preview: 100 continuous updates, max UI request "<<maxRequestMs<<" ms\n";
}
struct ImGuiFixture {
    ImGuiContext* previous=ImGui::GetCurrentContext();
    ImGuiContext* context=ImGui::CreateContext();
    ImGuiFixture() {
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={640,400};io.DeltaTime=1.0f/60;
        io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    }
    ~ImGuiFixture() {ImGui::DestroyContext(context);ImGui::SetCurrentContext(previous);}
    void Begin() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({640,400});
        ImGui::Begin("Brush fixture",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize);
    }
    void End() {ImGui::End();ImGui::Render();}
};
void GestureTests() {
    ImGuiFixture fixture;auto& io=ImGui::GetIO();
    auto recipe=MakeDefaultRecipe("gestures.dng");recipe.localRange.areas={Area(0)};
    EditorModuleTypes::RawZoneAreaUiState ui;ui.selectedId=recipe.localRange.areas[0].id;ui.mode=1;ui.followEdges=true;
    ui.history.Initialize(recipe.localRange.areas);
    auto frame=[&] {
        fixture.Begin();const auto edit=InteractRawLabAreaImage(ui,recipe,{}, {20,20},{620,320});
        if(edit.cancelled) recipe.localRange.areas=ui.history.Cancel();
        else if(edit.changed || edit.finished) ui.history.Observe(recipe.localRange.areas,edit.active);
        fixture.End();return edit;
    };
    frame();frame();
    for (int i=0;i<40;++i) {
        io.AddMousePosEvent(100+i*3,120);frame();io.AddMouseButtonEvent(0,true);
        Check(frame().changed && ui.active,"guided click rejected while graph readback unavailable");
        io.AddMouseButtonEvent(0,false);Check(frame().finished && !ui.active,"click did not finish");
    }
    Check(recipe.localRange.areas[0].strokes.size()==40,"rapid dabs were dropped or merged");
    for (const auto& s:recipe.localRange.areas[0].strokes) Check(s.path.size()==1 && s.followEdges,"dab semantics changed while awaiting guide");
    io.AddMousePosEvent(200,170);frame();io.AddMouseButtonEvent(0,true);frame();
    ui.mode=0; // The active gesture must retain its original meaning.
    io.AddMousePosEvent(240,170);frame();
    io.AddMousePosEvent(242,170);io.AddMouseButtonEvent(0,false);auto release=frame();
    const auto& stroke=recipe.localRange.areas[0].strokes.back();
    Check(release.maskEdited && stroke.path.size()==3,"release endpoint or gesture mode was lost");
    Check(std::abs(stroke.path.back().u-.37f)<.00001f,"release endpoint did not map to source");
    Check(recipe.localRange.areas[0].offsetEv==0,"paint gesture changed exposure after mode change");
    const auto complete=recipe.localRange.areas;
    recipe.localRange.areas=ui.history.Undo();Check(recipe.localRange.areas[0].strokes.size()==40,"stroke undo split gesture");
    recipe.localRange.areas=ui.history.Redo();Check(EqualZoneAreas(recipe.localRange.areas,complete),"redo lost final stroke point");
    ui.mode=2;io.AddMouseButtonEvent(0,true);frame();io.AddMousePosEvent(700,180);frame();io.AddMouseButtonEvent(0,false);frame();
    Check(recipe.localRange.areas[0].strokes.back().erase && recipe.localRange.areas[0].strokes.back().path.back().u==1,"erase release outside image failed");
    const auto beforeCancel=recipe.localRange.areas;
    io.AddMousePosEvent(200,170);frame();io.AddMouseButtonEvent(0,true);frame();io.AddKeyEvent(ImGuiKey_Escape,true);
    Check(frame().cancelled,"Escape did not cancel painting");
    Check(EqualZoneAreas(recipe.localRange.areas,beforeCancel),"cancel retained painted samples");
    io.AddKeyEvent(ImGuiKey_Escape,false);io.AddMouseButtonEvent(0,false);frame();
    io.AddMouseButtonEvent(0,true);Check(frame().changed,"could not draw again after cancellation");io.AddMouseButtonEvent(0,false);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);
    fixture.Begin();const auto hiddenRelease=InteractRawLabAreaImage(ui,recipe,{}, {900,900},{1500,1200});fixture.End();
    Check(hiddenRelease.finished && !ui.active,"clipped image left painting stuck active");
    ui.history.Observe(recipe.localRange.areas,false);
    auto guide=Guide(recipe);RawDevelopmentGraphScopeReadback scope;
    scope.valid=true;scope.stage=RawDevelopmentGraphScopeStage::LocalRangeInput;scope.zoneGuide=guide;
    Check(RawLabAreaGuide(ui,recipe,scope)==guide,"did not acquire neutral guide");
    recipe.preToneExposureEv=3;
    Check(RawLabAreaGuide(ui,recipe,{})==guide,"graph readback loss/global EV discarded usable guide");
    auto stale=Guide(MakeDefaultRecipe("other.dng"));scope.zoneGuide=stale;
    Check(RawLabAreaGuide(ui,recipe,scope)==guide,"stale scope replaced valid guide");
    recipe.source.sourcePath="other.dng";
    Check(!RawLabAreaGuide(ui,recipe,{}),"new source reused old guide");
}
}
void ValidateRawZoneInteractionCpu() {
    CacheTests();WorkerTests();GestureTests();
    std::cout<<"Zones brush worker, cache, history and gesture validation passed\n";
}
void ValidateRawZoneInteractionGpu() {
    ImGuiFixture fixture;auto recipe=MakeDefaultRecipe("overlay.dng");recipe.localRange.enabled=true;
    recipe.localRange.areas={Area()};auto& area=recipe.localRange.areas[0];
    auto guide=Guide(recipe);RawDevelopmentGraphScopeReadback scope;
    scope.valid=true;scope.stage=RawDevelopmentGraphScopeStage::LocalRangeInput;scope.zoneGuide=guide;
    RawZoneAreaStatistics stats;stats.areaId=area.id;stats.maskFingerprint=ZoneAreaMaskFingerprint(area);stats.fullResolution=true;
    stats.maskPreview=MakeZoneAreaMaskPreview(RasterizeZoneArea(area,256,128,{}, {},guide.get()),256,128,stats.maskFingerprint,768,&area);
    scope.zoneAreas={stats};EditorModuleTypes::RawZoneAreaUiState ui;ui.selectedId=area.id;ui.showMask=true;
    auto frame=[&](const RawDevelopmentGraphScopeReadback& readback) {
        fixture.Begin();DrawRawLabAreaImage(ui,recipe,readback,{20,20},{620,320});fixture.End();
    };
    frame(scope);const auto texture=ui.overlayTexture;Check(texture!=0,"coverage overlay missing");
    auto read=[&] {
        Renderer::GLState::PixelPackState pack;pack.ConfigureTightCpuReadback();
        std::vector<unsigned char> pixels(std::size_t(ui.overlayWidth)*ui.overlayHeight*4);
        glBindTexture(GL_TEXTURE_2D,texture);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());pack.Restore();return pixels;
    };
    const auto mask=read();frame({});Check(ui.overlayTexture==texture && read()==mask,"scope refresh hid or changed overlay");
    ui.effectPreview=true;frame({});const auto effect=read();
    Check(effect!=mask && ui.overlayTexture==texture,"effect preview failed or recreated texture");
    for (std::size_t i=3;i<effect.size();i+=4) Check(effect[i]==0,"zero effect contains coverage");
    ui.active=true;ui.gestureMode=1;frame({});Check(read()==mask,"painting zero-gain area hid coverage in effect mode");
    ui.showMask=false;frame({});Check(read()==mask,"hiding overlay mutated its pixels");
    glDeleteTextures(1,&ui.overlayTexture);ui.overlayTexture=0;
    Check(glGetError()==GL_NO_ERROR,"brush overlay upload GL failure");
    std::cout<<"Zones overlay continuity and texture reuse validation passed\n";
}
