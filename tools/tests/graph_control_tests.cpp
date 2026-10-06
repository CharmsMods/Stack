#include "App/settings/NodeControlStyle.h"
#include "Editor/NodeGraph/UI/ContinuousLinkStroke.h"
#include "App/settings/PrimaryAction.h"
#include <cstring>
#include "App/settings/CreamPalette.h"
#include "Utils/GraphNumericControls.h"
#include <memory>
#include "Utils/ImGuiExtras.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"
#include <iostream>
#include <stdexcept>
#include <vector>

void ValidateGraphCursor(const char* output);

namespace {
void Require(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}

float value = 0.0f;
int integerValue = 3;
bool changed = false;
bool nodeWheelTest = false;
bool primaryTest = false;
bool menuSpecimen = false;
bool selectedSpecimen = false;
bool curveSpecimen=false;
bool disablePrimary = false;
int primaryClicks = 0;
ImVec2 rowSize;
ImGuiExtras::GraphNodeControlScopeConfig config;

void Frame(bool specimen = false) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(600, 360);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGuiExtras::BeginFrameInputRouting();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGuiExtras::BeginGraphWheelFrame(&config, nodeWheelTest);
    std::unique_ptr<ImGuiExtras::GraphNumericNodeScope> nodeScope;
    if (nodeWheelTest || specimen) nodeScope = std::make_unique<ImGuiExtras::GraphNumericNodeScope>(
        &config, 7, ImGui::GetIO().MousePos.x < 350, false);
    int preservedVertex=-1, fadedVertex=-1;
    if (nodeWheelTest && !specimen) {
        auto* draw=ImGui::GetWindowDrawList();
        preservedVertex=draw->VtxBuffer.Size;
        draw->AddRectFilled(ImVec2(10,10),ImVec2(20,20),IM_COL32(100,70,40,255));
        nodeScope->PreserveCurrentDrawing();
        fadedVertex=draw->VtxBuffer.Size;
        draw->AddRectFilled(ImVec2(10,24),ImVec2(20,34),IM_COL32(100,70,40,255));
    }
    ImGui::SetCursorPos(ImVec2(40, 50));
    ImGuiExtras::ResetNodeControlState();
    ImGuiExtras::BeginGraphNodeControlScope(config);
    std::unique_ptr<StackAppearance::ScopedNodeControlStyle> nodeStyle;
    const auto nodePalette=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
    if (specimen) {
        using namespace Stack::Editor::NodeGraphUIVisuals;
        auto* draw=ImGui::GetWindowDrawList();
        GraphStyleTokens tokens; tokens.enabled=true; tokens.nodeAppearance=nodePalette.nodeAppearance;
        tokens.selected=nodePalette.nodeAppearance.selection;
        DrawGraphNodeSpotlightSurface(draw,ImVec2(30,42),ImVec2(352,178),{}, {}, {},tokens,
            selectedSpecimen,true,1,7,1);
        nodeScope->PreserveCurrentDrawing();
        nodeStyle=std::make_unique<StackAppearance::ScopedNodeControlStyle>(nodePalette.nodeAppearance);
    }
    ImGuiExtras::SetNextNodeNumericDefault(0.0);
    changed = ImGuiExtras::NodeSliderFloat("Exposure", "exposure", &value, -16, 16, "%.3f EV", 300);
    rowSize = ImGui::GetItemRectSize();
    if (!specimen) {
        ImGui::SetCursorPos(ImVec2(40, 94));
        ImGuiExtras::SetNextNodeNumericDefault(2.0);
        ImGuiExtras::NodeSliderInt("Iterations", "iterations", &integerValue, 1, 10, "%d", 300);
    }
    if (specimen) {
        ImGui::SetCursorPos(ImVec2(40, 20));
        ImGui::TextColored(nodePalette.text,"Production numeric controls and socket glyphs");
        ImGui::SetCursorPos(ImVec2(40, 94));
        float radius = 0.35f;
        ImGuiExtras::SetNextNodeNumericDefault(0.35);
        ImGuiExtras::NodeSliderFloat("Radius", "radius", &radius, 0.01f, 1.5f, "%.3f", 300);
        ImGui::SetCursorPos(ImVec2(40, 138));
        float wide = 25;
        ImGuiExtras::SetNextNodeNumericDefault(0.0);
        ImGuiExtras::NodeSliderFloat("Long parameter label", "long", &wide, -100, 100, "%.1f %%", 300);
        ImGui::SetCursorPos(ImVec2(40,164));
        nodeStyle.reset();
        nodeScope.reset();
        using namespace Stack::Editor::NodeGraphUIVisuals;
        GraphStyleTokens tokens;
        tokens.enabled=true; tokens.nodeAppearance=nodePalette.nodeAppearance;
        tokens.canvas = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        tokens.text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const EditorNodeGraph::SocketType types[] = {
            EditorNodeGraph::SocketType::Image, EditorNodeGraph::SocketType::Mask,
            EditorNodeGraph::SocketType::Raw, EditorNodeGraph::SocketType::Channel,
            EditorNodeGraph::SocketType::Scalar, EditorNodeGraph::SocketType::Spectrum };
        const char* names[] = { "Image", "Mask", "RAW", "Channel", "Scalar", "Spectrum" };
        const ImVec4 socketColors[]{nodePalette.imageSocket,nodePalette.maskSocket,nodePalette.rawSocket,nodePalette.valueSocket,nodePalette.valueSocket,nodePalette.analysisSocket};
        for (int i = 0; i < 6; ++i) {
            EditorNodeGraph::SocketDefinition socket;
            socket.type = types[i];
            const float x = 62.0f + i * 88.0f;
            DrawSocketPin(ImGui::GetWindowDrawList(), ImVec2(x, 235), 7.1f,
                ImGui::GetColorU32(socketColors[i]), tokens, i==0, socket, false);
            DrawSocketPin(ImGui::GetWindowDrawList(), ImVec2(x, 268), 7.1f,
                ImGui::GetColorU32(socketColors[i]), tokens, i==1, socket, true, i==1 ? 1.0f : 0.0f);
            ImGui::SetCursorPos(ImVec2(x - 20, 294)); ImGui::TextUnformatted(names[i]);
        }
    }
    if (specimen) {
        const auto palette=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
        const char* groupNames[]{"Blue", "Teal", "Olive"};
        auto* draw=ImGui::GetWindowDrawList();
        for (int i=0;i<3;++i) {
            auto choice=StackAppearance::CreamPalette{}; choice.numberAccent=static_cast<StackAppearance::NodeAccent>(i);
            const auto n=StackAppearance::ResolveCreamPalette(choice).nodeAppearance;
            const ImVec2 a(18.0f+i*194.0f,318.0f), b(a.x+184.0f,352.0f);
            draw->AddRectFilled(a,b,ImGui::GetColorU32(n.surface),5.0f);
            draw->AddText(ImVec2(a.x+9,a.y+10),ImGui::GetColorU32(n.text),groupNames[i]);
            draw->AddText(ImVec2(a.x+122,a.y+10),ImGui::GetColorU32(n.number),"1.750");
        }
        using namespace Stack::Editor::NodeGraphUIVisuals;
        GraphStyleTokens tokens; tokens.enabled=true; tokens.nodeAppearance=palette.nodeAppearance; tokens.selected=palette.nodeAppearance.selection;
        DrawGraphNodeSpotlightSurface(draw,ImVec2(372,42),ImVec2(580,178),{}, {}, {},tokens,false,true,1,7,1);
        StackAppearance::ScopedNodeControlStyle complexStyle(palette.nodeAppearance);
        ImGui::SetCursorPos(ImVec2(384,50)); ImGui::TextUnformatted("Complex controls");
        bool enabled=true; ImGui::SetCursorPos(ImVec2(384,73)); ImGui::Checkbox("Enabled",&enabled);
        const char* modes[]{"Overlay","Normal"}; int mode=0;
        ImGui::SetCursorPos(ImVec2(384,103)); ImGui::SetNextItemWidth(180); ImGui::Combo("##mode",&mode,modes,2);
        ImGui::SetCursorPos(ImVec2(384,139)); ImGui::BeginDisabled(); ImGui::Button("Unavailable",ImVec2(180,25)); ImGui::EndDisabled();

    }
    if (primaryTest) {
        const auto palette=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
        ImGui::SetCursorPos(ImVec2(380,180));
        ImGui::BeginDisabled(disablePrimary);
        if (StackAppearance::PrimaryActionButton("Primary test",ImVec2(140,26),&palette.primaryAction)) ++primaryClicks;
        ImGui::EndDisabled();
    }
    const auto beforeNode=ImGui::GetStyle();
    const int nodeColorDepth=ImGui::GetCurrentContext()->ColorStack.Size;
    {
        StackAppearance::ScopedNodeControlStyle scoped(nodePalette.nodeAppearance);
        Require(ImGui::GetStyleColorVec4(ImGuiCol_Text).x < ImGui::GetStyleColorVec4(ImGuiCol_FrameBg).x,"Dark text on light node controls");
    }
    Require(std::memcmp(beforeNode.Colors,ImGui::GetStyle().Colors,sizeof(beforeNode.Colors))==0 &&
        beforeNode.DisabledAlpha==ImGui::GetStyle().DisabledAlpha && nodeColorDepth==ImGui::GetCurrentContext()->ColorStack.Size,
        "Node control styling leaked into application controls");
    // Button colors and disabled alpha must never escape their local scope.
    const auto palette=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
    for (bool disabled : {false,true}) {
        ImGui::BeginDisabled(disabled);
        const ImGuiStyle before=ImGui::GetStyle();
        const int colorDepth=ImGui::GetCurrentContext()->ColorStack.Size;
        const int varDepth=ImGui::GetCurrentContext()->StyleVarStack.Size;
        {
            StackAppearance::ScopedPrimaryActionStyle primary(palette.primaryAction);
            Require(ImGui::GetStyleColorVec4(ImGuiCol_Text).x < ImGui::GetStyleColorVec4(ImGuiCol_Button).x,
                "Primary action has dark foreground on cream");
        }
        Require(std::memcmp(before.Colors,ImGui::GetStyle().Colors,sizeof(before.Colors))==0 &&
            before.Alpha==ImGui::GetStyle().Alpha && colorDepth==ImGui::GetCurrentContext()->ColorStack.Size &&
            varDepth==ImGui::GetCurrentContext()->StyleVarStack.Size,"Primary action style leaked");
        ImGui::EndDisabled();
    }
    if (specimen && menuSpecimen) {
        ImGui::SetNextWindowPos(ImVec2(375,175));
        ImGui::OpenPopup("Menu specimen");
        if (ImGui::BeginPopup("Menu specimen")) {
            ImGui::MenuItem("Copy value");
            ImGui::MenuItem("Reset value");
            ImGui::EndPopup();
        }
    }
    if (curveSpecimen) {
        using namespace Stack::Editor::NodeGraphUIVisuals;
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(0,0),ImVec2(600,360),ImGui::GetColorU32(nodePalette.canvas));
        draw->AddText(ImVec2(20,15),ImGui::GetColorU32(nodePalette.text),"Tight reverse bend / joined antialiased stroke");
        const ImVec2 a(400,85),b(1000,85),c(-400,265),d(210,265);
        DrawContinuousLinkStroke(draw,a,b,c,d,ImGui::GetColorU32(nodePalette.imageSocket),9,false,ImVec2(0,0),ImVec2(600,360),25);
        GraphStyleTokens tokens; tokens.enabled=true; tokens.nodeAppearance=nodePalette.nodeAppearance; tokens.canvas=nodePalette.canvas;
        EditorNodeGraph::SocketDefinition socket; socket.type=EditorNodeGraph::SocketType::Image;
        DrawSocketPin(draw,a,12,ImGui::GetColorU32(nodePalette.imageSocket),tokens,false,socket,true);
        DrawSocketPin(draw,d,12,ImGui::GetColorU32(nodePalette.imageSocket),tokens,false,socket,true);
    }
    ImGuiExtras::EndGraphNodeControlScope();
    nodeScope.reset();
    if (preservedVertex>=0 && ImGuiExtras::GraphWheelTargetNode(&config)==7) {
        auto* draw=ImGui::GetWindowDrawList();
        Require(draw->VtxBuffer[preservedVertex].col==IM_COL32(100,70,40,255),"Ctrl-wheel preserves the selection drawing");
        const auto faded=ImGui::ColorConvertU32ToFloat4(draw->VtxBuffer[fadedVertex].col);
        const auto bg=nodePalette.nodeAppearance.surface;
        Require(faded.x >= 100.0f/255 && faded.x <= (100.0f/255*0.25f+bg.x*0.75f)+0.005f,"Ctrl-wheel fades toward 25 percent contrast without overshooting");
    }
    ImGui::End();
    ImGui::Render();
    glViewport(0, 0, 600, 360);
    glClearColor(0.12f, 0.13f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Click(int button, float x = 300, float y = 60) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y); Frame();
    io.AddMouseButtonEvent(button, true); Frame();
    io.AddMouseButtonEvent(button, false); Frame(); Frame(); Frame();
}

void ReplaceText(const char* text) {
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); Frame();
    io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
    io.AddKeyEvent(ImGuiKey_Backspace, true); Frame();
    io.AddKeyEvent(ImGuiKey_Backspace, false); io.AddInputCharactersUTF8(text); Frame();
}

void Key(ImGuiKey key) {
    auto& io = ImGui::GetIO(); io.AddKeyEvent(key, true); Frame();
    io.AddKeyEvent(key, false); Frame();
}
}

int main(int argc, char** argv) {
    if (!glfwInit()) return 2;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(600, 360, "Graph controls validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 3; }
    glfwMakeContextCurrent(window);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsLight();
        const auto cream=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
        for (int i=0;i<ImGuiCol_COUNT;++i) ImGui::GetStyle().Colors[i]=cream.colors[i];
    ImGui_ImplOpenGL3_Init("#version 130");
    config.allowSliderTextEntry = true;
    config.useScrubHandles = true;
    config.rangePolicy = ImGuiExtras::GraphSliderRangePolicy::Bounded;
    int result = 0;
    try {
        using namespace Stack::Editor::NodeGraphUIVisuals;
        const ImVec2 a(800,30), b(1400,30), c(-500,500), d(100,500);
        const auto curve=SampleLinkCurve(a,b,c,d);
        Require(curve.front().point.x==a.x && curve.front().point.y==a.y && curve.back().point.x==d.x && curve.back().point.y==d.y,"Curve keeps exact socket-center endpoints");
        Require(curve.size()>160,"Extreme zoomed bend exceeds old fixed sampling cap");
        for (size_t i=1;i<curve.size();++i)
            Require(std::hypot(curve[i].point.x-curve[i-1].point.x,curve[i].point.y-curve[i-1].point.y)<=4.01f,"Curve subdivision remains screen-space accurate");
        Frame(); Frame();
        const ImVec2 idleSize = rowSize;
        Click(ImGuiMouseButton_Left);
        Require(!ImGui::GetIO().WantTextInput, "Left click must not switch to text mode");
        Click(ImGuiMouseButton_Right,160,60);
        Require(!ImGui::GetIO().WantTextInput,"Blank space left of a value must not be a value editing target");
        Click(ImGuiMouseButton_Right);
        Require(ImGui::GetIO().WantTextInput, "Right click must open inline text entry");
        ReplaceText("1.75");
        Click(ImGuiMouseButton_Left, 440, 60);
        Require(value == 1.75f, "Outside click commits a valid value");
        Require(rowSize.x == idleSize.x && rowSize.y == idleSize.y, "Editing and reset visibility must not move the row");
        Click(ImGuiMouseButton_Left);
        Require(ImGui::GetIO().WantTextInput, "Outside click must retain text mode");
        ReplaceText("1e");
        Click(ImGuiMouseButton_Left, 440, 60);
        Require(value == 1.75f, "Invalid text must preserve the previous value");
        Click(ImGuiMouseButton_Left);
        ReplaceText("9999"); Key(ImGuiKey_Enter);
        Require(value == 1.75f, "Out-of-domain text must not silently clamp");
        Click(ImGuiMouseButton_Right);
        Require(!ImGui::GetIO().WantTextInput, "Right click must return to dragging");
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(300,60); io.AddMouseButtonEvent(0,true); Frame();
        io.AddMousePosEvent(304,60); Frame();
        Require(value > 1.75f, "Dragging the number changes its value");
        const float moved=value; Frame(); Frame(); Frame();
        Require(value==moved,"Stationary held pointer must never keep changing a value");
        io.AddMousePosEvent(308,60); Frame();
        Require(std::abs((value-moved)-(moved-1.75f))<0.0001f,"Equal pointer distances must produce equal value changes");
        Key(ImGuiKey_Escape); io.AddMouseButtonEvent(0,false); Frame();
        Require(value == 1.75f, "Escape cancels a drag");
        Click(ImGuiMouseButton_Left, 252, 60);
        Require(value == 0.0f, "Reset restores the supplied default");
        Require(rowSize.x == idleSize.x && rowSize.y == idleSize.y, "Reset must retain row geometry");
        Click(ImGuiMouseButton_Right, 335, 104);
        ReplaceText("1.5");
        Click(ImGuiMouseButton_Left, 440, 104);
        Require(integerValue == 3, "An integer control must reject fractional entry");
        Click(ImGuiMouseButton_Left, 335, 104);
        ReplaceText("7");
        Click(ImGuiMouseButton_Left, 440, 104);
        Require(integerValue == 7, "An integer control must commit exact valid entry");
        Click(ImGuiMouseButton_Right, 335, 104);
        nodeWheelTest = true;
        value = 0;
        io.AddMousePosEvent(80, 30);
        io.AddKeyEvent(ImGuiMod_Ctrl, true); Frame(); Frame();
        Require(ImGuiExtras::GraphWheelTargetNode(&config) == 7, "Ctrl over a node latches its first numeric value");
        io.AddMousePosEvent(500, 300); Frame();
        io.AddMouseWheelEvent(0, 1); Frame();
        Require(value > 0 && integerValue == 7, "Wheel off-node adjusts only the latched primary value");
        const float latchedValue = value;
        io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
        io.AddMouseWheelEvent(0, 1); Frame();
        Require(value == latchedValue && ImGuiExtras::GraphWheelTargetNode(&config) < 0,
            "Releasing Ctrl releases wheel ownership");
        nodeWheelTest = false;
        primaryTest=true;
        Click(ImGuiMouseButton_Left,420,190);
        Require(primaryClicks==1,"Primary action retains normal click behavior");
        disablePrimary=true;
        Click(ImGuiMouseButton_Left,420,190);
        Require(primaryClicks==1,"Disabled primary action cannot be invoked");
        primaryTest=false;
        if (argc > 1) {
            value = 1.75f;
            io.AddMousePosEvent(-100,-100);
            for (int i = 0; i < 180; ++i) Frame();
            Frame(true);
            std::vector<unsigned char> pixels(600 * 360 * 4);
            glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            stbi_flip_vertically_on_write(1);
            Require(stbi_write_png(argv[1],600,360,4,pixels.data(),600*4) != 0, "Write production-control specimen");
            Click(ImGuiMouseButton_Right,320,60);
            Frame(true);
            glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            const std::string editingPath=std::string(argv[1])+".editing.png";
            Require(stbi_write_png(editingPath.c_str(),600,360,4,pixels.data(),600*4)!=0,"Write numeric editor specimen");
            selectedSpecimen=true; Frame(true);
            glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            Require(stbi_write_png((std::string(argv[1])+".selected.png").c_str(),600,360,4,pixels.data(),600*4)!=0,"Write selected node specimen");
            io.AddMousePosEvent(300,60); Frame(true);
            io.AddMouseButtonEvent(1,true); Frame(true); io.AddMouseButtonEvent(1,false); Frame(true); Frame(true);
            nodeWheelTest=true; io.AddMousePosEvent(80,60); io.AddKeyEvent(ImGuiMod_Ctrl,true); Frame(true); Frame(true);
            Require(ImGuiExtras::GraphWheelTargetNode(&config)==7,"Rendered wheel specimen must have a latched target");
            glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            Require(stbi_write_png((std::string(argv[1])+".wheel.png").c_str(),600,360,4,pixels.data(),600*4)!=0,"Write Ctrl-wheel specimen");
            io.AddKeyEvent(ImGuiMod_Ctrl,false); nodeWheelTest=false;
            io.AddMousePosEvent(256,60); Frame(true); Frame(true);
            glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            Require(stbi_write_png((std::string(argv[1])+".reset.png").c_str(),600,360,4,pixels.data(),600*4)!=0,"Write reset hover specimen");
            Click(ImGuiMouseButton_Right); ReplaceText("1e"); Click(ImGuiMouseButton_Left,440,60); Frame(true);
            glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            Require(stbi_write_png((std::string(argv[1])+".invalid.png").c_str(),600,360,4,pixels.data(),600*4)!=0,"Write invalid numeric specimen");
            for (int i=0;i<180;++i) Frame(true);
            menuSpecimen=true; Frame(true); Frame(true);
            glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            const std::string menuPath=std::string(argv[1])+".menu.png";
            Require(stbi_write_png(menuPath.c_str(),600,360,4,pixels.data(),600*4)!=0,"Write menu specimen");
        }
        if (argc>1) {
            curveSpecimen=true; Frame(true);
            std::vector<unsigned char> pixels(600*360*4); glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            Require(stbi_write_png((std::string(argv[1])+".curve.png").c_str(),600,360,4,pixels.data(),600*4)!=0,"Write extreme curve specimen");
        }
        ValidateGraphCursor(argc>1 ? argv[1] : nullptr);
        std::cout << "Graph control interaction checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; result = 1;
    }
    ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
