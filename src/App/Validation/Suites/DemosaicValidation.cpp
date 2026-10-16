#include "Raw/Demosaic/DemosaicCatalog.h"
#include "Raw/Demosaic/FloatDemosaic.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawGpuPipeline.h"
#include "Raw/RawLoader.h"
#include "Raw/RawProcessingMath.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
void Require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
std::vector<float> ReadRgb(GLuint texture,int width,int height) {
    std::vector<float> pixels(static_cast<std::size_t>(width)*height*3);
    glBindTexture(GL_TEXTURE_2D,texture);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_FLOAT,pixels.data());
    Require(glGetError()==GL_NO_ERROR,"GPU result readback failed");
    return pixels;
}
void CheckCpuGpu(Raw::RawGpuPipeline& pipeline,const Raw::RawImageData& raw,
    const Raw::RawDevelopSettings& settings,const std::vector<float>& expected,int preview=0) {
    const auto texture=pipeline.Render(raw,settings,preview);
    Require(texture!=0,pipeline.GetLastError());
    const int width=pipeline.GetOutputWidth(),height=pipeline.GetOutputHeight();
    const auto pixels=ReadRgb(texture,width,height);
    const int sw=raw.metadata.visibleWidth,sh=raw.metadata.visibleHeight;
    std::size_t mismatches=0;
    float maximum=0;
    double squared=0;
    std::string firstMismatch;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const int sx=std::min(sw-1,static_cast<int>((x+0.5)*sw/width));
        const int sy=std::min(sh-1,static_cast<int>((height-y-0.5)*sh/height));
        for(int c=0;c<3;++c) {
            const float actual=pixels[(static_cast<std::size_t>(y)*width+x)*3+c];
            const float reference=expected[(static_cast<std::size_t>(sy)*sw+sx)*3+c];
            const float difference=std::abs(actual-reference);
            maximum=std::max(maximum,difference);squared+=difference*difference;
            if(!std::isfinite(actual) || difference>=0.003f*(1+std::abs(reference))) {
                ++mismatches;
                if(firstMismatch.empty()) firstMismatch=std::string(Raw::DemosaicMethodName(settings.demosaicMethod))+" CPU/GPU disagreement at "+std::to_string(x)+","+std::to_string(y)+" channel "+std::to_string(c)+" size "+std::to_string(width)+"x"+std::to_string(height)+" sensor "+std::to_string(sx)+","+std::to_string(sy)+" actual "+std::to_string(actual)+" expected "+std::to_string(reference);
            }
        }
    }
    Require(mismatches==0,firstMismatch+"; mismatched samples "+std::to_string(mismatches)+
        "; maximum "+std::to_string(maximum)+"; RMS "+std::to_string(std::sqrt(squared/pixels.size())));
}
void Run(const std::filesystem::path& source,const std::filesystem::path& output) {
    Raw::RawImageData decoded;
    Require(Raw::RawLoader::LoadFile(source.u8string(),decoded),"Could not load the validation RAW");
    Require(decoded.metadata.pixelLayout==Raw::RawPixelLayout::MosaicBayer,"Validation needs a Bayer RAW");
    Raw::RawDevelopSettings settings;
    settings.whiteBalanceMode=Raw::WhiteBalanceMode::Neutral;
    settings.highlightMode=Raw::HighlightReconstructionMode::Off;
    settings.debugView=Raw::RawDebugView::DemosaicedCameraRgb;
    settings.mosaicDenoise.enabled=false;
    settings.lateralRedCyan=settings.lateralBlueYellow=0;
    std::vector<float> normalized;
    std::string error;
    Require(Raw::Processing::BuildTruthfulNormalizedMosaic(decoded,settings,normalized,&error),error);
    const auto area=Raw::Processing::ResolveActiveArea(decoded.metadata);
    Raw::RawImageData raw;
    auto& meta=raw.metadata;
    meta.rawWidth=meta.visibleWidth=std::min(513,(area.right-area.left));
    meta.rawHeight=meta.visibleHeight=std::min(385,(area.bottom-area.top));
    meta.orientation=1;
    meta.pixelLayout=Raw::RawPixelLayout::MosaicBayer;
    meta.cfaPattern=decoded.metadata.cfaPattern;
    raw.normalizedMosaicInputContract=Raw::NormalizedMosaicInputContract::BracketingPreGain;
    auto samples=std::make_shared<std::vector<float>>(static_cast<std::size_t>(meta.rawWidth)*meta.rawHeight);
    // A real center crop, with an even offset to preserve CFA phase. No source
    // file is changed and no generated image corpus is needed.
    const int left=area.left+(((area.right-area.left)-meta.rawWidth)/2 & ~1);
    const int top=area.top+(((area.bottom-area.top)-meta.rawHeight)/2 & ~1);
    for(int y=0;y<meta.rawHeight;++y) for(int x=0;x<meta.rawWidth;++x)
        (*samples)[static_cast<std::size_t>(y)*meta.rawWidth+x]=normalized[static_cast<std::size_t>(y+top)*decoded.metadata.rawWidth+x+left];
    raw.normalizedMosaicBuffer=samples;
    Raw::RawGpuPipeline pipeline;
    nlohmann::json report=nlohmann::json::array();
    std::vector<std::vector<float>> results;
    for(const auto& choice:Raw::Demosaic::Methods) {
        settings.demosaicMethod=choice.method;
        std::vector<float> expected;
        const auto start=std::chrono::steady_clock::now();
        Require(Raw::Demosaic::Reconstruct(*samples,meta.rawWidth,meta.rawHeight,meta.cfaPattern,choice.method,expected,error),error);
        CheckCpuGpu(pipeline,raw,settings,expected);
        CheckCpuGpu(pipeline,raw,settings,expected,257);
        // The saved recipe must preserve each key through actual disk I/O.
        RawRecipe::RawDevelopmentRecipe recipe;
        recipe.technical.demosaicMethod=choice.method;
        const auto path=output/(std::string(choice.key)+".json");
        {std::ofstream stream(path); stream<<RawRecipe::SerializeRecipe(recipe).dump(2); Require(bool(stream),"Recipe write failed");}
        nlohmann::json saved; {std::ifstream stream(path); stream>>saved;}
        Require(RawRecipe::DeserializeRecipe(saved).technical.demosaicMethod==choice.method,"Recipe method round trip failed");
        Require(EditorNodeGraph::DeserializeRawSettings(EditorNodeGraph::SerializeRawSettings(settings)).demosaicMethod==choice.method,
            "Graph source settings round trip failed");
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        report.push_back({{"method",choice.key},{"cropCpuAndGpuMs",ms}});
        std::cout<<choice.label<<": CPU, GPU, preview and saved recipe passed ("<<ms<<" ms)"<<std::endl;
        results.push_back(std::move(expected));
    }
    // Standalone methods/refinements must differ on this detailed real crop.
    // Hybrids may legitimately coincide in regions where a blend weight is zero.
    for(std::size_t a=0;a<results.size();++a) for(std::size_t b=0;b<a;++b) {
        if((a>=10 && a<16) || (b>=10 && b<16)) continue;
        double difference=0;
        for(std::size_t i=0;i<results[a].size();++i) difference+=std::abs(results[a][i]-results[b][i]);
        Require(difference>1e-5,"Two demosaicing choices produced identical results");
    }
    // AHD's perceptual analysis must use the camera matrix and invalidate its
    // reconstruction if that matrix changes while the same mosaic is retained.
    settings.demosaicMethod=Raw::DemosaicMethod::Ahd;
    meta.hasCameraMatrix=true;
    meta.cameraToSrgb=decoded.metadata.cameraToSrgb;
    std::vector<float> calibrated;
    Require(Raw::Demosaic::Reconstruct(*samples,meta.rawWidth,meta.rawHeight,meta.cfaPattern,
        settings.demosaicMethod,calibrated,error,{},meta.cameraToSrgb),error);
    CheckCpuGpu(pipeline,raw,settings,calibrated);
    meta.hasCameraMatrix=false;
    // Reuse the signed/HDR sample pattern from RawRgbDenoiseGpuValidation.
    meta.rawWidth=meta.visibleWidth=8; meta.rawHeight=meta.visibleHeight=8;
    auto range=std::make_shared<std::vector<float>>(64);
    for(int i=0;i<64;++i) (*range)[i]=static_cast<float>(i%19-9)*0.3f;
    raw.normalizedMosaicBuffer=range;
    for(auto pattern:{Raw::CfaPattern::RGGB,Raw::CfaPattern::BGGR,Raw::CfaPattern::GBRG,Raw::CfaPattern::GRBG}) {
        meta.cfaPattern=pattern;
        for(const auto& choice:Raw::Demosaic::Methods) {
            settings.demosaicMethod=choice.method;
            std::vector<float> expected;
            Require(Raw::Demosaic::Reconstruct(*range,8,8,pattern,choice.method,expected,error),error);
            CheckCpuGpu(pipeline,raw,settings,expected);
            const auto active=Raw::Processing::ResolveActiveArea(meta);
            for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
                const int color=Raw::Processing::CfaColorAt(meta,active,x,y);
                Require(expected[(y*8+x)*3+color]==(*range)[y*8+x],"Recorded sample was changed");
            }
        }
    }
    // Reuse the flat-field channel levels from DevelopSmokeValidation to check
    // an independent invariant, not merely CPU/GPU agreement.
    meta.rawWidth=meta.visibleWidth=64; meta.rawHeight=meta.visibleHeight=64;
    meta.cfaPattern=Raw::CfaPattern::RGGB;
    auto flat=std::make_shared<std::vector<float>>(64*64);
    constexpr std::array<float,3> levels{0.20f,0.40f,0.40f/1.5f};
    const auto flatArea=Raw::Processing::ResolveActiveArea(meta);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        (*flat)[y*64+x]=levels[Raw::Processing::CfaColorAt(meta,flatArea,x,y)];
    for(const auto& choice:Raw::Demosaic::Methods) {
        std::vector<float> values;
        Require(Raw::Demosaic::Reconstruct(*flat,64,64,meta.cfaPattern,choice.method,values,error),error);
        for(float value:values) Require(std::isfinite(value),"Flat field is non-finite");
        for(int y=12;y<52;++y) for(int x=12;x<52;++x) for(int c=0;c<3;++c)
            Require(std::abs(values[(y*64+x)*3+c]-levels[c])<0.002f,
                std::string(choice.label)+" changed a uniform color field");
        raw.normalizedMosaicBuffer=flat;settings.demosaicMethod=choice.method;
        CheckCpuGpu(pipeline,raw,settings,values);
    }
    // Check the real full-resolution sensor path with the most involved hybrid.
    settings.demosaicMethod=Raw::DemosaicMethod::DcbLmmseMalvar;
    const auto nativeBegin=std::chrono::steady_clock::now();
    Require(pipeline.Render(decoded,settings)!=0,pipeline.GetLastError());
    glFinish();
    const double nativeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-nativeBegin).count();
    const int nativeWidth=pipeline.GetOutputWidth(),nativeHeight=pipeline.GetOutputHeight();
    const auto cachedBegin=std::chrono::steady_clock::now();
    Require(pipeline.Render(decoded,settings,512)!=0,pipeline.GetLastError());
    glFinish();
    const double cachedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cachedBegin).count();
    const auto nativePreview=ReadRgb(pipeline.Render(decoded,settings,512),pipeline.GetOutputWidth(),pipeline.GetOutputHeight());
    for(float value:nativePreview) Require(std::isfinite(value),"Native RAW result is non-finite");
    report.push_back({{"nativeWidth",nativeWidth},{"nativeHeight",nativeHeight},{"nativeHybridMs",nativeMs},{"cachedPreviewMs",cachedMs}});
    std::cout<<"Native RAW hybrid "<<nativeWidth<<"x"<<nativeHeight<<": "<<nativeMs<<" ms; cached preview "<<cachedMs<<" ms"<<std::endl;
    std::vector<float> canceled;
    Require(!Raw::Demosaic::Reconstruct(*range,8,8,meta.cfaPattern,Raw::DemosaicMethod::Dcb,canceled,error,[]{return true;}),"Cancellation was ignored");
    settings.demosaicMethod=Raw::DemosaicMethod::Lmmse;
    int cancelChecks=0;
    Require(pipeline.Render(decoded,settings,512,[&]{return ++cancelChecks>12;})==0,"GPU cancellation was ignored");
    Require(cancelChecks>12,"GPU cancellation did not reach the requested processing step");
    Require(pipeline.Render(decoded,settings,512)!=0,pipeline.GetLastError());
    std::ofstream(output/"report.json")<<report.dump(2);
    std::cout<<"All "<<Raw::Demosaic::Methods.size()<<" methods: standalone methods differ; all four CFA phases; signed/HDR samples; CPU/GPU cancellation passed."<<std::endl;
}
} // namespace
bool ValidateDemosaicing(const std::filesystem::path& source,const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    if(!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window=glfwCreateWindow(16,16,"Demosaicing validation",nullptr,nullptr);
    if(!window) {glfwTerminate();return false;}
    glfwMakeContextCurrent(window);
    bool valid=false;
    try {Require(LoadGLFunctions(),"OpenGL initialization failed");Run(source,output);valid=true;}
    catch(const std::exception& e) {std::cerr<<"Demosaicing validation: "<<e.what()<<std::endl;}
    glfwMakeContextCurrent(nullptr);glfwDestroyWindow(window);glfwTerminate();
    return valid;
}
} // namespace Stack::Validation
