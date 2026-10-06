#include "Raw/RawZoneArea.h"
#include "Raw/RawRecipeCompatibility.h"
#include "Raw/RawEditAttributes.h"
#include "Editor/RawZoneAreaHistory.h"
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include "Renderer/Internal/RawZoneAreaRenderer.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation { bool ValidateRawZoneAreaGraph(); }
void ValidateRawZoneGuidanceCpu();
void ValidateRawZoneGuidanceGpu();
using namespace Stack::RawRecipe;
namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Near(float actual, float expected, float tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual-expected)>tolerance) {
        std::cerr << message << ": " << actual << " != " << expected << '\n';
        throw std::runtime_error(message);
    }
}
void ToneGraphViewTests() {
    using namespace Stack::Editor::RawLabInternal;
    RawLabGraphHistogram histogram;
    histogram.valid = true;
    constexpr std::size_t lumaFirstBin = 80;
    constexpr std::size_t lumaLastBin = 180;
    constexpr std::size_t redFirstBin = 40;
    constexpr std::size_t redLastBin = 60;
    for (std::size_t index = lumaFirstBin; index <= lumaLastBin; ++index) {
        histogram.luma[index] = 1.0f;
    }
    for (std::size_t index = redFirstBin; index <= redLastBin; ++index) {
        histogram.red[index] = 1.0f;
    }

    const RawLabToneGraphViewRange lumaFit =
        BuildRawLabToneGraphViewRange(histogram, 0, 1.0f);
    Near(lumaFit.minimum,
        static_cast<float>(lumaFirstBin) / static_cast<float>(kRawLabHistogramBinCount),
        0.0001f,
        "tone luma fit finds the first populated bin");
    Near(lumaFit.maximum,
        static_cast<float>(lumaLastBin + 1) / static_cast<float>(kRawLabHistogramBinCount),
        0.0001f,
        "tone luma fit finds the last populated bin");
    const RawLabToneGraphViewRange fullRange =
        BuildRawLabToneGraphViewRange(histogram, 0, 0.0f);
    Near(fullRange.minimum, 0.0f, 0.0001f,
        "tone graph zero zoom restores the authored minimum");
    Near(fullRange.maximum, 1.0f, 0.0001f,
        "tone graph zero zoom restores the authored maximum");
    const RawLabToneGraphViewRange redFit =
        BuildRawLabToneGraphViewRange(histogram, 1, 1.0f);
    Near(redFit.minimum,
        static_cast<float>(redFirstBin) / static_cast<float>(kRawLabHistogramBinCount),
        0.0001f,
        "tone red fit follows the active channel");
    Near(redFit.maximum,
        static_cast<float>(redLastBin + 1) / static_cast<float>(kRawLabHistogramBinCount),
        0.0001f,
        "tone red fit follows the active channel end");
    const RawLabToneGraphViewRange retainedRange =
        ApplyRawLabToneGraphViewZoom(lumaFit, 0.65f);
    Near(retainedRange.minimum, lumaFit.minimum * 0.65f, 0.0001f,
        "tone graph retains its fitted black coordinate during a scope refresh");
    Near(retainedRange.maximum,
        1.0f - (1.0f - lumaFit.maximum) * 0.65f,
        0.0001f,
        "tone graph retains its fitted white coordinate during a scope refresh");
    const RawLabGraphHistogram cropped =
        CropRawLabGraphHistogram(histogram, lumaFit);
    Check(cropped.luma.front() > 0.0f && cropped.luma.back() > 0.0f,
        "tone histogram crop maps image data to both graph edges");
}
RawZoneArea MakeArea() {
    RawZoneArea area; area.id="area-1"; area.name="Mountain"; area.sourceAspect=2;
    RawZoneBrushStroke stroke; stroke.radius=.3f; stroke.softness=0; stroke.path={{.25f,.5f}};
    area.strokes.push_back(stroke); return area;
}
void CpuTests() {
    auto area=MakeArea();
    Near(ZoneAreaCoverage(area,.25f,.5f),1,1e-6f,"painted object covered");
    Near(ZoneAreaCoverage(area,.75f,.5f),0,1e-6f,"same-brightness object outside mask excluded");
    area.strokes[0].softness=1;
    const float feather=ZoneAreaCoverage(area,.34f,.5f);
    area.strokes.push_back(area.strokes.front());
    Near(ZoneAreaCoverage(area,.34f,.5f),feather,1e-7f,"Add must not build feather strength");
    auto erase=area.strokes.front(); erase.erase=true; erase.radius=.1f; erase.softness=0;
    area.strokes.push_back(erase);
    Near(ZoneAreaCoverage(area,.25f,.5f),0,1e-7f,"Erase removes coverage");
    area.strokes.push_back(area.strokes.front());
    Near(ZoneAreaCoverage(area,.25f,.5f),1,1e-7f,"later Add can repair erased coverage");
    auto raster=RasterizeZoneArea(area,80,40,{});
    for(int y=0;y<40;++y) for(int x=0;x<80;++x)
        Near(raster[std::size_t(39-y)*80+x],ZoneAreaCoverage(area,(x+.5f)/80,(y+.5f)/40),2e-6f,"raster and hit-test parity");
    Check(RasterizeZoneArea(area,80,40,{},[]{return true;}).empty(),"cancel raster work");
    for(int degrees:{0,90,180,270}) for(bool flip:{false,true}) {
        RawCropRotationRecipe c; c.rotationDegrees=degrees;c.flipHorizontally=flip;c.flipVertically=!flip;
        c.cropEnabled=true;c.cropX=.1f;c.cropY=.15f;c.cropWidth=.7f;c.cropHeight=.65f;
        const auto d=ZoneAreaDisplayPoint(.25f,.5f,c,true);
        const auto p=ZoneAreaSourcePoint(d.u,d.v,c,true);
        Near(p.u,.25f,1e-6f,"crop rotation U round trip");Near(p.v,.5f,1e-6f,"crop rotation V round trip");
        const bool rotated=degrees==90||degrees==270;
        const int w=rotated?40:80,h=rotated?80:40;
        const auto rotatedMask=RasterizeZoneArea(area,w,h,c);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const auto source=ZoneAreaSourcePoint((x+.5f)/w,(y+.5f)/h,c,false);
            Near(rotatedMask[std::size_t(h-1-y)*w+x],ZoneAreaCoverage(area,source.u,source.v),2e-5f,"transformed mask alignment");
        }
    }
    area.points={{-5,-1},{-1,2},{4,0}};
    area.points[1].incoming={1,true,-.5f,.3f};area.points[1].outgoing={1,true,.75f,-.2f};
    area.offsetEv=1.5f;
    const auto old=area;
    const auto graph=ZoneAreaBezierPoints(area,-2,0,-4,5);
    StoreZoneAreaBezierPoints(area,graph,-2,0,-4,5);
    for(float ev=-8;ev<8;ev+=.1f) Near(ZoneAreaCurveGain(area,ev),ZoneAreaCurveGain(old,ev),2e-5f,"graph fitting preserves curve");
    area.offsetEv=8;
    for(float ev=-8;ev<8;ev+=.1f) Near(ZoneAreaCurveGain(area,ev),ZoneAreaCurveGain(old,ev),2e-5f,"offset preserves curve shape");
    Near(ZoneAreaCurveGain(area,-20),-1,1e-6f,"left endpoint extends");
    Near(ZoneAreaCurveGain(area,20),0,1e-6f,"right endpoint extends");
    RawDevelopmentRecipe recipe=MakeDefaultRecipe("test.dng"); recipe.localRange.enabled=true;recipe.localRange.areas={area};
    const auto saved=SerializeRecipe(recipe);
    const auto reopened=DeserializeRecipe(nlohmann::json::parse(saved.dump()));
    Check(SerializeRecipe(reopened)==saved,"recipe JSON round trip");
    Check(LocalRangeStateEquals(recipe,reopened),"area equality round trip");
    auto edited=recipe;edited.localRange.areas[0].offsetEv+=1;
    Check(!LocalRangeStateEquals(recipe,edited),"area gain changes recipe equality");
    Check(ZoneAreaMaskFingerprint(area)==ZoneAreaMaskFingerprint(edited.localRange.areas[0]),"gain does not invalidate mask");
    using namespace Stack::Renderer::RawDevelopmentCache;
    Check(BuildStageFingerprints(recipe,192).rawPlacement==BuildStageFingerprints(edited,192).rawPlacement,"area gain reuses RAW placement");
    Check(BuildStageFingerprints(recipe,192).postLocalRange!=BuildStageFingerprints(edited,192).postLocalRange,"area gain invalidates Zones output");
    auto legacy=saved;legacy["localRange"].erase("areas");legacy["localRange"].erase("areasVersion");
    legacy["rawRecipeVersion"]=22;
    Check(IsCanonicalRawRecipeDocument(legacy),"version 22 project document remains accepted");
    const auto oldRecipe=DeserializeRecipe(legacy);
    auto migrated=SerializeRecipe(oldRecipe); migrated["rawRecipeVersion"]=22;
    migrated["localRange"].erase("areas");migrated["localRange"].erase("areasVersion");
    Check(migrated==legacy,"version 22 recipe operations survive migration");
    Check(DeserializeRecipe(legacy).localRange.areas.empty(),"old recipes have no new areas");
    Stack::Editor::RawZoneAreaHistory history; history.Initialize({});
    history.Observe({area},true);history.Observe({edited.localRange.areas[0]},true);history.Observe(edited.localRange.areas,false);
    Check(history.CanUndo(),"completed gesture is undoable");
    Check(history.Undo().empty(),"one undo restores state before entire gesture");
    Check(history.Redo().size()==1,"redo restores painted area");
    history.Observe({},true);history.Cancel();
    Check(history.Undo().empty(),"cancel does not insert undo history");
    RawEditAttributeBundle bundle;
    // Attribute transfer carries the new collection with the existing Areas group.
    auto target=MakeDefaultRecipe("test.dng");
    bundle=CaptureRawEditAttributeBundle(recipe,"test");
    ApplyRawEditAttributeBundle(bundle,{"zones.targets"},target);
    Check(SerializeZoneAreas(target.localRange.areas)==SerializeZoneAreas(recipe.localRange.areas),"area attribute transfer");
    std::cout << "Zones CPU validation passed\n";
}

unsigned int Texture(const std::vector<float>& pixels,int w,int h) {
    unsigned int texture=GLHelpers::CreateStorageTexture(w,h,GL_RGBA32F);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_FLOAT,pixels.data());return texture;
}
std::vector<float> Read(unsigned int texture,int w,int h) {
    Check(texture!=0,"GPU render produced a texture");
    Stack::Renderer::GLState::PixelPackState pack;pack.ConfigureTightCpuReadback();
    std::vector<float> pixels(std::size_t(w)*h*4);
    glBindTexture(GL_TEXTURE_2D,texture);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());
    pack.Restore();return pixels;
}
void GpuTests() {
    using Stack::Renderer::ScopedGLTexture;
    constexpr int w=80,h=40;
    std::vector<float> pixels(w*h*4,.1f);for(int i=0;i<w*h;++i) pixels[i*4+3]=1;
    ScopedGLTexture reference(Texture(pixels,w,h));
    auto globalPixels=pixels;for(int i=0;i<w*h;++i) for(int c=0;c<3;++c) globalPixels[i*4+c]*=8;
    ScopedGLTexture global(Texture(globalPixels,w,h));
    Stack::Renderer::RawZoneAreaRenderer renderer;
    auto recipe=MakeDefaultRecipe("test.dng");recipe.localRange.enabled=true;recipe.localRange.areas={MakeArea()};
    recipe.localRange.areas[0].offsetEv=-2;
    std::vector<RawZoneAreaStatistics> stats;
    ScopedGLTexture output(renderer.Render(global.Get(),reference.Get(),w,h,recipe,true,true,stats));
    auto actual=Read(output.Get(),w,h);
    Near(actual[(20*w+20)*4],.2f,1e-6f,"global +3 plus local -2 = +1");
    Near(actual[(20*w+60)*4],.8f,1e-6f,"unpainted pixel unchanged");
    Check(stats.size()==1 && stats[0].valid && stats[0].fullResolution,"full-resolution range recorded");
    Near(stats[0].minimumEv,ZoneAreaReferenceEv(.1f),2e-5f,"neutral reference range");
    const auto maskBuilds=renderer.MaskBuildCount();
    recipe.localRange.areas[0].offsetEv=4;
    recipe.localRange.areas.push_back(recipe.localRange.areas[0]);recipe.localRange.areas[1].id="area-2";
    output.Reset(renderer.Render(global.Get(),reference.Get(),w,h,recipe,false,false,stats));
    actual=Read(output.Get(),w,h);
    Near(actual[(20*w+20)*4],.8f*256,1e-4f,"overlaps sum beyond legacy four-stop clamp");
    Check(renderer.MaskBuildCount()==maskBuilds,"gain edits and identical masks reuse raster cache");
    recipe.localRange.areas.resize(1);auto& area=recipe.localRange.areas[0];
    area.offsetEv=0;area.points={{-5,-2},{5,2}};
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,false,false,stats));
    const auto base=Read(output.Get(),w,h);
    output.Reset(renderer.Render(global.Get(),reference.Get(),w,h,recipe,true,false,stats));
    actual=Read(output.Get(),w,h);
    Near(actual[(20*w+20)*4],base[(20*w+20)*4]*8,2e-5f,"global exposure does not retarget area graph");
    area.points={{-8,0},{6,0}};area.offsetEv=2;area.strokes[0].opacity=.5f;
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,false,false,stats));
    actual=Read(output.Get(),w,h);
    Near(actual[(20*w+20)*4],.2f,1e-6f,"half mask weights gain in stops");
    area.strokes[0].softness=1;
    pixels[(20*w+31)*4]=pixels[(20*w+31)*4+1]=pixels[(20*w+31)*4+2]=32;
    pixels[(20*w+20)*4]=pixels[(20*w+20)*4+1]=pixels[(20*w+20)*4+2]=0;
    reference.Reset(Texture(pixels,w,h));
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,true,true,stats));
    Check(stats[0].hasBlack,"zero luminance explicitly reported");
    Near(stats[0].maximumEv,ZoneAreaReferenceEv(32),2e-5f,"isolated extremum in soft tail retained");
    area.strokes.clear();
    output.Reset(renderer.Render(reference.Get(),reference.Get(),w,h,recipe,true,true,stats));
    Check(!stats[0].valid,"empty mask has no invented range");
    Check(Read(output.Get(),w,h)==pixels,"empty mask preserves every pixel exactly");
    Check(glGetError()==GL_NO_ERROR,"no GL errors");
    std::cout << "Zones GPU validation passed\n";
}
}

void ValidateRawZoneInteractionCpu();
void ValidateRawZoneInteractionGpu();
int main() {
    try {ToneGraphViewTests(); ValidateRawZoneInteractionCpu(); CpuTests(); ValidateRawZoneGuidanceCpu(); Check(Stack::Validation::ValidateRawZoneAreaGraph(),"graph interaction validation");} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    if(!glfwInit()) return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window=glfwCreateWindow(80,40,"Zones validation",nullptr,nullptr);
    if(!window) {glfwTerminate();return 3;}
    glfwMakeContextCurrent(window);
    bool ok=LoadGLFunctions();
    if(ok) try {ValidateRawZoneInteractionGpu(); GpuTests(); ValidateRawZoneGuidanceGpu();} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';ok=false;}
    glfwDestroyWindow(window);glfwTerminate();return ok?0:4;
}
