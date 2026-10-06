#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace {
bool ReducedProcessingMotion() {
#if defined(_WIN32)
    BOOL enabled=TRUE;
    if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0))return !enabled;
#endif
    return false;
}
ImU32 ProcessingInk(float alpha,float brightness=1) {
    return ImGui::ColorConvertFloat4ToU32({brightness,brightness,brightness,alpha});
}
}
void Stack::Editor::ProcessingPresentation::RestoreFocus() {
    auto* context=ImGui::GetCurrentContext();
    if(focusCaptured&&context&&context->NavWindow&&context->NavWindow->ID==focusWindowId)
        if(auto* window=ImGui::FindWindowByID(previousFocus))ImGui::FocusWindow(window);
    previousFocus=focusWindowId=0;focusCaptured=false;
}
Stack::Editor::ProcessingPresentation::~ProcessingPresentation() {RestoreFocus();}
bool EditorModule::IsBracketingPresentationActive() const {
    return m_Bracketing&&m_Bracketing->presentation&&m_Bracketing->presentation->active;
}
void EditorModule::UpdateBracketingPresentation(bool foreground) {
    if (!foreground && m_Bracketing && m_Bracketing->presentation) {
        auto& presentation = *m_Bracketing->presentation;
        presentation.previousFocus = presentation.focusWindowId = 0;
        presentation.focusCaptured = false;
    }
    if(!IsBracketingPresentationActive()) {
        if(foreground&&m_Bracketing&&m_Bracketing->presentation)m_Bracketing->presentation->RestoreFocus();
        if(m_Bracketing&&m_Bracketing->presentation) {
            auto& presentation=*m_Bracketing->presentation;
            presentation.renderer.Release();presentation.director={};presentation.mailbox->DiscardEvidence();
        }
        return;
    }
    auto& p=*m_Bracketing->presentation;
    if(m_Bracketing->projectId!=p.projectId||m_Bracketing->storedRecipe!=p.recipeIdentity||m_Bracketing->failed) {
        p.active=false;if(foreground)p.RestoreFocus();return;
    }
    if(p.waitingForRaw) {
        unsigned texture=0;int width=0,height=0;
        const bool matched=!m_RenderDirty&&p.expectedHash&&m_BracketingPresentedSourceHash==p.expectedHash;
        const bool displayable=matched&&(TryGetActiveRawWorkspacePresentationTexture(texture,width,height)||
            m_RawWorkspacePreviewOutputKind==RawWorkspacePreviewOutputKind::Tiled);
        if(displayable&&p.finishedAt<0) {
            p.finishedAt=ImGui::GetTime();
            if(auto snapshot=p.mailbox->Read())p.director.Ready(*snapshot,p.finishedAt,ReducedProcessingMotion());
        }else if(!m_RawWorkspaceStaleRenderStatusText.empty()&&!m_RenderPending&&!IsAnyRenderBackendBusy()) {
            p.active=false;m_Bracketing->status=m_RawWorkspaceStaleRenderStatusText;
        }
    }
    if(p.director.Done(ImGui::GetTime()))p.active=false;
    if(!p.active&&foreground)p.RestoreFocus();
}
void EditorModule::CancelBracketingPresentation() {
    if(!IsBracketingPresentationActive())return;
    auto& ui=*m_Bracketing;auto& p=*ui.presentation;
    if(p.finishedAt>=0){p.director.Skip(ImGui::GetTime());return;}
    if(p.canceling)return;
    p.canceling=true;ui.pending=false;ui.publishRequested=false;ui.previewDirty=false;
    if(ui.job){ui.job->canceled=true;ui.status="Canceling...";}
    else {p.active=false;ui.status="Bracket completed. RAW preview remains available.";}
    if(ui.interactiveRaw){ui.interactiveRaw.reset();MarkRenderDirty();}
}
void EditorModule::RenderBracketingPresentation(ImVec4, ImVec2 bodyPosition, ImVec2 bodySize) {
    if(!IsBracketingPresentationActive())return;
    auto& p=*m_Bracketing->presentation;
    if(!p.focusCaptured) {
        auto* window=ImGui::GetCurrentContext()->NavWindow;
        p.previousFocus=window?window->ID:0;p.focusCaptured=true;
    }
    auto latest=p.mailbox->Read();if(!latest)latest=std::make_shared<Stack::Editor::ProcessingSnapshot>();
    const double now=ImGui::GetTime();const bool reduced=ReducedProcessingMotion();
    auto snapshot=p.director.Select(*latest,now,reduced);
    const float finish=p.director.Finish(now);
    const float enter=reduced?1.f:std::clamp(float((now-p.began)/.25),0.f,1.f);
    const float alpha=enter*(1-finish);
    const auto* viewport=ImGui::GetMainViewport();
    if(bodySize.x<1||bodySize.y<1)return;
    ImGui::SetNextWindowPos(bodyPosition);ImGui::SetNextWindowSize(bodySize);ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    const auto windowName="Bracketing processing###BracketingProcessing-"+p.projectId+"-"+std::to_string(p.generation);
    ImGui::Begin(windowName.c_str(),nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoBackground);
    p.focusWindowId=ImGui::GetCurrentWindow()->ID;
    ImGui::PopStyleVar(2);
    auto* draw=ImGui::GetWindowDrawList();
    draw->Flags|=ImDrawListFlags_AntiAliasedLines|ImDrawListFlags_AntiAliasedFill;
    const auto end=ImVec2{bodyPosition.x+bodySize.x,bodyPosition.y+bodySize.y};
    draw->AddRectFilled(bodyPosition,end,ImGui::ColorConvertFloat4ToU32({.012f,.014f,.018f,alpha}));
    const float font=ImGui::GetFontSize(),margin=std::max(28.f,font*2.2f);
    const float top=std::max(86.f,font*5.8f),bottom=std::max(76.f,font*4.5f);
    const ImVec2 scenePosition{bodyPosition.x,bodyPosition.y+top};
    const ImVec2 sceneSize{bodySize.x,std::max(1.f,bodySize.y-top-bottom)};
    unsigned scene=0;
    try {scene=p.renderer.Draw(snapshot,p.cards,sceneSize,{.012f,.014f,.018f,1},now-p.began,reduced);}
    catch(...) {scene=0;}
    if(scene) {
        draw->AddImage((ImTextureID)(intptr_t)scene,scenePosition,{end.x,scenePosition.y+sceneSize.y},{0,1},{1,0},ProcessingInk(alpha));
        p.renderer.Annotate(draw,scenePosition,sceneSize,snapshot,alpha);
    }
    const char* heading=p.canceling?"Canceling...":Raw::Bracketing::ProcessingStageTitle(snapshot.progress.stage);
    draw->AddText({bodyPosition.x+margin,bodyPosition.y+margin},ProcessingInk(alpha*.5f),"BRACKETING");
    draw->AddText({bodyPosition.x+margin,bodyPosition.y+margin+font*1.8f},ProcessingInk(alpha),heading);
    if(snapshot.evidence&&!snapshot.evidence->caption.empty())
        draw->AddText({bodyPosition.x+margin,bodyPosition.y+margin+font*3.25f},ProcessingInk(alpha*.45f),snapshot.evidence->caption.c_str());
    else if(!scene&&!snapshot.progress.detail.empty())
        draw->AddText({bodyPosition.x+margin,bodyPosition.y+margin+font*3.25f},ProcessingInk(alpha*.55f),snapshot.progress.detail.c_str());
    const float lineY=end.y-margin-font;
    const auto& live=latest->progress;
    const float dotStep=std::max(9.f,font*.7f);
    const unsigned dots=unsigned(std::min<std::size_t>(24,std::max<std::size_t>(1,p.cards.size())));
    const double fraction=live.total?std::clamp(double(live.completed)/live.total,0.,1.):0;
    for(unsigned i=0;i<dots;++i) {
        const ImVec2 point{bodyPosition.x+margin+i*dotStep,lineY};
        if(live.total&&double(i+1)/dots<=fraction)draw->AddCircleFilled(point,2.4f,ProcessingInk(alpha*.9f),12);
        else draw->AddCircle(point,2.4f,ProcessingInk(alpha*.28f),12,1);
    }
    std::string count=live.total?std::to_string(live.completed)+" / "+std::to_string(live.total):"";
    if(p.finishedAt>=0)count="Ready";
    draw->AddText({bodyPosition.x+margin+dots*dotStep+font,lineY-font*.5f},ProcessingInk(alpha*.65f),count.c_str());
    if(finish>0) {
        unsigned finalTexture=0;int w=0,h=0;
        if(TryGetActiveRawWorkspacePresentationTexture(finalTexture,w,h)) {
            float width=sceneSize.x*.49f,height=width*h/std::max(1,w);
            if(height>sceneSize.y*.60f){height=sceneSize.y*.60f;width=height*w/std::max(1,h);}
            ImVec2 minimum{scenePosition.x+sceneSize.x*.43f-width*.5f,scenePosition.y+sceneSize.y*.47f-height*.5f};
            ImVec2 maximum{minimum.x+width,minimum.y+height};
            if(m_BracketingImageMax.x>m_BracketingImageMin.x) {
                minimum={minimum.x+(m_BracketingImageMin.x-minimum.x)*finish,minimum.y+(m_BracketingImageMin.y-minimum.y)*finish};
                maximum={maximum.x+(m_BracketingImageMax.x-maximum.x)*finish,maximum.y+(m_BracketingImageMax.y-maximum.y)*finish};
            }
            draw->AddImage((ImTextureID)(intptr_t)finalTexture,minimum,maximum,{0,1},{1,0},ProcessingInk(std::min(1.f,finish*4)));
        }
    }
    const char* button=p.finishedAt>=0?"View result":p.canceling?"Canceling...":"Cancel";
    const float buttonWidth=ImGui::CalcTextSize(button).x+font*2;
    ImGui::SetCursorScreenPos({end.x-margin-buttonWidth,lineY-font});
    ImGui::PushStyleColor(ImGuiCol_Button,{.08f,.09f,.10f,alpha});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.14f,.16f,.18f,alpha});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,{.2f,.22f,.24f,alpha});
    ImGui::PushStyleColor(ImGuiCol_Text,{.9f,.92f,.95f,alpha});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5);
    ImGui::BeginDisabled(p.canceling);
    const bool keyboardInput = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::GetCurrentContext()->OpenPopupStack.empty() && !ImGui::GetIO().WantTextInput;
    if(ImGui::Button(button,{buttonWidth,font*2})||
        (keyboardInput&&(ImGui::IsKeyPressed(ImGuiKey_Escape,false)||
            (p.finishedAt>=0&&ImGui::IsKeyPressed(ImGuiKey_Enter,false)))))CancelBracketingPresentation();
    ImGui::EndDisabled();ImGui::PopStyleVar();ImGui::PopStyleColor(4);
    ImGui::End();
}
