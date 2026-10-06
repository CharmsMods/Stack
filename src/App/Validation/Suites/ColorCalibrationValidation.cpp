#include "App/Validation/ValidationImageUtils.h"
#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "Editor/EditorModule.h"
#include "Raw/RawColorCalibration.h"
#include "Raw/RawLoader.h"
#include "Raw/RawRecipeCompatibility.h"
#include "Raw/RawWorkspaceManagedGraph.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLStateGuards.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <GLFW/glfw3.h>

namespace Stack::Validation {
namespace {
using namespace RawRecipe;
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::array<double,3> Apply(const std::array<float,9>& m, const std::array<double,3>& v) {
    std::array<double,3> result {};
    for (int r=0;r<3;++r) for (int c=0;c<3;++c) result[r]+=m[r*3+c]*v[c];
    return result;
}
void CheckMathAndStorage() {
    const std::array<float,9> identity {1,0,0,0,1,0,0,0,1};
    Require(BuildColorCalibrationTransform({},Raw::RawWorkingSpace::LinearSrgbD65).matrix==identity,
        "Default matrix is not exact identity");
    for (int combination=0;combination<729;++combination) {
        RawColorCalibrationRecipe recipe;
        int remaining=combination;
        for (auto& primary:recipe.primaries) {
            primary.hue=float((remaining%3-1)*100); remaining/=3;
            primary.saturation=float((remaining%3-1)*100); remaining/=3;
        }
        for (auto space:{Raw::RawWorkingSpace::LinearRec2020D65,Raw::RawWorkingSpace::LinearSrgbD65}) {
            const auto m=BuildColorCalibrationTransform(recipe,space).matrix;
            for (float value:m) Require(std::isfinite(value),"Nonfinite matrix coefficient");
            const double determinant=m[0]*(double(m[4])*m[8]-double(m[5])*m[7])-
                m[1]*(double(m[3])*m[8]-double(m[5])*m[6])+m[2]*(double(m[3])*m[7]-double(m[4])*m[6]);
            Require(determinant>.01,"Primary transform collapsed or inverted");
            for (double gray:{-0.5,0.0,0.18,1.0,8.0})
                for (double value:Apply(m,{gray,gray,gray}))
                    Require(std::abs(value-gray)<3e-6,"Neutral preservation failed");
        }
        const std::array<float,9> toSrgb {1.660491f,-.587641f,-.072850f,
            -.124550f,1.132900f,-.008349f,-.018151f,-.100579f,1.118730f};
        const auto a=BuildColorCalibrationTransform(recipe,Raw::RawWorkingSpace::LinearRec2020D65).matrix;
        const auto b=BuildColorCalibrationTransform(recipe,Raw::RawWorkingSpace::LinearSrgbD65).matrix;
        const std::array<double,3> sample {1.5,-.12,.38};
        const auto left=Apply(toSrgb,Apply(a,sample)),right=Apply(b,Apply(toSrgb,sample));
        for (int i=0;i<3;++i) Require(std::abs(left[i]-right[i])<2e-5,"Working-space conversion changed calibration");
    }
    for (int i=0;i<3;++i) {
        RawColorCalibrationRecipe recipe;
        recipe.primaries[i].hue=100;
        const auto p=ColorCalibrationPrimaries(recipe)[i];
        const auto original=kCalibrationPrimaries[i];
        const double cross=(original[0]-kCalibrationWhite[0])*(p[1]-kCalibrationWhite[1])-
            (original[1]-kCalibrationWhite[1])*(p[0]-kCalibrationWhite[0]);
        Require(cross>0,"Positive hue has the wrong direction");
        recipe.primaries[i]={0,-100};
        const auto half=ColorCalibrationPrimaries(recipe)[i];
        for(int c=0;c<2;++c) Require(std::abs(half[c]-(original[c]+kCalibrationWhite[c])*.5)<1e-12,
            "Minimum saturation is not half distance");
    }
    auto recipe=MakeDefaultRecipe("calibration-validation");
    recipe.preToneExposureEv=1.25f;
    recipe.colorCalibration.primaries={{{25,-40},{-12,23},{45,60}}};
    const auto document=SerializeRecipe(recipe);
    Require(SerializeRecipe(DeserializeRecipe(nlohmann::json::parse(document.dump())))==document,
        "Calibration did not survive recipe round-trip");
    for (int version:{22,23,24,25}) {
        auto legacy=document; legacy["rawRecipeVersion"]=version; legacy.erase("colorCalibration");
        auto stages=DefaultStageOrder(); stages.erase(std::remove(stages.begin(),stages.end(),"color-calibration"),stages.end());
        legacy["stageOrder"]=stages;
        if(version<25) {legacy.erase("evGradients");legacy.erase("toneGradients");}
        if(version==22) {legacy["localRange"].erase("areas");legacy["localRange"].erase("areasVersion");}
        Require(!IsCanonicalRawRecipeDocument(legacy),"Unsupported old recipe was accepted as current");
    }
    auto invalid=recipe.colorCalibration;
    invalid.primaries[0]={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};
    invalid.primaries[1]={-200,200};
    invalid=SanitizeColorCalibration(invalid);
    Require(invalid.primaries[0].hue==0&&invalid.primaries[0].saturation==0&&
        invalid.primaries[1].hue==-100&&invalid.primaries[1].saturation==100,"Invalid slider values were not sanitized");
    Require(!IsColorCalibrationActive(BuildNeutralComparisonRecipe(recipe).colorCalibration),"Neutral comparison kept calibration");
    recipe.rgbDenoise.enabled=false;
    std::string reason;
    Require(!RawWorkspace::IsRecipeRepresentableAsManagedGraph(recipe,&reason),"Managed graph silently dropped calibration");
    std::cout<<"Calibration math, 729 combinations, working spaces, and recipe versions 22-26 passed.\n";
}

struct GraphCheck {
    RenderPipeline pipeline;
    RenderGraphSnapshot graph;
    GraphCheck(const std::shared_ptr<Raw::RawImageData>& raw) {
        pipeline.Initialize();
        pipeline.SetRawDevelopmentAnalysisEnabled(false);
        pipeline.SetRawDevelopmentViewportValidationEnabled(false);
        pipeline.SetRawRgbDenoiseAsyncEnabled(false);
        pipeline.SetGraphCacheBudget(256ull*1024*1024);
        RenderGraphNode source; source.nodeId=1;source.kind=RenderGraphNodeKind::RawDevelopment;
        source.rawDevelopment.embeddedRawData=raw;graph.nodes.push_back(source);
        RenderGraphNode output;output.nodeId=2;output.kind=RenderGraphNodeKind::Output;graph.nodes.push_back(output);
        graph.links.push_back({1,"imageOut",2,"imageIn"});graph.outputNodeId=2;graph.outputSocketId="imageOut";
    }
    std::vector<float> Render(const RawDevelopmentRecipe& recipe,int edge=0,Raw::ViewportRegion region={}) {
        graph.nodes[0].rawDevelopment.recipe=recipe;
        pipeline.SetPreviewMaxDimension(edge);pipeline.SetRawViewportRequest({region,1.0,1});pipeline.Resize(64,64);
        pipeline.ExecuteGraph(graph);
        Require(pipeline.GetOutputTexture()!=0&&!pipeline.GetLastGraphExecutionStats().allocationFailed,"Calibration render failed");
        std::vector<float> pixels(std::size_t(pipeline.GetCanvasWidth())*pipeline.GetCanvasHeight()*4);
        const Renderer::GLState::PixelPackState pack;pack.ConfigureTightCpuReadback();
        glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());pack.Restore();
        Require(glGetError()==GL_NO_ERROR,"GPU readback failed");
        for(float v:pixels) Require(std::isfinite(v),"Nonfinite calibration output");
        return pixels;
    }
};
RawDevelopmentRecipe LinearRecipe(const std::string& source) {
    auto recipe=MakeDefaultRecipe(source);
    recipe.rgbDenoise.enabled=false;recipe.technical.mosaicDenoise.enabled=false;
    recipe.finishTone.layerJson["enabled"]=false;recipe.viewTransform.layerJson["enabled"]=false;
    return recipe;
}
void CompareMatrix(const std::vector<float>& before,const std::vector<float>& after,
    const RawColorCalibrationTransform& transform,float exposure=1.0f) {
    Require(before.size()==after.size()&&!before.empty(),"Image sizes differ");
    double maximum=0;
    for(std::size_t i=0;i<before.size();i+=4) {
        const auto expected=Apply(transform.matrix,{before[i],before[i+1],before[i+2]});
        for(int c=0;c<3;++c) maximum=std::max(maximum,std::abs(after[i+c]-expected[c]*exposure)/
            std::max(.25,std::abs(expected[c]*exposure)));
        Require(after[i+3]==before[i+3],"Calibration changed alpha");
    }
    Require(maximum<.008,"GPU calibration differs from reference matrix");
    std::cout<<"GPU/reference maximum scaled error "<<maximum<<'\n';
}
void CheckGpuAndGuides() {
    auto samples=std::make_shared<Raw::RawImageData>();
    samples->metadata.sourcePath="calibration-signed-samples";
    samples->metadata.rawWidth=samples->metadata.visibleWidth=4;
    samples->metadata.rawHeight=samples->metadata.visibleHeight=2;
    samples->metadata.pixelLayout=Raw::RawPixelLayout::LinearRgb;
    samples->metadata.mosaiced=false;samples->metadata.linearChannels=3;
    samples->metadata.cameraWhiteBalance={1,1,1,1};
    samples->linearFloatBuffer={-.5f,-.5f,-.5f, 0,0,0, .18f,.18f,.18f, 4,4,4,
        2,-.25f,.5f, .5f,1.5f,-.125f, -.125f,.25f,3, .8f,.3f,.1f};
    GraphCheck test(samples);
    auto recipe=LinearRecipe(samples->metadata.sourcePath);
    for(auto space:{Raw::RawWorkingSpace::LinearSrgbD65,Raw::RawWorkingSpace::LinearRec2020D65}) {
        recipe.technical.workingSpace=space;recipe.colorCalibration={};recipe.preToneExposureEv=0;
        const auto baseline=test.Render(recipe);
        Require(*std::min_element(baseline.begin(),baseline.end())<-.1f&&
            *std::max_element(baseline.begin(),baseline.end())>3.0f,"Signed/HDR test input was clipped upstream");
        recipe.colorCalibration.primaries={{{100,-100},{-100,100},{100,100}}};
        CompareMatrix(baseline,test.Render(recipe),BuildColorCalibrationTransform(recipe.colorCalibration,space));
        recipe.preToneExposureEv=2;
        CompareMatrix(baseline,test.Render(recipe),BuildColorCalibrationTransform(recipe.colorCalibration,space),4);
        recipe.preToneExposureEv=0;recipe.colorCalibration.enabled=false;
        Require(test.Render(recipe)==baseline,"Bypass changed original pixels");
        recipe.colorCalibration={};Require(test.Render(recipe)==baseline,"Zero settings changed original pixels");
    }
    auto raw=MakeViewportValidationRaw(128,96);
    GraphCheck areas(raw);
    recipe=LinearRecipe(raw->metadata.sourcePath);recipe.rgbDenoise.enabled=true;
    recipe.rgbDenoise.lumaMap.baseMultiplier=.25f;
    areas.Render(recipe);
    recipe.colorCalibration.primaries[2]={30,45};
    areas.Render(recipe);
    Require(areas.pipeline.GetLastGraphExecutionStats().rawRgbDenoisePasses==0&&
        areas.pipeline.GetLastGraphExecutionStats().rawGpuPreprocessDispatches==0,"Calibration reran upstream processing");
    areas.pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::LocalRangeInput,64);
    RawZoneArea area;area.id="calibration-guide";area.offsetEv=.2f;
    RawZoneBrushStroke stroke;stroke.radius=.3f;stroke.path={{.5f,.5f}};stroke.followEdges=true;area.strokes={stroke};
    recipe.localRange.areas={area};areas.Render(recipe);
    auto guide=areas.pipeline.GetRawDevelopmentGraphScopeReadback().zoneGuide;
    Require(guide&&guide->Valid(),"Calibrated EV Area guide missing");
    Require(guide->recipeFingerprint==Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,0).calibratedNeutral,
        "EV Area guide has stale calibration identity");
    recipe.colorCalibration.primaries[0].hue=40;areas.Render(recipe);
    auto changed=areas.pipeline.GetRawDevelopmentGraphScopeReadback().zoneGuide;
    Require(changed&&changed->recipeFingerprint!=guide->recipeFingerprint,"Calibration did not refresh the guide");
    recipe.preToneExposureEv=2;areas.Render(recipe);
    Require(areas.pipeline.GetRawDevelopmentGraphScopeReadback().zoneGuide->recipeFingerprint==changed->recipeFingerprint,
        "Global exposure changed the pre-exposure guide identity");
    recipe.localRange.areas.clear();recipe.preToneExposureEv=0;
    areas.pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::None,0);
    const auto full=areas.Render(recipe);
    const auto regional=areas.Render(recipe,0,{128,96,32,24,32,24});
    Require(regional.size()==32u*24u*4u,"Regional calibration dimensions wrong");
    for(int y=0;y<24;++y) for(int x=0;x<32;++x) for(int c=0;c<4;++c)
        Require(std::abs(regional[(y*32+x)*4+c]-full[((96-24-24+y)*128+x+32)*4+c])<.002,
            "Native-detail calibration differs from full image");
    std::cout<<"Signed GPU values, bypass, denoise reuse, EV Area guidance, and native-detail checks passed.\n";
}

void CheckRealRaw(const char* path, bool checkExport) {
    auto raw=std::make_shared<Raw::RawImageData>();
    Require(Raw::RawLoader::LoadFile(path,*raw),"Could not load requested RAW");
    GraphCheck test(raw);
    auto recipe=LinearRecipe(path);
    const auto before=test.Render(recipe,768);
    recipe.colorCalibration.primaries={{{18,12},{-10,8},{-25,25}}};
    const auto after=test.Render(recipe,768);
    CompareMatrix(before,after,BuildColorCalibrationTransform(recipe.colorCalibration,recipe.technical.workingSpace));
    Require(after==test.Render(DeserializeRecipe(nlohmann::json::parse(SerializeRecipe(recipe).dump())),768),
        "Saved calibration changed the RAW render");
    const auto folder=std::filesystem::path("outputs/validation/color-calibration");
    std::filesystem::create_directories(folder);
    for(bool enabled:{false,true}) {
        recipe.colorCalibration.enabled=enabled;
        recipe.viewTransform=MakeDefaultRecipe(path).viewTransform;
        recipe.viewTransform.layerJson["encodeSrgbOutput"]=false;
        const auto pixels=test.Render(recipe,768);
        std::vector<unsigned char> bytes(pixels.size());
        for(std::size_t i=0;i<pixels.size();++i) bytes[i]=static_cast<unsigned char>(
            std::lround(std::clamp(pixels[i],0.0f,1.0f)*255));
        Require(WriteValidationPng(folder/(std::filesystem::path(path).stem().string()+(enabled?"-calibrated.png":"-baseline.png")),
            bytes,test.pipeline.GetCanvasWidth(),test.pipeline.GetCanvasHeight()),"Could not save inspection preview");
    }
    if (checkExport) {
        recipe.viewTransform.layerJson["encodeSrgbOutput"]=true;
        recipe.source.sourcePath=std::filesystem::absolute(path).string();
        test.Render(recipe);
        int width=0,height=0;
        const auto expected=test.pipeline.GetOutputPixels(width,height);
        EditorModule editor;
        editor.GetPipeline().Initialize();
        auto& graph=editor.GetNodeGraph();graph.Clear();
        EditorNodeGraph::RawDevelopmentPayload payload;payload.recipe=recipe;
        const int source=graph.AddRawDevelopmentNode(std::move(payload),{0,0})->id;
        const int output=graph.AddOutputNode({480,0},true)->id;
        Require(graph.TryConnectSockets(source,EditorNodeGraph::kImageOutputSocketId,
            output,EditorNodeGraph::kImageInputSocketId),"Could not connect export graph");
        std::vector<unsigned char> exported;int exportWidth=0,exportHeight=0;
        Require(editor.BuildSingleOutputExportRaster(exported,exportWidth,exportHeight),"Calibrated export failed");
        Require(width==exportWidth&&height==exportHeight&&expected.size()==exported.size(),"Export dimensions differ");
        int difference=0;
        for(std::size_t i=0;i<exported.size();++i) difference=std::max(difference,std::abs(int(exported[i])-int(expected[i])));
        Require(difference<=1,"Calibrated native preview and export differ");
        std::cout<<"Native preview/export agreement passed, maximum byte difference "<<difference<<'\n';
    }
    std::cout<<"Real RAW calibration and saved-edit check passed: "<<path<<'\n';
}
} // namespace

bool ValidateColorCalibration(int count,char** paths) {
    if(!glfwInit()) return false;
    glfwDefaultWindowHints();glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window=glfwCreateWindow(64,64,"Calibration validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);
    bool ok=false;
    try {
        Require(LoadGLFunctions(),"OpenGL initialization failed");
        CheckMathAndStorage();CheckGpuAndGuides();
        for(int i=0;i<count;++i) CheckRealRaw(paths[i],i==0);
        ok=true;
    } catch(const std::exception& error) {std::cerr<<"Color Calibration validation failed: "<<error.what()<<'\n';}
    glfwMakeContextCurrent(nullptr);glfwDestroyWindow(window);glfwTerminate();
    return ok;
}
} // namespace Stack::Validation
