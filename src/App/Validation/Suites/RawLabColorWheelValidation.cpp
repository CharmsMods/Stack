#include "App/Validation/ValidationSuites.h"
#include "Editor/Internal/RawLab/RawLabColorWheelRenderer.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Renderer/GLHelpers.h"

#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Stack::Validation {
namespace {

std::array<float, 4> ReferenceColor(float a, float b, Raw::RawWorkingSpace space) {
    constexpr float extent = 0.45f;
    const float radius = std::hypot(a, b);
    if (radius > extent) return {};
    if (radius < 0.000001f) return {1,1,1,1};
    Stack::RawRecipe::RawColorWarpCoordinate coordinate;
    coordinate.lightness = 0.7f;
    coordinate.a = a / radius * extent;
    coordinate.b = b / radius * extent;
    auto rgb = Stack::RawRecipe::ColorWarpCoordinateToWorkingRgb(coordinate, space);
    const float minimum = std::min({0.0f, rgb[0], rgb[1], rgb[2]});
    for (float& value : rgb) value = std::max(0.0f, value - minimum);
    const float peak = std::max({0.000001f, rgb[0], rgb[1], rgb[2]});
    const float blend = std::pow(std::clamp(radius / extent, 0.0f, 1.0f), 0.55f);
    for (float& value : rgb) value = 1.0f + (std::pow(std::clamp(value / peak, 0.0f, 1.0f), 1.0f / 2.2f) - 1.0f) * blend;
    const float fade = std::clamp((extent - radius) / (extent * 0.075f), 0.0f, 1.0f);
    return {rgb[0],rgb[1],rgb[2],fade * fade * (3.0f - 2.0f * fade)};
}

} // namespace

bool ValidateRawLabColorWheel() {
    using Stack::Editor::RawLabInternal::RawLabColorWheelRenderer;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(320,192);
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        ImGui::DestroyContext();
        return false;
    }
    bool ok = true;
    RawLabColorWheelRenderer renderer;
    for (int scale : {1,2}) {
        for (bool zoomed : {false,true}) {
            const int width = 320 * scale, height = 192 * scale;
            const auto texture = GLHelpers::CreateEmptyTexture(width,height);
            const auto fbo = GLHelpers::CreateFBO(texture);
            glBindFramebuffer(GL_FRAMEBUFFER,fbo);
            glViewport(0,0,width,height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui::GetStyle().Alpha = zoomed ? 0.6f : 1.0f;
            ImGui::GetIO().DisplayFramebufferScale = ImVec2(static_cast<float>(scale),static_cast<float>(scale));
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0,0));
            ImGui::SetNextWindowSize(ImVec2(320,192));
            ImGui::Begin("wheel-test",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
            auto& list = *ImGui::GetWindowDrawList();
            const int verticesBefore = list.VtxBuffer.Size;
            const ImVec2 mins[2] { zoomed ? ImVec2(-192,-192) : ImVec2(16,16), ImVec2(160,16) };
            const ImVec2 maxs[2] { zoomed ? ImVec2(384,384) : ImVec2(144,144), ImVec2(288,144) };
            const auto spaces = std::array<Raw::RawWorkingSpace,2> {
                Raw::RawWorkingSpace::LinearSrgbD65, Raw::RawWorkingSpace::LinearRec2020D65 };
            const ImVec2 clipMin = zoomed ? ImVec2(40,30) : ImVec2(0,0);
            const ImVec2 clipMax = zoomed ? ImVec2(260,150) : ImVec2(320,192);
            list.PushClipRect(clipMin,clipMax,false);
            const int draws = zoomed ? 1 : 2;
            for (int i = 0; i < draws; ++i) ok &= renderer.QueueDraw(list, mins[i],maxs[i],spaces[i],0.7f,0.45f);
            list.PopClipRect();
            ok &= list.VtxBuffer.Size == verticesBefore;
            list.AddRectFilled(ImVec2(280,160),ImVec2(300,180),IM_COL32(0,0,255,255));
            ImGui::End();
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            float worst = 0;
            for (int y = 3 * scale; y < 155 * scale; y += 7 * scale) {
                for (int x = 3 * scale; x < width; x += 7 * scale) {
                    const float sx = (x + 0.5f) / scale, sy = (y + 0.5f) / scale;
                    std::array<float,4> reference {};
                    if (sx >= clipMin.x && sx < clipMax.x && sy >= clipMin.y && sy < clipMax.y) {
                        for (int i = 0; i < draws; ++i) {
                            if (sx < mins[i].x || sx >= maxs[i].x || sy < mins[i].y || sy >= maxs[i].y) continue;
                            reference = ReferenceColor(((sx-mins[i].x)/(maxs[i].x-mins[i].x)*2.0f-1.0f)*0.45f,
                                (1.0f-(sy-mins[i].y)/(maxs[i].y-mins[i].y)*2.0f)*0.45f,spaces[i]);
                        }
                    }
                    const auto offset = (static_cast<std::size_t>(height-1-y) * width+x)*4;
                    for (int c = 0; c < 3; ++c) worst = std::max(worst,
                        std::abs(pixels[offset+c]/255.0f-reference[c]*reference[3]*ImGui::GetStyle().Alpha));
                }
            }
            if (worst > 0.008f) {
                std::cerr << "Color wheel pixel comparison failed: scale=" << scale << " zoom=" << zoomed << " difference=" << worst << '\n';
                ok = false;
            }
            const auto marker = (static_cast<std::size_t>(height-1-170*scale)*width+290*scale)*4;
            ok &= pixels[marker+2] > 200 && glGetError() == GL_NO_ERROR;
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            glDeleteFramebuffers(1,&fbo);
            glDeleteTextures(1,&texture);
        }
        renderer.Shutdown(); // Reinitialization on the next scale must work.
    }
    renderer.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    if (!ok) std::cerr << "RAW color wheel GPU validation failed.\n";
    return ok;
}

} // namespace Stack::Validation
