#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
void ValidatePointerInsertion(EditorModule& editor,const EditorNodeGraph::Link& edge,bool halation,bool outputFirst,bool reverseDrag,std::string& failures) {
    using namespace EditorNodeGraph;
    namespace Layout=Stack::Editor::NodeGraphUILayout;
    namespace Visuals=Stack::Editor::NodeGraphUIVisuals;
    const auto& catalog=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    const auto found=std::find_if(catalog.begin(),catalog.end(),[](const auto& d){return d.identity.id=="stack:compound/halation";});
    if(halation)editor.AddCompoundTemplateNodeAt(std::size_t(found-catalog.begin()),{600,240});
    else editor.AddDataMathNodeAt(DataMathMode::Multiply,{600,240});
    const int added=editor.GetNodeGraph().GetSelectedNodeId();
    const int from=edge.fromNodeId,to=edge.toNodeId;
    EditorNodeGraphUI ui;ui.Initialize();
    auto& io=ImGui::GetIO();io.AddMousePosEvent(-100,-100);io.AddMouseButtonEvent(0,false);
    ui.FitGraphPreviewToCanvas(&editor,editor.GetNodeGraph(),io.DisplaySize);
    ImVec2 origin;
    const auto frame=[&] {
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGuiExtras::BeginFrameInputRouting();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("RAW insertion pointer validation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        origin=ImGui::GetCursorScreenPos();ui.Render(&editor);editor.FinishRawLayerGraphFrame();
        ImGui::End();ImGui::Render();
    };
    for(int i=0;i<22;++i)frame();
    const auto pin=[&](int id,const std::string& port) {
        const auto& g=editor.GetNodeGraph();const auto& n=*g.FindNode(id);
        const auto bounds=ui.MeasureNodeForLayout(g,n);
        auto metrics=Visuals::MetricsForNode(n);Visuals::ApplyModernCompactMetrics(n,metrics);
        Visuals::ApplyLayerSurfaceMetrics(&ui,&editor,n,metrics);Visuals::ApplyCanonicalNodeMetrics(&ui,&editor,n,metrics);
        Layout::NodeLayoutBuildSpec spec;spec.width=bounds.max.x-Layout::kSocketCenterOffset-Layout::kSocketRadius;
        spec.height=bounds.max.y;spec.expanded=n.expanded;spec.headerInsetY=metrics.headerInsetY;spec.bodyInsetBottom=metrics.bodyInsetBottom;
        spec.framelessMedia=Visuals::BuildNodePresentationProfile(&ui,&editor,n,{}).kind==Visuals::NodePresentationKind::FramelessMedia;
        for(const auto& s:g.GetSockets(n,true))spec.sockets.push_back({s.id,s.direction});
        const auto logical=Layout::BuildLogicalNodeLayout(spec);
        const auto socket=std::find_if(logical.sockets.begin(),logical.sockets.end(),[&](const auto& s){return s.socketId==port;});
        if(socket==logical.sockets.end())throw std::runtime_error("Missing pointer-test pin "+port);
        const auto view=ui.GetGraphCaptureViewportSnapshot();
        return ImVec2{origin.x+view.panX+(n.position.x+socket->center.x)*view.zoom,
            origin.y+view.panY+(n.position.y+socket->center.y)*view.zoom};
    };
    const auto wire=[&](int source,const std::string& output,int target,const std::string& input) {
        const auto a=reverseDrag?pin(target,input):pin(source,output);
        const auto b=reverseDrag?pin(source,output):pin(target,input);
        io.AddMousePosEvent(a.x,a.y);frame();io.AddMouseButtonEvent(0,true);frame();
        io.AddMousePosEvent(b.x,b.y);frame();io.AddMouseButtonEvent(0,false);frame();
        const auto* accepted=editor.GetNodeGraph().FindAnyInputLink(target,input);
        const bool ok=accepted && accepted->fromNodeId==source && accepted->fromSocketId==output;
        const auto label=std::string(halation?"Halation":"Multiply")+" pointer "+(reverseDrag?"from input ":"from output ")+(outputFirst?"output-first ":"input-first ");
        if(!ok)failures+=label+
            std::to_string(source)+"/"+output+"->"+std::to_string(target)+"/"+input+" was not accepted\n";
        else std::cout<<"PASS RAW insertion "<<label<<source<<"->"<<target<<'\n';
        for(int i=0;i<22;++i)frame();
        return ok;
    };
    const auto input=[&]{return wire(from,edge.fromSocketId,added,halation?"image-in":"imageA");};
    const auto output=[&]{return wire(added,halation?"image-out":"imageOut",to,edge.toSocketId);};
    if(outputFirst){if(output())input();}else if(input())output();
    ui.Shutdown();
}
}
void ValidateRawGraphInsertions(EditorModule& editor) {
    using namespace EditorNodeGraph;
    const auto baseline=editor.SerializePipeline();
    std::vector<Link> chain;
    for(const auto& link:editor.GetNodeGraph().GetLinks()) {
        const auto* from=editor.GetNodeGraph().FindNode(link.fromNodeId);
        const auto* to=editor.GetNodeGraph().FindNode(link.toNodeId);
        if(from && to && link.fromSocketId=="imageOut" && link.toSocketId=="imageIn")chain.push_back(link);
    }
    std::string failures;
    for(bool halation:{true,false})for(bool outputFirst:{false,true})for(const auto& edge:chain) {
        editor.DeserializePipeline(baseline);editor.OpenRawLayerMaskGraph({});
        if(halation) {
            const auto& catalog=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
            const auto found=std::find_if(catalog.begin(),catalog.end(),[](const auto& d){return d.identity.id=="stack:compound/halation";});
            editor.AddCompoundTemplateNodeAt(std::size_t(found-catalog.begin()),{900,300});
        } else editor.AddDataMathNodeAt(DataMathMode::Multiply,{900,300});
        const int id=editor.GetNodeGraph().GetSelectedNodeId();
        const auto label=std::string(halation?"Halation":"Multiply")+" "+(outputFirst?"output-first":"input-first")+
            " "+std::to_string(edge.fromNodeId)+"->"+std::to_string(edge.toNodeId);
        const auto connect=[&](int from,const std::string& output,int to,const std::string& input,std::string& error) {
            GraphOutputContext context;
            context.resolveOutput=[&](const auto& node,const auto& port)->std::optional<GraphOutputDescription> {
                if(const auto* value=editor.GetGraphOutputDescription(node.id,port))return *value;
                return std::nullopt;
            };
            {
                ScopedGraphOutputAnalysis descriptions(editor.GetNodeGraph(),std::move(context));
                if(!editor.GetNodeGraph().CanConnectSockets(from,output,to,input,nullptr,&error)) {
                    failures+=label+" native preview rejected: "+error+"\n";
                }
            }
            return editor.ConnectGraphSockets(from,output,to,input,&error);
        };
        const auto input=[&](std::string& error){return connect(edge.fromNodeId,edge.fromSocketId,id,halation?"image-in":"imageA",error);};
        const auto output=[&](std::string& error){return connect(id,halation?"image-out":"imageOut",edge.toNodeId,edge.toSocketId,error);};
        std::string error;
        const bool first=outputFirst?output(error):input(error);
        if(!first){failures+=label+" first wire: "+error+"\n";continue;}
        const bool second=outputFirst?input(error):output(error);
        if(!second){failures+=label+" second wire: "+error+"\n";continue;}
        const auto* accepted=editor.GetNodeGraph().FindAnyInputLink(edge.toNodeId,edge.toSocketId);
        if(!accepted || accepted->fromNodeId!=id)failures+=label+": accepted wire was lost\n";
        else {
            const auto wired=Stack::Project::SerializeRawLayerStack(editor.GetProjectSession().rawLayers.State());
            if(!editor.UndoRawLayerEdit() ||
                Stack::Project::SerializeRawLayerStack(editor.GetProjectSession().rawLayers.State())==wired ||
                !editor.RedoRawLayerEdit() ||
                Stack::Project::SerializeRawLayerStack(editor.GetProjectSession().rawLayers.State())!=wired)
                failures+=label+": connection/layout undo and redo did not restore the owned document\n";
            else std::cout<<"PASS RAW insertion and history "<<label<<'\n';
        }
    }
    for(bool halation:{true,false})for(bool outputFirst:{false,true})for(bool reverseDrag:{false,true})for(const auto& edge:chain) {
        const auto* from=editor.GetNodeGraph().FindNode(edge.fromNodeId);
        const auto* to=editor.GetNodeGraph().FindNode(edge.toNodeId);
        if(!from || !to)throw std::runtime_error("Missing baseline connection endpoint");
        if(!(from->role==Stack::GraphModel::NodeRole::OriginalImage && from->rawLayerOwnerId.empty()) &&
            !(to->kind==NodeKind::Output && to->rawLayerOwnerId.empty()) &&
            !(from->kind==NodeKind::RawOperation && from->rawOperation.kind==Stack::RawRecipe::GraphOperationKind::LocalEv))continue;
        editor.DeserializePipeline(baseline);editor.OpenRawLayerMaskGraph({});
        ValidatePointerInsertion(editor,edge,halation,outputFirst,reverseDrag,failures);
    }
    editor.DeserializePipeline(baseline);editor.OpenRawLayerMaskGraph({});
    if(!failures.empty())throw std::runtime_error(failures);
}
}
