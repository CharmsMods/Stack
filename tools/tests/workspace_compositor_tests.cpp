#include "Renderer/GLLoader.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/WorkspaceCompositor.h"
#include "App/WorkspaceSwitcher.h"
#include "App/WorkspaceSwitcherDrawing.h"
#include "App/WorkspaceInputScope.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <chrono>

static void Check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    if(!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(800,600,"Workspace compositor validation",nullptr,nullptr);
    if(!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window); int result=0;
    try {
        Check(LoadGLFunctions(),"GL loader failed");
        std::cout << "GPU: " << glGetString(GL_RENDERER) << '\n';
        ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
        ImGui_ImplOpenGL3_Init("#version 430 core");
        Stack::Renderer::WorkspaceCompositor compositor;
        Stack::Workspace::Switcher sw; sw.Begin(0); sw.Tick(1);
        bool greenWorkspace=false;
        auto frame=[&](int w,int h) {
            io.DisplaySize=ImVec2(float(w),float(h)); io.DeltaTime=1.f/60;
            ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
            // Fresh startup has never allocated a popup entry.
            Stack::Workspace::CloseOrdinaryPopups();
            ImGui::OpenPopup("validation popup");
            Stack::Workspace::CloseOrdinaryPopups();
            Check(ImGui::GetCurrentContext()->OpenPopupStack.empty(),"popup did not close");
            Stack::Workspace::CloseOrdinaryPopups();
            auto* background=ImGui::GetBackgroundDrawList();
            background->AddRectFilled(ImVec2(0,0),ImVec2(float(w),h*.5f),greenWorkspace ? IM_COL32(20,220,30,255) : IM_COL32(220,50,30,255));
            background->AddRectFilled(ImVec2(0,h*.5f),ImVec2(float(w),float(h)),IM_COL32(30,70,220,255));
            for(int i=1;i<12;++i) background->AddLine(ImVec2(w*i/12.f,0),ImVec2(w*i/12.f,float(h)),IM_COL32(250,250,250,160),2);
            background->AddText(ImVec2(30,18),IM_COL32_WHITE,"File     Settings");
            auto* fixed=ImGui::GetForegroundDrawList();
            fixed->AddRectFilled(ImVec2(8,8),ImVec2(28,28),IM_COL32(0,255,0,255));
            compositor.BeginFrame(); compositor.KeepFixed(fixed);
            // Validate direct polling suppression and restoration independently of widgets.
            const ImVec2 before=io.MousePos;
            io.MouseClickedCount[0]=2;
            { Stack::Workspace::InputScope scope(true,false); Check(!ImGui::IsMouseDown(0),"preview leaks mouse input");
                Check(!ImGui::IsMouseDoubleClicked(0),"preview leaks double click"); }
            Check(io.MouseClickedCount[0]==2,"input scope did not restore click count"); io.MouseClickedCount[0]=0;
            Check(io.MousePos.x==before.x,"input scope did not restore cursor");
            ImGui::Render();
        };
        frame(800,600);
        glViewport(0,0,800,600);
        glEnable(GL_SCISSOR_TEST); glEnable(GL_CULL_FACE);
        Check(compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1)),"compositor failed");
        Check(glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_CULL_FACE),"compositor leaked GL enable state");
        glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
        auto pixel=[](int x,int y) { std::array<unsigned char,4> p{}; glReadPixels(x,599-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data()); return p; };
        auto top=pixel(410,160),bottom=pixel(410,450),fixed=pixel(15,15);
        Check(top[0]>top[2]*2,"texture vertically inverted at top");
        Check(bottom[2]>bottom[0]*2,"texture vertically inverted at bottom");
        Check(fixed[1]>240 && fixed[0]<5,"fixed overlay curved or disappeared");
        Check(pixel(400,52)[0]<40 && pixel(100,52)[0]>80,"screen center is not shorter than edges");
        greenWorkspace=true; sw.preview=1;
        frame(800,600); compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1));
        auto fadeStart=pixel(410,160);
        Check(fadeStart[0]>fadeStart[1]*2,"preview snapped at fade start");
        for(int i=0;i<5;++i) { frame(800,600); compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1)); }
        auto middle=pixel(410,160);
        Check(middle[1]>fadeStart[1]+20 && middle[0]>50,"preview did not crossfade");
        greenWorkspace=false; sw.preview=5;
        frame(800,600); compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1));
        auto reversed=pixel(410,160);
        Check(std::abs(int(reversed[0])-int(middle[0]))<3,"interrupted fade jumped");
        for(int i=0;i<15;++i) { frame(800,600); compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1)); }
        sw.progress=0;
        compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1));
        Check(pixel(410,160)[0]>210,"identity composition changed color");
        sw.progress=1;
        // The ring and labels use the same logical geometry as hit testing.
        io.DisplaySize=ImVec2(800,600); ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
        auto* bg=ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(ImVec2(0,0),ImVec2(800,600),IM_COL32(45,65,82,255));
        for(int i=0;i<14;++i) bg->AddRect(ImVec2(30+i*52.f,70),ImVec2(65+i*52.f,550),IM_COL32(145,155,165,255));
        sw.Move(0,-75); auto* ring=Stack::Workspace::DrawSwitcher(sw,ImGui::GetMainViewport());
        compositor.BeginFrame(); compositor.KeepFixed(ring);
        ImGui::Render(); compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1));
        if(argc>1) {
            std::vector<unsigned char> rgb(800*600*3); glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,800,600,GL_RGB,GL_UNSIGNED_BYTE,rgb.data());
            std::ofstream out(argv[1],std::ios::binary); out << "P6\n800 600\n255\n";
            for(int y=599;y>=0;--y) out.write(reinterpret_cast<char*>(rgb.data()+y*800*3),800*3);
        }
        for(const auto size : {ImVec2(1920,1080),ImVec2(3840,2160)}) {
            int w=int(size.x),h=int(size.y);
            auto tex=GLHelpers::CreateTextureFromPixels(nullptr,w,h,4); auto fb=GLHelpers::CreateFBO(tex);
            glBindFramebuffer(GL_FRAMEBUFFER,fb); glViewport(0,0,w,h); frame(w,h);
            auto measure=[&](bool composed) {
                for(int i=0;i<8;++i) { if(composed) compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1)); else ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); }
                glFinish(); GLuint timer[2]{}; glGenQueries(2,timer);
                glQueryCounter(timer[0],GL_TIMESTAMP);
                for(int i=0;i<40;++i) { if(composed) compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1)); else ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); }
                glQueryCounter(timer[1],GL_TIMESTAMP); glFinish();
                GLuint64 start=0,end=0; glGetQueryObjectui64v(timer[0],GL_QUERY_RESULT,&start); glGetQueryObjectui64v(timer[1],GL_QUERY_RESULT,&end);
                glDeleteQueries(2,timer); return double(end-start)/1e6/40;
            };
            const double baseline=measure(false), composed=measure(true);
            std::cout << w << 'x' << h << " baseline=" << baseline << "ms composed=" << composed << "ms added=" << composed-baseline << "ms\n";
            glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&fb); glDeleteTextures(1,&tex);
        }
        for(int i=0;i<30;++i) { frame(800+i,600+i); compositor.Render(ImGui::GetDrawData(),sw,ImVec4(0,0,0,1)); }
        auto* draw=ImGui::GetDrawData();
        draw->DisplaySize=ImVec2(0,0);
        Check(!compositor.Render(draw,sw,ImVec4(0,0,0,1)),"zero-size viewport was rendered");
        GLint maximum=0; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
        draw->DisplaySize=ImVec2(float(maximum+1),600);
        Check(!compositor.Render(draw,sw,ImVec4(0,0,0,1)),"oversized allocation did not fall back");
        draw->DisplaySize=ImVec2(800,600);
        Check(!compositor.Render(draw,sw,ImVec4(0,0,0,1)),"failed effects retried every frame");
        ImGui_ImplOpenGL3_RenderDrawData(draw);
        compositor.Shutdown(); compositor.Shutdown();
        Check(glGetError()==GL_NO_ERROR,"GL error after repeated resize/shutdown");
        ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext();
        std::cout << "Workspace compositor orientation, curvature, overlay, identity, resize and cleanup passed.\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; result=1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
