#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"
#include "Editor/EditorModule.h"
#include "Renderer/GLHelpers.h"
#include <backends/imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <iostream>
#include <memory>

// Exercise the same adoption and ImGui draw callbacks as the Raw viewport.
// No workspace is opened and no user settings are loaded or saved.
struct RawViewportPresentationValidationAccess {
    static bool Run() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(160,120);
        io.DeltaTime = 1.0f/60;
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) { ImGui::DestroyContext(); return false; }
        auto editor = std::make_unique<EditorModule>();
        auto& e = *editor;
        e.m_ShutdownComplete = true;
        e.m_RawViewportPreferences=std::make_shared<Raw::ViewportPreferencesStore>();
        e.m_RawWorkspaceVramWorkingBudgetBytes = 1024ull*1024*1024;
        bool ok = true;
        const auto check = [&](bool value, const char* message) {
            if (!value) { std::cerr << "RAW presentation: " << message << '\n'; ok = false; }
        };
        const auto result = [](int edge, float r, float g, float b) {
            EditorRenderWorker::Result frame;
            frame.success = true;
            frame.rawWorkspace.sourceKey = "presentation-test";
            frame.rawWorkspace.viewportGeneration = 1;
            frame.rawRenderPurpose = RawRenderPurpose::InteractivePresentation;
            frame.editStage = Raw::ViewportStage::RawPlacement;
            frame.telemetry.workerTotalMs = 200;
            frame.outputTexture.width = frame.outputTexture.height = edge;
            std::vector<float> pixels(std::size_t(edge)*edge*4);
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                pixels[i] = r; pixels[i+1] = g; pixels[i+2] = b; pixels[i+3] = 1;
            }
            glGenTextures(1,&frame.outputTexture.texture);
            glBindTexture(GL_TEXTURE_2D,frame.outputTexture.texture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,edge,edge,0,GL_RGBA,GL_FLOAT,pixels.data());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            return frame;
        };
        const auto adopt = [&](EditorRenderWorker::Result& frame) {
            const bool accepted = e.AdoptRawViewportFrame(frame);
            if (accepted) e.m_RawViewportPresentedRegion = frame.rawWorkspace.viewportRegion;
            return accepted;
        };
        const auto target = GLHelpers::CreateEmptyTexture(160,120);
        const auto fbo = GLHelpers::CreateFBO(target);
        const auto draw = [&](double advance, bool expectFade) {
            ImGui::GetCurrentContext()->Time += advance;
            glBindFramebuffer(GL_FRAMEBUFFER,fbo);
            glViewport(0,0,160,120);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0,0));
            ImGui::SetNextWindowSize(ImVec2(160,120));
            ImGui::Begin("presentation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground);
            auto* list = ImGui::GetWindowDrawList();
            const bool fading = e.DrawRawViewportTransition(list,ImVec2(0,0),ImVec2(160,120));
            check(fading == expectFade,"unexpected fade eligibility during actual draw");
            if (!fading) list->AddImage((ImTextureID)(intptr_t)e.m_RawWorkspacePresentationTexture.texture,
                ImVec2(0,0),ImVec2(160,120));
            ImGui::End();
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            std::array<float,4> pixel{};
            glReadPixels(80,60,1,1,GL_RGBA,GL_FLOAT,pixel.data());
            return pixel;
        };
        auto proxy = result(16,1,0,0);
        const GLuint base = proxy.outputTexture.texture;
        check(adopt(proxy),"first proxy was rejected");
        check(draw(0,false)[0] > 0.98,"first proxy is not visible");
        ImGui::GetCurrentContext()->Time += 0.2;
        auto native = result(64,0,1,0);
        native.rawWorkspace.viewportRegion = {128,128,32,32,64,64};
        const GLuint green = native.outputTexture.texture;
        check(adopt(native),"native refinement was rejected");
        check(e.m_RawViewportOverviewTexture.texture != 0,"regional handoff lost its overview");
        auto pixel = draw(0.3,true); // Longer than the entire fade before its first draw.
        check(pixel[0] > 0.98 && pixel[1] < 0.02,"fade expired before its first visible frame");
        const double duration = e.m_RawViewportFadeDuration;
        pixel = draw(duration*0.5-io.DeltaTime,true);
        check(std::abs(pixel[0]-0.5f) < 0.025f && std::abs(pixel[1]-0.5f) < 0.025f,
            "actual proxy-to-native draw does not blend both textures");

        auto newer = result(64,0,0,1);
        newer.rawWorkspace.viewportRegion = {128,128,32,32,64,64};
        check(adopt(newer),"new completion during a fade was rejected");
        check(e.m_RawViewportPreviousTexture.texture == base && e.m_RawViewportFadeStarted >= 0,
            "new completion restarted the old target instead of retaining blend progress");
        pixel = draw(0,true);
        check(pixel[0] > 0.1 && pixel[2] > 0.4 && pixel[1] < 0.02,
            "retargeted fade did not use the newest completion");
        pixel = draw(0.3,false);
        check(pixel[2] > 0.98,"finished fade failed to expose the full native result");
        e.PumpViewportOutputTextureDeletes(true);
        check(!glIsTexture(base) && !glIsTexture(green),"completed fade retained obsolete textures");
        EditorRenderWorker::Result failed;
        const GLuint accepted = e.m_RawWorkspacePresentationTexture.texture;
        check(!adopt(failed) && e.m_RawWorkspacePresentationTexture.texture == accepted,
            "invalid replacement discarded the accepted image");
        ImGui::GetCurrentContext()->Time += 0.2;
        auto unrelated = result(64,1,1,0);
        unrelated.rawWorkspace.sourceKey = "another-source";
        check(adopt(unrelated) && e.m_RawViewportFadeDuration == 0,"source change blended unrelated images");
        e.SetSmoothRawViewportUpdates(false);
        ImGui::GetCurrentContext()->Time += 0.2;
        auto disabled = result(128,1,0,1);
        disabled.rawWorkspace.sourceKey = "another-source";
        check(adopt(disabled) && e.m_RawViewportFadeDuration == 0,"disabled fading still retained a transition");

        e.ClearRawWorkspacePresentationTexture();
        e.PumpViewportOutputTextureDeletes(true);
        e.m_RawViewportFadeRenderer.Shutdown();
        editor.reset();
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDeleteFramebuffers(1,&fbo);
        glDeleteTextures(1,&target);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        std::cout << "RAW presentation " << (ok ? "passed" : "FAILED")
            << ": delayed first draw, proxy/native pixels, regional handoff, retargeting and releases.\n";
        return ok;
    }
};

namespace Stack::Validation {
bool ValidateRawViewportPresentation() { return RawViewportPresentationValidationAccess::Run(); }
}
