#include "App/Validation/Suites/ProjectCurveFeedbackValidation.h"
#include "Editor/EditorModule.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "NodeMath/ToneCurveParameters.h"
#include "Async/TaskSystem.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace Stack::Validation {
void ValidateProjectCurveFeedback(EditorModule& editor) {
    const auto require=[](bool condition,const std::string& message) {
        if (!condition) throw std::runtime_error(message);
    };
    std::cout<<"Curve check: prepare existing image\n";
    if (editor.GetProjectSessionKind()!=EditorModule::ProjectSessionKind::Empty)
        require(editor.CloseCurrentProject(true),"Could not prepare the canonical curve check.");
    auto& pipeline=editor.GetPipeline();
    require(pipeline.LoadSourceImage((std::filesystem::current_path()/"Assets/Splash/splash_banner.png").string()),
        "Could not load the existing curve-check image.");
    const int width=pipeline.GetCanvasWidth(),height=pipeline.GetCanvasHeight();
    EditorNodeGraph::ImagePayload image;
    image.width=width;image.height=height;image.channels=4;image.pixels=pipeline.GetSourcePixelsRaw();
    image.sourceColorMetadata=EditorNodeGraph::InspectImageFileColorMetadata(
        (std::filesystem::current_path()/"Assets/Splash/splash_banner.png").string(),width,height,4);
    auto& graph=editor.GetNodeGraph();
    const int source=graph.AddImageNode(std::move(image),{})->id;
    editor.AddTechnicalImageNodeAt(NodeMath::TechnicalImageOperation::AssignLinearSrgb,{150,0});
    const int linear=graph.GetSelectedNodeId();
    editor.AddLayerNodeAt(LayerType::ToneCurve,{300,0});
    const int curve=graph.GetSelectedNodeId();
    const std::string uuid=graph.FindNode(curve)->instanceUuid;
    editor.AddLayerNodeAt(LayerType::Brightness,{450,0});
    const int downstream=graph.GetSelectedNodeId();
    editor.AddOutputNodeAt({600,0});const int output=graph.GetSelectedNodeId();
    std::string error;
    require(editor.ConnectGraphSockets(source,"imageOut",linear,"imageIn",&error) &&
        editor.ConnectGraphSockets(linear,"imageOut",curve,"imageIn",&error) &&
        editor.ConnectGraphSockets(curve,"imageOut",downstream,"imageIn",&error) &&
        editor.ConnectGraphSockets(downstream,"imageOut",output,"imageIn",&error),error);
    editor.AddValueNodeAt(NodeMath::MakeUniformScalar(10),{300,220});
    const int unusedValue=graph.GetSelectedNodeId();
    require(editor.SetNodeParameterExposed(curve,"contrast",true) && editor.ConnectGraphSockets(
        unusedValue,"valueOut",curve,EditorNodeGraph::ParameterInputSocketId("contrast"),&error),error);
    ToneCurveLayer advanced;
    auto request=advanced.Serialize();
    request["autoCalibratePending"]=true;request["autoCalibrateForceReanalysis"]=true;
    request["autoCalibrateRequestId"]=std::uint64_t(1);
    auto context=editor.GetGraphEditorContext();
    auto proposal=GraphModel::ProposeEdit(*context.graph,context.revision,[&](auto& candidate) {
        auto& parameters=candidate.FindNode(curve)->rawOperation.parameters;
        parameters["curveMode"]="advanced";parameters["advanced"]=request;
        candidate.FindNode(downstream)->layerParameters["brightness"]=.125;
    });
    require(context.applyEdit(std::move(proposal),error),error);
    const auto authored=graph.FindNode(curve)->rawOperation.parameters;
    std::cout<<"Curve check: capture explicit frames\n";
    const auto persisted=EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(),graph);
    const auto& storedNodes=persisted.at("nodeGraph").at("nodes");
    const auto stored=std::find_if(storedNodes.begin(),storedNodes.end(),[&](const auto& node){return node.value("instanceUuid",std::string{})==uuid;});
    require(stored!=storedNodes.end() && !stored->at("rawOperation").at("parameters").at("advanced").value("autoCalibratePending",false),
        "Saving retained a transient Auto request or lost its instance.");
    const auto frame0=editor.BuildGraphSnapshotForTimelineFrame(0);
    const auto frame1=editor.BuildGraphSnapshotForTimelineFrame(1);
    const auto findCurve=[&](const auto& snapshot)->const RenderGraphNode& {
        const auto node=std::find_if(snapshot.nodes.begin(),snapshot.nodes.end(),[&](const auto& node){return node.authoredNodeUuid==uuid;});
        require(node!=snapshot.nodes.end(),"The canonical curve disappeared during frame capture.");return *node;
    };
    require(findCurve(frame0).uniformParameters.empty(),
        "Inactive advanced-mode controls still added uniform dependencies.");
    require(findCurve(frame0).evaluatedFrame==0 && findCurve(frame1).evaluatedFrame==1 &&
        findCurve(frame0).uniformSourceIdentity!=findCurve(frame1).uniformSourceIdentity,
        "Explicit frame snapshots did not stamp their actual evaluated frame.");
    std::vector<unsigned char> exported;int exportedWidth=0,exportedHeight=0;
    std::cout<<"Curve check: export frame 1\n";
    const bool frameExported=editor.BuildSingleOutputTimelineFrameRaster(1,exported,exportedWidth,exportedHeight);
    std::cout<<"Curve frame raster: success="<<frameExported<<" extent="<<exportedWidth<<"x"<<exportedHeight
        <<" expected="<<width<<"x"<<height<<" authored="<<(graph.FindNode(curve)->rawOperation.parameters==authored)
        <<" connected="<<graph.IsOutputConnected()<<" diagnostic="<<graph.GetOutputConnectionDiagnostic()<<'\n';
    require(frameExported &&
        exportedWidth==width && exportedHeight==height && graph.FindNode(curve)->rawOperation.parameters==authored,
        "Explicit frame export lost full extent or changed the current authored curve: "+editor.GetExportStatusText());
    std::cout<<"Curve check: accept worker feedback\n";
    require(editor.OpenRawProjectNodeEditor(curve),"The canonical instance could not open in View.");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    bool completed=false;
    do {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1000,700});
        ImGui::Begin("Curve feedback validation",nullptr,ImGuiWindowFlags_NoSavedSettings);
        editor.RenderUI();ImGui::End();ImGui::EndFrame();
        Async::TaskSystem::Get().PumpMainThreadTasks(4);
        const auto* current=graph.FindNode(curve);
        completed=current && current->instanceUuid==uuid &&
            !current->rawOperation.parameters.at("advanced").value("autoCalibratePending",false);
        if (completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now()<deadline);
    require(completed,"Current canonical Auto Curve feedback was not accepted by its authored instance.");
    require(NodeMath::ValidateToneCurveParameters(graph.FindNode(curve)->rawOperation.parameters,error),error);
    const auto completedParameters=graph.FindNode(curve)->rawOperation.parameters;
    require(editor.UndoProjectGraphEdit() &&
        graph.FindNode(curve)->rawOperation.parameters.value("curveMode",std::string{})=="components" &&
        !graph.FindNode(curve)->rawOperation.parameters.at("advanced").value("autoCalibratePending",false) &&
        editor.RedoProjectGraphEdit() && graph.FindNode(curve)->rawOperation.parameters==completedParameters,
        "Canonical Auto Curve completion did not participate in the project history.");
    exported.clear();
    require(editor.BuildSingleOutputTimelineFrameRaster(0,exported,exportedWidth,exportedHeight) &&
        exportedWidth==width && exportedHeight==height,
        "Current-frame export reused an incomplete viewport preview.");
    require(editor.CloseCurrentProject(true),"Could not close the curve validation project.");
    std::cout<<"PASS canonical Auto Curve owned feedback, explicit frame stamps, history and complete final-output raster\n";
}
}
