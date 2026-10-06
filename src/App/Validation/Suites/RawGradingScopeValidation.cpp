#include "App/Validation/ValidationSuites.h"
#include "Editor/Internal/RawLab/RawGradingScopeRenderer.h"
#include "Renderer/GLHelpers.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

namespace Stack::Validation {
namespace {

bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "RAW grading scope validation failed: " << message << '\n';
    return condition;
}

bool ValidateAnalysis() {
    RawDevelopmentGradingScopeReadback input;
    bool ok = Check(!Raw::BuildGradingScopeVisualization(input), "invalid input was accepted");
    input.valid = true;
    input.source = RawDevelopmentGradingScopeSource::NeutralScene;
    input.sourceKey = "scope-fixture";
    input.generation = 7;
    input.width = 2;
    input.height = 1;
    input.pixels = {0, 0, 0, 1, 1, 1};
    auto packet = Raw::BuildGradingScopeVisualization(input);
    ok &= Check(packet && packet->sourceKey == input.sourceKey &&
        packet->generation == 7 && packet->source == input.source,
        "source identity was lost");
    if (!packet) return false;
    ok &= Check(packet->histogram.front() == 1.0f && packet->histogram.back() == 1.0f &&
        std::count(packet->histogram.begin(), packet->histogram.end(), 0.0f) == 254,
        "black and white did not occupy the histogram endpoints");
    ok &= Check(packet->vectorscopePoints.size() == 1 && packet->paradePoints.size() == 6,
        "neutral samples did not merge at the vectorscope center");
    ok &= Check(std::abs(packet->vectorscopePoints.front().x - 0.5f) < 0.006f &&
        std::abs(packet->vectorscopePoints.front().y - 0.5f) < 0.006f,
        "neutral chroma was displaced");
    input.width = 1;
    input.pixels = {0.18f, 0.18f, 0.18f};
    input.sceneLinear = true;
    packet = Raw::BuildGradingScopeVisualization(input);
    ok &= Check(packet && packet->histogram[118] == 1.0f,
        "scene-linear gray was not encoded for the scope");
    input.encodedSrgb = true;
    packet = Raw::BuildGradingScopeVisualization(input);
    ok &= Check(packet && packet->histogram[46] == 1.0f,
        "already encoded pixels received a second transfer function");
    input.encodedSrgb = false;
    input.workingSpace = Raw::RawWorkingSpace::LinearRec2020D65;
    input.pixels = {0.2f, 0.1f, 0.05f};
    packet = Raw::BuildGradingScopeVisualization(input);
    const auto peak = std::max_element(packet->histogram.begin(), packet->histogram.end());
    ok &= Check(std::distance(packet->histogram.begin(), peak) == 94,
        "Rec.2020 scope conversion changed");
    input.pixels = {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    packet = Raw::BuildGradingScopeVisualization(input);
    ok &= Check(packet && packet->histogram.front() == 1.0f,
        "nonfinite inputs were not sanitized");
    input.pixels.pop_back();
    ok &= Check(!Raw::BuildGradingScopeVisualization(input), "incomplete pixels were accepted");
    input.width = std::numeric_limits<int>::max();
    input.height = std::numeric_limits<int>::max();
    ok &= Check(!Raw::BuildGradingScopeVisualization(input), "invalid dimensions were accepted");
    return ok;
}

bool ValidatePointDraws() {
    using namespace Stack::Editor::RawLabInternal;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(320, 192);
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        ImGui::DestroyContext();
        return false;
    }
    RawGradingScopeRenderer renderer;
    auto packet = std::make_shared<RawGradingScopeVisualization>();
    packet->vectorscopePoints.push_back({0.25f, 0.25f, {1, 0, 0, 1}});
    packet->paradePoints.push_back({0.75f, 0.75f, {0, 1, 0, 1}});
    bool ok = true;
    for (const int scale : {1, 2}) {
        for (const bool clipped : {false, true}) {
            const int width = 320 * scale, height = 192 * scale;
            const auto texture = GLHelpers::CreateEmptyTexture(width, height);
            const auto fbo = GLHelpers::CreateFBO(texture);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, width, height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui::GetIO().DisplayFramebufferScale = ImVec2(static_cast<float>(scale), static_cast<float>(scale));
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(320, 192));
            ImGui::Begin("scope-test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
            auto& list = *ImGui::GetWindowDrawList();
            const int verticesBefore = list.VtxBuffer.Size;
            list.PushClipRect(ImVec2(0, 0), ImVec2(clipped ? 30.0f : 320.0f, 192), false);
            ok &= Check(renderer.QueueDraw(list, packet, GradingScopePlot::Vectorscope,
                ImVec2(8, 8), ImVec2(136, 136)), "vectorscope upload failed");
            list.PopClipRect();
            ok &= Check(renderer.QueueDraw(list, packet, GradingScopePlot::Parade,
                ImVec2(152, 8), ImVec2(280, 136)), "parade upload failed");
            ok &= Check(list.VtxBuffer.Size == verticesBefore,
                "scope traces still generated ImGui geometry");
            list.AddRectFilled(ImVec2(280, 160), ImVec2(300, 180), IM_COL32(0, 0, 255, 255));
            ImGui::End();
            ImGui::Render();
            glDisable(GL_PROGRAM_POINT_SIZE);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            ok &= Check(!glIsEnabled(GL_PROGRAM_POINT_SIZE), "point-size state leaked into UI rendering");
            std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            auto peak = [&](int x, int y, int channel) {
                int value = 0;
                for (int dy = -3 * scale; dy <= 3 * scale; ++dy)
                    for (int dx = -3 * scale; dx <= 3 * scale; ++dx) {
                        const auto index = (static_cast<std::size_t>(height - 1 - (y * scale + dy)) * width +
                            x * scale + dx) * 4 + channel;
                        value = std::max(value, static_cast<int>(pixels[index]));
                    }
                return value;
            };
            ok &= Check(clipped ? peak(40, 40, 0) == 0 : peak(40, 40, 0) > 30,
                "vectorscope position, clipping, or DPI scale is wrong");
            ok &= Check(peak(248, 104, 1) > 30, "parade position or DPI scale is wrong");
            ok &= Check(peak(290, 170, 2) > 200, "scope draw broke later ImGui rendering");
            ok &= Check(glGetError() == GL_NO_ERROR, "scope rendering generated an OpenGL error");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &texture);
        }
    }
    renderer.Shutdown();
    renderer.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    return ok;
}

} // namespace

bool ValidateRawGradingScopes() {
    bool ok = ValidateAnalysis();
    if (!glfwInit()) return Check(false, "GLFW initialization failed");
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(320, 192, "Stack scope validation", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return Check(false, "hidden GL context creation failed");
    }
    glfwMakeContextCurrent(window);
    ok &= Check(LoadGLFunctions(), "GL loading failed");
    if (ok) ok &= ValidateRawGradingScopeGpu();
    if (ok) ok &= ValidatePointDraws();
    if (ok) ok &= ValidateRawLabColorWheel();
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    if (ok) std::cout << "RAW grading scope and color wheel GPU validation passed.\n";
    return ok;
}

} // namespace Stack::Validation
