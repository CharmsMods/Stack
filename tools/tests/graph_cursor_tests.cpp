#include "Utils/GraphCursor.h"
#include "Utils/GraphCursorBitmap.h"
#include "App/GraphNativeCursor.h"
#include "ThirdParty/stb_image_write.h"
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

void ValidateGraphCursor(const char* output) {
    using namespace ImGuiExtras;
    auto require=[](bool ok,const char* text) { if (!ok) throw std::runtime_error(text); };
    const auto palette=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
    const int graph=1;
    auto& io=ImGui::GetIO();
    ImGui::GetCurrentContext()->OpenPopupStack.clear(); ImGui::ClearActiveID();
    io.AddMouseButtonEvent(0,false); io.AddKeyEvent(ImGuiMod_Ctrl,false);
    auto frame=[&](ImVec2 pointer,bool hovered,bool drag,bool focused=true,bool graphVisible=true,const ImVec2* release=nullptr) {
        io.AddMousePosEvent(pointer.x,pointer.y); io.AddMouseButtonEvent(0,drag);
        io.DisplaySize=ImVec2(600,360); io.DeltaTime=1.0f/60;
        ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame(); BeginGraphCursorFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImVec2(600,360));
        ImGui::Begin("Cursor specimen",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        if (graphVisible) {
            ConfigureGraphCursor(&graph,ImVec2(0,0),ImVec2(600,360),true,palette,ImGui::GetWindowViewport());
            RegisterGraphCursorSurface(&graph,ImVec2(175,150),ImVec2(425,245),palette.nodeAppearance.surface);
            RequestGraphValueCursor(&graph,42,hovered,drag,ImVec2(300,130),palette.nodeAppearance.number);
        }
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(175,150),ImVec2(425,245),ImGui::GetColorU32(palette.nodeAppearance.surface),8);
        draw->AddText(ImVec2(190,169),ImGui::GetColorU32(palette.nodeAppearance.text),"Brightness");
        draw->AddText(ImVec2(365,205),ImGui::GetColorU32(palette.nodeAppearance.number),"1.750");
        ImGui::SetCursorPos(ImVec2(20,20)); ImGui::TextUnformatted("Graph cursor / input and visual motion are separate");
        ImGui::End();
        const auto inputBefore=io.MousePos;
        if (release) SetGraphCursorReleaseTarget(*release);
        RenderGraphCursor(focused,drag,true);
        require(io.MousePos.x==inputBefore.x && io.MousePos.y==inputBefore.y,"Cursor animation changed input coordinates");
        ImGui::Render(); glViewport(0,0,600,360); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return GetGraphCursorSnapshot();
    };
    auto save=[&](const char* suffix) {
        if (!output) return;
        std::vector<unsigned char> pixels(600*360*4); glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        stbi_flip_vertically_on_write(1);
        require(stbi_write_png((std::string(output)+suffix).c_str(),600,360,4,pixels.data(),600*4)!=0,"Write cursor specimen");
    };
    frame(ImVec2(90,100),false,false); auto state=frame(ImVec2(90,100),false,false);
    require(state.visible && state.nativeDot && state.position.x==90 && state.position.y==100 && state.morph==0,"Idle dot uses the native pointer"); save(".cursor-idle.png");
    const auto nativeState=state;
    for (float scale:{1.0f,1.5f,2.0f}) {
        const auto bitmap=BuildGraphCursorBitmap(scale,state.dotColor,state.glowColor,0);
        for (int y=0;y<bitmap.size;++y) for (int x=0;x<bitmap.size;++x) {
            const float dx=float(x-bitmap.hotspot),dy=float(y-bitmap.hotspot),r=std::sqrt(dx*dx+dy*dy);
            const auto alpha=bitmap.pixels[(y*bitmap.size+x)*4+3];
            if (r<=4*scale-0.5f) require(alpha==255,"Native dot must have an opaque interior");
            if (r>=4*scale+0.5f) require(alpha==0,"Native idle dot has no feather outside its AA boundary");
        }
        if (output && scale==2) {
            stbi_flip_vertically_on_write(0);
            require(stbi_write_png((std::string(output)+".native-dot.png").c_str(),bitmap.size,bitmap.size,4,
                bitmap.pixels.data(),bitmap.size*4)!=0,"Write native cursor bitmap");
        }
    }
    auto* window=glfwGetCurrentContext();
    const auto originalFlags=io.ConfigFlags;
    GraphNativeCursor::Apply(window,state,false);
    require((io.ConfigFlags&ImGuiConfigFlags_NoMouseCursorChange)!=0,"Native dot owns platform cursor updates");
    require(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"Native dot remains an OS-positioned cursor");
    for (int i=0;i<20;++i) state=frame(ImVec2(380,211),true,false);
    require(state.glow>0.9f && state.morph==0,"Value hover glows without creating a ring"); save(".cursor-hover.png");
    state=frame(ImVec2(400,211),true,true);
    require(state.adjusting && !state.nativeDot && state.morph>0 && state.morph<1,"Drag begins a software morph");
    glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_DISABLED);
    GraphNativeCursor::Apply(window,state,true);
    require(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED,"Cursor artwork must preserve raw capture");
    glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
    for (int i=0;i<20;++i) state=frame(ImVec2(460+i,280),false,true);
    require(state.morph==1 && state.position.x==300 && state.position.y==130,"Wheel stays above node while input moves");
    const auto angle=state.rotation; frame(ImVec2(480,280),false,true); state=frame(ImVec2(480,280),false,true);
    require(state.rotation!=angle,"Adjustment wheel rotates independently of mouse motion"); save(".cursor-wheel.png");
    // Every interior pixel belongs to a sector, including the center and joins.
    unsigned char wheel[22*22*4]{};
    glReadPixels(289,360-141,22,22,GL_RGBA,GL_UNSIGNED_BYTE,wheel);
    const ImVec4 colors[]{palette.analysisSocket,palette.imageSocket,palette.maskSocket,palette.valueSocket,palette.selection,palette.text};
    for (int y=0;y<22;++y) for (int x=0;x<22;++x) {
        const float dx=x-10.5f,dy=y-10.5f;
        if (dx*dx+dy*dy>64) continue;
        const auto* pixel=wheel+(y*22+x)*4;
        bool matches=false;
        for (const auto& color:colors) {
            const auto packed=ImGui::ColorConvertFloat4ToU32(color);
            matches|=std::abs(int(pixel[0])-int((packed>>IM_COL32_R_SHIFT)&255))<=2 &&
                std::abs(int(pixel[1])-int((packed>>IM_COL32_G_SHIFT)&255))<=2 &&
                std::abs(int(pixel[2])-int((packed>>IM_COL32_B_SHIFT)&255))<=2;
        }
        require(matches,"Filled wheel contains a gap or translucent internal seam");
    }
    const ImVec2 pickup(400,211);
    state=frame(ImVec2(2480,1280),false,false,true,true,&pickup);
    require(state.returning && state.morph<1,"Release reverses the morph");
    require(state.position.x<310 && state.position.y<140,"Release uses restored pickup instead of accumulated raw coordinates");
    for (int i=0;i<16;++i) state=frame(pickup,false,false);
    require(state.nativeDot && state.position.x==pickup.x && state.position.y==pickup.y,"Stationary release returns exactly to pickup");
    for (int i=0;i<20;++i) frame(ImVec2(380,211),true,true);
    frame(ImVec2(2480,1280),false,false,true,true,&pickup);
    for (int i=0;i<16;++i) state=frame(ImVec2(510,290),false,false);
    require(!state.returning && state.position.x==510 && state.position.y==290,"Physical movement after release remains unrestricted");
    for (int i=0;i<20;++i) frame(ImVec2(380,211),true,true);
    frame(ImVec2(480,280),false,false);
    // A new click with no adjustment request interrupts return immediately.
    frame(ImVec2(500,290),false,false);
    io.AddMouseButtonEvent(1,true); state=frame(ImVec2(500,290),false,false);
    require(!state.returning && state.position.x==500,"New click interrupts return animation");
    io.AddMouseButtonEvent(1,false);
    frame(ImVec2(380,211),true,true); state=frame(ImVec2(380,211),true,true,false);
    require(!state.visible,"Focus loss clears custom cursor");
    state=frame(ImVec2(380,211),true,false,true,false);
    require(!state.visible,"Leaving graph clears custom cursor");
    GraphNativeCursor::Apply(window,state,false);
    require(io.ConfigFlags==originalFlags,"Native cursor ownership must not leak outside graph");
    io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    GraphNativeCursor::Apply(window,nativeState,false);
    GraphNativeCursor::Apply(window,state,false);
    require((io.ConfigFlags&ImGuiConfigFlags_NoMouseCursorChange)!=0,"Preserve preexisting platform cursor ownership");
    io.ConfigFlags=originalFlags;
    GraphNativeCursor::Shutdown(window);
    frame(ImVec2(90,100),false,false);
}
