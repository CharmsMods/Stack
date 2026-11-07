#ifndef NOMINMAX
#define NOMINMAX
#endif
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
#include "WindowsHdrValidation.h"
#include "HdrPhotoExportValidation.h"
#include "HdrRenditionValidation.h"
#include "HdrGalleryCheck.h"
#include "Hdr/Presentation.h"
#include "Hdr/ImageFile.h"

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
    const Stack::RawRecipe::SceneTone& curve,const std::function<bool()>& cancel={},
    const Stack::Renderer::SceneColorGuidance& guidance={},bool colorRelationships=true) {
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
    settings["executionColorRelationships"]=colorRelationships;
    layer.Deserialize(settings);layer.SetProcessingCancellation(cancel);
    layer.SetSceneColorGuidance(guidance);
    layer.Execute(input.Get(),width,height,quad);
    std::vector<float> pixels(rgba.size());
    glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,pixels.data());
    Check(glGetError()==GL_NO_ERROR,"Local tone render produced an OpenGL error");
    return pixels;
}

void ValidateColorLocalTone() {
    using namespace Stack::RawRecipe;
    using Stack::Renderer::ScopedGLTexture;
    constexpr int width=128,height=64;
    constexpr std::array<float,3> luma{.2627002f,.6779981f,.0593017f};
    std::vector<float> colored(width*height*4),neutral(colored.size()),evidence(colored.size());
    std::vector<float> confidence(width*height,1.f);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto i=(y*width+x)*4;
        const float luminance=.18f*std::exp2(x<width/2 ? 0.f : .4f);
        const std::array<float,3> hue=x<width/2 ? std::array<float,3>{1,.06f,.02f} :
            std::array<float,3>{.04f,.1f,1};
        const float hueY=hue[0]*luma[0]+hue[1]*luma[1]+hue[2]*luma[2];
        for(int c=0;c<3;++c) {
            colored[i+c]=hue[c]*luminance/hueY;
            neutral[i+c]=luminance;
            evidence[i+c]=hue[c];
        }
        colored[i+3]=neutral[i+3]=.6f;
        evidence[i+3]=0; // Evidence alpha is deliberately unrelated to confidence.
    }
    ScopedGLTexture colors(GLHelpers::CreateStorageTexture(width,height,GL_RGBA32F));
    ScopedGLTexture reliability(GLHelpers::CreateStorageTexture(width,height,GL_R32F));
    const auto upload=[&] {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,colors.Get());
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,evidence.data());
        glBindTexture(GL_TEXTURE_2D,reliability.Get());
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RED,GL_FLOAT,confidence.data());
    };
    upload();
    const Stack::Renderer::SceneColorGuidance guidance{colors.Get(),reliability.Get(),1};
    SceneTone curve;curve.curve={{0,.4375f},{1,.5625f}};curve.localAmount=1;
    const auto scalar=RenderLocalTone(colored,width,height,curve);
    const auto neutralScalar=RenderLocalTone(neutral,width,height,curve);
    curve.localColorProtection=1;
    const auto self=RenderLocalTone(colored,width,height,curve);
    const auto external=RenderLocalTone(neutral,width,height,curve,{},guidance);
    const auto ev=[&](const std::vector<float>& image,int i) {
        return std::log2((image[i]*luma[0]+image[i+1]*luma[1]+image[i+2]*luma[2])/.18f);
    };
    const auto boundaryError=[&](const std::vector<float>& image) {
        float maximum=0;
        for(int x=width/2-8;x<width/2+8;++x)
            maximum=std::max(maximum,std::abs(ev(image,(height/2*width+x)*4)-(x<width/2 ? 0.f : .05f)));
        return maximum;
    };
    const float scalarError=boundaryError(scalar),colorError=boundaryError(self),externalError=boundaryError(external);
    Check(colorError<scalarError*.35f && colorError<.03f,"Color tone failed to protect a weak-luminance color boundary");
    Check(externalError<scalarError*.35f && externalError<.03f,"Stable evidence failed to protect an edited neutral boundary");
    for(std::size_t i=0;i<colored.size();i+=4) {
        Check(self[i+3]==colored[i+3] && external[i+3]==neutral[i+3],"Color tone changed coverage");
        const float gain=self[i]/colored[i];
        for(int c=0;c<3;++c)
            Check(std::isfinite(self[i+c]) && std::abs(self[i+c]-colored[i+c]*gain)<1e-5f,
                "Color-guided luminance tone changed chromaticity");
    }
    // Evidence brightness and alpha cannot define the current operation's EV.
    for(std::size_t i=0;i<evidence.size();i+=4) for(int c=0;c<3;++c)
        evidence[i+c]*=(i/4)%2 ? 1e12f : 1e-12f;
    upload();
    const auto rescaled=RenderLocalTone(neutral,width,height,curve,{},guidance);
    for(std::size_t i=0;i<neutral.size();i+=4)
        Check(std::abs(ev(rescaled,int(i))-ev(external,int(i)))<.0001f,"Evidence exposure leaked into local tone");

    // An isolated evidence hole must fade the surrounding fitted models too;
    // simply switching the final gain at that pixel would create a dark speck.
    const int hole=height/2*width+width/2-1;
    confidence[hole]=0;upload();
    const auto withHole=RenderLocalTone(neutral,width,height,curve,{},guidance);
    Check(std::abs(ev(withHole,hole*4)-ev(neutralScalar,hole*4))<.00002f,
        "A color-evidence hole did not abstain");
    float holeStep=0;
    for(int y=height/2-16;y<height/2+16;++y) {
        const int p=(y*width+width/2-1)*4;
        holeStep=std::max(holeStep,std::abs(ev(withHole,p)-ev(withHole,p+width*4)));
    }
    Check(holeStep<.02f,"Missing color evidence introduced an isolated tone speck");

    std::fill(confidence.begin(),confidence.end(),0.f);
    evidence[0]=std::numeric_limits<float>::quiet_NaN();upload();
    const auto unsupported=RenderLocalTone(neutral,width,height,curve,{},guidance);
    const auto nonColor=RenderLocalTone(neutral,width,height,curve,{},guidance,false);
    for(std::size_t i=0;i<neutral.size();i+=4) {
        Check(std::abs(ev(unsupported,int(i))-ev(neutralScalar,int(i)))<.00002f,"Zero confidence did not recover scalar tone");
        Check(std::abs(ev(nonColor,int(i))-ev(neutralScalar,int(i)))<.00002f,"Non-color input used color guidance");
    }
    // A guide with color texture but no luminance relationship cannot invent a
    // brightness texture, even with spatially varying confidence and signed RGB.
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto i=(y*width+x)*4;
        neutral[i]=-.25f;neutral[i+1]=1;neutral[i+2]=3;
        evidence[i]=(x+y)%2 ? 2.f : -.1f;
        evidence[i+1]=.4f;evidence[i+2]=(x+y)%2 ? .02f : 1.f;
        confidence[y*width+x]=(x/3+y/5)%2 ? 1.f : .1f;
    }
    upload();
    const auto flat=RenderLocalTone(neutral,width,height,curve,{},guidance);
    const auto expected=ApplySceneTone(curve,{-.25f,1.f,3.f},luma);
    for(std::size_t i=0;i<neutral.size();i+=4) for(int c=0;c<3;++c)
        Check(std::isfinite(flat[i+c]) && std::abs(flat[i+c]-expected[c])<.0001f,
            "Color guide texture or confidence imprinted on a flat signed input");

    // Confidence holes explicitly abstain at full-resolution evaluation. A
    // constant hue confined to one brightness region must also have zero effect.
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto i=(y*width+x)*4;
        const float value=.18f*std::exp2(x<width/2 ? 0.f : .4f);
        neutral[i]=neutral[i+1]=neutral[i+2]=value;
        evidence[i]=1;evidence[i+1]=.06f;evidence[i+2]=.02f;
        confidence[y*width+x]=x<width/2 ? 1.f : 0.f;
    }
    upload();
    const auto oneRegion=RenderLocalTone(neutral,width,height,curve,{},guidance);
    for(std::size_t i=0;i<neutral.size();i+=4)
        Check(std::abs(ev(oneRegion,int(i))-ev(neutralScalar,int(i)))<.0001f,
            "Reliability selection changed the regional brightness reference");

    int checks=0;bool canceled=false;
    try {RenderLocalTone(neutral,width,height,curve,[&]{return ++checks>6;},guidance);}
    catch(const std::exception&) {canceled=true;}
    Check(canceled && checks==7,"Color guide did not honor bounded cancellation");
    const auto recovered=RenderLocalTone(neutral,width,height,curve,{},guidance);
    Check(std::isfinite(recovered[0]),"Color tone did not recover after cancellation");

    auto saved=SerializeSceneTone(curve);
    Check(ReadSceneTone(saved).localColorProtection==1,"Color protection did not round-trip");
    saved.erase("localColorProtection");
    Check(ReadSceneTone(saved).localColorProtection==0,"Old edits enabled color protection");
    saved["localColorProtection"]=std::numeric_limits<float>::quiet_NaN();
    Check(ReadSceneTone(saved).localColorProtection==0,"Non-finite color protection was not sanitized");
    Check(Stack::Renderer::LocalToneGuide::WorkingBytes(4096,4096,true)==40u*1024u*1024u &&
        Stack::Renderer::LocalToneGuide::WorkingBytes(4096,4096)==12u*1024u*1024u,
        "Local tone planning does not distinguish color scratch allocation");
    std::cout<<"  Color local tone: boundary error "<<scalarError<<" -> "<<colorError<<
        " EV; external "<<externalError<<" EV; evidence-hole step "<<holeStep<<
        " EV; confidence/texture/signed/cancellation checks passed\n";
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
    ValidateColorLocalTone();
}
}
bool ValidateHdrGallery(const char* raw) {
    try {
        Context context;
        struct Cleanup {
            GLFWwindow* window;
            ~Cleanup() { Hdr::ShutdownPresentation(window); }
        } cleanup{context.window};
        if(!Hdr::BeginWindow(context.window,32,32,true))
            throw std::runtime_error(Hdr::MainDisplayStatus());
        CheckHdrGallery(context.window,std::filesystem::u8path(raw));
        return true;
    } catch(const std::exception& e) {
        std::cerr<<"HDR Gallery validation failed: "<<e.what()<<'\n';
        return false;
    }
}

bool ValidateHdrGraphExport(const char* raw,const char* folder) {
    try {
        Context context;
        ValidatePhotoExports(context.window,std::filesystem::u8path(raw),std::filesystem::u8path(folder),true);
        return true;
    } catch(const std::exception& error) {
        std::cerr<<"HDR Graph export validation failed: "<<error.what()<<'\n';
        return false;
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
        for(float detail:{0.f,1.f}) {
            auto trimSettings=standardSettings;
            trimSettings["sdrBrightness"]=-.65f;trimSettings["sdrHighlightDetail"]=detail;
            trimSettings["sdrSaturation"]=.85f;
            Raw::ViewTransform::Parameters parameters;parameters.sdr={-.65f,detail,.85f};
            const Raw::ViewTransform::PreparedTransform expected(parameters);
            const auto pixels=Render(patch,19,13,trimSettings);
            for(std::size_t i=0;i<patch.size();i+=4) {
                const auto rgb=expected.Rgb({patch[i],patch[i+1],patch[i+2]});
                for(int c=0;c<3;++c) Check(std::abs(pixels[i+c]-rgb[c])<3e-5f,"SDR trim CPU/GPU mismatch");
            }
        }
        auto unchanged=standardSettings;
        unchanged.erase("sdrBrightness");unchanged.erase("sdrHighlightDetail");unchanged.erase("sdrSaturation");
        Raw::ViewTransform::Parameters originalParameters;originalParameters.sdr={0,0,1};
        const Raw::ViewTransform::PreparedTransform originalFinish(originalParameters);
        const auto originalPixels=Render(patch,19,13,unchanged);
        for(std::size_t i=0;i<patch.size();i+=4) {
            const auto expected=originalFinish.Rgb({patch[i],patch[i+1],patch[i+2]});
            for(int c=0;c<3;++c) Check(std::abs(originalPixels[i+c]-expected[c])<3e-5f,"Absent SDR trim changed a saved finish");
        }
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
        auto extended=standardSettings;
        extended["_presentationHeadroom"]=Stack::Hdr::MasterHeadroom;
        extended["encodeSrgbOutput"]=false;
        Raw::ViewTransform::Parameters hdrParameters;
        hdrParameters.hdrHeadroom=Stack::Hdr::MasterHeadroom; hdrParameters.encodeSrgbOutput=false;
        Raw::ViewTransform::PreparedTransform master(hdrParameters);
        auto rendered=Render(ramp,width,1,extended);
        float worst=0.f,previous=-1.f;
        for(unsigned x=0;x<width;++x) {
            const float v=ramp[4*x]; const auto expected=master.Rgb({v,v,v});
            Check(rendered[4*x]>=previous-1e-5f,"HDR grayscale reversed");
            previous=rendered[4*x];
            for(int c=0;c<3;++c) worst=std::max(worst,std::abs(rendered[4*x+c]-expected[c]));
        }
        auto colors=Render(patch,19,13,extended);
        auto trimmedHdr=extended;trimmedHdr["sdrBrightness"]=-2.f;trimmedHdr["sdrSaturation"]=.5f;
        trimmedHdr["sdrHighlightDetail"]=0.f;
        const auto unchangedHdr=Render(patch,19,13,trimmedHdr);
        Check(unchangedHdr==colors,"SDR adjustments changed the HDR master");
        for(size_t i=0;i<patch.size();i+=4) {
            const auto expected=master.Rgb({patch[i],patch[i+1],patch[i+2]});
            for(int c=0;c<3;++c) worst=std::max(worst,std::abs(colors[i+c]-expected[c]));
            Check(colors[i+3]==patch[i+3],"HDR changed image coverage");
        }
        Check(std::abs(master.Luma(grey)-grey)<2e-6f && previous>4.8f && previous<=Stack::Hdr::MasterHeadroom+.0001f &&
            worst<.0001f,"HDR gray, highlight shoulder or CPU/GPU parity failed");
        Check(std::abs(Stack::Hdr::Pq(100.f)-.50807842f)<.000001f &&
              std::abs(Stack::Hdr::Nits(.75182710f)-1000.f)<.02f,
              "PQ absolute luminance anchors failed");
        Stack::Hdr::Image coverage{19,13,colors};
        coverage.rgba[3]=.375f;
        const auto png=Stack::Hdr::EncodePqPng(coverage);
        const auto decoded=Stack::Hdr::DecodePqPng(png);
        Check(png.size()>25 && png[24]==16 && Stack::Hdr::IsPqPng(png) &&
            decoded.width==coverage.width && decoded.height==coverage.height &&
            std::abs(decoded.rgba[3]-.375f)<1.f/65535.f,
            "16-bit PQ PNG did not preserve straight alpha");
        std::cout<<"HDR CPU/GPU max difference "<<worst<<", brightest neutral "<<previous<<" x diffuse white.\n";
#ifdef _WIN32
        CheckNativeHdrPresentation(context.window);
#endif
        if(argc==2) {
            const auto raw=std::filesystem::u8path(argv[0]),dest=std::filesystem::u8path(argv[1]);
            struct DisplayCleanup {GLFWwindow* w; ~DisplayCleanup(){Stack::Hdr::ShutdownPresentation(w);}} cleanup{context.window};
            const auto target=Stack::Hdr::BeginWindow(context.window,32,32,true);
            ValidatePhotoExports(context.window,raw,dest);
            if(target) CheckHdrGallery(context.window,raw);
        } else if(argc==4 && std::string(argv[0])=="--renditions") {
            ValidateHdrRenditionPair(std::filesystem::u8path(argv[1]),std::filesystem::u8path(argv[2]),std::filesystem::u8path(argv[3]));
        } else if(argc) throw std::runtime_error("Expected a RAW path and output directory, or --renditions HDR-PNG SDR-PNG new-output-directory.");
        return true;
    }catch(const std::exception& e){std::cerr<<"HDR display validation failed: "<<e.what()<<'\n';return false;}
}
}
