#include "StartupScene.h"
#include "StartupSceneShaders.h"
#include "Renderer/GLHelpers.h"
#include "stb_image.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace Stack::Startup {
namespace {
struct Vertex { float x,y,z,nx,ny,nz,u,v,part,material; };
static_assert(sizeof(Vertex)==40);
void Box(std::vector<Vertex>& out,float x,float y,float z,float w,float h,float d,float part=2) {
    const float p[8][3]={{x,y,z},{x+w,y,z},{x+w,y+h,z},{x,y+h,z},
        {x,y,z+d},{x+w,y,z+d},{x+w,y+h,z+d},{x,y+h,z+d}};
    const int faces[6][4]={{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{0,1,5,4},{3,7,6,2}};
    const float normals[6][3]={{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};
    for(int f=0;f<6;++f) for(int k:{0,1,2,0,2,3}) {
        const auto* a=p[faces[f][k]]; const auto* n=normals[f];
        out.push_back({a[0],a[1],a[2],n[0],n[1],n[2],0,0,part,0});
    }
}
void Flame(std::vector<Vertex>& out) {
    auto vertex=[](float u,float v) {
        const float r=std::sin(v),cy=std::cos(v);
        return Vertex{std::cos(u)*r*.014f,.21f+cy*.045f,std::sin(u)*r*.014f,
            std::cos(u)*r,cy,std::sin(u)*r,0,0,0,2};
    };
    for(int y=0;y<8;++y)for(int x=0;x<12;++x) {
        float u=x*6.2831853f/12,v=y*3.14159265f/8,du=6.2831853f/12,dv=3.14159265f/8;
        for(auto p:{vertex(u,v),vertex(u+du,v),vertex(u+du,v+dv),
            vertex(u,v),vertex(u+du,v+dv),vertex(u,v+dv)})out.push_back(p);
    }
}
std::vector<unsigned char> Blur(const DesktopImage& image) {
    if(image.rgba.empty()) return {};
    constexpr int radius=16;
    std::array<float,radius+1> weights{}; float sum=0;
    for(int i=0;i<=radius;++i) { weights[i]=std::exp(-float(i*i)/128.f);sum+=weights[i]*(i?2:1); }
    for(auto& w:weights)w/=sum;
    auto result=image.rgba;std::vector<unsigned char> scratch(result.size());
    for(int pass=0;pass<2;++pass) {
        for(int y=0;y<image.height;++y) for(int x=0;x<image.width;++x) {
            const size_t dest=(static_cast<size_t>(y)*image.width+x)*4;
            for(int c=0;c<3;++c) {
                float value=0;
                for(int i=-radius;i<=radius;++i) {
                    const int sx=std::clamp(x+(pass==0?i:0),0,image.width-1);
                    const int sy=std::clamp(y+(pass==1?i:0),0,image.height-1);
                    value+=result[(static_cast<size_t>(sy)*image.width+sx)*4+c]*weights[std::abs(i)];
                }
                scratch[dest+c]=static_cast<unsigned char>(value+.5f);
            }
            scratch[dest+3]=255;
        }
        result.swap(scratch);
    }
    return result;
}
}
bool Scene::Initialize(const std::filesystem::path& assets,const DesktopImage& desktop,int width,int height) {
    Shutdown();
    std::ifstream file(assets/"lantern.mesh",std::ios::binary);
    char magic[8]{};uint32_t count=0;
    file.read(magic,8);file.read(reinterpret_cast<char*>(&count),4);
    if(!file || std::memcmp(magic,"STKLANT1",8)!=0 || !count || count>100000 || count%3) return false;
    std::vector<Vertex> mesh(count);
    file.read(reinterpret_cast<char*>(mesh.data()),mesh.size()*sizeof(Vertex));
    if(!file)return false;
    for(const auto& v:mesh) {
        const float values[]={v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v,v.part,v.material};
        for(float value:values)if(!std::isfinite(value))return false;
    }
    Flame(mesh);
    // Front post, its metal peg, and a bounded receding board/support set.
    Box(mesh,1.1f,-.8f,.1f,.19f,2.83f,.19f);
    Box(mesh,.78f,1.88f,-.025f,.44f,.035f,.045f,3);
    Box(mesh,.78f,1.88f,-.025f,.035f,.085f,.045f,3);
    for(int i=0;i<25;++i) {
        const float z=.5f-i*.42f;
        Box(mesh,-.35f,.08f,z,1.7f,.085f,.39f);
        if(i%5==0) {
            Box(mesh,-.28f,-1.05f,z,.13f,1.13f,.14f);
            Box(mesh,1.1f,-1.05f,z,.13f,1.13f,.14f);
        }
    }
    // Glass is drawn after opaque objects, with depth testing but no depth writes.
    const auto glass=std::stable_partition(mesh.begin(),mesh.end(),[](const Vertex& v){
        return !(v.part<1.5f && v.material==1.f);
    });
    opaqueVertices_=static_cast<int>(glass-mesh.begin());
    int w=0,h=0,c=0;
    stbi_set_flip_vertically_on_load_thread(0);
    // filesystem streams preserve non-ASCII installation paths on Windows.
    std::ifstream imageFile(assets/"lantern.jpg",std::ios::binary|std::ios::ate);
    const auto imageSize=imageFile.tellg();
    if(!imageFile || imageSize<=0 || imageSize>8*1024*1024)return false;
    std::vector<unsigned char> encoded(static_cast<size_t>(imageSize));
    imageFile.seekg(0);imageFile.read(reinterpret_cast<char*>(encoded.data()),imageSize);
    if(!imageFile)return false;
    auto* pixels=stbi_load_from_memory(encoded.data(),static_cast<int>(encoded.size()),&w,&h,&c,4);
    if(!pixels)return false;
    if(w>1024 || h>1024) {stbi_image_free(pixels);return false;}
    albedo_=GLHelpers::CreateTextureFromPixels(pixels,w,h,4,true);stbi_image_free(pixels);
    geometry_=GLHelpers::CreateShaderProgram(Shaders::Vertex,Shaders::Material);
    backdrop_=GLHelpers::CreateShaderProgram(Shaders::Fullscreen,Shaders::Background);
    finish_=GLHelpers::CreateShaderProgram(Shaders::Fullscreen,Shaders::Finish);
    DesktopImage small;
    if(!desktop.rgba.empty()) {
        const float ratio=std::min(1.f,960.f/std::max(desktop.width,desktop.height));
        small.width=std::max(1,int(desktop.width*ratio));small.height=std::max(1,int(desktop.height*ratio));
        small.rgba.resize(size_t(small.width)*small.height*4);
        for(int y=0;y<small.height;++y)for(int x=0;x<small.width;++x) {
            const size_t from=(size_t(y*desktop.height/small.height)*desktop.width+x*desktop.width/small.width)*4;
            std::memcpy(&small.rgba[(size_t(y)*small.width+x)*4],&desktop.rgba[from],4);
        }
    }
    auto blurred=Blur(small);
    const unsigned char fallback[]={15,26,29,255};
    desktop_=GLHelpers::CreateTextureFromPixels(blurred.empty()?fallback:blurred.data(),
        blurred.empty()?1:small.width,blurred.empty()?1:small.height,4);
    sharpDesktop_=GLHelpers::CreateTextureFromPixels(desktop.rgba.empty()?fallback:desktop.rgba.data(),
        desktop.rgba.empty()?1:desktop.width,desktop.rgba.empty()?1:desktop.height,4);
    const float scale=std::min({1.f,1920.f/std::max(1,width),1080.f/std::max(1,height)});
    width_=std::max(1,static_cast<int>(width*scale));height_=std::max(1,static_cast<int>(height*scale));
    color_=GLHelpers::CreateTextureFromData(nullptr,width_,height_,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,GL_LINEAR);
    depth_=GLHelpers::CreateDepthTexture(width_,height_);
    framebuffer_=GLHelpers::CreateFBO(color_);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth_,0);
    const bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glGenVertexArrays(1,&vao_);glGenBuffers(1,&vbo_);
    glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,mesh.size()*sizeof(Vertex),mesh.data(),GL_STATIC_DRAW);
    const int sizes[]={3,3,2,2};const size_t offsets[]={0,12,24,32};
    for(unsigned i=0;i<4;++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsets[i]));
    }
    glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
    vertices_=static_cast<int>(mesh.size());
    if(!albedo_ || !geometry_ || !backdrop_ || !finish_ || !color_ || !depth_ || !desktop_ || !sharpDesktop_ || !complete) {
        Shutdown();return false;
    }
    return true;
}
void Scene::Render(float seconds,float handoff,float aspect,float intro) {
    if(!Ready())return;
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);
    glViewport(0,0,width_,height_);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glBlendEquation(GL_FUNC_ADD);glBindSampler(0,0);glBindSampler(1,0);
    glDepthMask(GL_TRUE);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
    glBindVertexArray(vao_);
    glUseProgram(backdrop_);glUniform1f(0,seconds);glUniform1f(1,aspect);glUniform1f(2,intro);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,sharpDesktop_);glBindSampler(2,0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,desktop_);
    glDrawArrays(GL_TRIANGLES,0,3);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(geometry_);glUniform1f(0,seconds);glUniform1f(1,aspect);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,albedo_);
    glDrawArrays(GL_TRIANGLES,0,opaqueVertices_);
    glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES,opaqueVertices_,vertices_-opaqueVertices_);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(finish_);glUniform1f(0,seconds);glUniform1f(1,aspect);glUniform1f(2,handoff);
    glDrawArrays(GL_TRIANGLES,0,3);
    glDisable(GL_BLEND);glUseProgram(0);glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
void Scene::PrimeWindow(int width,int height) {
    Render(0,0,float(width)/std::max(1,height),0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,width_,height_,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
void Scene::Shutdown() {
    for(auto* p:{&geometry_,&backdrop_,&finish_})if(*p){glDeleteProgram(*p);*p=0;}
    for(auto* p:{&color_,&depth_,&desktop_,&sharpDesktop_,&albedo_})if(*p){glDeleteTextures(1,p);*p=0;}
    if(framebuffer_){glDeleteFramebuffers(1,&framebuffer_);framebuffer_=0;}
    if(vbo_){glDeleteBuffers(1,&vbo_);vbo_=0;}
    if(vao_){glDeleteVertexArrays(1,&vao_);vao_=0;}
    vertices_=opaqueVertices_=width_=height_=0;
}
}
