#include "App/Validation/ValidationSuites.h"
#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
bool ValidateRawZoneAreaGraph();
bool ValidateRawZoneAreaWorker(GLFWwindow*);
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Compare(const std::vector<float>& a, const std::vector<float>& b, float tolerance, const char* message) {
    Require(!a.empty() && a.size() == b.size(), "Pipeline output dimensions differ");
    float error = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        Require(std::isfinite(a[i]) && std::isfinite(b[i]), "Nonfinite pipeline pixels");
        error = std::max(error, std::abs(a[i]-b[i]) / std::max(.05f, std::abs(b[i])));
    }
    std::cout << message << ": maximum relative error " << error << '\n';
    Require(error <= tolerance, message);
}
void ValidatePipeline() {
    auto raw = MakeViewportValidationRaw(128,96);
    RenderPipeline pipeline; pipeline.Initialize();
    pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawDevelopmentViewportValidationEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    pipeline.SetGraphCacheBudget(128ull*1024*1024);
    pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::LocalRangeInput, 64);
    RenderGraphSnapshot graph;
    RenderGraphNode development; development.nodeId=1; development.kind=RenderGraphNodeKind::RawDevelopment;
    development.rawDevelopment.embeddedRawData=raw; graph.nodes.push_back(development);
    RenderGraphNode output; output.nodeId=2; output.kind=RenderGraphNodeKind::Output; graph.nodes.push_back(output);
    graph.links.push_back({1,"imageOut",2,"imageIn"}); graph.outputNodeId=2; graph.outputSocketId="imageOut";
    auto base=RawRecipe::MakeDefaultRecipe(raw->metadata.sourcePath,"Zones pipeline fixture");
    base.technical.processingVersion=Raw::RawProcessingVersion::TruthfulV2;
    base.rgbDenoise.enabled=false;
    base.finishTone.layerJson["enabled"]=false;
    base.finishTone.layerJson["localBaselineEnabled"]=false;
    base.finishTone.layerJson["foundationAdaptiveAssist"]=false;
    base.viewTransform.layerJson["enabled"]=false;
    auto render = [&](const RawRecipe::RawDevelopmentRecipe& recipe, int edge=0) {
        graph.nodes[0].rawDevelopment.recipe=recipe;
        pipeline.SetPreviewMaxDimension(edge); pipeline.SetRawViewportRequest({}); pipeline.Resize(128,96);
        pipeline.ExecuteGraph(graph);
        const int width=pipeline.GetCanvasWidth(),height=pipeline.GetCanvasHeight();
        Require(pipeline.GetOutputTexture()!=0,"Missing pipeline output");
        std::vector<float> pixels(std::size_t(width)*height*4);
        const Renderer::GLState::PixelPackState pack; pack.ConfigureTightCpuReadback();
        glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data()); pack.Restore();
        return pixels;
    };
    RawRecipe::RawZoneArea area; area.id="paint"; area.sourceAspect=128.0f/96;
    RawRecipe::RawZoneBrushStroke stroke; stroke.radius=1; stroke.softness=0; stroke.path={{.5f,.5f}};
    area.strokes={stroke};
    for (bool denoise : {false,true}) {
        base.rgbDenoise.enabled=denoise;
        auto expectedRecipe=base; expectedRecipe.preToneExposureEv=1;
        const auto expected=render(expectedRecipe);
        auto local=base; local.preToneExposureEv=3; local.localRange.enabled=true;
        area.offsetEv=-2; local.localRange.areas={area};
        Compare(render(local),expected,.005f,"Global +3 and local -2 equal global +1");
        auto stats=pipeline.GetRawDevelopmentGraphScopeReadback();
        Require(stats.valid && stats.zoneAreas.size()==1 && stats.zoneAreas[0].valid && stats.zoneAreas[0].fullResolution,
            "Full resolution area statistics missing");
        local.preToneExposureEv=-1; render(local);
        const auto after=pipeline.GetRawDevelopmentGraphScopeReadback();
        Require(after.zoneAreas.size()==1 && std::abs(stats.zoneAreas[0].minimumEv-after.zoneAreas[0].minimumEv)<.0001f &&
            std::abs(stats.zoneAreas[0].maximumEv-after.zoneAreas[0].maximumEv)<.0001f,"Global EV moved area reference luminance");
        auto overlap=base; overlap.localRange.enabled=true; area.offsetEv=1; overlap.localRange.areas={area,area};
        overlap.localRange.areas[1].id="second";
        expectedRecipe.preToneExposureEv=2;
        Compare(render(overlap),render(expectedRecipe),.005f,"Overlapping +1 areas equal global +2");
        std::string error;
        RawRecipe::RawDevelopmentRecipe restored;
        restored=RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(local));
        Compare(render(local),render(restored),0,"Reopened recipe renders identically");
    }
    base.rgbDenoise.enabled=false;
    {
        auto guided=base;
        guided.localRange.enabled=true;
        area.offsetEv=1;
        area.strokes[0].radius=.35f;
        area.strokes[0].path={{.42f,.5f}};
        area.strokes[0].followEdges=true;
        guided.localRange.areas={area};
        const auto pixels=render(guided);
        const auto scope=pipeline.GetRawDevelopmentGraphScopeReadback();
        Require(scope.zoneGuide && scope.zoneGuide->Valid() && scope.zoneAreas.size()==1 && scope.zoneAreas[0].maskPreview,
            "Guided RAW render did not publish neutral guide and mask");
        const auto& preview=*scope.zoneAreas[0].maskPreview;
        const auto expectedMask=RawRecipe::RasterizeZoneArea(area,scope.zoneGuide->width,scope.zoneGuide->height,
            guided.cropRotation,{},scope.zoneGuide.get());
        Require(expectedMask==preview.coverage,"RAW pipeline overlay differs from guided coverage");
        guided.preToneExposureEv=2;
        const auto brighter=render(guided);
        auto expected=pixels;
        for(std::size_t i=0;i<expected.size();++i) if(i%4!=3) expected[i]*=4;
        Compare(brighter,expected,.005f,"Global EV does not change guided selection");
        const auto changedScope=pipeline.GetRawDevelopmentGraphScopeReadback();
        Require(changedScope.zoneAreas.size()==1 && changedScope.zoneAreas[0].maskPreview &&
            changedScope.zoneAreas[0].maskPreview->coverage==preview.coverage,"EV edit moved guided boundary");
        Compare(render(RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(guided))),brighter,0,
            "Saved guided recipe preserves rendered output");
        const auto reduced=render(guided,64);
        Require(!reduced.empty() && pipeline.GetRawDevelopmentGraphScopeReadback().zoneGuide,
            "Guided preview did not regenerate reference at preview resolution");
        area.strokes[0].followEdges=false;
        area.strokes[0].radius=1;
        area.strokes[0].path={{.5f,.5f}};
    }
    auto legacy=base; legacy.localRange.enabled=true;
    RawRecipe::RawLocalRangeTargetZone target; target.id="saved-target"; target.centerEv=0; target.deltaEv=.8f;
    target.seeds={{.3f,.5f}}; legacy.localRange.targetZones={target};
    const auto legacyPixels=render(legacy);
    auto document=RawRecipe::SerializeRecipe(legacy);
    document["rawRecipeVersion"]=22; document["localRange"].erase("areas");document["localRange"].erase("areasVersion");
    RawRecipe::RawDevelopmentRecipe reopened; std::string error;
    reopened=RawRecipe::DeserializeRecipe(document);
    Compare(render(reopened),legacyPixels,0,"Legacy recipe retains its rendered result");
    area.offsetEv=0; reopened.localRange.areas={area};
    Compare(render(reopened),legacyPixels,.005f,"Zero gain painted area preserves existing target behavior");
    for(int rotation : {0,90,180,270}) {
        auto transform=base; transform.cropRotation.rotationDegrees=rotation;
        transform.cropRotation.flipHorizontally=true; transform.cropRotation.cropEnabled=true;
        transform.cropRotation.cropX=.1f; transform.cropRotation.cropY=.2f;
        transform.cropRotation.cropWidth=.7f; transform.cropRotation.cropHeight=.6f;
        auto local=transform; local.localRange.enabled=true; area.offsetEv=1; local.localRange.areas={area};
        transform.preToneExposureEv=1;
        Compare(render(local),render(transform),.005f,"Painted gain aligns with transformed export");
    }
    // Locate a small painted patch in rendered pixels after each transform.
    // Compare against a baseline so RAW scene detail cannot stand in for mask alignment.
    area.strokes[0].radius=.07f;area.strokes[0].path={{.25f,.375f}};area.offsetEv=1;
    for(int edge : {0,64}) for(int rotation : {0,90,180,270}) for(bool flip : {false,true}) {
        auto plain=base;plain.cropRotation.rotationDegrees=rotation;plain.cropRotation.flipHorizontally=flip;
        plain.cropRotation.cropEnabled=true;plain.cropRotation.cropX=.05f;plain.cropRotation.cropY=.05f;
        plain.cropRotation.cropWidth=.9f;plain.cropRotation.cropHeight=.9f;
        auto painted=plain;painted.localRange.enabled=true;painted.localRange.areas={area};
        const auto before=render(plain,edge),after=render(painted,edge);
        const int width=pipeline.GetCanvasWidth(),height=pipeline.GetCanvasHeight();
        float cx=0,cy=0,count=0;
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            const auto index=(std::size_t(height-1-y)*width+x)*4;
            if(after[index]>before[index]*1.5f && before[index]>.001f) {cx+=(x+.5f)/width;cy+=(y+.5f)/height;count+=1;}
        }
        Require(count>0,"Painted patch disappeared in preview/export");cx/=count;cy/=count;
        float expectedX=.25f,expectedY=.375f;
        if(rotation==90) {expectedX=.625f;expectedY=.25f;}
        if(rotation==180) {expectedX=.75f;expectedY=.625f;}
        if(rotation==270) {expectedX=.375f;expectedY=.75f;}
        if(flip) expectedX=1-expectedX;
        expectedX=(expectedX-.05f)/.9f;expectedY=(expectedY-.05f)/.9f;
        Require(std::abs(cx-expectedX)<2.0f/width && std::abs(cy-expectedY)<2.0f/height,
            "Painted patch moved relative to the cropped/rotated image");
    }
    Require(glGetError()==GL_NO_ERROR,"Pipeline OpenGL error");
    pipeline.Shutdown();
}
}
bool ValidateRawZoneAreaPipeline() {
    if(!glfwInit()) return false;
    glfwDefaultWindowHints(); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window=glfwCreateWindow(128,96,"Zones pipeline validation",nullptr,nullptr);
    if(!window) {glfwTerminate(); return false;}
    glfwMakeContextCurrent(window);
    bool ok=false;
    try {Require(LoadGLFunctions(),"GL loading failed");ValidatePipeline();ok=ValidateRawZoneAreaGraph() && ValidateRawZoneAreaWorker(window);}
    catch(const std::exception& error) {std::cerr<<"Zones pipeline validation failed: "<<error.what()<<'\n';}
    glfwMakeContextCurrent(nullptr);glfwDestroyWindow(window);glfwTerminate();
    if(ok) std::cout<<"Zones RAW pipeline validation passed.\n";
    return ok;
}
}
