#include "Raw/RawZoneArea.h"
#include "Raw/RawRecipeCompatibility.h"
#include "Editor/RawZoneAreaHistory.h"
#include "Editor/Internal/RawLab/RawLabAreaMask.h"
#include "Renderer/Internal/RawZoneAreaRenderer.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Stack::RawRecipe;
namespace {
void Check(bool ok,const char* why) {if (!ok) throw std::runtime_error(why);}
void Near(float a,float b,float tolerance,const char* why) {Check(std::isfinite(a) && std::abs(a-b)<=tolerance,why);}
ImageGuide Guide(int w,int h,const std::function<std::array<float,3>(int,int)>& color) {
    ImageGuide result;result.width=w;result.height=h;
    for (int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const auto p=WorkingRgbToColorWarpCoordinate(color(x,y),Raw::RawWorkingSpace::LinearSrgbD65);
        result.pixels.push_back({p.a,p.b,p.sceneEv});
    }
    return result;
}
std::array<float,3> Gray(float value) {return {value,value,value};}
RawZoneArea Area(float aspect=2) {
    RawZoneArea a;a.id="edge-area";a.sourceAspect=aspect;
    RawZoneBrushStroke s;s.radius=.36f;s.softness=0;s.followEdges=true;s.path={{.42f,.5f}};
    a.strokes={s};return a;
}
float At(const std::vector<float>& mask,int w,int h,float u,float v) {
    Check(mask.size()==std::size_t(w)*h,"mask dimensions");
    return mask[std::size_t(h-1-std::clamp(int(v*h),0,h-1))*w+std::clamp(int(u*w),0,w-1)];
}
std::vector<float> Mask(const RawZoneArea& a,const ImageGuide& g,const RawCropRotationRecipe& c={}) {
    return RasterizeZoneArea(a,g.width,g.height,c,{},&g);
}
void GeometryAndBoundaryTests() {
    constexpr int w=256,h=128;
    auto a=Area();
    const auto uniform=Guide(w,h,[](int,int){return Gray(.1f);});
    auto soft=a;soft.strokes[0].followEdges=false;
    Check(Mask(a,uniform)==RasterizeZoneArea(soft,w,h,{}),"uniform guided brush preserves soft geometry");
    auto split=Guide(w,h,[=](int x,int){return Gray(x<w/2 ? .1f : .8f);});
    const auto guided=Mask(a,split);
    Near(At(guided,w,h,.43f,.5f),1,1e-5f,"painted side remains selected");
    Near(At(guided,w,h,.55f,.5f),0,1e-5f,"brightness edge stops spill inside brush");
    Near(At(guided,w,h,.1f,.5f),0,1e-5f,"matching content outside footprint excluded");
    auto colors=Guide(w,h,[=](int x,int) {
        return x<w/2 ? std::array<float,3>{.3f,0,0} : std::array<float,3>{0,.3f*.2126729f/.7151522f,0};
    });
    Near(colors.pixels[0].sceneEv,colors.pixels[w-1].sceneEv,1e-5f,"color fixture has equal luminance");
    Near(At(Mask(a,colors),w,h,.55f,.5f),0,1e-5f,"equal-luminance color boundary stops spill");
    a.strokes[0].path.push_back({.65f,.5f});
    const auto gap=Guide(w,h,[=](int x,int){return Gray(x==w/2 ? .0001f : .1f);});
    Near(At(Mask(a,gap),w,h,.6f,.5f),0,1e-5f,"one-pixel gap separates identical regions despite later path points");
    auto other=Area();other.id="other-area";other.strokes[0].path={{.6f,.5f}};
    const auto otherMask=Mask(other,gap);
    Near(At(otherMask,w,h,.6f,.5f),1,1e-5f,"second area independently selects equally bright region");
    Near(At(otherMask,w,h,.43f,.5f),0,1e-5f,"second area does not cross back into first region");
    Near(At(Mask(a,split),w,h,.6f,.5f),0,1e-5f,"stroke does not learn background after crossing edge");
    a.strokes[0].softness=1;
    const auto feather=Mask(a,split);
    soft=a;soft.strokes[0].followEdges=false;
    const auto envelope=RasterizeZoneArea(soft,w,h,{});
    for(std::size_t i=0;i<feather.size();++i) Check(feather[i]>=0 && feather[i]<=envelope[i]+1e-6f,"guidance cannot expand or harden footprint");
    a.strokes.push_back(a.strokes.front());
    Check(Mask(a,split)==feather,"repeated assisted Add does not build feather");
    const auto texture=Guide(w,h,[](int x,int y){return Gray(.1f*std::exp2((x+y)%2 ? .12f : -.12f));});
    a=Area();
    Near(At(Mask(a,texture),w,h,.48f,.5f),1,.02f,"fine texture does not fragment selection");
    const auto slope=Guide(w,h,[=](int x,int){return Gray(.05f*std::exp2(2.0f*x/w));});
    Near(At(Mask(a,slope),w,h,.53f,.5f),1,.03f,"gradual shading remains within region");
    const auto black=Guide(w,h,[](int,int){return Gray(0);});
    Near(At(Mask(a,black),w,h,.43f,.5f),1,1e-5f,"zero luminance is valid guide content");
    auto invalid=uniform;invalid.pixels[w*h/2+w/2].sceneEv=std::numeric_limits<float>::quiet_NaN();
    const auto invalidMask=Mask(a,invalid);
    Check(std::all_of(invalidMask.begin(),invalidMask.end(),[](float v){return std::isfinite(v);}),"nonfinite guide cannot poison mask");
    Check(RasterizeZoneArea(a,w,h,{}).empty(),"missing guidance must not silently paint a soft mask");
    int cancellations=0;
    Check(RasterizeZoneArea(a,w,h,{},[&]{return ++cancellations>12;},&uniform).empty() && cancellations>12,"cancel unfinished guided mask");
    a.strokes[0].opacity=0;
    const auto empty=Mask(a,uniform);
    Check(std::all_of(empty.begin(),empty.end(),[](float v){return v==0;}),"empty footprint has no selected region");
}
void CorrectionAndRecipeTests() {
    auto a=Area();
    const auto guide=Guide(256,128,[](int,int){return Gray(.1f);});
    RawZoneBrushStroke erase;erase.erase=true;erase.softness=0;erase.radius=.06f;erase.path={{.48f,.5f}};
    a.strokes.push_back(erase);
    a.strokes.push_back(a.strokes.front());
    Near(At(Mask(a,guide),256,128,.48f,.5f),0,1e-6f,"assistance preserves manual Erase correction");
    erase.erase=false;a.strokes.push_back(erase);
    Near(At(Mask(a,guide),256,128,.48f,.5f),1,1e-6f,"manual Add repairs manual Erase");
    auto guidedErase=a.strokes.front();guidedErase.erase=true;a.strokes.push_back(guidedErase);
    Near(At(Mask(a,guide),256,128,.48f,.5f),1,1e-6f,"assistance preserves manual Add correction");
    auto recipe=MakeDefaultRecipe("guided.dng");recipe.localRange.areas={a};
    const auto saved=SerializeRecipe(recipe);
    const auto reopened=DeserializeRecipe(nlohmann::json::parse(saved.dump()));
    Check(SerializeRecipe(reopened)==saved,"guided settings and ordered corrections survive serialization");
    Check(Mask(reopened.localRange.areas.front(),guide)==Mask(a,guide),"reopened guided area regenerates same coverage");
    auto edited=a;edited.strokes.front().edgeSensitivity=.9f;
    Check(ZoneAreaMaskFingerprint(a)!=ZoneAreaMaskFingerprint(edited),"sensitivity invalidates mask cache");
    edited.strokes.front().followEdges=false;
    const auto disabled=DeserializeZoneAreas(SerializeZoneAreas({edited}));
    Check(!disabled[0].strokes[0].followEdges && disabled[0].strokes[0].edgeSensitivity==.9f,
        "disabled guidance retains its sensitivity after reopening");
    edited=a;edited.offsetEv=3;edited.points[0].deltaEv=2;
    Check(ZoneAreaMaskFingerprint(a)==ZoneAreaMaskFingerprint(edited),"gain and graph preserve guided mask cache");
    auto legacy=recipe;legacy.localRange.areas={Area()};legacy.localRange.areas[0].strokes[0].followEdges=false;
    auto legacyDocument=SerializeRecipe(legacy);legacyDocument["rawRecipeVersion"]=23;
    Check(IsCanonicalRawRecipeDocument(legacyDocument),"version 23 soft area document remains canonical");
    const auto legacyArea=DeserializeRecipe(legacyDocument).localRange.areas.front();
    Check(!legacyArea.strokes[0].followEdges,"old strokes remain ordinary soft painting");
    Stack::Editor::RawZoneAreaHistory history;history.Initialize({a});
    history.Observe({edited},true);history.Observe({edited},false);
    Check(SerializeZoneAreas(history.Undo())==SerializeZoneAreas({a}),"undo restores guided recipe exactly");
    Check(SerializeZoneAreas(history.Redo())==SerializeZoneAreas({edited}),"redo restores guided edit exactly");
    history.Observe({},true);history.Cancel();
    Check(SerializeZoneAreas(history.Undo())==SerializeZoneAreas({a}),"cancel adds no guided history entry");
    auto restored=Area();restored.strokes[0].followEdges=false;
    Check(RasterizeZoneArea(restored,256,128,{})==Mask(Area(),guide),"disabling guidance restores authored soft footprint");
}
void TransformAndResolutionTests() {
    auto a=Area();
    const auto baselineGuide=Guide(256,128,[=](int x,int){return Gray(x<128 ? .1f : .8f);});
    const auto baseline=Mask(a,baselineGuide);
    for(int rotation:{0,90,180,270}) for(bool flip:{false,true}) {
        RawCropRotationRecipe transform;transform.rotationDegrees=rotation;transform.flipHorizontally=flip;
        transform.cropEnabled=true;transform.cropX=.1f;transform.cropY=.15f;transform.cropWidth=.75f;transform.cropHeight=.7f;
        const bool rotated=rotation==90 || rotation==270;
        const int w=rotated?128:256,h=rotated?256:128;
        const auto guide=Guide(w,h,[&](int x,int y) {
            const auto p=ZoneAreaSourcePoint((x+.5f)/w,(y+.5f)/h,transform,false);
            return Gray(p.u<.5f ? .1f : .8f);
        });
        const auto mask=Mask(a,guide,transform);
        for(const auto point:{RawZoneBrushPoint{.42f,.5f},{.55f,.5f},{.1f,.5f}}) {
            const auto d=ZoneAreaDisplayPoint(point.u,point.v,transform,false);
            Near(At(mask,w,h,d.u,d.v),At(baseline,256,128,point.u,point.v),.001f,"guided mask follows source through rotation/flip");
        }
    }
    for(int w:{256,1024,4096}) {
        const int h=w/4;
        auto area=Area(4);area.strokes[0].path={{.46f,.5f},{.55f,.5f}};
        const auto guide=Guide(w,h,[&](int x,int){return Gray(x==w/2 ? .0001f : .1f);});
        const auto start=std::chrono::steady_clock::now();
        const auto mask=Mask(area,guide);
        Near(At(mask,w,h,.47f,.5f),1,.001f,"native foreground coverage");
        Near(At(mask,w,h,.53f,.5f),0,.001f,"native single-pixel boundary retained beyond proxy limit");
        std::cout<<"Guided brush "<<w<<"x"<<h<<": "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
    }
}
}

void ValidateRawZoneGuidanceCpu() {
    GeometryAndBoundaryTests();CorrectionAndRecipeTests();TransformAndResolutionTests();
    std::cout<<"Zones guided brush CPU validation passed\n";
}

void ValidateRawZoneGuidanceGpu() {
    using namespace Stack::Renderer;
    constexpr int w=128,h=64;
    std::vector<float> rgb(std::size_t(w)*h*4,1);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) for(int c=0;c<3;++c) rgb[(std::size_t(y)*w+x)*4+c]=x<w/2 ? .1f : .8f;
    const GLState::PixelUnpackState unpack;unpack.ConfigureTightCpuUpload();
    ScopedGLTexture reference(GLHelpers::CreateStorageTexture(w,h,GL_RGBA32F));
    glBindTexture(GL_TEXTURE_2D,reference.Get());glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_FLOAT,rgb.data());unpack.Restore();
    RawZoneAreaRenderer renderer;
    auto recipe=MakeDefaultRecipe("guided.dng");recipe.localRange.enabled=true;recipe.localRange.areas={Area()};recipe.localRange.areas[0].offsetEv=1;
    std::vector<RawZoneAreaStatistics> stats;
    ScopedGLTexture output(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,true,true,stats,{},100));
    Check(output.Get()!=0 && stats.size()==1 && stats[0].maskPreview,"guided renderer publishes actual mask");
    auto read=[&] {
        std::vector<float> pixels(rgb.size());const GLState::PixelPackState pack;pack.ConfigureTightCpuReadback();
        glBindTexture(GL_TEXTURE_2D,output.Get());glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());pack.Restore();return pixels;
    };
    const auto pixels=read();
    Near(pixels[(32*w+55)*4],.2f,1e-5f,"guided gain applied inside region");
    Near(pixels[(32*w+70)*4],.8f,1e-5f,"guided gain leaves adjacent background untouched");
    auto second=Area();second.id="second";second.strokes[0].path={{.58f,.5f}};second.offsetEv=2;
    recipe.localRange.areas.push_back(second);
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,true,true,stats,{},100));
    const auto twoAreas=read();
    Near(twoAreas[(32*w+55)*4],.2f,1e-5f,"first guided area keeps its own EV");
    Near(twoAreas[(32*w+70)*4],3.2f,1e-5f,"second guided area has independent EV despite overlapping brush footprints");
    recipe.localRange.areas.resize(1);
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,true,true,stats,{},100));
    const auto masks=renderer.MaskBuildCount(),guides=renderer.GuideBuildCount();
    const auto acceptedMask=stats[0].maskPreview;
    recipe.preToneExposureEv=3;recipe.localRange.areas[0].offsetEv=-2;
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,true,true,stats,{},100));
    Check(renderer.MaskBuildCount()==masks && renderer.GuideBuildCount()==guides,"EV dragging reuses image guide and mask");
    Check(stats[0].maskPreview==acceptedMask,"gain edit keeps exact accepted coverage");
    RawDevelopmentGraphScopeReadback scope;scope.valid=true;scope.stage=RawDevelopmentGraphScopeStage::LocalRangeInput;
    scope.zoneGuide=renderer.PreviewGuide();scope.zoneAreas=stats;
    Stack::EditorModuleTypes::RawZoneAreaUiState ui;
    using namespace Stack::Editor::RawLabInternal;
    Check(RawLabAreaGuideReady(recipe,scope),"neutral guide survives global EV edit");
    const auto overlay=ResolveRawLabAreaMask(ui,recipe.localRange.areas[0],recipe,scope);
    Check(overlay==acceptedMask,"settled overlay uses rendered mask");
    Near(SampleRawLabAreaMask(*overlay,.55f,.5f,{}),0,1e-6f,"hit testing cannot select rejected background");
    ui.showMask=false;ui.showCursor=false;
    Check(ResolveRawLabAreaMask(ui,recipe.localRange.areas[0],recipe,scope)==overlay,"overlay/cursor visibility leaves coverage untouched");
    auto different=recipe;different.source.sourcePath="other.dng";
    Check(!RawLabAreaGuideReady(different,scope) && ResolveRawLabAreaMask(ui,different.localRange.areas[0],different,scope)!=overlay,"reject stale source guidance");
    different=recipe;different.cropRotation.rotationDegrees=90;
    Check(!RawLabAreaGuideReady(different,scope),"reject stale orientation guidance");
    // Reuse the same texture name with new image contents and a new revision.
    for(std::size_t i=0;i<rgb.size();++i) if(i%4!=3) rgb[i]=.1f;
    glBindTexture(GL_TEXTURE_2D,reference.Get());glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_FLOAT,rgb.data());
    recipe.localRange.areas[0].offsetEv=1;
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,true,true,stats,{},101));
    Check(renderer.MaskBuildCount()==masks+1 && renderer.GuideBuildCount()==guides+1,"new image revision invalidates guide and mask");
    Near(read()[(32*w+70)*4],.2f,1e-5f,"new source changes guidance despite reused GL name");
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,true,true,stats,[]{return true;},102));
    Check(!output && stats.empty(),"cancelled render publishes no partial mask");
    {
        constexpr int bigWidth=1024,bigHeight=128;
        std::vector<float> large(std::size_t(bigWidth)*bigHeight*4,.1f);
        ScopedGLTexture full(GLHelpers::CreateStorageTexture(bigWidth,bigHeight,GL_RGBA32F));
        glBindTexture(GL_TEXTURE_2D,full.Get());glTexSubImage2D(GL_TEXTURE_2D,0,0,0,bigWidth,bigHeight,GL_RGBA,GL_FLOAT,large.data());
        auto soft=recipe;soft.localRange.areas[0].strokes[0].followEdges=false;
        const auto wasScissor=glIsEnabled(GL_SCISSOR_TEST);GLint box[4];glGetIntegerv(GL_SCISSOR_BOX,box);
        glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
        output.Reset(renderer.Render(full.Get(),full.Get(),bigWidth,bigHeight,soft,true,true,stats,{},200));
        Check(output && glIsEnabled(GL_SCISSOR_TEST),"guide readback restores scissor state");
        glScissor(box[0],box[1],box[2],box[3]);if (!wasScissor) glDisable(GL_SCISSOR_TEST);
        const auto preview=renderer.PreviewGuide();
        Check(preview && preview->width==768,"first soft area prepares bounded neutral guide");
        Near(preview->pixels.back().sceneEv,ZoneAreaReferenceEv(.1f),1e-5f,"preview guide blit ignores unrelated scissor rectangle");
    }
    Check(glGetError()==GL_NO_ERROR,"guided renderer preserves GL state");
    std::cout<<"Zones guided brush GPU and presentation validation passed\n";
}
