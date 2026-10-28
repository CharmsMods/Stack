#include "Editor/UI/GraphFrameTransition.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <array>
#include <iostream>

namespace {
bool Check(bool value, const char* message) {
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
GLuint Texture(bool blue) {
    GLuint name = 0; glGenTextures(1, &name); glBindTexture(GL_TEXTURE_2D, name);
    const std::array<unsigned char, 4> pixel{static_cast<unsigned char>(blue ? 0 : 255),
        0, static_cast<unsigned char>(blue ? 255 : 0), 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return name;
}
bool Scenario(int mutation) {
    const auto baseline = Raw::RawGpuImageLease::Residency().totalBytes;
    GLuint previous = Texture(false), current = Texture(true);
    GraphFrameTransition transition;
    Stack::GraphRendering::FramePresentation frame;
    frame.identity.enabled = true; frame.identity.outputNodeId = 1;
    frame.width = frame.height = 1; frame.completedAt = 0;
    transition.Prepare(frame, 100);
    frame.completedAt = .1;
    bool passed = Check(transition.Prepare(frame, 100), "compatible second frame prepares fade");
    transition.RetainOwned(previous, 1, 1);
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({200, 200});
    ImGui::Begin("viewport", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar);
    auto* list = ImGui::GetWindowDrawList();
    passed = Check(transition.Draw(list, current, {20, 20}, {120, 120}, .1,
        {0, 0}, {1, 1}, IM_COL32_WHITE, 0), "real blend draw submits") && passed;
    if (mutation == 0) transition.Clear();
    else if (mutation == 1) transition.Reset();
    else if (mutation == 2) {
        auto changed = frame.identity; changed.sourceIdentity = 123;
        transition.Validate(changed, .1);
    } else {
        // Main and detached views plus the backdrop can borrow the same base.
        passed = Check(transition.Draw(list, current, {20, 20}, {120, 120}, .1,
            {0, 0}, {1, 1}, IM_COL32_WHITE, 0), "second view submits same base") && passed;
        GraphFrameTransition::BlendDescription description;
        passed = Check(transition.DescribeBlend(.1, description), "backdrop borrows same base") && passed;
    }
    transition.ReleaseSubmittedDraw();
    passed = Check(Raw::RawGpuImageLease::Residency().totalBytes == baseline + 8,
        "same-frame cleanup preserves exactly one shared image") && passed;
    passed = Check(transition.Active() == (mutation == 3), "explicit clear leaves no active fade") && passed;
    ImGui::End(); ImGui::Render();
    glViewport(0, 0, 200, 200); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    std::array<unsigned char, 4> pixel{};
    glReadPixels(30, 170, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    passed = Check(pixel[0] > 240 && pixel[2] < 10, "queued previous pixels survive cancellation") && passed;
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
    // The common pump runs for hidden editors without Draw/Describe/Validate.
    transition.ReleaseSubmittedDraw();
    passed = Check(Raw::RawGpuImageLease::Residency().totalBytes == baseline + (mutation == 3 ? 8 : 0),
        "next-frame cleanup releases only submitted references") && passed;
    if (mutation == 3) {
        passed = Check(transition.Active(), "active fade base is not evicted by hidden-owner cleanup") && passed;
        transition.Clear();
    }
    passed = Check(Raw::RawGpuImageLease::Residency().totalBytes == baseline,
        "cleared fade leaves no resident image lease") && passed;
    ImGui::EndFrame(); transition.Shutdown(); glDeleteTextures(1, &current);
    return Check(glGetError() == GL_NO_ERROR, "scenario leaves no GL error") && passed;
}
bool OwnersAndShutdown() {
    const auto baseline = Raw::RawGpuImageLease::Residency().totalBytes;
    GraphFrameTransition first, second;
    for (auto* owner : {&first, &second}) {
        Stack::GraphRendering::FramePresentation frame;
        frame.identity.enabled = true; frame.identity.outputNodeId = 1;
        frame.width = frame.height = 1; frame.completedAt = 0;
        owner->Prepare(frame, 100); frame.completedAt = .1;
        owner->Prepare(frame, 100); owner->RetainOwned(Texture(false), 1, 1);
    }
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
    GraphFrameTransition::BlendDescription description;
    first.DescribeBlend(.1, description); second.DescribeBlend(.1, description);
    first.Clear(); ImGui::EndFrame();
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
    first.ReleaseSubmittedDraw();
    bool passed = Check(!first.Active() && second.Active() &&
        Raw::RawGpuImageLease::Residency().totalBytes == baseline + 8,
        "one hidden owner's retirement leaves the other owner's image intact");
    second.ReleaseSubmittedDraw();
    passed = Check(second.Active() && Raw::RawGpuImageLease::Residency().totalBytes == baseline + 8,
        "second owner retains its active fade after submitted cleanup") && passed;
    ImGui::EndFrame();
    passed = Check(glfwGetCurrentContext() != nullptr, "shutdown runs with owner GL context current") && passed;
    first.Shutdown(); second.Shutdown();
    return Check(Raw::RawGpuImageLease::Residency().totalBytes == baseline && glGetError() == GL_NO_ERROR,
        "explicit shutdown releases both owners on the current context") && passed;
}
}
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(200, 200, "Graph frame lifetime", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    ImGui::CreateContext(); auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr; io.DisplaySize = {200, 200}; io.DeltaTime = 1.f / 60;
    if (!ImGui_ImplOpenGL3_Init("#version 430")) return 1;
    bool passed = true;
    for (int mutation = 0; mutation < 4; ++mutation) passed = Scenario(mutation) && passed;
    passed = OwnersAndShutdown() && passed;
    ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    if (!passed) return 1;
    std::cout << "Graph frame submitted lifetime passed (Clear, Reset, Validate, repeated draws, hidden cleanup, active base).\n";
}
