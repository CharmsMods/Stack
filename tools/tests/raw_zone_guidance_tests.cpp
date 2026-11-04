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
void ColorConfidenceTests() {
    constexpr int w=128,h=64;
    const auto area=Area();
    auto colors=Guide(w,h,[=](int x,int) {
        return x<w/2 ? std::array<float,3>{.3f,0,0} : std::array<float,3>{0,.3f*.2126729f/.7151522f,0};
    });
    const auto original=Mask(area,colors);
    colors.colorConfidence.assign(colors.pixels.size(),1);
    Check(Mask(area,colors)==original,"full color confidence preserves existing native boundary");
    for (int y=0;y<h;++y) for (int x=w/2;x<w;++x) colors.colorConfidence[std::size_t(y)*w+x]=0;
    Near(At(Mask(area,colors),w,h,.55f,.5f),1,1e-6f,"uncertain chroma alone cannot create a hard boundary");
    auto brightness=Guide(w,h,[=](int x,int){return Gray(x<w/2 ? .1f : .8f);});
    brightness.colorConfidence.assign(brightness.pixels.size(),0);
    Near(At(Mask(area,brightness),w,h,.55f,.5f),0,1e-6f,"weak color retains independent luminance barrier");
    auto noisy=Guide(w,h,[](int,int){return Gray(.1f);});
    const auto clean=Mask(area,noisy);
    noisy.colorConfidence.assign(noisy.pixels.size(),1);
    const auto seed=std::size_t(h/2)*w+int(area.strokes[0].path[0].u*w);
    noisy.pixels[seed].a+=100; noisy.pixels[seed].b-=100;
    noisy.colorConfidence[seed]=0;
    Check(Mask(area,noisy)==clean,"uncertain seed color does not fragment otherwise coherent coverage");
    noisy.colorConfidence[seed]=std::numeric_limits<float>::quiet_NaN();
    Check(Mask(area,noisy)==clean,"nonfinite confidence cannot create a color barrier");
    noisy.colorConfidence.pop_back();
    Check(!noisy.Valid() && Mask(area,noisy).empty(),"malformed evidence dimensions are unavailable");
    auto thin=Guide(w,h,[=](int x,int) {
        return x==w/2 ? std::array<float,3>{.3f,0,0} : Gray(.3f*.2126729f);
    });
    const auto nativeThin=Mask(area,thin);
    thin.colorConfidence.assign(thin.pixels.size(),1);
    Check(Mask(area,thin)==nativeThin,"supported one-pixel chroma boundary retains native geometry");
    Near(At(nativeThin,w,h,.55f,.5f),0,1e-6f,"one-pixel color boundary separates equal-luminance regions");
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
    auto soft=recipe;soft.localRange.areas={Area()};soft.localRange.areas[0].strokes[0].followEdges=false;
    const auto softDocument=SerializeRecipe(soft);
    auto obsoleteDocument=softDocument;obsoleteDocument["rawRecipeVersion"]=23;
    Check(!IsCanonicalRawRecipeDocument(obsoleteDocument),"obsolete version 23 soft area document is rejected");
    Check(IsCanonicalRawRecipeDocument(softDocument),"current soft area document remains canonical");
    const auto softArea=DeserializeRecipe(softDocument).localRange.areas.front();
    Check(!softArea.strokes[0].followEdges,"current soft strokes remain ordinary soft painting");
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
    GeometryAndBoundaryTests();ColorConfidenceTests();CorrectionAndRecipeTests();TransformAndResolutionTests();
    std::cout<<"Zones guided brush CPU validation passed\n";
}

void ValidateRawZoneColorGuidanceGpu() {
    using namespace Stack::Renderer;
    constexpr int w=65,h=32,cw=7,ch=4;
    const float gray=.3f*.2126729f;
    std::vector<float> referencePixels(std::size_t(w)*h*4,1),colorPixels(std::size_t(cw)*ch*4,1);
    std::vector<float> confidence(std::size_t(cw)*ch,1);
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) for (int c=0;c<3;++c)
        referencePixels[(std::size_t(y)*w+x)*4+c]=x==w/2 ? (c==0 ? .3f : 0) : gray;
    for (int y=0;y<ch;++y) for (int x=0;x<cw;++x) {
        const double left=double(x)*w/cw,right=double(x+1)*w/cw;
        const float fraction=float(std::max(0.0,std::min(right,double(w/2+1))-std::max(left,double(w/2)))/(right-left));
        for (int c=0;c<3;++c) colorPixels[(std::size_t(y)*cw+x)*4+c]=
            gray*(1-fraction)+(c==0 ? .3f : 0)*fraction;
    }
    const GLState::PixelUnpackState unpack;unpack.ConfigureTightCpuUpload();
    ScopedGLTexture reference(GLHelpers::CreateStorageTexture(w,h,GL_RGBA32F));
    ScopedGLTexture color(GLHelpers::CreateStorageTexture(cw,ch,GL_RGBA32F));
    ScopedGLTexture trust(GLHelpers::CreateStorageTexture(cw,ch,GL_R32F));
    const auto upload=[&](unsigned int texture,int width,int height,GLenum format,const float* pixels) {
        glBindTexture(GL_TEXTURE_2D,texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,format,GL_FLOAT,pixels);
    };
    upload(reference.Get(),w,h,GL_RGBA,referencePixels.data());
    upload(color.Get(),cw,ch,GL_RGBA,colorPixels.data());
    upload(trust.Get(),cw,ch,GL_RED,confidence.data());
    const auto readMask=[&](unsigned int texture) {
        Check(texture!=0,"color-guided mask is available");
        std::vector<float> pixels(std::size_t(w)*h);
        const GLState::PixelPackState pack;pack.ConfigureTightCpuReadback();
        glBindTexture(GL_TEXTURE_2D,texture);glGetTexImage(GL_TEXTURE_2D,0,GL_RED,GL_FLOAT,pixels.data());
        pack.Restore();return pixels;
    };
    RawZoneAreaRenderer renderer;
    const auto area=Area();
    SceneColorGuidance evidence{color.Get(),trust.Get(),1};
    const auto native=readMask(renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,300,{}));
    const auto supported=readMask(renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,300,{},evidence));
    Check(supported==native,"compact color evidence retains supported native one-pixel boundary");
    Near(At(supported,w,h,.55f,.5f),0,1e-6f,"supported thin chroma fence does not leak");
    const auto guide=renderer.PreviewGuide();
    Check(guide && guide->Valid() && std::all_of(guide->colorConfidence.begin(),guide->colorConfidence.end(),
        [](float q){return q>.999f;}),"odd-sized native and compact footprints agree without shifted cells");
    const auto masks=renderer.MaskBuildCount(),guides=renderer.GuideBuildCount();
    renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,300,{},evidence);
    Check(renderer.MaskBuildCount()==masks && renderer.GuideBuildCount()==guides,"immutable color evidence reuses guide and mask");
    std::fill(confidence.begin(),confidence.end(),0);upload(trust.Get(),cw,ch,GL_RED,confidence.data());
    ++evidence.revision;
    const auto uncertain=readMask(renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,300,{},evidence));
    Near(At(uncertain,w,h,.55f,.5f),1,1e-6f,"uncertain color falls back to luminance without lowering coverage");
    Check(renderer.MaskBuildCount()==masks+1 && renderer.GuideBuildCount()==guides+1,"evidence revision invalidates derived guide and mask");
    std::fill(confidence.begin(),confidence.end(),1);upload(trust.Get(),cw,ch,GL_RED,confidence.data());
    for (std::size_t i=0;i<colorPixels.size();i+=4) {colorPixels[i]=0;colorPixels[i+1]=0;colorPixels[i+2]=.3f;}
    upload(color.Get(),cw,ch,GL_RGBA,colorPixels.data());++evidence.revision;
    const auto disagreement=readMask(renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,300,{},evidence));
    Near(At(disagreement,w,h,.55f,.5f),1,1e-6f,"inconsistent source color cannot authorize a native color barrier");
    const auto settledGuide=renderer.PreviewGuide();
    const auto settledBuilds=renderer.GuideBuildCount();
    int cancelledChecks=0;
    Check(!renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,303,
        [&]{return ++cancelledChecks>2;},evidence) && cancelledChecks>2,
        "cancellation during evidence preparation publishes no partial mask");
    Check(renderer.PreviewGuide()==settledGuide && renderer.GuideBuildCount()==settledBuilds,
        "cancelled evidence leaves the accepted guide intact");
    GLuint emptyName=0;glGenTextures(1,&emptyName);ScopedGLTexture emptyReference(emptyName);
    glBindTexture(GL_TEXTURE_2D,emptyReference.Get());
    GLint beforeRead=0,beforeDraw=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&beforeRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&beforeDraw);
    bool failedReadback=false;
    try {renderer.Coverage(area,emptyReference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,304,{},evidence);}
    catch (const std::runtime_error&) {failedReadback=true;}
    GLint afterRead=0,afterDraw=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&afterRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&afterDraw);
    Check(failedReadback && beforeRead==afterRead && beforeDraw==afterDraw &&
        renderer.PreviewGuide()==settledGuide && renderer.GuideBuildCount()==settledBuilds,
        "unavailable reference readback restores state and preserves the accepted guide");
    for (int y=0;y<h;++y) referencePixels[(std::size_t(y)*w+w/2)*4+3]=0;
    upload(reference.Get(),w,h,GL_RGBA,referencePixels.data());
    const auto hidden=readMask(renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,301,{}));
    Near(At(hidden,w,h,.55f,.5f),0,1e-6f,"hidden coverage never bridges a guided mask");
    Check(!std::isfinite(renderer.PreviewGuide()->pixels[std::size_t(h/2)*w+w/2].sceneEv),"zero alpha is not valid guide color");
    for (int y=0;y<h;++y) referencePixels[(std::size_t(y)*w+w/2)*4+3]=std::numeric_limits<float>::quiet_NaN();
    upload(reference.Get(),w,h,GL_RGBA,referencePixels.data());
    renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,302,{});
    Check(!std::isfinite(renderer.PreviewGuide()->pixels[std::size_t(h/2)*w+w/2].sceneEv),"nonfinite alpha is not valid guide color");
    bool rejected=false;
    try {renderer.Coverage(area,reference.Get(),w,h,Raw::RawWorkingSpace::LinearSrgbD65,302,{},SceneColorGuidance{color.Get(),0,4});}
    catch (const std::runtime_error&) {rejected=true;}
    Check(rejected,"incomplete color guidance cannot silently change its contract");
    {
        constexpr int nativeWidth=1024,nativeHeight=64;
        std::vector<float> nativePixels(std::size_t(nativeWidth)*nativeHeight*4,.1f);
        for (std::size_t i=3;i<nativePixels.size();i+=4) nativePixels[i]=1;
        ScopedGLTexture large(GLHelpers::CreateStorageTexture(nativeWidth,nativeHeight,GL_RGBA32F));
        upload(large.Get(),nativeWidth,nativeHeight,GL_RGBA,nativePixels.data());
        std::fill(confidence.begin(),confidence.end(),0);upload(trust.Get(),cw,ch,GL_RED,confidence.data());
        ++evidence.revision;
        Check(renderer.Coverage(area,large.Get(),nativeWidth,nativeHeight,Raw::RawWorkingSpace::LinearSrgbD65,305,{},evidence)!=0,
            "native color-guided coverage is available above preview size");
        const auto preview=renderer.PreviewGuide();
        Check(preview && preview->width==768 && preview->height==48 && preview->revision!=settledGuide->revision &&
            preview->colorConfidence.size()==preview->pixels.size() &&
            std::all_of(preview->colorConfidence.begin(),preview->colorConfidence.end(),[](float q){return q==0;}),
            "bounded preview retains native evidence and revision");
        const auto builds=renderer.GuideBuildCount();
        renderer.Coverage(area,large.Get(),nativeWidth,nativeHeight,Raw::RawWorkingSpace::LinearSrgbD65,305,{},evidence);
        Check(renderer.PreviewGuide()==preview && renderer.GuideBuildCount()==builds,
            "native cache hit retains the same preview evidence object");
    }
    unpack.Restore();
    Check(glGetError()==GL_NO_ERROR,"color guidance readback preserves valid GL execution");
    std::cout<<"Zones source-color confidence GPU validation passed\n";
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
    ValidateRawZoneColorGuidanceGpu();
    Check(glGetError()==GL_NO_ERROR,"guided renderer preserves GL state");
    std::cout<<"Zones guided brush GPU and presentation validation passed\n";
}
