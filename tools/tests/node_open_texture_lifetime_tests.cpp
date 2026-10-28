#include "Editor/UI/NodeOpenTransition.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <array>
#include <iostream>

namespace {
using Transition = Stack::Editor::NodeOpenTransition;
bool Check(bool value, const char* message) {
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
Transition::Appearance Appearance(GLuint& name, bool blue) {
    glGenTextures(1, &name); glBindTexture(GL_TEXTURE_2D, name);
    const std::array<unsigned char, 4> pixel{static_cast<unsigned char>(blue ? 0 : 255),
        0, static_cast<unsigned char>(blue ? 255 : 0), 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    Transition::Appearance result;
    result.minimum = {20, 20}; result.maximum = {120, 120};
    result.meshes.emplace_back();
    auto& mesh = result.meshes.back();
    mesh.clip = {0, 0, 200, 200};
    mesh.texture = std::make_shared<Stack::Editor::NodeAppearanceTexture>();
    mesh.texture->name = name;
    mesh.vertices = {{{20, 20}, {0, 0}, IM_COL32_WHITE},
        {{120, 20}, {1, 0}, IM_COL32_WHITE}, {{20, 120}, {0, 1}, IM_COL32_WHITE}};
    return result;
}
bool Scenario(int mutation) {
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
    Transition transition;
    GLuint original = 0, replacement = 0;
    transition.Begin(Appearance(original, false), "document", 0);
    ImVec2 low{0, 0}, high{200, 200};
    transition.Present(low, high, 0);
    transition.DrawNode(*ImGui::GetForegroundDrawList(), 0);
    // The workspace's Escape handling runs after drawing the photo/node.
    bool passed = true;
    if (mutation == 0 || mutation == 4) {
        passed = Check(transition.Close(Appearance(replacement, true), "document", .01),
            "close accepts the current document") && passed;
        if (mutation == 4) transition.DrawNode(*ImGui::GetForegroundDrawList(), .01);
    } else if (mutation == 1) transition.Reset();
    else if (mutation == 2) transition.Tick("another-document", .01, false);
    else if (mutation == 3) transition.Tick("document", .01, true);
    else if (mutation == 5) transition.Begin(Appearance(replacement, true), "another-document", .01);
    // Exercise repeated submission of an appearance without losing earlier
    // appearances drawn before Close in this same frame.
    if (mutation == 4) transition.DrawNode(*ImGui::GetForegroundDrawList(), .01);
    passed = Check(glIsTexture(original) == GL_TRUE, "queued original survives navigation") && passed;
    if (replacement) passed = Check(glIsTexture(replacement) == GL_TRUE, "replacement remains live") && passed;
    ImGui::Render();
    glViewport(0, 0, 200, 200); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    std::array<unsigned char, 4> pixel{};
    glReadPixels(30, 170, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    passed = Check(mutation == 4 ? pixel[2] > 240 && pixel[0] < 10
        : pixel[0] > 240 && pixel[2] < 10, "submitted node pixels render after navigation") && passed;
    if (mutation == 6) {
        // Explicit teardown happens after backend submission, with GL current.
        transition.Shutdown();
        passed = Check(glIsTexture(original) == GL_FALSE, "shutdown releases submitted texture") && passed;
    } else {
        transition.Reset();
        passed = Check(glIsTexture(original) == GL_TRUE, "same-frame reset retains submitted texture") && passed;
        ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
        // The common editor pump does this even when hidden/inactive; no Tick.
        transition.ReleaseSubmittedDraw();
        passed = Check(glIsTexture(original) == GL_FALSE, "next-frame pump releases original without Tick") && passed;
        if (replacement) passed = Check(glIsTexture(replacement) == GL_FALSE, "next frame releases every submitted appearance") && passed;
        ImGui::EndFrame(); transition.Shutdown();
    }
    return Check(glGetError() == GL_NO_ERROR, "lifetime scenario leaves no GL error") && passed;
}
}
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    GLFWwindow* window = glfwCreateWindow(200, 200, "Node appearance lifetime", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    ImGui::CreateContext(); auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr; io.DisplaySize = {200, 200}; io.DeltaTime = 1.f / 60;
    if (!ImGui_ImplOpenGL3_Init("#version 430")) return 1;
    bool passed = true;
    for (int mutation = 0; mutation < 7; ++mutation) passed = Scenario(mutation) && passed;
    ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(window); glfwTerminate();
    if (!passed) return 1;
    std::cout << "Node appearance lifetime passed (Close, Reset, identity, reduced motion, repeated draws, replacement, shutdown).\n";
}
