#include "AppShell.h"
#include "UiPerformanceCapture.h"
#include "UiPerformanceProbe.h"
#include "Utils/DisplayRefreshRate.h"
#include "Raw/RawLoader.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>

namespace {
namespace P=Stack::UiPerformance::Probe;
using V=P::Visual;
constexpr std::array<const char*,19> phases={"gallery_grid_scroll","gallery_panel_toggle","gallery_rail_reveal",
    "gallery_to_filmstrip","filmstrip_fractional","filmstrip_fast","filmstrip_reverse","filmstrip_scrollbar",
    "gallery_preview_scroll","gallery_to_grid","gallery_folders","gallery_selection","gallery_open_return","gallery_settled",
    "gallery_panel_reversal","gallery_panel_resize","gallery_rail_focus","gallery_rail_drag_suppression","gallery_properties"};
}

void AppShell::TickUiResponsivenessCapture() {
    auto& s=*m_UiPerformanceCapture;auto& r=s.responsiveness;auto& io=ImGui::GetIO();
    const double now=ImGui::GetTime();
    const auto fail=[&](const std::string& message){s.failure=message;s.finished=true;s.passed=false;
        if(r.processing)r.processing->Cancel();m_IsRunning=false;};
    if(std::chrono::steady_clock::now()-s.started>std::chrono::seconds(240)){fail("Gallery capture deadline exceeded");return;}
    auto* galleryWorkspace=FindProjectWorkspace(m_GalleryWorkspaceId);
    if(!galleryWorkspace){fail("Permanent Gallery unavailable");return;}
    auto& gallery=*galleryWorkspace->editor;
    if(!r.initialized) {
        const auto root=std::filesystem::is_directory(s.project)?s.project:s.project.parent_path();
        ActivateProjectWorkspace(m_GalleryWorkspaceId);
        gallery.SetWorkspaceAppStatePersistenceEnabled(false);gallery.SetDocumentPersistenceEnabled(false);
        gallery.SetRawWorkspaceAllFolders(false);gallery.SetRawWorkspaceFolder(root);
        gallery.OpenRawWorkspaceGalleryWorkspace();
        gallery.SetRawGalleryGridView(true);
        m_NavigationRail.panelOpen=true;gallery.SetViewSectionPanelOpen(true);
        r.initialized=true;s.ready=true;r.began=now;
        std::ofstream metadata(s.output/"environment.txt");
        int width=0,height=0;glfwGetFramebufferSize(m_Window,&width,&height);
        metadata<<"project="<<s.project.u8string()<<"\nrenderer="<<glGetString(GL_RENDERER)
            <<"\nglVersion="<<glGetString(GL_VERSION)<<"\nimgui="<<IMGUI_VERSION
            <<"\nsize="<<width<<'x'<<height<<"\nhidden="<<!s.visible<<"\nvsync="<<s.visible
            <<"\nrefreshHz="<<DisplayRefreshRate::ForWindow(m_Window)<<"\nworkload="<<s.workload
            <<"\ninput=ImGui events; panel/open/return use production UI commands\n";
        std::cout<<"Gallery responsiveness capture: "<<s.workload<<'\n';
        return;
    }
    r.scanBusy=gallery.IsRawWorkspaceScanBusy();r.thumbnailsBusy=gallery.IsRawWorkspaceThumbnailBusy();
    if(r.sources.empty() && !gallery.GetRawWorkspaceState().sources.empty()) {
        for(const auto& source:gallery.GetRawWorkspaceState().sources) {
            if(Raw::RawLoader::IsRawPath(source.absolutePath.u8string()))r.sources.push_back(source.absolutePath);
            if(r.sources.size()==3)break;
        }
        if(r.sources.size()>=2 && (s.workload=="processing" || s.workload=="combined"))
            r.processing=std::make_shared<UiProcessingLoad>(r.sources,s.output);
    }
    if(r.processing) {
        r.processingBusy=r.processing->busy;r.processingRequests=r.processing->requests;
        r.processingCompletions=r.processing->completed;
        if(r.processing->failed){fail("Real processing workload failed; see processing.csv");return;}
    }
    r.moving=m_NavigationRail.panelAmount>0 && m_NavigationRail.panelAmount<1 ||
        m_NavigationRail.revealAmount>0 && m_NavigationRail.revealAmount<1 || m_GalleryNavigationMotion->Active();
    if(r.phase<0) {
        if(gallery.GetRawWorkspaceState().sources.empty() || now-r.began<2) return;
        if(s.workload=="idle" && (r.scanBusy || r.thumbnailsBusy) && now-r.began<90)return;
        if((s.workload=="processing" || s.workload=="combined") && !r.processing) {fail("Need two distinct RAW images for processing load");return;}
        r.phase=r.onlyPhase>=0?r.onlyPhase:0;r.phaseStarted=now;r.event=0;
    }
    if((s.workload=="loading" || s.workload=="combined") && now-r.lastScan>7 && !r.scanBusy) {
        gallery.RescanRawWorkspace();r.lastScan=now;
    }
    if(now-r.phaseStarted>=2.8) {
        io.AddMouseButtonEvent(0,false);r.click=0;
        if(r.onlyPhase>=0)++r.cycle;
        else if(++r.phase==int(phases.size())) {r.phase=0;++r.cycle;}
        if(r.cycle==2) {
            if(r.processing)r.processing->Cancel();
            s.passed=true;s.finished=true;m_IsRunning=false;return;
        }
        r.phaseStarted=now;r.event=0;
        std::cout<<"UI capture "<<r.cycle<<' '<<phases[r.phase]<<'\n';
    }
    r.scenario=std::string(r.cycle?"warm/":"first/")+phases[r.phase];
    const double age=now-r.phaseStarted;
    const auto point=[&](V visual,float x=.5f,float y=.5f) {
        const auto item=P::Last(visual);
        if(!item.valid || item.region.width<=0 || item.region.height<=0)return false;
        r.pointerX=item.region.x+item.region.width*x;r.pointerY=item.region.y+item.region.height*y;return true;
    };
    const auto click=[&](V target,V observed,const char* action) {
        if(!point(target))return;
        r.click=1;r.clickVisual=int(observed);r.clickAction=action;
    };
    const auto wheel=[&](V visual,float amount) {
        P::BeginInteraction(visual,r.scenario,"wheel","imgui-wheel");io.AddMouseWheelEvent(0,amount);
    };
    // Keep the pointer stable between events: GLFW also publishes physical-cursor fallback.
    if(r.click==2){io.AddMouseButtonEvent(0,true);r.click=3;}
    else if(r.click==3){P::BeginInteraction(V(r.clickVisual),r.scenario,r.clickAction,"imgui-release");io.AddMouseButtonEvent(0,false);r.click=0;}
    else if(r.click==1)r.click=2;
    if(r.phase==0) {
        point(V::Gallery,.7f,.5f);
        if(age>.3+r.event*.4 && r.event<5){wheel(V::Gallery,r.event<3?-2.f:2.f);++r.event;}
    } else if(r.phase==1) {
        if(age>.25+r.event*.38 && r.event<6) {
            P::BeginInteraction(V::Panel,r.scenario,"toggle","ui-command");ToggleWorkspaceSectionPanel();++r.event;
        }
    } else if(r.phase==2) {
        const auto* v=ImGui::GetMainViewport();
        const bool edge=age<.7 || age>1.65 && age<2.15;
        r.pointerX=v->Pos.x+(edge?1.f:500.f);r.pointerY=v->Pos.y+400.f;
        if(r.event==0 || r.event==1 && age>=.7 || r.event==2 && age>=1.65 || r.event==3 && age>=2.15) {
            P::BeginInteraction(V::Rail,r.scenario,edge?"edge_reveal":"edge_leave","imgui-pointer");++r.event;
        }
    } else if(r.phase==3 || r.phase==9) {
        if(age>.3 && r.event++==0)click(r.phase==3?V::FilmstripButton:V::GridButton,V::GalleryMode,"mode");
    } else if(r.phase>=4 && r.phase<=6) {
        point(V::Filmstrip,.5f,.5f);
        const double spacing=r.phase==4?.28:.085;
        if(age>.2+r.event*spacing && age<1.9) {
            wheel(V::Filmstrip,r.phase==4?-.18f:r.phase==6 && age>1.0?1.2f:-1.2f);++r.event;
        }
    } else if(r.phase==7) {
        if(age<.3)point(V::FilmstripScrollbar,.5f,.5f);
        if(age>.3 && r.event==0){P::BeginInteraction(V::Filmstrip,r.scenario,"scrollbar_drag","imgui-event");io.AddMouseButtonEvent(0,true);++r.event;}
        if(age>.3 && age<1.7)point(V::FilmstripScrollbar,float(.15+.7*(age-.3)/1.4),.5f);
        if(age>=1.7 && r.event==1){io.AddMouseButtonEvent(0,false);++r.event;}
    } else if(r.phase==8) {
        const auto* v=ImGui::GetMainViewport();r.pointerX=v->Pos.x+v->Size.x*.65f;r.pointerY=v->Pos.y+v->Size.y*.3f;
        if(age>.4+r.event*.6 && r.event<3){wheel(V::Filmstrip,-1.f);++r.event;}
    } else if(r.phase==10) {
        if(age>.3 && r.event==0){click(V::BrowseButton,V::GalleryMode,"folders");++r.event;}
        if(age>1.4 && r.event==1) {
            P::BeginInteraction(V::GalleryMode,r.scenario,"return_folder","ui-command");
            gallery.SetRawWorkspaceFolder(std::filesystem::is_directory(s.project)?s.project:s.project.parent_path());++r.event;
        }
    } else if(r.phase==11) {
        if(age>.3 && r.event++==0)click(V::GalleryItem,V::GalleryItem,"select");
    } else if(r.phase==12) {
        if(age>.25 && r.event==0 && !r.sources.empty()) {
            P::BeginInteraction(V::Project,r.scenario,"open_original","ui-command");
            gallery.SelectRawWorkspaceSourceForGallery(r.sources.front().lexically_relative(gallery.GetRawWorkspaceState().workspaceRoot).generic_u8string(),false,false,true);
            ++r.event;
        }
        if(age>1.5 && r.event==1) {
            P::BeginInteraction(V::Project,r.scenario,"return_gallery","ui-command");
            QueueRailNavigation(0);++r.event;
        }
    } else if(r.phase==14) {
        if(age>.2+r.event*.08 && r.event<12) {
            P::BeginInteraction(V::Panel,r.scenario,"rapid_toggle","ui-command");ToggleWorkspaceSectionPanel();++r.event;
        }
    } else if(r.phase==15) {
        if(age<.1 && !m_NavigationRail.panelOpen)ToggleWorkspaceSectionPanel();
        if(age<.3)point(V::PanelResize);
        if(age>.3 && r.event==0) {
            io.AddMouseButtonEvent(0,true);r.dragStartX=r.pointerX;++r.event;
        } else if(age>.3 && r.event==1) {
            P::BeginInteraction(V::Panel,r.scenario,"resize","imgui-event");
            ++r.event;
        }
        if(r.event>=2 && age<1.4)r.pointerX=r.dragStartX+4+float((age-.3)*100);
        if(age>=1.4 && age<2.4)r.pointerX=r.dragStartX+float((2.4-age)*114);
        if(age>=2.4)r.pointerX=r.dragStartX;
        if(age>=2.45 && r.event==2){io.AddMouseButtonEvent(0,false);++r.event;}
    } else if(r.phase==16) {
        const auto* v=ImGui::GetMainViewport();r.pointerX=v->Pos.x+1;r.pointerY=v->Pos.y+400;
        const bool focused=age<.7 || age>1.5;
        io.AddFocusEvent(focused);
        if(r.event==0 || r.event==1 && age>=.7 || r.event==2 && age>=1.5) {
            P::BeginInteraction(V::Rail,r.scenario,focused?"focus_return":"focus_leave","imgui-event");++r.event;
        }
    } else if(r.phase==17) {
        const auto* v=ImGui::GetMainViewport();
        r.pointerX=v->Pos.x+(age<.3 || age>1.7 && age<2?500.f:1.f);r.pointerY=v->Pos.y+400;
        if(r.event==0){io.AddMouseButtonEvent(0,true);++r.event;}
        if(age>1.3 && r.event==1){io.AddMouseButtonEvent(0,false);++r.event;}
        if(age>2 && r.event==2){P::BeginInteraction(V::Rail,r.scenario,"reenter_after_drag","imgui-pointer");++r.event;}
    } else if(r.phase==18) {
        if(age>.3+r.event*.8 && r.event<3){click(V::PropertiesButton,V::Controls,"properties");++r.event;}
    }
    io.AddMousePosEvent(r.pointerX,r.pointerY);
}
