#include "Raw/HdrDisplayMapping.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Editor/Layers/ToneLayers.h"
#include "Renderer/GLLoader.h"
#include "Renderer/FullscreenQuad.h"
#include "ThirdParty/stb_image_write.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
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
}
// Optional arguments render actual display-shader comparisons from linear-sRGB
// float RGB. This is a diagnostic input, never a substitute RAW import path.
bool ValidateHdrDisplay(int argc,char** argv) {
    try {
        Context context;
        auto settings=Stack::RawRecipe::DefaultViewTransformJson();
        settings["inputWorkingSpace"]="linear-srgb-d65";settings["encodeSrgbOutput"]=false;
        settings["displayCurve"]=Raw::HdrDisplay::Photographic;
        constexpr unsigned width=256;
        std::vector<float> ramp(width*4);
        for(unsigned x=0;x<width;++x) {
            const float y=.18f*std::exp2(-12.f+28.f*x/(width-1));
            for(unsigned c=0;c<3;++c)ramp[x*4+c]=y;
            ramp[x*4+3]=1;
        }
        float worst=0;
        for(const float white:{2.f,4.f,8.f,14.f})for(const float contrast:{.5f,1.f,1.7f}) {
            Raw::HdrDisplay::Parameters p;p.whiteEv=white;p.contrast=contrast;
            settings["whiteEv"]=white;settings["contrast"]=contrast;
            const auto output=Render(ramp,width,1,settings);
            float previous=-1;
            for(unsigned x=0;x<width;++x) {
                const float expected=Raw::HdrDisplay::Evaluate(ramp[x*4],p);
                worst=std::max(worst,std::abs(expected-output[x*4]));
                Check(output[x*4]>=previous-1e-6f,"Photographic display curve reversed brightness order");
                Check(std::abs(output[x*4]-output[x*4+2])<2e-6f,"Neutral HDR ramp acquired color");
                previous=output[x*4];
            }
            Check(std::abs(Raw::HdrDisplay::Evaluate(.18f,p)-.18f)<1e-6f,
                "Changing HDR white range moved middle grey");
        }
        Check(worst<2e-5f,"Photographic GPU curve differs from CPU reference");
        // Equal brightness must map alike regardless of neighboring texture,
        // tile extent or native inspection crop. There is no spatial filter.
        settings["whiteEv"]=8;settings["contrast"]=1;
        std::vector<float> patch(19*13*4);
        for(unsigned i=0;i<19*13;++i) {
            const float y=i%3?.18f:.181f;
            for(unsigned c=0;c<3;++c)patch[4*i+c]=y;
            patch[4*i+3]=1;
        }
        const auto texture=Render(patch,19,13,settings);
        double contrast=0;
        for(unsigned i=0;i<19*13;++i) {
            Raw::HdrDisplay::Parameters p;p.whiteEv=8;
            Check(std::abs(texture[4*i]-Raw::HdrDisplay::Evaluate(patch[4*i],p))<2e-6f,
                "Display mapping depended on image dimensions or neighbors");
            if(i==0)contrast=texture[0]-texture[4];
        }
        Check(contrast>.0007&&contrast<.0011,"Photographic display unexpectedly flattened or amplified weak midtone texture");
        ViewTransformLayer saved;saved.Deserialize(settings);ViewTransformLayer loaded;loaded.Deserialize(saved.Serialize());
        Check(loaded.Serialize()==saved.Serialize(),"Photographic display settings did not round-trip");
        // The RAW recipe has a separate allowed-field list. A layer-only round
        // trip cannot detect that list dropping the method or aliasing caches.
        auto recipe=Stack::RawRecipe::MakeDefaultRecipe("fixture.dng","HDR display fixture");
        const auto standardRecipe=Stack::RawRecipe::SerializeRecipe(recipe);
        recipe.viewTransform.layerJson=settings;
        const auto photographicRecipe=Stack::RawRecipe::SerializeRecipe(recipe);
        Check(photographicRecipe.at("viewTransform").at("displayCurve")==Raw::HdrDisplay::Photographic&&
            photographicRecipe!=standardRecipe,"RAW recipe discarded the selected display curve");
        std::cout<<"HDR display: CPU/GPU maximum error "<<worst<<", middle-grey anchors, monotonicity, neutral colors, weak texture and native-size parity passed.\n";
        if(argc) {
            Check(argc==5,"Use input.f32 width height output-directory white-EV for HDR display comparisons");
            const int w=std::stoi(argv[1]),h=std::stoi(argv[2]);
            Check(w>0&&h>0&&std::uint64_t(w)*h<=16000000,"HDR display comparison dimensions exceed its memory bound");
            std::ifstream input(argv[0],std::ios::binary);
            std::vector<float> rgb(std::size_t(w)*h*3),rgba(std::size_t(w)*h*4);
            input.read(reinterpret_cast<char*>(rgb.data()),std::streamsize(rgb.size()*sizeof(float)));
            Check(bool(input),"Could not read HDR comparison linear RGB");
            for(std::size_t i=0;i<rgb.size()/3;++i) {for(unsigned c=0;c<3;++c)rgba[i*4+c]=rgb[i*3+c];rgba[i*4+3]=1;}
            const std::filesystem::path directory=argv[3];std::filesystem::create_directories(directory);
            settings["whiteEv"]=std::stof(argv[4]);settings["encodeSrgbOutput"]=true;
            for(const auto method:{"standard",Raw::HdrDisplay::Photographic}) {
                settings["displayCurve"]=method;
                const auto pixels=Render(rgba,w,h,settings);std::vector<unsigned char> png(std::size_t(w)*h*3);
                for(std::size_t i=0;i<png.size()/3;++i)for(unsigned c=0;c<3;++c)
                    png[i*3+c]=static_cast<unsigned char>(std::lround(255*std::clamp(pixels[i*4+c],0.f,1.f)));
                const auto path=directory/(std::string(method)+".png");
                Check(stbi_write_png(path.string().c_str(),w,h,3,png.data(),w*3)!=0,"Could not write HDR display comparison");
                std::ofstream(directory/(std::string(method)+".json"))<<settings.dump(2);
            }
        }
        return true;
    }catch(const std::exception& e){std::cerr<<"HDR display validation failed: "<<e.what()<<'\n';return false;}
}
}
