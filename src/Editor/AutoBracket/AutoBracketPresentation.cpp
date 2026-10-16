#include "AutoBracketPresentation.h"
#include "Renderer/GLHelpers.h"
#include "Notifications/NotificationPresenter.h"
#include <imgui_internal.h>
#include <algorithm>
namespace Stack::AutoBracket {
namespace {
bool ReducedMotion() {
    return Stack::Notifications::SystemReducedMotion();
}
void ReleaseTerminal(Presentation& ui) {
    ui.animation.active=false;
    ui.animation.renderer.Release();ui.animation.sceneVisible=false;
    ui.animation.director={};ui.animation.cards.clear();
    if(ui.animation.mailbox)ui.animation.mailbox->DiscardEvidence();
    ui.completed.reset();
    if(ui.texture){glDeleteTextures(1,&ui.texture);ui.texture=0;}
}
}
Presentation::~Presentation(){if(texture)glDeleteTextures(1,&texture);}
void Presentation::Begin(const std::shared_ptr<Work>& work,double now) {
    if(texture){glDeleteTextures(1,&texture);texture=0;}
    completed.reset();animation.renderer.Release();animation.director={};animation.cards.clear();
    generation=work->generation;animation.generation=generation;animation.mailbox=work->mailbox;
    animation.active=true;animation.began=now;animation.finishedAt=-1;animation.canceling=false;visible=true;
    cardsPublication=-1;cardsSequence=0;
}
void Presentation::Complete(const std::shared_ptr<Work>& work,double now) {
    if(work->generation!=generation)Begin(work,now);
    if(!animation.active||!foreground) {
        ReleaseTerminal(*this);return;
    }
    completed=work;
    if(work->outcome!=State::Completed||work->coverPixels.empty()) {
        ReleaseTerminal(*this);return;
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
void Presentation::SetForeground(bool value) {
    if(foreground==value)return;
    foreground=value;
    if(!foreground) {
        if(animation.finishedAt>=0){ReleaseTerminal(*this);return;}
        animation.renderer.Release();animation.sceneVisible=false;completed.reset();
        if(texture){glDeleteTextures(1,&texture);texture=0;}
    }
}
void Presentation::Tick(double now) {
    if(animation.active&&animation.finishedAt>=0&&animation.director.Done(now)) {
        ReleaseTerminal(*this);
    }
}
void DrawPresentation(Presentation& ui,AutoBracketCoordinator& coordinator,bool workspace) {
    if(!workspace&&(!ui.animation.active||!ui.visible))return;
    const auto* viewport=ImGui::GetMainViewport();
    if(!workspace) {
        ImGui::SetNextWindowPos(viewport->Pos);ImGui::SetNextWindowSize(viewport->Size);ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        ImGui::Begin("Automatic bracketing",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
            ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoBackground);
    }
    const double now=ImGui::GetTime();
    if(ui.animation.active) {
        auto& animation=ui.animation;
        auto latest=animation.mailbox?animation.mailbox->Read():nullptr;
        static const Editor::ProcessingSnapshot empty;
        const auto& current=latest?*latest:empty;
        if(ui.cardsPublication!=current.publishedAt||ui.cardsSequence!=current.progress.sequence) {
            for(const auto& [id,image]:current.images)
                if(std::none_of(animation.cards.begin(),animation.cards.end(),[&](const auto& card){return card.id==id;})) {
                    Editor::ProcessingCard card;card.id=id;card.group=image->group;
                    card.label="Capture "+std::to_string(animation.cards.size()+1);animation.cards.push_back(std::move(card));
                }
            ui.cardsPublication=current.publishedAt;ui.cardsSequence=current.progress.sequence;
        }
        const bool reduced=ReducedMotion();
        const auto& snapshot=animation.director.Select(current,now,reduced);
        const ImVec2 origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
        const ImVec2 end{origin.x+size.x,origin.y+size.y};
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin,end,IM_COL32(0,0,0,255));
        animation.sceneVisible=true;
        const auto scene=animation.renderer.Draw(snapshot,animation.cards,size,{0,0,0,1},now-animation.began,reduced);
        if(scene) {
            draw->AddImage(static_cast<ImTextureID>(scene),origin,end,{0,1},{1,0});
            animation.renderer.Annotate(draw,origin,size,snapshot,1);
        }
        const float finish=animation.finishedAt>=0?animation.director.Finish(now):0.f;
        if(ui.texture&&finish>0.f) {
            const float fit=std::min(size.x/ui.width,size.y/ui.height)*(.5f+.5f*finish);
            const ImVec2 extent(ui.width*fit,ui.height*fit);
            const ImVec2 minimum(origin.x+(size.x-extent.x)*.5f,origin.y+(size.y-extent.y)*.5f);
            draw->AddImage(static_cast<ImTextureID>(ui.texture),minimum,{minimum.x+extent.x,minimum.y+extent.y},
                {0,0},{1,1},ImGui::GetColorU32(ImVec4(1,1,1,std::min(1.f,finish*4))));
        }
        ImGui::Dummy(size);
        const float font=ImGui::GetFontSize(),margin=std::max(24.f,font*2);
        draw->AddText({origin.x+margin,end.y-margin-font*3},IM_COL32(160,160,160,255),"Automatic burst");
        draw->AddText({origin.x+margin,end.y-margin-font*1.5f},IM_COL32(235,235,235,255),
            Raw::Bracketing::ProcessingStageTitle(snapshot.progress.stage));
        if(current.progress.total) {
            const float amount=std::clamp(float(double(current.progress.completed)/current.progress.total),0.f,1.f);
            const ImVec2 bar{origin.x+margin,end.y-margin};
            draw->AddRectFilled(bar,{bar.x+120,bar.y+2},IM_COL32(60,60,60,255));
            draw->AddRectFilled(bar,{bar.x+120*amount,bar.y+2},IM_COL32(205,205,205,255));
        }
        const char* pause=coordinator.GetQueue().paused?"Resume":"Pause after this burst";
        const char* action=coordinator.HasWork()?"Cancel burst":"Done";
        const float width=ImGui::CalcTextSize(pause).x+ImGui::CalcTextSize(action).x+
            ImGui::GetStyle().FramePadding.x*4+ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorScreenPos({std::max(origin.x+margin,end.x-margin-width),end.y-margin-ImGui::GetFrameHeight()});
        if(ImGui::Button(pause)) {
            if(coordinator.GetQueue().paused)coordinator.SetPaused(false);else coordinator.PauseUntilIdle(now);
        }
        ImGui::SameLine();
        if(ImGui::Button(action)) {
            if(coordinator.HasWork()){coordinator.PauseUntilIdle(now);coordinator.CancelCurrent();}
            else animation.director.Skip(now);
        }
    } else {
        ImGui::TextUnformatted("Automatic bracketing");
        ImGui::TextUnformatted(coordinator.GetQueue().resumeWhenIdle?"Paused until one minute without input.":
            coordinator.GetQueue().paused?"Processing paused.":"Waiting for the next bracket.");
        ImGui::TextWrapped("Switch projects to keep editing. Saved bursts remain in Gallery.");
        ImGui::TextWrapped("Starts automatically when the folder is ready. Pauses resume after one minute without input.");
        if(ImGui::Button("Run now"))coordinator.RunNow();
        ImGui::SameLine();
        if(ImGui::Button(coordinator.GetQueue().paused?"Resume":"Pause")) {
            if(coordinator.GetQueue().paused)coordinator.SetPaused(false);else coordinator.PauseUntilIdle(now);
        }
        if(!coordinator.Error().empty())ImGui::TextWrapped("%s",coordinator.Error().c_str());
    }
    if(!workspace){ImGui::End();ImGui::PopStyleVar();}
}
}
