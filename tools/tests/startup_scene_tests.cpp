#include "App/Startup/StartupScene.h"
#include "Renderer/GLHelpers.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"
#include <filesystem>
#include <iostream>
#include <vector>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

int main(int argc,char** argv) {
    if(argc!=3 || !glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(960,540,"Stack startup check",nullptr,nullptr);
    if(!window)return 2;
    glfwMakeContextCurrent(window);
    if(!LoadGLFunctions())return 3;
    bool ok=true;
    auto check=[&](bool value,const char* message){if(!value){std::cerr<<message<<'\n';ok=false;}};
    Stack::Startup::Scene scene;
    // Empty capture exercises the supported gradient fallback, without saving desktop pixels.
    check(scene.Initialize(argv[1],{},960,540),"scene initialization failed");
    if(scene.Ready()) {
        std::filesystem::create_directories(argv[2]);
        std::vector<unsigned char> pixels(960*540*4);
        for(float t:{.50f,.86f,2.8f}) {
            scene.Render(t,0,960.f/540);
            check(glGetError()==GL_NO_ERROR,"scene draw GL error");
            glBindTexture(GL_TEXTURE_2D,scene.Texture());
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            bool opaque=true;
            for(size_t i=3;i<pixels.size();i+=4)if(pixels[i]!=255){opaque=false;break;}
            check(opaque,"scene must remain opaque over transparent native framebuffer");
            stbi_flip_vertically_on_write(1);
            const auto path=std::filesystem::path(argv[2])/("scene-"+std::to_string(t)+".png");
            check(stbi_write_png(path.string().c_str(),960,540,4,pixels.data(),960*4)!=0,"frame write failed");
        }
        const auto texture=scene.Texture();
        scene.Shutdown();
        check(!scene.Ready() && !glIsTexture(texture),"scene texture retained after shutdown");
        scene.Shutdown();
    }
    check(!scene.Initialize(std::filesystem::path(argv[1])/"missing",{},960,540),"missing asset did not fail safely");
    Stack::Startup::WindowPresentation presentation;
#ifdef _WIN32
    HWND hwnd=glfwGetWin32Window(window);
    const auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
#endif
    { auto capture=presentation.Prepare(window); } // capture released without serialization
    check(presentation.Active(),"native presentation failed");
    check(!presentation.DisplayChanged(),"unchanged monitor detected as changed");
#ifdef _WIN32
    RECT rect{};GetWindowRect(hwnd,&rect);
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTOPRIMARY),&info);
    check(EqualRect(&rect,&info.rcMonitor)!=FALSE,"startup does not cover the monitor");
#endif
    presentation.Show();
    presentation.Finish();
#ifdef _WIN32
    check(IsZoomed(hwnd)!=FALSE,"handoff did not maximize");
    check((GetWindowLongPtrW(hwnd,GWL_STYLE)&(WS_CAPTION|WS_THICKFRAME))==(style&(WS_CAPTION|WS_THICKFRAME)),"native chrome not restored");
    ShowWindow(hwnd,SW_HIDE);
#endif
    check(!presentation.Active(),"native presentation not retired");
    glfwDestroyWindow(window);glfwTerminate();
    std::cout<<(ok?"Startup scene checks passed\n":"Startup scene checks failed\n");
    return ok?0:1;
}
