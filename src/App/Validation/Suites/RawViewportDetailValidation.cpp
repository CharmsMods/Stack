#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"
#include "Editor/Internal/RawWorkspace/RawViewportDetailDrawing.h"
#include "Renderer/GLHelpers.h"
#include <backends/imgui_impl_opengl3.h>
#include <iostream>

namespace Stack::Validation {
bool ValidateRawViewportDetailDrawing() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(160,120);
    io.DeltaTime = 1.0f/60;
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) { ImGui::DestroyContext(); return false; }
    GLuint detail = 0;
    glGenTextures(1,&detail);
    glBindTexture(GL_TEXTURE_2D,detail);
    const float green[] = {0,1,0,1};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,1,1,0,GL_RGBA,GL_FLOAT,green);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    bool ok = true;
    const Raw::ViewportRegion region {4000,3000,1000,750,2000,1500};
    for (int scale : {1,2}) for (int view = 0; view < 3; ++view) {
        const int width = 160*scale, height = 120*scale;
        const auto target = GLHelpers::CreateEmptyTexture(width,height);
        const auto fbo = GLHelpers::CreateFBO(target);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glViewport(0,0,width,height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        io.DisplayFramebufferScale = ImVec2(float(scale),float(scale));
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(160,120));
        ImGui::Begin("detail",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground);
        auto& list = *ImGui::GetWindowDrawList();
        list.PushClipRect(ImVec2(10,12),ImVec2(150,108),false);
        list.AddRectFilled(ImVec2(0,0),ImVec2(160,120),IM_COL32(128,128,128,255));
        const ImVec2 minimum = view == 0 ? ImVec2(0,0) : view == 1 ? ImVec2(-80,-60) : ImVec2(35,-15);
        const ImVec2 maximum = view == 0 ? ImVec2(160,120) : view == 1 ? ImVec2(240,180) : ImVec2(195,105);
        Raw::DrawViewportDetail(list,detail,region,minimum,maximum);
        list.PopClipRect();
        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        std::vector<float> pixels(static_cast<std::size_t>(width)*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,pixels.data());
        for (int y = 3; y < 120; y += 8) for (int x = 3; x < 160; x += 8) {
            const float px = x + 0.5f/scale, py = y + 0.5f/scale;
            const bool clipped = px < 10 || px >= 150 || py < 12 || py >= 108;
            const bool inside = px >= minimum.x+(maximum.x-minimum.x)*0.25f && px < minimum.x+(maximum.x-minimum.x)*0.75f &&
                py >= minimum.y+(maximum.y-minimum.y)*0.25f && py < minimum.y+(maximum.y-minimum.y)*0.75f;
            const auto at = (static_cast<std::size_t>(height-1-y*scale)*width+x*scale)*4;
            const float expectedR = clipped || inside ? 0 : 128.0f/255;
            const float expectedG = clipped ? 0 : inside ? 1 : 128.0f/255;
            ok = ok && std::abs(pixels[at]-expectedR) < 0.005 && std::abs(pixels[at+1]-expectedG) < 0.005;
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDeleteFramebuffers(1,&fbo);
        glDeleteTextures(1,&target);
    }
    glDeleteTextures(1,&detail);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    std::cout << "RAW retained-detail drawing " << (ok ? "passed" : "FAILED") << ": fit, zoom and pan at 1x/2x scale.\n";
    return ok;
}
}
