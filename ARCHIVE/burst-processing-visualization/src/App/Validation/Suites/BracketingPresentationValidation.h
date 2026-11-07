#pragma once
#include "Editor/EditorModule.h"
#include "Editor/Bracketing/BracketingSession.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image_write.h"
#include "ThirdParty/stb_image.h"
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

struct BracketingPresentationValidationAccess {
    static void ValidateProjectOwnership() {
        using namespace Stack::Editor;
        auto check = [](bool value, const char* message) {
            if (!value) throw std::runtime_error(message);
        };
        EditorModule a, b;
        a.SetDocumentPersistenceEnabled(false);
        b.SetDocumentPersistenceEnabled(false);
        for (auto* editor : {&a, &b}) {
            editor->m_Bracketing = std::make_shared<BracketingSession>();
            auto& session = *editor->m_Bracketing;
            session.projectId = editor == &a ? "animation-a" : "animation-b";
            session.storedRecipe = "recipe";
            session.job = std::make_shared<BracketingJob>();
            session.presentation = std::make_shared<ProcessingPresentation>();
            auto& presentation = *session.presentation;
            presentation.projectId = session.projectId;
            presentation.recipeIdentity = session.storedRecipe;
            presentation.active = true;
            presentation.mailbox = std::make_shared<ProcessingMailbox>(1);
        }
        ImGui::NewFrame();
        ImGui::Begin("Previous project focus");
        auto* previous = ImGui::GetCurrentWindow();
        ImGui::End();
        ImGui::Begin("Foreground project focus");
        auto* foreground = ImGui::GetCurrentWindow();
        ImGui::FocusWindow(foreground);
        ImGui::End();
        auto& presentationA = *a.m_Bracketing->presentation;
        auto& presentationB = *b.m_Bracketing->presentation;
        // Simulate a hidden owner's stale focus capture. Its terminal update
        // must discard it without restoring focus over the visible project.
        presentationA.focusCaptured = presentationB.focusCaptured = true;
        presentationA.previousFocus = presentationB.previousFocus = previous->ID;
        presentationA.focusWindowId = presentationB.focusWindowId = foreground->ID;
        a.CancelBracketingPresentation();
        check(a.m_Bracketing->job->canceled && presentationA.canceling &&
            !b.m_Bracketing->job->canceled && b.IsBracketingPresentationActive() &&
            !presentationB.canceling, "Canceling one animation changed the other project.");
        a.m_Bracketing->failed = true;
        a.UpdateBracketingPresentation(false);
        check(!a.IsBracketingPresentationActive() && b.IsBracketingPresentationActive() &&
            ImGui::GetCurrentContext()->NavWindow == foreground &&
            !presentationA.focusCaptured && presentationA.previousFocus == 0 &&
            presentationB.focusCaptured && presentationB.previousFocus == previous->ID &&
            presentationB.focusWindowId == foreground->ID,
            "A hidden animation update changed foreground focus or presentation state.");
        presentationB.focusCaptured = false;
        ImGui::EndFrame();
        std::cout << "PASS project animation cancellation and hidden focus ownership\n";
    }

    static void Handoff(const Raw::Bracketing::ProcessingRequest& request,const Raw::Bracketing::BracketingResult& completed) {
        using namespace Stack::Editor;using namespace Raw::Bracketing;
        EditorModule editor;editor.SetDocumentPersistenceEnabled(false);
        editor.m_Project->snapshot=std::make_shared<Stack::Project::RawProjectSnapshot>();
        auto& project=*editor.m_Project->snapshot;project.projectId="handoff-check";project.activeSourceSetId="bracket";project.hdrInputRevision=2;
        Stack::Project::MultiFrameSourceSet set;set.sourceSetId=project.activeSourceSetId;
        set.operationIntent=Stack::Project::MultiFrameOperationIntent::RawBurstHdr;set.settings["bracketing"]=Serialize(request.recipe);project.sourceSets.push_back(set);
        editor.m_Bracketing=std::make_shared<BracketingSession>();auto& ui=*editor.m_Bracketing;
        ui.projectId=project.projectId;ui.setId=set.sourceSetId;ui.recipe=request.recipe;
        ui.editedRecipe=ui.storedRecipe=set.settings["bracketing"].dump();ui.pending=ui.publishRequested=true;ui.changedAt=ImGui::GetTime()+100;
        ui.job=std::make_shared<BracketingJob>();ui.job->canceled=true;ui.job->done=true;
        editor.TickBracketing();
        if(ui.failed||!ui.pending||!ui.publishRequested)throw std::runtime_error("Superseded contribution render blocked the replacement recipe.");
        ui.result=std::make_shared<BracketingResult>(completed);ui.preview=completed.preview;
        ui.interactiveRaw=MakeBracketingInteractiveRaw(ui);ui.previewDirty=true;ui.pending=false;
        auto job=std::make_shared<BracketingJob>();job->projectId=project.projectId;job->setId=set.sourceSetId;
        job->revision=project.hdrInputRevision;job->recipeIdentity=ui.storedRecipe;job->result=completed;job->done=true;ui.job=job;
        editor.m_RenderDirty=false;editor.TickBracketing();
        if(ui.interactiveRaw||ui.previewDirty||ui.pending||!editor.m_RenderDirty||!editor.m_HdrAdoptedRawResult||
           editor.m_HdrAdoptedRawResult->rawData!=completed.raw||!editor.m_RawWorkspaceFullResolutionPreviewPending)
            throw std::runtime_error("Completed contribution render failed to restore and refine the full RAW result.");
    }
    static bool Run(GLFWwindow* window,const std::filesystem::path& directory,
        const Stack::Editor::ProcessingSnapshot* recorded=nullptr,
        const std::vector<Stack::Editor::ProcessingCard>& recordedCards={}) {
        using namespace Raw::Bracketing;using namespace Stack::Editor;
        try {
            auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
            ProcessingMailbox mailbox(7);ProcessingProgress event;event.generation=8;
            mailbox.Publish(event);check(!mailbox.Read(),"Stale presentation generation accepted");
            event.generation=7;event.stage=ProcessingStage::Groups;event.total=20;
            mailbox.Publish(event);auto first=mailbox.Read();
            for(int i=0;i<100;++i)mailbox.Publish(event);
            check(mailbox.Read()==first,"Repeated progress was not coalesced");
            event.stage=ProcessingStage::Blend;mailbox.Publish(event);
            check(mailbox.Read()->progress.stage==ProcessingStage::Blend,"Stage transition was coalesced away");
            event.completed=20;mailbox.Publish(event);check(mailbox.Read()->progress.completed==20,"Final work count was lost");
            for(unsigned i=0;i<40;++i) {
                auto thumb=std::make_shared<ProcessingThumbnail>();thumb->frameId=std::to_string(i);thumb->width=thumb->height=1;thumb->rgba={0,0,0,255};
                event.thumbnail=thumb;mailbox.Publish(event);
            }
            check(mailbox.Read()->images.size()==16,"Thumbnail mailbox exceeded its bound");
            check(mailbox.Read()->images.count("39"),"Active captures beyond the first sixteen were lost");
            ProcessingMailbox ordered(9,"reference");ProcessingProgress orderedEvent;
            orderedEvent.sequence=20;orderedEvent.stage=ProcessingStage::Exposure;ordered.Publish(orderedEvent);
            orderedEvent.sequence=19;orderedEvent.stage=ProcessingStage::Noise;ordered.Publish(orderedEvent);
            check(ordered.Read()->progress.stage==ProcessingStage::Exposure,"Out-of-order event changed stages");
            orderedEvent.sequence=21;orderedEvent.stage=ProcessingStage::LocalAlignment;ordered.Publish(orderedEvent);
            orderedEvent.sequence=22;orderedEvent.stage=ProcessingStage::Exposure;ordered.Publish(orderedEvent);
            check(ordered.Read()->milestones.size()==3,"Repeated stage lost its occurrence");
            const auto occurrence=ordered.Read()->progress.occurrence;
            orderedEvent.sequence=23;orderedEvent.occurrence=occurrence-1;ordered.Publish(orderedEvent);
            check(ordered.Read()->progress.occurrence==occurrence,"Stale occurrence was accepted");
            orderedEvent.occurrence=0;orderedEvent.sequence=24;orderedEvent.stage=ProcessingStage::Canceled;ordered.Publish(orderedEvent);
            orderedEvent.sequence=25;orderedEvent.stage=ProcessingStage::Blend;ordered.Publish(orderedEvent);
            check(ordered.Read()->progress.stage==ProcessingStage::Canceled,"Late evidence revived cancellation");
            ProcessingSnapshot fast;
            ProcessingDirector pacing;ProcessingSnapshot paced;
            paced.progress.stage=ProcessingStage::Groups;paced.progress.occurrence=1;
            pacing.Select(paced,0,false);
            paced.progress.stage=ProcessingStage::Blend;paced.progress.occurrence=2;
            check(pacing.Select(paced,.2,false).progress.stage==ProcessingStage::Groups,"Fast work snapped past the readable stage hold");
            check(pacing.Select(paced,2.3,false).progress.stage==ProcessingStage::Blend,"Presentation failed to catch up after its stage hold");
            BracketingSession previewSession;previewSession.interactiveRaw=std::make_shared<Raw::RawImageData>();
            previewSession.pending=true;check(previewSession.HasInteractivePreview(),"Pending contributions lost their interaction preview");
            previewSession.pending=false;check(!previewSession.HasInteractivePreview(),"A completed recipe still used its low-resolution proxy");
            for(unsigned i=0;i<8;++i) {
                ProcessingMilestone milestone;milestone.progress.stage=ProcessingStage::Blend;milestone.progress.occurrence=i+1;
                milestone.evidence=std::make_shared<ProcessingEvidence>();fast.milestones.push_back(milestone);
            }
            ProcessingDirector director;director.Ready(fast,10,false);
            check(director.Deadline()<=16&&!director.Done(15.9)&&director.Done(16),"Finish deadline exceeded six seconds");
            director.Skip(11);check(director.Done(11.16),"View result failed to skip the ending");
            ProcessingDirector reducedDirector;reducedDirector.Ready(fast,10,true);
            check(reducedDirector.Done(10.16),"Reduced motion delayed the result");
            const auto a=ProcessingPlanePose(ProcessingStage::LocalAlignment,2,8,0,0,true);
            const auto b=ProcessingPlanePose(ProcessingStage::LocalAlignment,2,8,0,500,true);
            check(a.x==b.x&&a.yaw==b.yaw,"Reduced motion moved the camera");
            std::filesystem::create_directories(directory/"processing");
            EditorModule editor;editor.SetDocumentPersistenceEnabled(false);
            editor.m_Bracketing=std::make_shared<BracketingSession>();auto& ui=*editor.m_Bracketing;
            ui.projectId="replay";ui.storedRecipe="replay-recipe";
            ui.presentation=std::make_shared<ProcessingPresentation>();auto& p=*ui.presentation;
            p.active=true;p.projectId=ui.projectId;p.recipeIdentity=ui.storedRecipe;
            p.mailbox=std::make_shared<ProcessingMailbox>(11);
            int photoW=0,photoH=0,photoC=0;
            auto* photo=stbi_load((directory/"processing-source.png").string().c_str(),&photoW,&photoH,&photoC,4);
            for(unsigned i=0;i<8;++i) {
                auto thumb=std::make_shared<ProcessingThumbnail>();thumb->frameId="capture-"+std::to_string(i);
                thumb->width=256;thumb->height=170;thumb->group=i/3;thumb->rgba.resize(256*170*4);
                for(unsigned y=0;y<170;++y)for(unsigned x=0;x<256;++x) {
                    const auto q=(y*256+x)*4;const bool building=x>60&&x<190&&y>35;
                    const bool window=building&&x%30<13&&y%28<16;
                    thumb->rgba[q]=window?25:building?150:55;
                    thumb->rgba[q+1]=window?37:building?100:90;
                    thumb->rgba[q+2]=window?45:building?68:130;thumb->rgba[q+3]=255;
                }
                if(photo && photoW>0 && photoH>0)for(unsigned y=0;y<170;++y)for(unsigned x=0;x<256;++x)
                    for(unsigned c=0;c<4;++c)thumb->rgba[(y*256+x)*4+c]=photo[((y*photoH/170)*photoW+x*photoW/256)*4+c];
                ProcessingProgress sample;sample.thumbnail=thumb;p.mailbox->Publish(sample);
                p.cards.push_back({thumb->frameId,"Capture "+std::to_string(i+1),i/3,{.35f+.08f*i,.65f,.8f-.06f*i,1}});
            }
            if(photo)stbi_image_free(photo);
            if(recorded) {
                p.cards=recordedCards;
                for(const auto& [id,thumbnail]:recorded->images){ProcessingProgress event;event.thumbnail=thumbnail;p.mailbox->Publish(event);}
                check(recorded->EvidenceBytes()<=64ull*1024*1024,"CPU evidence exceeded its budget");
            }
            const ImVec2 sizes[]={{1280,720},{1920,1080},{2560,1440}};
            const ProcessingStage stages[]={ProcessingStage::GlobalAlignment,ProcessingStage::LocalAlignment,ProcessingStage::Blend};
            unsigned shot=0;
            for(auto size:sizes) {
                p.director=ProcessingDirector{};
                glfwSetWindowSize(window,int(size.x),int(size.y));glfwPollEvents();
                ImGui::GetIO().FontGlobalScale=shot==2?2.f:1.f;
                ProcessingProgress progress;progress.stage=stages[shot];progress.completed=3;progress.total=8;
                progress.captureId="capture-3";progress.detail="Measured coverage 94%";p.mailbox->Publish(progress);
                if(recorded) {
                    bool found=false;
                    for(const auto& m:recorded->milestones)if(m.progress.stage==stages[shot]&&m.evidence) {
                        progress=m.progress;progress.sequence=progress.occurrence=progress.generation=0;
                        progress.evidence=m.evidence;progress.evidenceOnly=false;p.mailbox->Publish(progress);found=true;
                    }
                    check(found,"A required processing stage did not publish evidence");
                    if(stages[shot]==ProcessingStage::LocalAlignment)
                        check(progress.evidence->rasters.size()>=2,"Local alignment did not publish confidence and rejection masks");
                }
                unsigned captureTexture=0;glGenTextures(1,&captureTexture);glBindTexture(GL_TEXTURE_2D,captureTexture);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,int(size.x),int(size.y),0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
                const auto captureFbo=GLHelpers::CreateFBO(captureTexture);
                for(int frame=0;frame<24;++frame) {
                    ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();
                    // A hidden Win32 window can be clamped to the monitor size.
                    // Render the requested DPI case into an explicit target.
                    ImGui::GetIO().DisplaySize=size;ImGui::GetIO().DisplayFramebufferScale={1,1};ImGui::NewFrame();
                    p.began=ImGui::GetTime()-8-frame*.06;
                    editor.RenderBracketingPresentation({.105f,.095f,.085f,1},
                        ImGui::GetMainViewport()->Pos, ImGui::GetMainViewport()->Size);ImGui::Render();
                    glBindFramebuffer(GL_FRAMEBUFFER,captureFbo);glViewport(0,0,int(size.x),int(size.y));glClear(GL_COLOR_BUFFER_BIT);
                    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());glFinish();
                }
                check(p.renderer.TextureBytes()<64ull*1024*1024,"Presentation exceeded memory budget");
                std::vector<unsigned char> pixels(std::size_t(size.x)*int(size.y)*4);
                glReadPixels(0,0,int(size.x),int(size.y),GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                stbi_flip_vertically_on_write(1);
                check(stbi_write_png((directory/"processing"/("scene-"+std::to_string(shot++)+".png")).string().c_str(),
                    int(size.x),int(size.y),4,pixels.data(),int(size.x)*4)!=0,"Could not write scene replay");
                glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&captureFbo);glDeleteTextures(1,&captureTexture);
            }
            auto state=p.mailbox->Read();check(p.renderer.Draw(*state,p.cards,{1280,720},{.1f,.1f,.1f,1},200,true)!=0,"Scene renderer failed");
            ui.job=std::make_shared<BracketingJob>();
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);ImGui::NewFrame();
            editor.RenderBracketingPresentation({.1f,.09f,.08f,1},
                ImGui::GetMainViewport()->Pos, ImGui::GetMainViewport()->Size);ImGui::EndFrame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);
            check(ui.job->canceled&&p.canceling,"Keyboard cancel did not reach the worker");
            editor.CancelBracketingPresentation();check(!ui.pending&&!ui.publishRequested,"Cancel allowed a restart/publication");
            p.canceling=false;p.active=true;ui.storedRecipe="superseded";
            editor.UpdateBracketingPresentation();check(!p.active,"Superseded presentation remained modal");
            ui.storedRecipe=p.recipeIdentity;p.active=true;ui.failed=true;
            editor.UpdateBracketingPresentation();check(!p.active,"Failure remained modal");
            ui.failed=false;p.active=true;p.waitingForRaw=true;p.expectedHash=42;p.finishedAt=-1;
            editor.m_RenderDirty=false;editor.m_BracketingPresentedSourceHash=41;
            editor.m_RawWorkspacePreviewOutputKind=EditorModule::RawWorkspacePreviewOutputKind::Tiled;
            editor.UpdateBracketingPresentation();check(p.finishedAt<0,"Unmatched RAW output completed the presentation");
            editor.m_BracketingPresentedSourceHash=42;editor.UpdateBracketingPresentation();
            check(p.finishedAt>=0&&p.active,"Tiled RAW output bypassed the bounded finish");
            p.active=false;editor.UpdateBracketingPresentation();
            check(!p.mailbox->Read()->evidence&&p.mailbox->Read()->milestones.empty(),"Finished presentation retained evidence history");
            ImGui::GetIO().FontGlobalScale=1;
            std::cout<<"Processing presentation generations, coalescing, memory, cancellation, failure, reduced motion and replay passed.\n";
            return true;
        }catch(const std::exception& e){std::cerr<<"Processing presentation validation: "<<e.what()<<'\n';return false;}
    }
};
