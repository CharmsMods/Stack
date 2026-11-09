#include "HdrRenditionValidation.h"
#include "HdrRenditionComparison.h"
#include "Hdr/ImageFile.h"
#include "Hdr/SdrFinish.h"
#include "Hdr/GainMapPreview.h"
#include "Hdr/RasterSource.h"
#include "Queue/ExportPreferences.h"
#include "NodeMath/PngMetadataWriter.h"
#include "Utils/PngEncodingUtils.h"
#include "Platform/ImGuiWindowColor.h"
#include "Renderer/FullscreenQuad.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace Stack::Validation {
namespace {
std::vector<unsigned char> Read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot read supplied rendition.");
    return {(std::istreambuf_iterator<char>(f)),{}};
}
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void CheckPreviewShader(const Hdr::Image& hdr,const std::vector<unsigned char>& sdr) {
    // Sample the supplied photograph, then exercise the production photo shader
    // at three display capacities without changing the user's Windows settings.
    constexpr int w=96,h=64;
    std::vector<float> a(w*h*4),b(a.size()),actual(a.size());
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const auto source=4*(std::size_t(y*hdr.height/h)*hdr.width+x*hdr.width/w);
        const auto dest=4*(y*w+x);
        for(int c=0;c<3;++c) {a[dest+c]=sdr[source+c]/255.f;b[dest+c]=Hdr::Srgb(hdr.rgba[source+c]);}
        a[dest+3]=b[dest+3]=1.f;
    }
    Renderer::ScopedGLTexture base(GLHelpers::CreateTextureFromData(a.data(),w,h,GL_RGBA32F,GL_RGBA,GL_FLOAT,GL_NEAREST));
    Renderer::ScopedGLTexture master(GLHelpers::CreateTextureFromData(b.data(),w,h,GL_RGBA32F,GL_RGBA,GL_FLOAT,GL_NEAREST));
    Renderer::ScopedGLTexture out(GLHelpers::CreateStorageTexture(w,h,GL_RGBA32F));
    Renderer::ScopedGLProgram program(GLHelpers::CreateShaderProgram(R"(
#version 430 core
layout(location=0) in vec2 Position; layout(location=1) in vec2 UV;
out vec2 Frag_UV; out vec4 Frag_Color;
void main(){Frag_UV=UV;Frag_Color=vec4(1);gl_Position=vec4(Position,0,1);}
)",(std::string("#version 430 core\n")+Platform::UiFragmentShader).c_str()));
    Require(bool(program),"HDR photo preview shader did not compile.");
    GLuint fbo=GLHelpers::CreateFBO(out.Get());
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,w,h);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glUseProgram(program.Get());
    glUniform1i(glGetUniformLocation(program.Get(),"Texture"),0);
    glUniform1i(glGetUniformLocation(program.Get(),"StackPhotoBase"),1);
    glUniform1i(glGetUniformLocation(program.Get(),"StackHasPhotoBase"),1);
    glUniform1i(glGetUniformLocation(program.Get(),"StackLinearTarget"),1);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,base.Get());
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,master.Get());
    FullscreenQuad quad;quad.Initialize();
    float worst=0;
    for(bool paired:{false,true}) for(float headroom:{1.f,2.f,Hdr::MasterHeadroom}) {
        glUniform1i(glGetUniformLocation(program.Get(),"StackHasPhotoBase"),paired);
        glUniform1f(glGetUniformLocation(program.Get(),"StackPhotoHeadroom"),headroom);
        quad.Draw();glReadPixels(0,0,w,h,GL_RGBA,GL_FLOAT,actual.data());
        for(std::size_t i=0;i<a.size();i+=4) {
            Hdr::Rgb av{},bv{};
            for(int c=0;c<3;++c) {av[c]=Hdr::Linear(a[i+c]);bv[c]=Hdr::Linear(b[i+c]);}
            auto expected=paired ? Hdr::GainMapPreview(av,bv,headroom) : bv;
            if(!paired && headroom<Hdr::MasterHeadroom) {
                const float y=.2126f*bv[0]+.7152f*bv[1]+.0722f*bv[2];
                if(y>1.f) {
                    const float x=std::min(y,Hdr::MasterHeadroom)-1.f;
                    const float mapped=1.f+x/(1.f+x*(1.f/std::max(.00001f,headroom-1.f)-1.f/(Hdr::MasterHeadroom-1.f)));
                    for(auto& v:expected) v*=mapped/y;
                }
            }
            for(int c=0;c<3;++c) {
                Require(std::isfinite(actual[i+c]),"Nonfinite HDR preview pixel.");
                worst=std::max(worst,std::abs(actual[i+c]-expected[c]));
            }
        }
    }
    Require(glGetError()==GL_NO_ERROR && worst<.00005f,"Photo preview shader disagrees with gain-map reconstruction.");
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glUseProgram(0);
    std::cout<<"Production preview shader, paired/unpaired at 1x, 2x and full headroom: worst linear error "<<worst<<'\n';
}
}
void ValidateHdrRenditionPair(const std::filesystem::path& hdrPath,
    const std::filesystem::path& sdrPath,const std::filesystem::path& output) {
    Require(!std::filesystem::exists(output),"Choose a new rendition-check output folder.");
    Hdr::Image hdr;Hdr::FileFormat hdrFormat;
    Require(Hdr::DecodeDisplaySource(Read(hdrPath),hdr,&hdrFormat) && Hdr::IsHdr(hdrFormat),
        "The supplied HDR rendition must be a PQ PNG or gain-map JPEG.");
    const auto old=Read(sdrPath);
    int w=0,h=0,c=0;
    stbi_set_flip_vertically_on_load_thread(0);
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> base(
        stbi_load_from_memory(old.data(),int(old.size()),&w,&h,&c,4),stbi_image_free);
    Require(base && w==hdr.width && h==hdr.height,"Supplied rendition dimensions differ.");
    const Hdr::PreparedSdrFinish finish;
    std::vector<unsigned char> sdr(hdr.rgba.size());
    for(std::size_t i=0;i<sdr.size();i+=4) {
        Hdr::Rgb a{},b{};
        for(int k=0;k<3;++k) {a[k]=Hdr::Linear(base.get()[i+k]/255.f);b[k]=hdr.rgba[i+k];}
        const auto rgb=finish.Apply(a,b);
        const auto imported=Hdr::SdrPreviewRgb(b,Hdr::FileFormat::PqPng);
        for(int k=0;k<3;++k) Require(std::abs(rgb[k]-imported[k])<1e-6f,"Imported PQ fallback differs from SDR finish.");
        for(int k=0;k<3;++k) sdr[i+k]=static_cast<unsigned char>(std::lround(255.f*Hdr::Srgb(rgb[k])));
        sdr[i+3]=base.get()[i+3];
    }
    CheckPreviewShader(hdr,sdr);
    const auto jpeg=Hdr::EncodeGainMapJpeg(hdr,sdr);
    Require(Hdr::IsGainMapJpeg(jpeg),"Output does not identify as Ultra HDR.");
    for(float headroom:{1.f,2.f,Hdr::MasterHeadroom}) {
        const auto decoded=Hdr::DecodeGainMapJpeg(jpeg,headroom);
        Require(decoded.width==w && decoded.height==h,"Gain-map decode changed dimensions.");
        double sum=0,worst=0;std::size_t count=0;
        for(int y=0;y<h;y+=4) for(int x=0;x<w;x+=4) {
            const auto i=4*(std::size_t(y)*w+x);
            Hdr::Rgb a{},b{};
            for(int k=0;k<3;++k) {a[k]=Hdr::Linear(sdr[i+k]/255.f);b[k]=hdr.rgba[i+k];}
            const auto expected=Hdr::GainMapPreview(a,b,headroom);
            for(int k=0;k<3;++k) {
                const double delta=std::abs(expected[k]-decoded.rgba[i+k]);
                Require(std::isfinite(delta),"Nonfinite gain-map sample.");
                sum+=delta;worst=std::max(worst,delta);++count;
            }
        }
        std::cout<<hdrPath.filename().u8string()<<" headroom "<<headroom<<": mean/worst linear error "<<sum/count<<"/"<<worst<<'\n';
        Require(sum/count<.035,"Gain-map output does not agree with the SDR/HDR finish.");
    }
    std::filesystem::create_directories(output);
    const auto jpg=output/"HDR-gain-map.jpg";
    std::ofstream f(jpg,std::ios::binary);f.write(reinterpret_cast<const char*>(jpeg.data()),jpeg.size());
    Require(bool(f),"Could not write gain-map inspection file.");f.close();
    const auto png=output/"SDR.png";
    NodeMath::PngColorMetadataChunks metadata;metadata.writeSrgb=true;
    std::vector<NodeMath::ContractIssue> issues;
    const auto pngBytes=NodeMath::InsertPngColorMetadataChunks(PngEncoding::EncodeInterleaved(sdr,w,h,4),metadata,issues);
    Require(!pngBytes.empty() && issues.empty(),"Could not encode tagged SDR inspection PNG.");
    std::ofstream pf(png,std::ios::binary);
    pf.write(reinterpret_cast<const char*>(pngBytes.data()),pngBytes.size());
    pf.close();Require(bool(pf),"Could not write SDR inspection file.");
    const auto sdrJpeg=Hdr::EncodeSdrJpeg(w,h,sdr);
    Require(!Hdr::IsGainMapJpeg(sdrJpeg),"SDR JPEG unexpectedly contains a gain map.");
    Hdr::Image profiled;
    Require(Hdr::DecodeProfiledSdr(sdrJpeg,profiled) && profiled.width==w && profiled.height==h,
        "SDR JPEG sRGB profile or extent did not reopen.");
    std::ofstream sf(output/"SDR.jpg",std::ios::binary);
    sf.write(reinterpret_cast<const char*>(sdrJpeg.data()),sdrJpeg.size());
    sf.close();Require(bool(sf),"Could not write SDR JPEG inspection file.");
    WriteHdrRenditionComparison(hdr,jpeg,sdr,output);
    sdr[3]=128;
    bool alphaRejected=false;
    try {Hdr::EncodeSdrJpeg(w,h,sdr);} catch(const std::exception&) {alphaRejected=true;}
    Require(alphaRejected,"SDR JPEG silently discarded transparency.");
    const auto preferences=output/"QueueExportSettings.json";
    {
        Queue::ExportPreferences settings(preferences);
        Require(settings.Read()==Hdr::FileFormat::GainMapJpeg,"Queue initial default is not HDR JPEG.");
        settings.Set(Hdr::FileFormat::SdrPng);
        settings.Set(Hdr::FileFormat::SdrJpeg);
        Require(settings.Flush().empty(),"Queue setting could not be saved.");
    }
    {
        Queue::ExportPreferences restored(preferences);
        Require(restored.Read()==Hdr::FileFormat::SdrJpeg,"Queue format did not survive owner restart.");
    }
    std::cout<<"SDR JPEG profile, alpha rejection, imported PQ parity and Queue preference reload passed.\n";
    std::cout<<"Wrote "<<jpg.u8string()<<" and "<<png.u8string()<<". Original renditions unchanged.\n";
}
}
