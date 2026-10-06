#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "AutoBracketPresentation.h"
#include "Renderer/GLHelpers.h"
#include <imgui_internal.h>
#include <algorithm>
namespace Stack::AutoBracket {
namespace {
bool ReducedMotion() {
#if defined(_WIN32)
    BOOL enabled=TRUE;if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0))return !enabled;
#endif
    return false;
}
}
Presentation::~Presentation(){if(texture)glDeleteTextures(1,&texture);}
void Presentation::Begin(const std::shared_ptr<Work>& work,double now) {
    if(texture){glDeleteTextures(1,&texture);texture=0;}
    completed.reset();animation.renderer.Release();animation.director={};animation.cards.clear();
    generation=work->generation;animation.generation=generation;animation.mailbox=work->mailbox;
    animation.active=true;animation.began=now;animation.finishedAt=-1;animation.canceling=false;visible=true;
}
void Presentation::Complete(const std::shared_ptr<Work>& work,double now) {
    if(work->generation!=generation)Begin(work,now);
    if(!animation.active) {
        animation.renderer.Release();animation.mailbox->DiscardEvidence();completed.reset();return;
    }
    completed=work;
    if(work->outcome!=State::Completed||work->coverPixels.empty()) {
        animation.active=false;animation.renderer.Release();animation.mailbox->DiscardEvidence();completed.reset();return;
    }
    width=work->coverWidth;height=work->coverHeight;
    GLint previous=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
    if(!texture)glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,work->coverPixels.data());
    glBindTexture(GL_TEXTURE_2D,previous);
    animation.finishedAt=now;
    if(auto latest=animation.mailbox->Read())animation.director.Ready(*latest,now,ReducedMotion());
}
void Presentation::Tick(double now) {
    if(animation.active&&animation.finishedAt>=0&&animation.director.Done(now)) {
        animation.active=false;animation.renderer.Release();animation.mailbox->DiscardEvidence();completed.reset();
        if(texture){glDeleteTextures(1,&texture);texture=0;}
    }
}
void DrawPresentation(Presentation& ui,AutoBracketCoordinator& coordinator,bool workspace) {
    if(workspace||(ui.animation.active&&ui.visible)) {
        auto& animation=ui.animation;
        auto latest=animation.mailbox?animation.mailbox->Read():nullptr;
        if(!latest)latest=std::make_shared<Editor::ProcessingSnapshot>();
        for(const auto& [id,image]:latest->images)if(std::none_of(animation.cards.begin(),animation.cards.end(),[&](const auto& card){return card.id==id;})) {
            Editor::ProcessingCard card;card.id=id;card.group=image->group;card.label="Capture "+std::to_string(animation.cards.size()+1);
            animation.cards.push_back(std::move(card));
        }
        const auto* viewport=ImGui::GetMainViewport();const double now=ImGui::GetTime();
        auto snapshot=animation.director.Select(*latest,now,ReducedMotion());
        if(!workspace) {
            ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleColor(ImGuiCol_WindowBg,{.012f,.014f,.018f,1});
            ImGui::Begin("Automatic bracketing",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking);
        }
        ImGui::TextUnformatted("Automatic bracketing");
        std::string status=Raw::Bracketing::ProcessingStageTitle(snapshot.progress.stage);
        if(auto work=coordinator.Current()){std::lock_guard<std::mutex> lock(work->mutex);status=work->status;}
        else if(!animation.active)status=coordinator.GetQueue().resumeWhenIdle
            ? "Paused until one minute without input."
            : coordinator.GetQueue().paused?"Processing paused.":"Waiting for the next bracket.";
        ImGui::TextUnformatted(status.c_str());
        if(workspace) {
            ImGui::TextDisabled("Switch tabs to keep editing. Closing this tab stops processing until one minute without input. Saved brackets are kept.");
            bool enabled=coordinator.Enabled();
            if(ImGui::Checkbox("Process automatically",&enabled))coordinator.SetEnabled(enabled);
            if(!coordinator.Error().empty())ImGui::TextWrapped("%s",coordinator.Error().c_str());
        }else {
            if(ImGui::Button("Browse gallery"))ui.visible=false;
            ImGui::SameLine();if(ImGui::Button("Return to editing")){
                const auto leave=[&ui]{ui.visible=false;ui.animation.active=false;ui.animation.renderer.Release();ui.completed.reset();};
                if(!coordinator.RequestForeground("continue editing",leave))leave();
            }
            ImGui::SameLine();
        }
        if(ImGui::Button(coordinator.GetQueue().paused?"Resume":"Pause after this bracket")) {
            if(coordinator.GetQueue().paused)coordinator.SetPaused(false);
            else coordinator.PauseUntilIdle(now);
        }
        if(coordinator.HasWork()) {
            ImGui::SameLine();if(ImGui::Button("Cancel current")){coordinator.PauseUntilIdle(now);coordinator.CancelCurrent();ui.visible=false;}
        }else if(workspace) {
            ImGui::SameLine();if(ImGui::Button("Run now"))coordinator.RunNow();
        }
        if(!animation.active) {
            if(workspace) {
                const auto& items=coordinator.GetQueue().items;
                const bool pending=std::any_of(items.begin(),items.end(),[](const auto& item){return item.available&&item.state==State::Pending;});
                if(!pending)ImGui::TextWrapped("All available brackets have been handled. Saved results are in the folder gallery.");
            }else {ImGui::End();ImGui::PopStyleColor();}
            return;
        }
        const auto size=ImGui::GetContentRegionAvail();const auto origin=ImGui::GetCursorScreenPos();
        const float finish=animation.finishedAt>=0?animation.director.Finish(now):0.f;
        if(ui.texture&&finish>=1.f) {
            const float scale=std::min(size.x/ui.width,size.y/ui.height);
            const ImVec2 extent(ui.width*scale,ui.height*scale);
            ImGui::SetCursorScreenPos({origin.x+(size.x-extent.x)*.5f,origin.y+(size.y-extent.y)*.5f});
            ImGui::Image(static_cast<ImTextureID>(ui.texture),extent);
        }else {
            const auto scene=animation.renderer.Draw(snapshot,animation.cards,size,{.012f,.014f,.018f,1},now-animation.began,ReducedMotion());
            if(scene){ImGui::Image(static_cast<ImTextureID>(scene),size,{0,1},{1,0});animation.renderer.Annotate(ImGui::GetWindowDrawList(),origin,size,snapshot,1);}
            if(ui.texture&&finish>0.f) {
                const float scale=std::min(size.x/ui.width,size.y/ui.height)*(.5f+.5f*finish);
                const ImVec2 extent(ui.width*scale,ui.height*scale);
                const ImVec2 minimum(origin.x+(size.x-extent.x)*.5f,origin.y+(size.y-extent.y)*.5f);
                ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(ui.texture),minimum,
                    {minimum.x+extent.x,minimum.y+extent.y},{0,0},{1,1},
                    ImGui::GetColorU32(ImVec4(1,1,1,std::min(1.f,finish*4))));
            }
        }
        if(!workspace){ImGui::End();ImGui::PopStyleColor();}
    }
}
}
