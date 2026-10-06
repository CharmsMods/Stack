#pragma once
#include "BracketingRawToolValidation.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <cstring>

// Exercise the real RAW composition. Rendering the Bracketing panel alone
// cannot catch the parent layout hiding it before a project has been opened.
inline bool ValidateBracketingDraftLayout(GLFWwindow* window,const std::filesystem::path& directory) {
    using Access=BracketingRawToolValidationAccess;
    bool success=true;
    for(float scale:{1.f,1.5f,2.f}) {
        const int width=scale==1.f?960:scale==1.5f?1440:1920;
        const int height=scale==1.f?640:scale==1.5f?900:1080;
        glfwSetWindowSize(window,width,height);glfwPollEvents();
        ImGui::GetIO().FontGlobalScale=scale;
        for(bool selected:{false,true}) {
            EditorModule editor;editor.SetDocumentPersistenceEnabled(false);
            Access::PrepareDraftWorkspace(editor,selected);
            std::string labels;
            const auto frame=[&](ImVec2 mouse=ImVec2(-100,-100),bool down=false) {
                ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();
                ImGui::GetIO().DeltaTime=1.f/60.f;
                ImGui::GetIO().AddMousePosEvent(mouse.x,mouse.y);
                ImGui::GetIO().AddMouseButtonEvent(0,down);
                ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});
                ImGui::SetNextWindowSize({static_cast<float>(width),static_cast<float>(height)});
                ImGui::Begin("RAW draft workflow",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
                ImGui::LogToBuffer(0);
                editor.RenderRawWorkspaceLabUI();
                labels=ImGui::GetCurrentContext()->LogBuffer.c_str();
                ImGui::LogFinish();ImGui::End();ImGui::Render();
                glViewport(0,0,width,height);glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            };
            for(int i=0;i<60;++i)frame();
            ImGuiWindow* controls=nullptr;
            ImGuiWindow* navigation=nullptr;
            for(auto* candidate:ImGui::GetCurrentContext()->Windows) {
                if(!candidate->Active)continue;
                if(std::strstr(candidate->Name,"RAW draft workflow/")&&
                    std::strstr(candidate->Name,"/BracketToolControls"))controls=candidate;
                if(std::strstr(candidate->Name,"RawLabFilmstripDrawerOverlay/")&&
                    std::strstr(candidate->Name,"/RawLabFilmstripNavigation"))navigation=candidate;
            }
            const bool visible=controls&&controls->ClipRect.GetHeight()>30&&
                controls->ParentWindow&&controls->ParentWindow->ClipRect.GetHeight()>100;
            bool passed=visible&&labels.find("[ Process ]")!=std::string::npos&&
                labels.find(selected?"4 captures selected":"0 captures selected")!=std::string::npos&&
                Access::DraftOnly(editor);
            if(selected&&navigation) {
                const auto draft=Access::Draft(editor);
                // New bracket is the final header control. Use its layout
                // position so this exercises the same pointer path as the UI.
                const ImVec2 button(navigation->DC.CursorStartPos.x+20.f,
                    navigation->DC.CursorPosPrevLine.y+ImGui::GetFrameHeight()*.5f);
                frame(button);frame(button,true);frame(button);frame();
                const auto replacement=Access::Draft(editor);
                passed&=replacement!=draft&&replacement->sources.size()==4&&Access::DraftOnly(editor);
                if(replacement==draft)std::cerr<<"Gallery New bracket did not create a fresh draft at "<<button.x<<", "<<button.y<<'\n';
                // Feed completed metadata through the same regrouping path
                // as the worker, without decoding fixture thumbnail paths.
                auto metadata=std::make_shared<Stack::Editor::BracketingMetadataJob>();
                metadata->sources=replacement->sources;
                for(auto& source:metadata->sources) {
                    const auto original=std::find_if(draft->sources.begin(),draft->sources.end(),
                        [&](const auto& item){return item.sourceKey==source.sourceKey;});
                    if(original!=draft->sources.end())source.metadata=original->metadata;
                    source.inspected=true;
                }
                metadata->done=true;replacement->metadataJob=metadata;
                editor.TickBracketingDraft();
                frame();
                passed&=labels.find("[ Process ]")!=std::string::npos&&
                    labels.find("Draft ready.")!=std::string::npos&&replacement->recipe.groups.size()==2&&
                    !replacement->metadataJob&&Access::DraftOnly(editor);
            } else if(selected)passed=false;
            glFinish();
            std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*4);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            for(int y=0;y<height/2;++y)for(int x=0;x<width*4;++x)
                std::swap(pixels[static_cast<std::size_t>(y)*width*4+x],pixels[static_cast<std::size_t>(height-1-y)*width*4+x]);
            std::filesystem::create_directories(directory);
            const auto file=directory/(std::string("raw-draft-")+(selected?"selected-":"empty-")+std::to_string(int(scale*100))+".png");
            passed&=stbi_write_png(file.string().c_str(),width,height,4,pixels.data(),width*4)!=0;
            std::cout<<"RAW draft before project open, "<<(selected?"Gallery New bracket":"empty selection")
                <<", "<<int(scale*100)<<"%: "<<(passed?"passed":"failed")<<'\n';
            if(!passed)std::cerr<<"Visible controls: "<<visible<<"; UI labels: "<<labels<<'\n';
            success&=passed;
            Access::FinishDraftWorkspace(editor);
        }
    }
    ImGui::GetIO().FontGlobalScale=1.f;
    return success;
}
