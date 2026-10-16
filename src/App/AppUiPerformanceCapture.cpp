#include "App/AppShell.h"
#include "App/UiPerformanceCapture.h"
#include "App/UiPerformanceProbe.h"
#include "Platform/GlfwMonitorRefresh.h"
#include "Raw/RawLoader.h"
#include "Graph/GraphDocumentRules.h"
#include "Project/ProjectSession.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {
using Visual = Stack::UiPerformance::Probe::Visual;
namespace Probe = Stack::UiPerformance::Probe;
constexpr std::array<const char*,23> scenarios {
    "graph_idle_closed","graph_idle_open","graph_pan","graph_zoom","graph_node_drag",
    "catalog_search","catalog_scroll","graph_panel_toggle","graph_bar_reveal",
    "view_idle_closed","view_idle_open","view_adjustment","view_panel_toggle","view_bar_reveal",
    "graph_view_switch","graph_multiple_projects","view_multiple_projects","project_switch",
    "view_pan","view_tool_drag","view_zoom","view_pan_over_tool","view_edit_resolution"
};
const char* stages[] {"frame","mainCompletions","interface","drawSubmission","swap","projectWorkspaces",
    "graphCanvas","logicalLayout","projection","nodeOrder","links","linkHitTest","catalog","previewRequests",
    "viewWorkspace","viewPanel","floatingControls","navigationBars","compositor","nativeChrome",
    "viewServices","viewContext","viewImage","viewSubmission",
    "backdrop","backdropBlur","backdropNoise","compositorBody",
    "frameTarget","compositorState","compositorPresent","platformPresent"};
static_assert(std::size(stages)==static_cast<std::size_t>(Stack::UiPerformance::Stage::Count));
void Pointer(float x,float y) { ImGui::GetIO().AddMousePosEvent(x,y); }
void PointAt(Visual kind,float x=.5f,float y=.5f) {
    const auto region=Probe::Last(kind).region;
    Pointer(region.x+region.width*x,region.y+region.height*y);
}
void ClearInput() {
    auto& io=ImGui::GetIO();
    for(int button=0;button<3;++button) io.AddMouseButtonEvent(button,false);
    for(auto key : {ImGuiKey_A,ImGuiKey_Escape,ImGuiKey_Tab,ImGuiKey_Backspace,ImGuiMod_Ctrl}) io.AddKeyEvent(key,false);
}
bool LoadFixture(EditorModule& editor,const std::filesystem::path& path) {
    if(path.extension()==".stack" || std::filesystem::is_directory(path))return editor.RequestOpenProjectFromPath(path,true);
    if(!editor.GetPipeline().LoadSourceImage(path.u8string()) || !editor.AddImageNodeFromFile(path.u8string(),{20,30}))return false;
    auto& graph=editor.GetNodeGraph();const int source=graph.GetSelectedNodeId();
    editor.AddTechnicalImageNodeAt(Stack::NodeMath::TechnicalImageOperation::AssignLinearSrgb,{290,30});const int linear=graph.GetSelectedNodeId();
    editor.AddLayerNodeAt(LayerType::Brightness,{540,30});const int brightness=graph.GetSelectedNodeId();
    editor.AddLayerNodeAt(LayerType::Contrast,{790,30});const int contrast=graph.GetSelectedNodeId();
    editor.AddLayerNodeAt(LayerType::ToneCurve,{20,330});const int curve=graph.GetSelectedNodeId();
    editor.AddPreviewNodeAt({290,330});const int preview=graph.GetSelectedNodeId();
    editor.AddScopeNodeAt(EditorNodeGraph::ScopeKind::Histogram,{540,330});const int scope=graph.GetSelectedNodeId();
    editor.AddOutputNodeAt({790,330});const int output=graph.GetSelectedNodeId();
    std::string error;
    return editor.ConnectGraphSockets(source,"imageOut",linear,"imageIn",&error) &&
        editor.ConnectGraphSockets(linear,"imageOut",brightness,"imageIn",&error) &&
        editor.ConnectGraphSockets(brightness,"imageOut",contrast,"imageIn",&error) &&
        editor.ConnectGraphSockets(contrast,"imageOut",curve,"imageIn",&error) &&
        editor.ConnectGraphSockets(curve,"imageOut",preview,EditorNodeGraph::kPreviewInputSocketId,&error) &&
        editor.ConnectGraphSockets(curve,"imageOut",scope,EditorNodeGraph::kScopeInputSocketId,&error) &&
        editor.ConnectGraphSockets(curve,"imageOut",output,"imageIn",&error);
}
}
std::string UiPerformanceCapture::Scenario() const {
    if (qualityCapture) return "view_quality";
    return step>=0 && step<int(scenarios.size()) ? scenarios[step] : "warmup";
}
void AppShell::ConfigureUiPerformanceCapture(std::filesystem::path project,std::filesystem::path output,const std::string& onlyScenario) {
    m_UiPerformanceCapture=std::make_unique<UiPerformanceCapture>();
    auto& s=*m_UiPerformanceCapture; s.project=std::move(project);s.output=std::move(output);
    s.qualityCapture=onlyScenario=="view_quality";
    s.imageSurfaceCapture=onlyScenario=="view_pan" || onlyScenario=="view_tool_drag" ||
        onlyScenario=="view_zoom" || onlyScenario=="view_pan_over_tool" || onlyScenario=="view_edit_resolution";
    if(!onlyScenario.empty() && !s.qualityCapture) {
        const auto found=std::find(scenarios.begin(),scenarios.end(),onlyScenario);
        if(found==scenarios.end())throw std::runtime_error("Unknown UI performance scenario");
        s.onlyStep=int(found-scenarios.begin());
    }
    s.frames.reserve(2400);s.started=s.nextTick=std::chrono::steady_clock::now();
    std::filesystem::create_directories(s.output); Probe::Enable(s.output);
    Stack::UiPerformance::EnableFrameCapture(true);
}
bool AppShell::WasUiPerformanceCaptureSuccessful() const {
    return m_UiPerformanceCapture && m_UiPerformanceCapture->passed;
}
void AppShell::PaceUiPerformanceCapture() {
    if(!m_UiPerformanceCapture)return;
    auto& s=*m_UiPerformanceCapture;
    std::this_thread::sleep_until(s.nextTick);
    s.nextTick=std::chrono::steady_clock::now()+std::chrono::microseconds(16667);
}
void AppShell::TickUiPerformanceCapture() {
    if(!m_UiPerformanceCapture)return;
    auto& s=*m_UiPerformanceCapture; auto& io=ImGui::GetIO();io.AddFocusEvent(true);
    if(std::chrono::steady_clock::now()-s.started>std::chrono::seconds(180)) {
        s.failure="Diagnostic deadline exceeded";s.finished=true;m_IsRunning=false;return;
    }
    if(!s.loadQueued) {
        for(auto& workspace:m_ProjectWorkspaces)workspace->editor->SetDocumentPersistenceEnabled(false);
        s.firstWorkspace=CreateProjectWorkspace();
        if(!ActivateProjectWorkspace(s.firstWorkspace)){s.failure="Could not activate fixture workspace";s.finished=true;m_IsRunning=false;return;}
        m_Editor->SetDocumentPersistenceEnabled(false);
        if ((s.qualityCapture || s.imageSurfaceCapture) && Raw::RawLoader::IsRawPath(s.project.u8string())) {
            m_SharedRawWorkspaceRoot=s.project.parent_path();
            SynchronizeRawWorkspaceFolder();
        }
        Probe::WatchEditor(m_Editor);
        s.loadQueued=(s.qualityCapture || s.imageSurfaceCapture)
            ? LoadUiViewportQualityFixture(*m_Editor,s.project,s.qualityTone,s.qualityLocalEv)
            : LoadFixture(*m_Editor,s.project);
        if(!s.loadQueued){s.failure="Could not queue fixture project";s.finished=true;m_IsRunning=false;}
        return;
    }
    if(!s.ready) {
        if ((s.qualityCapture || s.imageSurfaceCapture) && Raw::RawLoader::IsRawPath(s.project.u8string())) {
            if (!s.qualityRawOpened) {
                const auto& sources=m_Editor->GetRawWorkspaceState().sources;
                const auto found=std::find_if(sources.begin(),sources.end(),[&](const auto& source) {
                    return source.absolutePath.lexically_normal()==s.project.lexically_normal();
                });
                if (found==sources.end()) return;
                m_Editor->SelectRawWorkspaceSourceForGallery(found->relativePathKey,false,false,true);
                m_Editor->RequestOpenRawLabTab();s.qualityRawOpened=true;
                return;
            }
            if (m_Editor->IsRawWorkspaceProjectLoadBusy() || !m_Editor->IsRawWorkspaceProjectActive()) return;
            const auto& background=m_Editor->GetProjectSession().rawLayers.State().background;
            m_Editor->OpenRawLayerMaskGraph(background.id,false);
            for (const auto& node:m_Editor->GetNodeGraph().GetNodes()) {
                if (node.kind!=EditorNodeGraph::NodeKind::RawOperation) continue;
                if (node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::ToneCurve) s.qualityTone=node.id;
                if (node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::LocalEv) s.qualityLocalEv=node.id;
            }
            if (s.qualityTone<0 || s.qualityLocalEv<0) return;
        }
        if(m_Editor->GetProjectLoadTaskState()==Async::TaskState::Failed || m_Editor->HasDeferredLoadedProjectApplyFailed()) {
            s.failure=m_Editor->GetProjectLoadStatusText();s.finished=true;m_IsRunning=false;return;
        }
        if(m_Editor->GetNodeGraph().GetNodes().empty() || m_Editor->IsDeferredLoadedProjectApplyActive() ||
            Async::IsBusy(m_Editor->GetProjectLoadTaskState())) return;
        s.ready=true;RequestTabSwitch(1);m_Editor->SetWorkspaceMode(Stack::Project::WorkspaceMode::Graph);
        for(const auto& node:m_Editor->GetNodeGraph().GetNodes()) {
            if(node.kind==EditorNodeGraph::NodeKind::Layer && node.layerType==LayerType::Brightness)s.editNode=node.id;
            if(s.dragNode<0 && node.kind==EditorNodeGraph::NodeKind::Layer)s.dragNode=node.id;
        }
        if (s.imageSurfaceCapture) s.editNode=s.qualityTone;
        Probe::WatchNode(s.dragNode);
        std::ofstream metadata(s.output/"environment.txt");
        metadata<<"project="<<s.project.u8string()<<"\nrenderer="<<glGetString(GL_RENDERER)
            <<"\nglVersion="<<glGetString(GL_VERSION)<<"\nsize=1280x800\nhidden=1\nvsync=0\npacingHz=60\n"
            <<"nodes="<<m_Editor->GetNodeGraph().GetNodes().size()<<"\nlinks="<<m_Editor->GetNodeGraph().GetLinks().size()<<'\n';
        const auto preferences=m_Editor->GetRawViewportPreferences();
        metadata<<"viewportTargetFps="<<m_Editor->GetRawViewportTargetFps()
            <<"\nviewportMode="<<static_cast<int>(preferences.mode)
            <<"\nminimumDetailPercent="<<preferences.minimumDetailPercent<<'\n';
        if(!s.qualityCapture && !s.imageSurfaceCapture && (s.editNode<0 || s.dragNode<0)){s.failure="Fixture requires an editable brightness node";s.finished=true;m_IsRunning=false;}
        return;
    }
    if(s.qualityCapture){TickUiViewportQualityCapture();return;}
    if(s.step<0) {
        if(++s.warmup<120)return;
        s.step=s.onlyStep>=0?s.onlyStep-1:-1;s.frame=150;
    }
    if(s.frame>=(s.step==22?420:s.step==20?360:150)) {
        ClearInput();++s.step;s.frame=0;
        if(s.step>=int(scenarios.size()) || s.onlyStep>=0 && s.step>s.onlyStep) {s.passed=true;s.finished=true;m_IsRunning=false;return;}
        const bool view=s.step>=9 && s.step<=13 || s.step==16 || s.step>=18;
        m_Editor->SetWorkspaceMode(view ? Stack::Project::WorkspaceMode::View : Stack::Project::WorkspaceMode::Graph);
        m_Editor->SetWorkspacePanelOpen(s.step==1 || s.step==5 || s.step==6 || s.step==10);
        m_NavigationRail.panelOpen=m_Editor->WorkspacePanelOpen();
        if(view && s.editNode>0)m_Editor->OpenRawProjectNodeEditor(s.editNode);
        Pointer(800,600);
        if(s.step==15 || s.onlyStep>=15 && s.onlyStep<=17 && !s.secondWorkspace) {
            s.secondWorkspace=CreateProjectWorkspace();
            auto* second=FindProjectWorkspace(s.secondWorkspace);
            second->editor->SetDocumentPersistenceEnabled(false);
            if(!LoadFixture(*second->editor,s.project)) {
                s.failure="Could not queue second project";s.finished=true;m_IsRunning=false;return;
            }
        }
        if(s.step==17) {
            auto* second=FindProjectWorkspace(s.secondWorkspace);
            if(!second || second->editor->GetNodeGraph().GetNodes().empty() ||
                second->editor->GetProjectLoadTaskState()==Async::TaskState::Failed) {
                s.failure="Second fixture project did not load";s.finished=true;m_IsRunning=false;return;
            }
            second->editor->SetWorkspaceMode(Stack::Project::WorkspaceMode::Graph);
        }
        std::cout<<"UI performance scenario: "<<s.Scenario()<<'\n';
    }
    const int f=s.frame++;
    const auto measure=[&](Visual kind,const char* action,const char* dispatch="imgui-event") {
        Probe::BeginInteraction(kind,s.Scenario(),action,dispatch);
    };
    if(s.step==22) {
        Pointer(900,500);
        if(f==140)io.AddMouseWheelEvent(0,24.f);
        if((f>=40 && f<100 || f>=240 && f<300) && f%2==0) {
            auto context=m_Editor->GetGraphEditorContext();std::string error;
            auto edit=Stack::GraphModel::ProposeEdit(*context.graph,context.revision,[&](auto& graph) {
                auto* node=graph.FindNode(s.qualityTone);
                node->rawOperation.parameters["curveMode"]="contrast";
                node->rawOperation.parameters["luminanceTone"]["enabled"]=true;
                node->rawOperation.parameters["luminanceTone"]["contrast"]=1.0f+float(f%20)*.005f;
            });
            if(!context.applyEdit(std::move(edit),error)){s.failure=error;s.finished=true;m_IsRunning=false;return;}
        }
    }
    if(s.step==20) {
        // Start at fit, then remain magnified while reversing the wheel.
        // Keep the pointer outside the floating tool and stable between events.
        Pointer(900,500);
        if(f==40 || f==120 || f==180 || f==240 || f==300) {
            measure(Visual::ImageCamera,"image_zoom");
            io.AddMouseWheelEvent(0,f==40?24.f:(f==180 || f==300?3.f:-3.f));
        }
    }
    if(s.step==21) {
        const auto tool=Probe::Last(Visual::Controls).region;
        if(f==28) {
            s.pointerX=tool.x+tool.width+80.f;
            s.pointerY=tool.y+std::min(100.f,tool.height*.5f);
        }
        if(f>=28 && f<125) {
            const float distance=f<40?0.f:f<64?40.f:f<88?120.f:160.f;
            Pointer(s.pointerX-distance,s.pointerY);
        }
        if(f==30)io.AddMouseButtonEvent(ImGuiMouseButton_Middle,true);
        if(f==40 || f==64 || f==88)measure(Visual::ImageCamera,"pan_across_tool");
        if(f==125)io.AddMouseButtonEvent(ImGuiMouseButton_Middle,false);
    }
    if(s.step==2) {
        if(f==30){PointAt(Visual::Camera,.88f,.8f);io.AddMouseButtonEvent(2,true);}
        if(f>=40 && f<120 && f%16==8){measure(Visual::Camera,"pan");const auto r=Probe::Last(Visual::Camera).region;
            Pointer(r.x+r.width*.88f+(f-32)*.5f,r.y+r.height*.8f+(f-32)*.15f);}
        if(f==125)io.AddMouseButtonEvent(2,false);
    }
    if(s.step==3 && (f==40 || f==80 || f==120)){measure(Visual::Camera,"zoom");PointAt(Visual::Camera,.8f,.75f);io.AddMouseWheelEvent(0,f==80?-1.f:1.f);}
    if(s.step==4) {
        if(f==30){auto r=Probe::Last(Visual::Node).region;s.pointerX=r.x+r.width*.5f;s.pointerY=r.y+r.height*.5f;
            Pointer(s.pointerX,s.pointerY);io.AddMouseButtonEvent(0,true);}
        if(f>=40 && f<120 && f%16==8){measure(Visual::Node,"node_drag");Pointer(s.pointerX+(f-32)*1.f,s.pointerY+(f-32)*.25f);}
        if(f==125)io.AddMouseButtonEvent(0,false);
    }
    if(s.step==5 || s.step==6) {
        if(f==15){PointAt(Visual::Search);io.AddMouseButtonEvent(0,true);}
        if(f==16)io.AddMouseButtonEvent(0,false);
        if(f==20 || s.step==5 && f==78){io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);}
        if(f==21 || s.step==5 && f==79){io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);}
        if(s.step==5 && (f==25 || f==82)){measure(Visual::Search,"search");io.AddInputCharactersUTF8(f==25?"blur":"tone");}
        if(s.step==6 && f==25){io.AddKeyEvent(ImGuiKey_Backspace,true);}
        if(s.step==6 && f==26)io.AddKeyEvent(ImGuiKey_Backspace,false);
        if(s.step==6 && (f==45 || f==70 || f==95 || f==120)){measure(Visual::Catalog,"scroll");PointAt(Visual::Catalog);io.AddMouseWheelEvent(0,-1);}
    }
    if((s.step==7 || s.step==12) && (f==40 || f==80 || f==120)){
        measure(Visual::Panel,"panel_toggle","ui-command");ToggleWorkspaceSectionPanel();}
    if((s.step==8 || s.step==13) && (f==40 || f==100)){
        const auto* viewport=ImGui::GetMainViewport();
        measure(s.step==8?Visual::Rail:Visual::Toolbar,"bar_reveal");
        Pointer(viewport->Pos.x+(s.step==8?1.f:viewport->Size.x*.5f),viewport->Pos.y+(s.step==8?400.f:1.f));}
    if((s.step==8 || s.step==13) && (f==70 || f==130))Pointer(800,600);
    if(s.step==11 && (f==40 || f==80 || f==120)) {
        measure(Visual::Controls,"brightness_control","ui-command");measure(Visual::Image,"brightness_image","ui-command");
        auto context=m_Editor->GetGraphEditorContext();std::string error;
        auto edit=Stack::GraphModel::ProposeEdit(*context.graph,context.revision,[&](auto& graph) {
            auto* node=graph.FindNode(s.editNode);node->layerParameters["brightness"]=f==80?-.1:.1;
        });
        if(!context.applyEdit(std::move(edit),error)){s.failure=error;s.finished=true;m_IsRunning=false;return;}
        Probe::RequireKey(Visual::Image,m_Editor->GetRenderRevision());
    }
    if(s.step==14 && (f==40 || f==80 || f==120)){
        measure(Visual::Workspace,"graph_view_switch","ui-command");m_Editor->SetWorkspaceMode(
            m_Editor->IsViewMode()?Stack::Project::WorkspaceMode::Graph:Stack::Project::WorkspaceMode::View);}
    if(s.step==17 && (f==40 || f==80 || f==120)) {
        measure(Visual::Project,"project_switch","ui-command");
        ActivateProjectWorkspace(m_ActiveProjectWorkspace==s.firstWorkspace?s.secondWorkspace:s.firstWorkspace);
    }
    if(s.step==18 || s.step==19) {
        const auto visual=s.step==18?Visual::ImageCamera:Visual::Controls;
        const int button=s.step==18?ImGuiMouseButton_Middle:ImGuiMouseButton_Left;
        if(f==28) {
            const auto r=Probe::Last(visual).region;
            s.pointerX=r.x+r.width*.65f;s.pointerY=r.y+r.height*(s.step==18?.7f:.015f);
            Pointer(s.pointerX,s.pointerY);
        }
        if(f>=28 && f<125) {
            const int lastMove=f>=40?40+std::min(4,(f-40)/16)*16:32;
            // A hidden GLFW window still publishes the physical cursor's
            // fallback position. Keep the diagnostic pointer at its last
            // authored position between normal event-driven motion steps.
            const float distance=f>=40?float(lastMove-32):0.f;
            Pointer(s.pointerX+distance*1.1f,s.pointerY+distance*.3f);
        }
        if(f==30) {
            io.AddMouseButtonEvent(button,true);
            if(s.step==19) io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);
        }
        if(f>=40 && f<120 && f%16==8) {
            measure(visual,s.step==18?"image_pan":"floating_tool_drag");
            Pointer(s.pointerX+(f-32)*1.1f,s.pointerY+(f-32)*.3f);
        }
        if(f==125) {
            io.AddMouseButtonEvent(button,false);
            if(s.step==19) io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);
        }
    }
}
void AppShell::FinishUiPerformanceFrame() {
    if(!m_UiPerformanceCapture)return;
    auto& s=*m_UiPerformanceCapture;Probe::Presented();
    if(s.ready && (s.qualityCapture || s.step>=0 && s.step<int(scenarios.size()) && s.frame>30) && s.frames.size()<2400) {
        if(s.gpuActive)s.queries[s.query].frame=s.frames.size();
        s.frames.push_back({s.Scenario(),Stack::UiPerformance::LastFrame(),-1,s.shellPhases});
    }
    if(!s.finished)return;
    if (s.qualityStream.is_open()) s.qualityStream.close();
    std::ofstream monitorStats(s.output/"monitor-cache.txt");
    const auto monitorCounters=Stack::Platform::glfwMonitorRefreshCounters;
    monitorStats<<"complete="<<monitorCounters.complete<<"\nworkArea="<<monitorCounters.workArea
        <<"\ncached="<<monitorCounters.cached<<"\ncompleteMs="<<monitorCounters.completeMs
        <<"\ncompleteMaxMs="<<monitorCounters.completeMaxMs<<"\nworkAreaMs="<<monitorCounters.workAreaMs
        <<"\nworkAreaMaxMs="<<monitorCounters.workAreaMaxMs
        <<"\npassthroughChanges="<<monitorCounters.passthroughChanges
        <<"\npassthroughReused="<<monitorCounters.passthroughReused<<'\n';
    Probe::Finish();s.CloseGpu();Stack::UiPerformance::EnableFrameCapture(false);std::ofstream out(s.output/"frames.csv");
    out<<"scenario";for(auto stage:stages)out<<','<<stage<<"_ms";
    out<<",nodes,visible_nodes,links,visible_links,vertices,indices,gpu_draw_ms,frame_setup_ms,shell_services_ms,interface_tail_ms,backend_newframe_ms\n"<<std::fixed<<std::setprecision(6);
    for(const auto& frame:s.frames) {
        out<<frame.scenario;for(double value:frame.sample.milliseconds)out<<','<<value;
        for(auto counter:{Stack::UiPerformance::Counter::Nodes,Stack::UiPerformance::Counter::VisibleNodes,
            Stack::UiPerformance::Counter::Links,Stack::UiPerformance::Counter::VisibleLinks})out<<','<<frame.sample.counters[static_cast<std::size_t>(counter)];
        out<<','<<frame.sample.vertices<<','<<frame.sample.indices<<','<<frame.gpuMs;
        for (double phase : frame.shellPhases) out<<','<<phase;
        out<<'\n';
    }
    std::ofstream status(s.output/"status.txt");status<<(s.passed?"passed":"failed")<<'\n'<<s.failure<<'\n';
}
