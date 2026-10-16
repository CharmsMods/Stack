#include "App/AppShell.h"
#include "App/UiPerformanceCapture.h"
#include "App/UiPerformanceProbe.h"
#include "Graph/GraphDocumentRules.h"
#include "Raw/RawLoader.h"
#include "Raw/LibRawRuntime.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace Probe = Stack::UiPerformance::Probe;

bool LoadUiViewportQualityFixture(EditorModule& editor, const std::filesystem::path& source,
    int& toneNode, int& localEvNode) {
    if (Raw::RawLoader::IsRawPath(source.u8string())) {
        // Use the same Gallery-open route as the ordinary RAW view. The legacy
        // Graph import helper cannot construct this application's current RAW canvas.
        editor.SetRawWorkspaceFolder(source.parent_path());
        return true;
    }
    if (!editor.AddImageNodeFromFile(source.u8string(), {20,30})) {
        std::cerr<<"Quality fixture import failed: "<<Raw::GetLibRawRuntimeStatus().message<<'\n';return false;
    }
    auto& graph=editor.GetNodeGraph();
    int upstream=graph.GetSelectedNodeId(), destination=-1;
    editor.AddTechnicalImageNodeAt(Stack::NodeMath::TechnicalImageOperation::AssignLinearSrgb,{290,30});
    const int linear=graph.GetSelectedNodeId();
    if (!editor.ConnectGraphSockets(upstream,"imageOut",linear,"imageIn")) return false;
    upstream=linear;
    editor.AddOutputNodeAt({1100,30});destination=graph.GetSelectedNodeId();
    if (destination<0) {std::cerr<<"Quality fixture has no destination after node "<<upstream<<'\n';return false;}
    editor.AddRawOperationNodeAt(Stack::RawRecipe::GraphOperationKind::ToneCurve,{550,30});toneNode=graph.GetSelectedNodeId();
    editor.AddRawOperationNodeAt(Stack::RawRecipe::GraphOperationKind::LocalEv,{820,30});localEvNode=graph.GetSelectedNodeId();
    std::string error;
    const bool connected=editor.ConnectGraphSockets(upstream,"imageOut",toneNode,"imageIn",&error) &&
        editor.ConnectGraphSockets(toneNode,"imageOut",localEvNode,"imageIn",&error) &&
        editor.ConnectGraphSockets(localEvNode,"imageOut",destination,"imageIn",&error);
    if (!connected) std::cerr<<"Quality fixture connection failed: "<<error<<'\n';
    return connected;
}

void AppShell::TickUiViewportQualityCapture() {
    auto& s=*m_UiPerformanceCapture;
    const auto now=std::chrono::steady_clock::now();
    if (!s.qualityStream.is_open()) {
        s.qualityStream.open(s.output/"quality.csv");
        s.qualityStream<<"stage,elapsed_ms,required_revision,presented_revision,native,logical_width,logical_height,storage_width,storage_height,busy,interaction,image_screen_width,settled_kernels\n";
        s.qualityStageStarted=now;s.step=9;
        m_Editor->SetWorkspaceMode(Stack::Project::WorkspaceMode::View);
        m_Editor->SetWorkspacePanelOpen(false);
        m_Editor->OpenRawProjectNodeEditor(s.qualityTone);
    }
    ++s.frame;
    Stack::Renderer::ViewportPreviewGeometry geometry;
    const bool valid=m_Editor->TryGetViewportPreviewGeometry(geometry);
    const auto presented=Probe::Last(Probe::Visual::Image);
    const double elapsed=std::chrono::duration<double,std::milli>(now-s.qualityStageStarted).count();
    const bool native=valid && geometry.NativeSamples();
    s.qualityStream<<std::fixed<<std::setprecision(3)<<s.qualityStage<<','<<elapsed<<','<<s.qualityRequiredRevision
        <<','<<(presented.valid?presented.key:0)<<','<<native<<','<<geometry.logicalWidth<<','<<geometry.logicalHeight
        <<','<<geometry.storageWidth<<','<<geometry.storageHeight<<','<<m_Editor->IsEditorRenderBusy()
        <<','<<m_Editor->IsRawWorkspaceUiInteractionActive()<<','<<presented.region.width<<','<<presented.settledKernels<<'\n';
    if (elapsed>30000) {
        s.failure=(s.qualityStage==1 && std::abs(presented.region.width-s.qualityInitialImageWidth)<=1.f
            ? "Wheel input did not change the image camera" : "Native quality did not settle") +
            std::string(" at stage ")+std::to_string(s.qualityStage)+": "+m_Editor->GetFullQualityRenderDiagnostic();
        s.finished=true;m_IsRunning=false;return;
    }
    if (s.qualityStage==0) {
        if (!valid || !presented.valid || m_RootTabBodyFadeActive || m_RequestedTab>=0) return;
        // Zoom as soon as a presentation exists, while native refinement may
        // still be queued. These are normal ImGui wheel events.
        const auto& r=presented.region;
        const auto controls=Probe::Last(Probe::Visual::Controls);
        const auto* viewport=ImGui::GetMainViewport();
        float x=r.x+r.width*.8f,y=r.y+r.height*.75f;
        for (float fx : {.8f,.2f,.5f}) for (float fy : {.75f,.3f,.5f}) {
            const float cx=r.x+r.width*fx,cy=r.y+r.height*fy;
            const auto& c=controls.region;
            const bool underControls=controls.valid && cx>=c.x && cx<c.x+c.width && cy>=c.y && cy<c.y+c.height;
            if (!underControls && cx>viewport->Pos.x+40 && cx<viewport->Pos.x+viewport->Size.x-40 &&
                cy>viewport->Pos.y+80 && cy<viewport->Pos.y+viewport->Size.y-40) {x=cx;y=cy;}
        }
        s.qualityInitialImageWidth=r.width;
        std::cout<<"Quality zoom pointer "<<x<<','<<y<<" image "<<r.x<<','<<r.y<<','<<r.width<<','<<r.height
            <<" controls "<<controls.region.x<<','<<controls.region.y<<','<<controls.region.width<<','<<controls.region.height<<'\n';
        ImGui::GetIO().AddMousePosEvent(x,y);
        s.qualityStage=1;s.qualityStageStarted=now;return;
    }
    if (s.qualityStage==1 && !s.qualityWheelQueued) {
        // Let ImGui update the hovered window from the new pointer before
        // enqueueing the wheel; outgoing tab fades intentionally block input.
        ImGui::GetIO().AddMouseWheelEvent(0,1);
        s.qualityWheelQueued=true;
        return;
    }
    const bool settled=native && presented.valid && presented.settledKernels && presented.key>=s.qualityRequiredRevision &&
        std::abs(presented.region.width-s.qualityInitialImageWidth)>1.f &&
        !m_Editor->IsEditorRenderBusy() && !m_Editor->IsRawWorkspaceUiInteractionActive() && elapsed>=500;
    s.qualityStableFrames=settled?s.qualityStableFrames+1:0;
    if (s.qualityStableFrames<10) return;
    if (s.qualityStage==3) {s.passed=true;s.finished=true;m_IsRunning=false;return;}
    const bool curve=s.qualityStage==1;
    auto context=m_Editor->GetGraphEditorContext();std::string error;
    auto edit=Stack::GraphModel::ProposeEdit(*context.graph,context.revision,[&](auto& candidate) {
        auto* node=candidate.FindNode(curve?s.qualityTone:s.qualityLocalEv);
        if (curve) {
            node->rawOperation.parameters["curveMode"]="contrast";
            node->rawOperation.parameters["luminanceTone"]["enabled"]=true;
            node->rawOperation.parameters["luminanceTone"]["contrast"]=1.15f;
        } else {
            auto recipe=Stack::RawRecipe::ReadGraphOperation(node->rawOperation);
            recipe.localRange.enabled=true;
            if (recipe.localRange.points.empty()) recipe.localRange.points=Stack::RawRecipe::DefaultLocalRangePoints();
            recipe.localRange.points[recipe.localRange.points.size()/2].deltaEv+=.25f;
            Stack::RawRecipe::WriteGraphOperation(node->rawOperation,recipe);
        }
    });
    if (!context.applyEdit(std::move(edit),error)) {
        s.failure=error;s.finished=true;m_IsRunning=false;return;
    }
    ++s.qualityStage;s.qualityStableFrames=0;s.qualityStageStarted=now;
    m_Editor->OpenRawProjectNodeEditor(curve?s.qualityTone:s.qualityLocalEv);
    s.qualityRequiredRevision=m_Editor->GetRenderRevision();
}
