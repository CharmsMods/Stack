#include "Raw/ViewTransform.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Editor/Layers/ToneLayers.h"
#include "Renderer/GLLoader.h"
#include "Renderer/FullscreenQuad.h"
#include "Renderer/ScopedGLObjects.h"
#include "ThirdParty/stb_image_write.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Stack::Validation {
namespace {
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Context {
    GLFWwindow* window=nullptr;
    Context() {
        Check(glfwInit()!=0,"GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        window=glfwCreateWindow(32,32,"HDR display validation",nullptr,nullptr);
        Check(window!=nullptr,"HDR display validation needs OpenGL 4.3");
        glfwMakeContextCurrent(window);Check(LoadGLFunctions(),"OpenGL functions unavailable");
    }
    ~Context(){if(window)glfwDestroyWindow(window);glfwTerminate();}
};
std::vector<float> Render(const std::vector<float>& rgba,int width,int height,const nlohmann::json& settings) {
    const auto input=GLHelpers::CreateStorageTexture(width,height,GL_RGBA32F);
    const auto output=GLHelpers::CreateStorageTexture(width,height,GL_RGBA32F);
    glBindTexture(GL_TEXTURE_2D,input);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,rgba.data());
    unsigned fbo=0;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Incomplete HDR display framebuffer");
    glViewport(0,0,width,height);
    FullscreenQuad quad;quad.Initialize();ViewTransformLayer layer;layer.Deserialize(settings);
    layer.Execute(input,width,height,quad);
    std::vector<float> pixels(rgba.size());glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,pixels.data());
    Check(glGetError()==GL_NO_ERROR,"HDR display render produced an OpenGL error");
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);
    glDeleteTextures(1,&input);glDeleteTextures(1,&output);
    return pixels;
}

std::vector<float> RenderLocalTone(const std::vector<float>& rgba,int width,int height,
    const Stack::RawRecipe::SceneTone& curve,const std::function<bool()>& cancel={}) {
    Stack::Renderer::ScopedGLTexture input(GLHelpers::CreateStorageTexture(width,height,GL_RGBA32F));
    Stack::Renderer::ScopedGLTexture output(GLHelpers::CreateStorageTexture(width,height,GL_RGBA32F));
    glBindTexture(GL_TEXTURE_2D,input.Get());
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,rgba.data());
    struct Target {
        GLuint fbo=0;
        Target() {glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);}
        ~Target() {glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);}
    } target;
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output.Get(),0);
    Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Incomplete local tone target");
    glViewport(0,0,width,height);
    FullscreenQuad quad;quad.Initialize();ToneCurveLayer layer;
    auto settings=Stack::RawRecipe::DefaultFinishToneJson();
    settings.erase("pointCurves");settings.erase("pointCurveSetVersion");
    settings["luminanceTone"]=Stack::RawRecipe::SerializeSceneTone(curve);
    settings["inputWorkingSpace"]="linear-rec2020-d65";
    settings["truthfulV2SignedMath"]=true;
    layer.Deserialize(settings);layer.SetProcessingCancellation(cancel);
    layer.Execute(input.Get(),width,height,quad);
    std::vector<float> pixels(rgba.size());
    glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,pixels.data());
    Check(glGetError()==GL_NO_ERROR,"Local tone render produced an OpenGL error");
    return pixels;
}

void ValidateLocalTone() {
    using namespace Stack::RawRecipe;
    constexpr int width=128,height=64;
    std::vector<float> input(width*height*4);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const float ev=(x<width/2?0.f:8.f)+((x/2)%2?.125f:-.125f);
        const auto i=(y*width+x)*4;
        input[i]=input[i+1]=input[i+2]=.18f*std::exp2(ev);input[i+3]=.75f;
    }
    // One bounded scene: weak texture on both sides of a large exposure edge.
    // A linear 1/8 EV slope has an independent, analytic pointwise reference.
    SceneTone curve;curve.curve={{0,.4375f},{1,.5625f}};
    const auto global=RenderLocalTone(input,width,height,curve);
    curve.localAmount=1;
    const auto local=RenderLocalTone(input,width,height,curve);
    const int dark=(height/2*width+24)*4,bright=(height/2*width+96)*4;
    const float globalDetail=std::abs(std::log2(global[bright+8]/global[bright]));
    const float localDetail=std::abs(std::log2(local[bright+8]/local[bright]));
    Check(std::abs(globalDetail-.03125f)<.001f,"Pointwise tone lost the analytic EV slope");
    Check(localDetail>.22f && localDetail<.255f,"Local tone lost or boosted fine highlight detail");
    const float darkDetail=std::abs(std::log2(local[dark+8]/local[dark]));
    Check(std::abs(localDetail-darkDetail)<.003f,"Log-domain guide changed its detail response with exposure");
    for(std::size_t i=0;i<input.size();i+=4) {
        Check(std::isfinite(local[i]) && local[i]>0,"Local tone produced an invalid scene value");
        Check(local[i]==local[i+1] && local[i]==local[i+2],"Local luminance tone colored a neutral input");
        Check(local[i+3]==input[i+3],"Local tone changed coverage");
    }
    SceneTone identity;identity.localAmount=1;
    const auto unchanged=RenderLocalTone(input,width,height,identity);
    for(std::size_t i=0;i<input.size();++i)
        Check(std::abs(unchanged[i]-input[i])<1e-5f*std::max(1.f,std::abs(input[i])),"Identity local mode changed pixels");

    // Compress a clean 8 EV edge without a large new rim. The tolerance is a
    // bounded edge check, not a claim that guided filtering cannot make halos.
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto i=(y*width+x)*4;
        input[i]=input[i+1]=input[i+2]=x<width/2?.18f:.18f*256.f;
    }
    const auto edge=RenderLocalTone(input,width,height,curve);
    float edgeExcursion=0;
    for(int x=0;x<width;++x) {
        const float ev=std::log2(edge[(height/2*width+x)*4]/.18f);
        edgeExcursion=std::max(edgeExcursion,std::abs(ev-(x<width/2?0.f:1.f)));
    }
    Check(edgeExcursion<.12f,"Local tone introduced a large bright/dark edge rim");

    // Hidden/invalid neighbors must not darken the guide of a uniform valid
    // region. Signed scene RGB retains its component ratios and alpha.
    const std::array<float,3> rgb={-.25f,1.f,3.f};
    for(std::size_t i=0;i<input.size();i+=4) {
        for(int c=0;c<3;++c) input[i+c]=rgb[c];
        input[i+3]=.4f;
    }
    input[0]=std::numeric_limits<float>::quiet_NaN();input[3]=0;
    for(int x=1;x<width/4;++x) {input[x*4]=1e20f;input[x*4+3]=0;}
    const auto masked=RenderLocalTone(input,width,height,curve);
    const auto expected=ApplySceneTone(curve,rgb,{.2627002f,.6779981f,.0593017f});
    for(std::size_t i=0;i<input.size();i+=4) if(input[i+3]>0) {
        for(int c=0;c<3;++c)
            Check(std::isfinite(masked[i+c]) && std::abs(masked[i+c]-expected[c])<2e-4f,
                "Invalid coverage contaminated the guide or signed RGB ratios");
        Check(masked[i+3]==input[i+3],"Local tone changed partial alpha");
    }
    bool canceled=false;
    int cancellationChecks=0;
    try {RenderLocalTone(input,width,height,curve,[&]{return ++cancellationChecks>3;});}
    catch(const std::exception& error) {canceled=std::string(error.what()).find("canceled")!=std::string::npos;}
    Check(canceled && cancellationChecks==4,"Canceled local tone silently returned another image");
    const auto recovered=RenderLocalTone(input,width,height,curve);
    Check(std::abs(recovered[bright]-masked[bright])<1e-6f,"Local tone failed to recover after cancellation");
    std::cout<<"Local tone: 0.25 EV input detail, pointwise "<<globalDetail<<" EV, preserved "<<localDetail
        <<" EV; edge excursion "<<edgeExcursion
        <<" EV; identity, exposure response, signed RGB, coverage and cancellation passed.\n";
}
}
// Focused curve and actual display-shader parity checks.
bool ValidateHdrDisplay(int argc,char** argv) {
    try {
        Context context;
        ValidateLocalTone();
        const Raw::SigmoidDisplay::PreparedCurve sigmoid;
        Check(std::abs(sigmoid.Evaluate(Raw::SigmoidDisplay::MiddleGrey)-Raw::SigmoidDisplay::MiddleGrey)<2e-7f,
            "Sigmoid moved middle grey at zero exposure");
        Check(std::abs(sigmoid.Evaluate(0.f)-Raw::SigmoidDisplay::Black)<2e-8f,
            "Sigmoid changed the SDR black target");
        // Frozen zero-skew darktable-reference values, not a second curve implementation.
        Check(std::abs(sigmoid.Evaluate(.018f)-.007583f)<2e-6f &&
            std::abs(sigmoid.Evaluate(.72f)-.633594f)<2e-6f,
            "Sigmoid lost its photographic shadow/midtone separation");
        const float grey=Raw::SigmoidDisplay::MiddleGrey;
        const float localSlope=std::log(sigmoid.Evaluate(grey*1.01f)/sigmoid.Evaluate(grey/1.01f))/(2.f*std::log(1.01f));
        Check(localSlope>1.20f&&localSlope<1.23f,"Default Sigmoid flattened middle-grey contrast");
        Check(sigmoid.Evaluate(3.f)<sigmoid.Evaluate(6.f)&&sigmoid.Evaluate(6.f)<1.f,
            "Sigmoid hard-clipped finite highlights");
        Check(sigmoid.Evaluate(1e30f)==1.f,"Sigmoid overflow did not converge to white");
        constexpr unsigned width=256;
        std::vector<float> ramp(width*4),patch(19*13*4);
        for(unsigned x=0;x<width;++x) {
            const float y=.18f*std::exp2(-12.f+28.f*x/(width-1));
            for(unsigned c=0;c<3;++c)ramp[x*4+c]=y;
            ramp[x*4+3]=1;
        }
        for(unsigned i=0;i<19*13;++i) {
            const float y=i%3?.18f:.181f;
            for(unsigned c=0;c<3;++c)patch[4*i+c]=y;
            patch[4*i+3]=1;
        }
        // File output uses the shared CPU finish; compare it with the actual
        // Stack layer shader, including signed and over-range camera colors.
        const auto standardSettings=Stack::RawRecipe::DefaultViewTransformJson();
        Check(standardSettings.at("displayCurve")==Raw::SigmoidDisplay::Id&&
            standardSettings.at("exposure")==0.f,"Fresh RAW did not select Sigmoid at zero EV");
        const Raw::ViewTransform::PreparedTransform standard;
        float standardWorst=0;
        const auto compareStandard=[&](const std::vector<float>& input,unsigned w,unsigned h) {
            const auto output=Render(input,int(w),int(h),standardSettings);
            for(std::size_t i=0;i<input.size()/4;++i) {
                const auto expected=standard.Rgb({input[4*i],input[4*i+1],input[4*i+2]});
                for(unsigned c=0;c<3;++c)standardWorst=std::max(standardWorst,
                    std::abs(expected[c]-output[4*i+c]));
                Check(output[4*i+3]==input[4*i+3],"Default view transform changed alpha");
            }
        };
        compareStandard(ramp,width,1);
        float last=-1.f;
        for(unsigned x=0;x<width;++x) {
            const auto value=standard.Rgb({ramp[4*x],ramp[4*x],ramp[4*x]});
            Check(std::isfinite(value[0])&&value[0]>=last&&value[0]>=0.f&&value[0]<=1.f,
                "Default Sigmoid reversed or invalidated the neutral ramp");
            last=value[0];
        }
        compareStandard(patch,19,13);
        patch[0]=-.025f;patch[1]=.18f;patch[2]=3.f;
        patch[4]=5.f;patch[5]=.10f;patch[6]=.03f;
        compareStandard(patch,19,13);
        Check(standardWorst<3e-5f,"Default Stack GPU finish differs from standalone CPU finish");
        for (const bool encode : {false,true}) for (const bool hue : {false,true}) {
            auto custom=standardSettings;
            custom["inputWorkingSpace"]="linear-srgb-d65";
            custom["encodeSrgbOutput"]=encode;custom["preserveHue"]=hue;
            custom["exposure"]=-.7f;custom["contrast"]=1.4f;custom["contrastPivotEv"]=.5f;
            Raw::ViewTransform::Parameters p;
            p.inputIsRec2020=false;p.encodeSrgbOutput=encode;p.preserveHue=hue;
            p.exposure=-.7f;p.contrast=1.4f;p.contrastPivotEv=.5f;
            const Raw::ViewTransform::PreparedTransform expected(p);
            const auto pixels=Render(patch,19,13,custom);
            for(std::size_t i=0;i<patch.size()/4;++i) {
                const auto rgb=expected.Rgb({patch[i*4],patch[i*4+1],patch[i*4+2]});
                for(int c=0;c<3;++c)Check(std::isfinite(pixels[i*4+c])&&
                    std::abs(pixels[i*4+c]-rgb[c])<3e-5f,"Custom Sigmoid CPU/GPU mismatch");
            }
        }
        ViewTransformLayer saved;saved.Deserialize(standardSettings);
        ViewTransformLayer loaded;loaded.Deserialize(saved.Serialize());
        Check(loaded.Serialize()==saved.Serialize(),"Sigmoid layer settings did not round-trip");
        auto recipe=Stack::RawRecipe::MakeDefaultRecipe("fixture.dng","Display fixture");
        const auto serialized=Stack::RawRecipe::SerializeRecipe(recipe);
        Check(Stack::RawRecipe::SerializeRecipe(Stack::RawRecipe::DeserializeRecipe(serialized))==serialized,
            "Sigmoid recipe did not round-trip canonically");
        auto obsolete=serialized;
        obsolete["rawRecipeVersion"]=Stack::RawRecipe::kRawDevelopmentRecipeVersion-1;
        bool obsoleteRejected=false;
        try { Stack::RawRecipe::DeserializeRecipe(obsolete); }
        catch(const std::exception&) { obsoleteRejected=true; }
        Check(obsoleteRejected,"Obsolete RAW recipe was silently converted or reset");
        for(const auto* removed:{"standard","photographic-hdr-v1"}) {
            auto retired=standardSettings;retired["displayCurve"]=removed;
            bool rejected=false;
            try { ViewTransformLayer layer;layer.Deserialize(retired); }
            catch(const std::exception&) { rejected=true; }
            Check(rejected,"Removed mapper was accepted in a graph layer");
            recipe.viewTransform.layerJson=retired;
            rejected=false;
            try { Stack::RawRecipe::SerializeRecipe(recipe); }
            catch(const std::exception&) { rejected=true; }
            Check(rejected,"Removed mapper was accepted in a RAW recipe");
        }
        Check(!serialized.at("viewTransform").contains("blackEv")&&
            !serialized.at("viewTransform").contains("shoulder"),"Retired controls leaked into the View recipe");
        std::cout<<"Sigmoid display: CPU/GPU maximum error "<<standardWorst
            <<", zero-EV anchors, monotonicity, signed color, alpha, native-size parity, round trips and removed-mode checks passed.\n";
        if(argc)throw std::runtime_error("The removed display-curve comparison arguments are no longer supported.");
        return true;
    }catch(const std::exception& e){std::cerr<<"HDR display validation failed: "<<e.what()<<'\n';return false;}
}
}
