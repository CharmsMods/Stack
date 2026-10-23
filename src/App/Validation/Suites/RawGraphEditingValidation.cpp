#include "Editor/EditorModule.h"
#include "Graph/LayerOperationParameters.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeEditors/NodeEditorRegistry.h"
#include "Editor/LayerRegistry.h"
#include "Project/RawNodeEditing.h"
#include "Editor/RawLayerTools.h"
#include "Renderer/GLLoader.h"
#include "Async/TaskSystem.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <imgui_internal.h>
#include <backends/imgui_impl_opengl3.h>
#include <chrono>
#include <thread>

namespace Stack::Validation {
void ValidateSharedCurveInteractions();
void ValidateRawGraphInsertions(EditorModule& editor);
namespace {
void ValidateNodeOpening(EditorModule& editor, int nodeId, int expectedTool, bool checkTransition = false) {
    // Tool selection switches the editor to RAW's render graph. Return to the
    // project canvas before sending graph pointer events, as the tab rail does.
    if (checkTransition && !editor.LeaveRawWorkspaceRootTab(true))
        throw std::runtime_error("Could not enter Graph from RAW.");
    editor.OpenRawLayerMaskGraph({});
    editor.ConsumeOpenEditorTabRequest();
    EditorNodeGraphUI ui;
    ui.Initialize();
    editor.CancelGraphAutoFocusTracking();
    auto& graph = editor.GetNodeGraph();
    auto* node = graph.FindNode(nodeId);
    if (!node) throw std::runtime_error("The graph node to open is missing.");
    const auto nodeTitle = node->title;
    const auto originalPosition = node->position;
    node->position = {340, 100};
    ImVec2 origin;
    const auto frame = [&](bool outgoingGraph = false) {
        ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Graph node validation", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        origin = ImGui::GetCursorScreenPos();
        if (outgoingGraph) {
            // AppShell continues drawing Graph, with input disabled, until its
            // fade-out finishes. Drawing it must not request another tab switch.
            ImGui::BeginDisabled();
            editor.RenderUI();
            ImGui::EndDisabled();
        } else ui.Render(&editor);
        ImGui::End(); ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    };
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(-100,-100);
    for (int i=0;i<25;++i) frame();
    const auto view = ui.GetGraphCaptureViewportSnapshot();
    // Use the lower body, outside the header and away from either socket edge.
    const ImVec2 point(origin.x+view.panX+(node->position.x+52)*view.zoom,
        origin.y+view.panY+(node->position.y+78)*view.zoom);
    editor.ConsumeOpenRawLabTabRequest();
    io.AddMousePosEvent(point.x,point.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(0,false); frame();
    if (editor.ConsumeOpenRawLabTabRequest())
        throw std::runtime_error("A single node click opened RAW.");
    io.AddMouseButtonEvent(0,true); frame();
    const bool opened = editor.ConsumeOpenRawLabTabRequest();
    if (!opened || editor.GetRawLayerToolIndex() != expectedTool)
        throw std::runtime_error("Double-clicking " + nodeTitle + " did not open its RAW editing tool: request=" +
            std::to_string(opened) + ", selected=" + std::to_string(editor.GetRawLayerToolIndex()) +
            ", expected=" + std::to_string(expectedTool));
    if (!graph.FindNode(nodeId)->expanded)
        throw std::runtime_error("Opening RAW collapsed its graph node.");
    io.AddMouseButtonEvent(0,false); frame(checkTransition);
    if (checkTransition) {
        for (int i=0;i<4;++i) {
            if (editor.ConsumeOpenEditorTabRequest())
                throw std::runtime_error("The outgoing Graph frame requested a return to Graph after opening RAW.");
            frame(true);
        }
        if (editor.ConsumeOpenEditorTabRequest() || !editor.EnterRawWorkspaceRootTab() ||
            editor.IsEditingRawLayerMaskGraph() || editor.GetRawLayerToolIndex()!=expectedTool)
            throw std::runtime_error("The Graph-to-RAW handoff lost the selected tool or retained the canvas as the editing graph.");
        std::cout<<nodeTitle<<": outgoing Graph frames and RAW handoff passed.\n";
    }
    // The canvas now survives opening another tool. Restore this fixture's
    // temporary position so the next tested node does not overlap it.
    if (auto* restored=graph.FindNode(nodeId)) restored->position=originalPosition;
    ui.Shutdown();
}

bool ValidateCanvas(EditorModule& editor) {
    const auto require = [](bool ok, const std::string& message) {
        if (!ok) throw std::runtime_error(message);
    };
    try {
        using Role = Stack::GraphModel::NodeRole;
        using Kind = Stack::RawRecipe::GraphOperationKind;
        std::vector<std::string> inventoryErrors;
        require(EditorNodeGraphDefinitions::ValidateUnifiedNodeDefinitionRegistry(&inventoryErrors),
            inventoryErrors.empty() ? "Production operation inventory failed." : inventoryErrors.front());
        std::cout << "Production operation inventory passed: " << EditorNodeGraphDefinitions::GetUnifiedNodeDefinitionRegistry().size() << " definitions.\n";
        // Use the public load and editor commands, not just the layer projection.
        // A normal RAW document has a source owner and a composition output.
        // No pixel data is needed to exercise its editing transactions.
        auto& sourceGraph = editor.GetNodeGraph();
        sourceGraph.Clear();
        EditorNodeGraph::RawDevelopmentPayload source;
        source.recipe = Stack::RawRecipe::MakeDefaultRecipe({});
        const int sourceId = sourceGraph.AddRawDevelopmentNode(std::move(source), {})->id;
        const int outputId = sourceGraph.AddOutputNode({300, 0}, true)->id;
        require(sourceGraph.TryConnect(sourceId, outputId), "Could not connect the source document.");
        editor.DeserializePipeline(editor.SerializePipeline());
        require(editor.GetRawLayerToolLabels() == std::vector<std::string>{"Local EV", "Tone Curve", "Color Calibration", "Color Warp"} && editor.GetRawLayerToolIndex() == 0,
            "A fresh Background did not select an authored tool for Alt navigation.");
        editor.OpenRawLayerMaskGraph({});
        require(editor.IsRawProjectCanvas(), "The project canvas did not open.");
        ValidateRawGraphInsertions(editor);
        const auto find = [&](auto predicate) {
            for (const auto& node : editor.GetNodeGraph().GetNodes()) if (predicate(node)) return node.id;
            return 0;
        };
        const int end = find([](const auto& n) { return n.role == Role::LayerResult; });
        const int local = find([](const auto& n) { return n.kind == EditorNodeGraph::NodeKind::RawOperation && n.rawOperation.kind == Kind::LocalEv; });
        const int calibration = find([](const auto& n) { return n.kind == EditorNodeGraph::NodeKind::RawOperation && n.rawOperation.kind == Kind::Calibration; });
        const int warp = find([](const auto& n) { return n.kind == EditorNodeGraph::NodeKind::RawOperation && n.rawOperation.kind == Kind::ColorWarp; });
        auto& graph = editor.GetNodeGraph();
        const auto initialCount = graph.GetNodes().size();
        const auto visible = graph.GetSockets(*graph.FindNode(local), true);
        require(std::none_of(visible.begin(), visible.end(), [](const auto& s) { return s.id.rfind("param:", 0) == 0; }),
            "RAW numeric inputs are all exposed by default.");
        editor.AddLayerNodeAt(LayerType::Saturation, {800, 220});
        const int saturation = graph.GetSelectedNodeId();
        require(graph.GetNodes().size() == initialCount + 1 && graph.FindNode(saturation)->kind == EditorNodeGraph::NodeKind::Layer,
            "The library's Add operation command did not commit.");
        std::string error;
        require(editor.GetGraphEditorContext().canConnect(warp, "imageOut", saturation, "imageIn", &error),
            "The editor rejected a compatible wire: " + error);
        require(editor.ConnectGraphSockets(warp, "imageOut", saturation, "imageIn", &error), error);
        require(editor.ConnectGraphSockets(saturation, "imageOut", end, "imageIn", &error), error);
        require(editor.RemoveGraphLink(saturation, "imageOut", end, "imageIn") &&
            !graph.FindInputLink(end, "imageIn"), "Deleting a connection did not commit its draft.");
        require(editor.ConnectGraphSockets(saturation, "imageOut", end, "imageIn", &error), error);
        require(!editor.GetGraphEditorContext().canConnect(saturation, "imageOut", local, "imageIn", &error),
            "Connection feedback accepted a cycle.");
        require(!editor.ConnectGraphSockets(saturation, "imageOut", local, "imageIn", &error),
            "A feedback loop was accepted.");
        require(editor.SetNodeParameterExposed(local, "strength", true), "Expose as Input failed.");
        require(editor.SetNodeParameterExposed(local, "strength", false), "Hide parameter input failed.");
        editor.RequestRawLayerToolIndex(2);
        require(editor.GetRawLayerToolIndex() == 2 && editor.GetRawLayerToolLabels().size() == 5,
            "Alt navigation did not select Calibration after adding an operation.");
        editor.RequestRawLayerToolIndex(4);
        require(editor.GetRawLayerToolIndex() == 4,
            "Alt navigation did not select the new graph operation.");
        using namespace Stack::Editor::NodeGraphUIVisuals;
        require(IsSummaryOnlyNode(nullptr,&editor,*graph.FindNode(calibration)) &&
                ResolveNodeWidthClass(nullptr,&editor,*graph.FindNode(calibration)) ==
                    Stack::Editor::NodeGraphUILayout::NodeWidthClass::Tile,
            "A RAW operation reserved an empty expanded control panel.");
        ValidateNodeOpening(editor,calibration,2);
        ValidateNodeOpening(editor,end,2);
        // The handoff makes GetNodeGraph return the source graph. Inspect the
        // retained project canvas whose selection the layer cap synchronized.
        require(graph.GetSelectedNodeId()==calibration,
            "Opening a layer cap did not focus its remembered image tool.");
        ValidateNodeOpening(editor,local,0);
        const auto saved = editor.SerializePipeline();
        editor.DeserializePipeline(saved);
        require(editor.GetRawLayerToolLabels().size() == 5 && editor.GetRawLayerToolIndex() == 0,
            "Reload lost the authored tool choices or kept a stale surface.");
        editor.OpenRawLayerMaskGraph({});
        require(editor.GetNodeGraph().GetNodes().size() == initialCount + 1,
            "Reload lost the newly added operation.");
        auto context = editor.GetGraphEditorContext();
        auto edit = Stack::GraphModel::ProposeEdit(*context.graph, context.revision, [](auto&) {});
        require(context.applyEdit(std::move(edit), error), "The reopened graph rejected edits: " + error);
        editor.AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::RadialGradient,{600,400});
        const int maskNode=editor.GetNodeGraph().GetSelectedNodeId();
        editor.ConsumeOpenRawLabTabRequest();
        require(editor.OpenRawProjectNodeEditor(maskNode) && editor.ConsumeOpenRawLabTabRequest() &&
            editor.GetRawLayerToolIndex()>=0 && editor.GetRawLayerToolLabels().size()==6,
            "An editable mask was missing from node selection and Alt navigation.");
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
        ImGui::Begin("Mask inspector validation");editor.RenderUI();ImGui::End();ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        // This target links real effect factories; model-only tests intentionally
        // stub them. Verify default construction without a renderer/UI instance.
        using namespace Stack::Project;
        RawLayerStackState maskState;
        auto& layer=maskState.background;
        auto defaults=LayerRegistry::CreateLayer(LayerType::Saturation);
        require(defaults!=nullptr,"The Saturation definition has no default factory.");
        const int previous=FindRawOperation(layer,Kind::ColorWarp)->id;
        const int result=FindRawRole(layer,Role::LayerResult)->id;
        const int effect=layer.graph.AddLayerNode(LayerType::Saturation,0,{})->id;
        const auto effectUuid=layer.graph.FindNode(effect)->instanceUuid;
        require(layer.graph.TryConnectSockets(previous,"imageOut",effect,"imageIn",&error),error);
        require(layer.graph.TryConnectSockets(effect,"imageOut",result,"imageIn",&error),error);
        require(Stack::Editor::FindNodeEditor(*layer.graph.FindNode(effect)).primary,
            "The core adjustment lost its parameter-aware editor.");
        EditorNodeGraph::Node borrowed;
        borrowed.kind=EditorNodeGraph::NodeKind::Layer; borrowed.layerType=LayerType::GaussianBlur;
        EditorNodeGraphDefinitions::ApplyNodeMetadata(borrowed);
        require(!Stack::Editor::FindNodeEditor(borrowed).primary,
            "An undesigned effect lost the borrowed graph-controls fallback.");
        require(Stack::Editor::FindNodeEditor(*FindRawOperation(layer,Kind::Calibration)).primary!=nullptr,
            "Calibration lost its custom editor registration.");
        layer.graph.FindNode(effect)->layerParameters["saturation"]=-.7f;
        const auto masked=AddRawLocalAdjustment(maskState,kRawBackgroundId,effectUuid,
            EditorNodeGraph::MaskGeneratorKind::RadialGradient,error);
        require(!masked.operationUuid.empty(),error);
        require(std::any_of(layer.graph.GetNodes().begin(),layer.graph.GetNodes().end(),[&](const auto& node) {
            return node.instanceUuid==masked.operationUuid && node.layerParameters==GraphModel::CaptureLayerParameters(*defaults);
        }),
            "Creating a masked effect copied the selected effect's settings.");
        require(ValidateRawLayerStack(maskState,error),error);
        RawLayerStackState workflow;
        const auto original=FindRawOperation(workflow.background,Kind::LocalEv)->instanceUuid;
        auto* originalNode=FindRawOperation(workflow.background,Kind::LocalEv);
        originalNode->rawOperation.parameters["strength"]=.6f;
        const auto originalParameters=originalNode->rawOperation.parameters;
        const auto localMask=ApplyRawShapeMask(workflow,kRawBackgroundId,original,
            EditorNodeGraph::MaskGeneratorKind::LinearGradient,error);
        require(!localMask.operationUuid.empty() && localMask.operationUuid!=original,error);
        const auto* fresh=FindRawOperation(workflow.background,Kind::LocalEv,localMask.operationUuid);
        require(FindRawOperation(workflow.background,Kind::LocalEv,original)->rawOperation.parameters==originalParameters &&
            fresh->rawOperation.parameters==Stack::RawRecipe::MakeGraphOperation(Kind::LocalEv).parameters,
            "Background masking changed the original or copied its settings.");
        require(Stack::Editor::RawLayerToolLabel(workflow.background.graph,*fresh)=="Local EV 2",
            "The repeated image tool was not numbered in the picker.");
        const auto reference=GetRawOperationMask(workflow,workflow.background,localMask.operationUuid);
        const auto* sourceMask=reference?FindRawMaskOutput(workflow,*reference):nullptr;
        const auto* texture=sourceMask?FindRawPublishedNode(workflow.background,*sourceMask):nullptr;
        require(texture && texture->kind==EditorNodeGraph::NodeKind::MaskTexture,"Drawing did not create a texture.");
        const auto sockets=workflow.background.graph.GetSockets(*texture,true);
        require(std::none_of(sockets.begin(),sockets.end(),[](const auto& socket){
            return socket.direction==EditorNodeGraph::SocketDirection::Input;}),"The mask texture has an input.");
        const int textureId=texture->id,freshId=fresh->id;
        const int invert=workflow.background.graph.AddMaskUtilityNode(EditorNodeGraph::MaskUtilityKind::Invert,{})->id;
        require(workflow.background.graph.TryConnectSockets(textureId,"maskOut",invert,"maskIn",&error),error);
        require(workflow.background.graph.TryConnectSockets(invert,"maskOut",freshId,"maskIn",&error),error);
        const auto choices=Stack::Editor::RawLayerToolNodes(workflow.background.graph);
        require(std::any_of(choices.begin(),choices.end(),[&](const auto* n){return n->id==textureId;}) &&
            std::any_of(choices.begin(),choices.end(),[&](const auto* n){return n->id==invert;}),
            "Alt omitted an editable texture or its enabled Invert operation.");
        {
            auto typedCoverage=workflow.background;
            auto areaRecipe=ReadRawLayerOperation(typedCoverage,original);
            Stack::RawRecipe::RawZoneArea area;area.id="picker-area";
            areaRecipe.localRange.areas.push_back(area);
            require(WriteRawLayerOperation(typedCoverage,original,areaRecipe,error),error);
            const int operation=FindRawOperation(typedCoverage,Kind::LocalEv,original)->id;
            const auto incoming=*typedCoverage.graph.FindInputLink(operation,"area:picker-area");
            const int blur=typedCoverage.graph.AddLayerNode(LayerType::GaussianBlur,0,{})->id;
            require(typedCoverage.graph.TryConnectSockets(incoming.fromNodeId,incoming.fromSocketId,blur,"imageIn",&error),error);
            require(typedCoverage.graph.TryConnectSockets(blur,"imageOut",operation,"area:picker-area",&error),error);
            auto picker=Stack::Editor::RawLayerToolNodes(typedCoverage.graph);
            require(std::any_of(picker.begin(),picker.end(),[&](const auto* node){return node->id==blur;}),
                "The editable blur feeding a Local EV area was missing from Alt navigation.");
            const auto serial=*typedCoverage.graph.FindInputLink(operation,"imageIn");
            require(typedCoverage.graph.TryConnectSockets(serial.fromNodeId,serial.fromSocketId,blur,"imageIn",&error),error);
            require(typedCoverage.graph.TryConnectSockets(blur,"imageOut",operation,"imageIn",&error),error);
            picker=Stack::Editor::RawLayerToolNodes(typedCoverage.graph);
            require(std::any_of(picker.begin(),picker.end(),[&](const auto* node){return node->id==blur;}),
                "An operation shared by the main image path and coverage branch disappeared from Alt navigation.");
        }
        const auto third=ApplyRawShapeMask(workflow,kRawBackgroundId,localMask.operationUuid,
            EditorNodeGraph::MaskGeneratorKind::RadialGradient,error);
        require(!third.operationUuid.empty() && Stack::Editor::RawLayerToolLabel(workflow.background.graph,
            *FindRawOperation(workflow.background,Kind::LocalEv,third.operationUuid))=="Local EV 3",
            "Repeated masking did not number the next tool 3.");
        const auto layerId=AddRawAdjustmentLayer(workflow);
        auto* other=FindRawAdjustmentLayer(workflow,layerId);
        const int layerStart=FindRawRole(*other,Role::CurrentImage)->id,layerEnd=FindRawRole(*other,Role::LayerResult)->id;
        const int editId=other->graph.AddRawOperationNode(Kind::Exposure,{})->id;
        const auto editUuid=other->graph.FindNode(editId)->instanceUuid;
        require(other->graph.TryConnectSockets(layerStart,"imageOut",editId,"imageIn",&error),error);
        require(other->graph.TryConnectSockets(editId,"imageOut",layerEnd,"imageIn",&error),error);
        const auto direct=ApplyRawShapeMask(workflow,layerId,editUuid,EditorNodeGraph::MaskGeneratorKind::RadialGradient,error);
        const auto layerTools=Stack::Editor::RawLayerToolNodes(FindRawAdjustmentLayer(workflow,layerId)->graph);
        require(direct.operationUuid==editUuid && std::count_if(layerTools.begin(),layerTools.end(),
            [](const auto* node){return node->kind==EditorNodeGraph::NodeKind::RawOperation;})==1,
            "A non-Background mask duplicated the open operation.");
        RawLayerStackState reopened;
        const auto authoredChoices=Stack::Editor::RawLayerToolLabels(Stack::Editor::RawLayerToolNodes(workflow.background.graph));
        require(DeserializeRawLayerStack(SerializeRawLayerStack(workflow),reopened,error),error);
        require(Stack::Editor::RawLayerToolLabels(Stack::Editor::RawLayerToolNodes(reopened.background.graph))==authoredChoices &&
            GetRawOperationMask(reopened,*FindRawAdjustmentLayer(reopened,layerId),editUuid).has_value(),
            "Reopening lost mask attachments or editable mask branches from Alt.");
        {
            editor.OpenRawLayerMaskGraph({});
            const auto& catalog=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
            const auto recipe=std::find_if(catalog.begin(),catalog.end(),[](const auto& definition) {
                return definition.identity.id=="stack:compound/halation";
            });
            require(recipe!=catalog.end(),"The Halation template is missing.");
            const auto recipeIndex=static_cast<std::size_t>(recipe-catalog.begin());
            editor.AddCompoundTemplateNodeAt(recipeIndex,{1000,260});
            auto& canvas=editor.GetNodeGraph();
            const int first=canvas.GetSelectedNodeId();
            const auto firstUuid=canvas.FindNode(first)->instanceUuid;
            const auto resolved=EditorNodeGraphDefinitions::ResolveNodeDefinition(canvas,*canvas.FindNode(first));
            require(resolved && resolved.compound && resolved.ParameterCount()==recipe->parameters.size(),
                "The owning graph did not resolve the embedded compound parameters.");
            EditorNodeGraph::Graph unrelated;
            require(!EditorNodeGraphDefinitions::ResolveNodeDefinition(unrelated,*canvas.FindNode(first)),
                "An unrelated graph supplied an embedded compound definition.");
            editor.AddCompoundTemplateNodeAt(recipeIndex,{1300,260});
            const int second=canvas.GetSelectedNodeId();
            const auto labels=editor.GetRawLayerToolLabels();
            const auto choice=std::find(labels.begin(),labels.end(),"Halation 2");
            require(choice!=labels.end(),"Repeated compound instances are absent from Alt navigation.");
            const int secondIndex=static_cast<int>(choice-labels.begin());
            editor.ConsumeOpenRawLabTabRequest();
            require(editor.OpenRawProjectNodeEditor(second) && editor.ConsumeOpenRawLabTabRequest() &&
                editor.GetRawLayerToolIndex()==secondIndex,
                "An embedded compound did not open its existing graph controls in View.");
            const auto& fallback=Stack::Editor::FindNodeEditor(*canvas.FindNode(first));
            require(!fallback.primary && fallback.reset,
                "Compounds did not reuse graph controls with a parameter reset.");
            Stack::Editor::RawWorkspaceEditContext reset;
            reset.nodeDraft=*canvas.FindNode(first);
            Stack::NodeMath::SetCompoundParameterOverride(reset.nodeDraft->compound.instance,"r",2.5);
            Stack::NodeMath::SetCompoundParameterOverride(reset.nodeDraft->compound.instance,"radius",.02);
            reset.drivenParameters.insert("r");
            require(fallback.reset(editor,reset) &&
                Stack::NodeMath::FindCompoundParameterOverride(reset.nodeDraft->compound.instance,"r") &&
                !Stack::NodeMath::FindCompoundParameterOverride(reset.nodeDraft->compound.instance,"radius"),
                "Reset overwrote a driven compound fallback or retained an unlinked override.");
            require(!fallback.reset(editor,reset),"An unchanged compound reset reported an edit.");
            editor.OpenRawLayerMaskGraph({});
            editor.AddValueNodeAt(Stack::NodeMath::MakeUniformScalar(2.0),{750,260});
            const int scalar=editor.GetNodeGraph().GetSelectedNodeId();
            const auto firstView=std::find_if(editor.GetNodeGraph().GetNodes().begin(),editor.GetNodeGraph().GetNodes().end(),
                [&](const auto& node) { return node.instanceUuid==firstUuid; });
            require(firstView!=editor.GetNodeGraph().GetNodes().end(),"The first compound instance was lost.");
            const int firstViewId=firstView->id;
            const auto owner=firstView->rawLayerOwnerId;
            require(editor.ConnectGraphSockets(scalar,"valueOut",firstViewId,"param:r",&error),error);
            const auto driven=editor.GetUniformParameterState({editor.GetProjectDocumentId(),owner,firstUuid},"r");
            require(driven.available && driven.value==2.0,
                "A compound's connected Scalar Value was not resolved through its definition.");
            const auto compoundSaved=editor.SerializePipeline();
            editor.DeserializePipeline(compoundSaved);
            editor.OpenRawLayerMaskGraph({});
            const auto reloadedTools=Stack::Editor::RawLayerToolLabels(Stack::Editor::RawLayerToolNodes(editor.GetNodeGraph()));
            require(std::find(reloadedTools.begin(),reloadedTools.end(),"Halation 2")!=reloadedTools.end(),
                "Reload lost compound instance eligibility or numbering.");
            std::cout << "Embedded compound definition resolution, View opening, Alt instances, driven fallbacks and reset passed.\n";
        }
        std::cout << "Layer-aware masks, neutral Background insertion, editable mask branches in the picker and reopen passed.\n";
        std::cout << "RAW canvas commands, connections, compact nodes, body double-click, tool selection and reload passed.\n";
        std::cout << "Custom editor registration, graph-controls fallback and fresh masked legacy effect defaults passed.\n";
        return true;
    } catch (const std::exception& failure) {
        std::cerr << "RAW graph editing failed: " << failure.what() << std::endl;
        return false;
    }
}

// Exercise the actual RAW host through public Gallery/editor commands. The
// caller supplies an existing RAW test capture; document and app-state writes
// are disabled by the validation runner.
bool ValidateFloatingTools(EditorModule& editor, const std::filesystem::path& source, bool captureOnly) {
    auto& io=ImGui::GetIO();
    const auto require=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    const auto bounds=[&] {
        ImVec2 low,high;
        require(editor.GetRawActiveControlBounds(low,high),"The RAW tool was not visible");
        return ImRect(low,high);
    };
    double uiMilliseconds=0, pumpMilliseconds=0, drawMilliseconds=0, taskMilliseconds=0;
    const auto frame=[&] {
        ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
        const auto pumpStarted=std::chrono::steady_clock::now();
        editor.PumpNonRenderingWork(4);
        pumpMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pumpStarted).count();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("RAW input validation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        const auto uiStarted=std::chrono::steady_clock::now();
        editor.RenderRawWorkspaceLabUI();
        uiMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uiStarted).count();
        ImGui::End(); ImGui::Render();
        const auto drawStarted=std::chrono::steady_clock::now();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        drawMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-drawStarted).count();
        const auto taskStarted=std::chrono::steady_clock::now();
        Async::TaskSystem::Get().PumpMainThreadTasks(4);
        taskMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-taskStarted).count();
    };
    const auto await=[&](auto ready) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        do { frame(); if(ready()) return; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("RAW input fixture did not become ready: "+editor.GetRawWorkspaceProjectLoadStatusText());
    };
    editor.SetRawWorkspaceFolder(source.parent_path());
    editor.EnterRawWorkspaceRootTab();
    await([&] {return !editor.IsRawWorkspaceScanBusy() && !editor.GetRawWorkspaceState().sources.empty();});
    editor.SelectRawWorkspaceSourceForGallery(source.filename().generic_string(),false,false,true);
    await([&] {return editor.IsRawWorkspaceProjectActive() && !editor.IsRawWorkspaceProjectLoadBusy();});
    const std::vector<std::string> defaultTools{"Local EV", "Tone Curve", "Color Calibration", "Color Warp"};
    require(editor.GetRawLayerToolLabels()==defaultTools && editor.GetRawLayerToolIndex()==0,
        "Opening a RAW image did not bind the default tools to the adopted project identity.");
    require(editor.LeaveRawWorkspaceRootTab(true) && editor.EnterRawWorkspaceRootTab(),
        "The tab-bar Graph-to-RAW transition failed.");
    require(!editor.IsEditingRawLayerMaskGraph() && editor.GetRawLayerToolLabels()==defaultTools && editor.GetRawLayerToolIndex()==0,
        "Entering RAW through the tab bar lost the default layer or its picker.");
    for (int i=0;i<4;++i) frame();
    ImVec2 defaultLow,defaultHigh;
    require(editor.GetRawActiveControlBounds(defaultLow,defaultHigh),
        "The default Local EV editing surface was not displayed.");
    std::cout<<"New image: Background tools, Local EV surface and tab-bar RAW entry passed without opening a node.\n";
    editor.OpenRawLayerMaskGraph({});
    int sourceNode=0,localNode=0,calibrationNode=0;
    for (const auto& node:editor.GetNodeGraph().GetNodes()) {
        if (node.role==Stack::GraphModel::NodeRole::OriginalImage && node.rawLayerOwnerId.empty()) sourceNode=node.id;
        if (node.kind!=EditorNodeGraph::NodeKind::RawOperation) continue;
        if (node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::LocalEv) localNode=node.id;
        if (node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::Calibration) calibrationNode=node.id;
    }
    require(sourceNode && localNode && calibrationNode,"The RAW navigation fixture lacks its default source and tools");
    ValidateNodeOpening(editor,calibrationNode,2,true);
    require(editor.LeaveRawWorkspaceRootTab(true) && editor.EnterRawWorkspaceRootTab() && editor.GetRawLayerToolIndex()==2,
        "Returning to RAW through the tab bar replaced the chosen tool.");
    ValidateNodeOpening(editor,localNode,0,true);
    const auto maskTarget=editor.QueryRawOperationMaskAvailability();
    require(maskTarget.target.has_value(),"The selected Local EV could not create a mask.");
    std::string maskError;
    require(editor.CreateRawOperationMask(*maskTarget.target,
        EditorNodeGraph::MaskGeneratorKind::LinearGradient,maskError),maskError.c_str());
    const auto maskedTarget=editor.QueryRawOperationMaskAvailability();
    const auto maskedToolLabels=editor.GetRawLayerToolLabels();
    require(maskedTarget.target && maskedTarget.target->layerId==maskTarget.target->layerId &&
        maskedTarget.target->operationUuid!=maskTarget.target->operationUuid &&
        maskedTarget.target->displayLabel=="Local EV 2" && editor.GetRawLayerToolIndex()>=0,
        "Creating a Background mask did not select the new local operation.");
    require(editor.UndoRawLayerEdit() && editor.GetRawLayerToolLabels()==defaultTools &&
        editor.GetRawLayerToolIndex()==0,
        "Undoing the open local operation left RAW with a stale target or empty picker.");
    require(editor.RedoRawLayerEdit() && editor.GetRawLayerToolLabels()==maskedToolLabels &&
        editor.GetRawLayerToolIndex()==0,
        "Redo replaced the surviving tool selection or lost the restored operation.");
    require(editor.UndoRawLayerEdit() && editor.GetRawLayerToolLabels()==defaultTools,
        "The selection fixture did not restore the original Background.");
    ValidateNodeOpening(editor,sourceNode,-1,true);
    require(editor.RedoRawLayerEdit() && editor.GetRawLayerToolIndex()==-1 &&
        editor.GetRawLayerToolLabels().empty(),
        "A history change replaced the intentional source-preparation surface.");
    require(editor.UndoRawLayerEdit() && editor.GetRawLayerToolIndex()==-1 &&
        editor.GetRawLayerToolLabels().empty(),
        "Undo replaced the intentional source-preparation surface.");
    std::cout<<"RAW selection: mask creation undo/redo kept a valid tool and preserved source preparation.\n";
    io.AddMousePosEvent(900,550);
    for(int i=0;i<40;++i)frame();
    ImVec2 sourceControlsMin,sourceControlsMax;
    require(editor.GetRawActiveControlBounds(sourceControlsMin,sourceControlsMax),
        "Source-preparation controls were not visible.");
    io.AddMousePosEvent(sourceControlsMin.x+60,
        sourceControlsMin.y+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeight()*.5f);
    frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();
    require(ImGui::GetCurrentContext()->OpenPopupStack.Size==1,
        "The demosaic selector did not open from its RAW control.");
    const auto chooseMethod=[&](int row) {
        const auto* popup=ImGui::GetCurrentContext()->OpenPopupStack[0].Window;
        require(popup && popup->Active,"The demosaic selector lost its popup.");
        io.AddMousePosEvent(popup->Pos.x+35,
            popup->Pos.y+ImGui::GetStyle().WindowPadding.y+(row+.5f)*ImGui::GetTextLineHeightWithSpacing());
        frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();
        require(ImGui::GetCurrentContext()->OpenPopupStack.Size==1,
            "Choosing a demosaic method closed the comparison list.");
    };
    chooseMethod(0);
    const auto firstMethod=editor.SerializePipeline();
    const auto beforeNative=editor.GetViewportOutputRenderGeneration();
    chooseMethod(1);
    require(editor.SerializePipeline()!=firstMethod,"The comparison list did not change demosaic methods.");
    require(editor.GetFullQualityRenderDiagnostic().find("fullResolutionRequested=true")!=std::string::npos ||
        (editor.GetViewportOutputRenderGeneration()>beforeNative &&
         editor.GetGraphPerformanceStats().lastRawPreviewMaxDimension==0),
        "A source-preparation change did not request native refinement directly.");
    await([&] {return editor.GetViewportOutputRenderGeneration()>beforeNative &&
        editor.GetGraphPerformanceStats().lastRawPreviewMaxDimension==0;});
    std::cout<<"Demosaic: consecutive choices kept the list open and rendered native detail directly.\n";
    io.AddMousePosEvent(900,550);frame();
    const auto sourceBeforeZoom=editor.SerializePipeline();
    io.AddMouseWheelEvent(0,4);frame();
    for(int i=0;i<25;++i)frame();
    const auto* sourceZoom=editor.GetRawImageBackdrop();
    require(sourceZoom && sourceZoom->imageMaximum.x-sourceZoom->imageMinimum.x>io.DisplaySize.x*.85f,
        "Source preparation did not zoom its RAW image under the pointer.");
    require(editor.SerializePipeline()==sourceBeforeZoom,"Source-preparation zoom changed processing values.");
    require(ImGui::GetCurrentContext()->OpenPopupStack.Size==1,
        "Zooming the exposed RAW image dismissed the demosaic comparison list.");
    const auto zoomedMinimum=sourceZoom->imageMinimum,zoomedMaximum=sourceZoom->imageMaximum;
    const auto* methodPopup=ImGui::GetCurrentContext()->OpenPopupStack[0].Window;
    io.AddMousePosEvent(methodPopup->Pos.x+methodPopup->Size.x*.5f,methodPopup->Pos.y+methodPopup->Size.y*.5f);
    frame();io.AddMouseWheelEvent(0,-1);frame();frame();
    const auto* listScroll=editor.GetRawImageBackdrop();
    require(listScroll && std::abs(listScroll->imageMinimum.x-zoomedMinimum.x)<.01f &&
        std::abs(listScroll->imageMaximum.x-zoomedMaximum.x)<.01f,
        "Scrolling over the demosaic list also zoomed the image.");
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
    require(ImGui::GetCurrentContext()->OpenPopupStack.empty(),"Escape did not close the comparison list.");
    io.AddMousePosEvent(900,550);frame();
    io.AddMouseButtonEvent(2,true);frame();io.AddMouseButtonEvent(2,false);frame();
    io.AddMouseButtonEvent(2,true);frame();io.AddMouseButtonEvent(2,false);frame();
    for(int i=0;i<25;++i)frame();
    std::cout<<"Source preparation: zoom with the list open, independent list scrolling and Escape passed.\n";
    std::cout<<"Source preparation, Calibration and Local EV: double-click, outgoing Graph frames and RAW handoff passed.\n";
    editor.RequestRawLayerToolIndex(2);
    for(int i=0;i<40;++i) frame();
    const auto initial=editor.SerializePipeline();
    const auto before=bounds();
    const ImVec2 grab(before.Min.x-5,before.Min.y-3);
    io.AddMousePosEvent(grab.x,grab.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(1,true); frame();
    io.AddMousePosEvent(grab.x+80,grab.y+50); frame();
    io.AddMouseButtonEvent(1,false); frame();
    io.AddMouseButtonEvent(0,false); frame(); frame();
    const auto moved=bounds();
    require(moved.Min.x>before.Min.x+40,"The full RAW host did not move");
    require(editor.SerializePipeline()==initial,"Moving the RAW tool changed the image's operations");
    const float radius=std::min(moved.GetWidth(),moved.GetHeight())*.5f-14;
    const auto primary=Stack::RawRecipe::kCalibrationPrimaries[0];
    const auto white=Stack::RawRecipe::kCalibrationWhite;
    const ImVec2 red(moved.GetCenter().x+float((primary[0]-white[0])*radius/.65),
        moved.GetCenter().y-float((primary[1]-white[1])*radius/.65));
    io.AddMousePosEvent(red.x,red.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    for(int i=1;i<=8;++i) {io.AddMousePosEvent(red.x+i*3,red.y);frame();}
    io.AddMouseButtonEvent(0,false);frame();frame();
    require(editor.SerializePipeline()!=initial,"Moving before the first Calibration edit locked the RAW tool");
    std::cout<<"Full RAW workspace: open image, move tool first, then edit Calibration passed.\n";

    // Follow the public UI path with the loaded photograph. Measure drawing
    // separately from async completions, GPU submission and monitor present.
    for(int tool=0;tool<4;++tool) {
        editor.RequestRawLayerToolIndex(tool);
        for(int n=0;n<8;++n)frame();
        const auto start=bounds();
        const auto authored=editor.SerializePipeline();
        const ImVec2 pointer(start.Min.x-5,start.Min.y-3);
        io.AddMousePosEvent(pointer.x,pointer.y);frame();
        io.AddMouseButtonEvent(0,true);frame();
        io.AddMouseButtonEvent(1,true);frame();
        const float priorDelta=io.DeltaTime;io.DeltaTime=1.f/400.f;
        double sum=0,peak=0;float positionError=0;
        for(int n=0;n<24;++n) {
            const float offset=float(n%8)*4;
            io.AddMousePosEvent(pointer.x+offset,pointer.y);frame();
            const auto current=bounds();
            positionError=std::max(positionError,std::abs(current.Min.x-start.Min.x-offset));
            sum+=uiMilliseconds;peak=std::max(peak,uiMilliseconds);
        }
        io.DeltaTime=priorDelta;
        io.AddMouseButtonEvent(1,false);frame();
        io.AddMouseButtonEvent(0,false);frame();frame();
        require(editor.SerializePipeline()==authored,"Floating tool movement changed authored values.");
        std::cout<<defaultTools[tool]<<": RAW UI during movement mean "<<sum/24
            <<" ms, maximum "<<peak<<" ms, same-frame position error "<<positionError<<" px.\n";
        require(positionError<=1.1f,"Floating tool placement trails current-frame pointer input.");
    }

    const auto beforePan=editor.SerializePipeline();
    io.AddMousePosEvent(900,550);frame();
    io.AddMouseButtonEvent(0,true);frame();
    io.AddMouseButtonEvent(1,true);frame();
    require(editor.IsRawImageNavigationActive(),"Two-button image drag did not acquire navigation.");
    io.AddMousePosEvent(1200,550);frame();
    io.AddMouseButtonEvent(1,false);frame();
    io.AddMouseButtonEvent(0,false);frame();frame();
    require(!editor.IsRawImageNavigationActive() && editor.SerializePipeline()==beforePan,
        "Image dragging retained pointer ownership or changed an adjustment.");
    const auto* rawFrame=editor.GetRawImageBackdrop();
    require(rawFrame,"Panned RAW image did not publish its background geometry.");
    const auto rawMinimum=rawFrame->imageMinimum,rawMaximum=rawFrame->imageMaximum;
    if (!captureOnly) {
    require(editor.LeaveRawWorkspaceRootTab(true),"Cannot enter Graph after image panning.");
    const auto graphFrame=[&] {
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
        editor.PumpNonRenderingWork(4);
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Graph background validation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        editor.RenderUI();ImGui::End();ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    };
    io.AddMousePosEvent(900,550);
    for(int i=0;i<22;++i)graphFrame();
    const auto* graphBackdrop=editor.GetRawImageBackdrop();
    require(graphBackdrop && graphBackdrop->extendImage && graphBackdrop->extensionOpacity==1 && graphBackdrop->completePhotoCoverage,
        "Graph did not publish its full fitted image and blurred surround.");
    const auto graphMinimum=graphBackdrop->imageMinimum,graphMaximum=graphBackdrop->imageMaximum;
    const auto* viewport=ImGui::GetMainViewport();
    const bool touchesHorizontal=std::abs(graphMinimum.x-viewport->Pos.x)<.01f &&
        std::abs(graphMaximum.x-viewport->Pos.x-viewport->Size.x)<.01f;
    const bool touchesVertical=std::abs(graphMinimum.y-viewport->Pos.y)<.01f &&
        std::abs(graphMaximum.y-viewport->Pos.y-viewport->Size.y)<.01f;
    require(touchesHorizontal || touchesVertical,"Graph background did not fit to two viewport edges.");
    Stack::Renderer::RawImageBackdrop surround;
    require(surround.Render(*graphBackdrop,*ImGui::GetDrawData(),{.06f,.07f,.08f,1},0),"Graph surround GPU composition failed.");
    const auto readBackdrop=[&] {
        std::vector<unsigned char> pixels(std::size_t(io.DisplaySize.x)*std::size_t(io.DisplaySize.y)*4);
        glReadPixels(0,0,int(io.DisplaySize.x),int(io.DisplaySize.y),GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        return pixels;
    };
    const auto uncachedBackdrop=readBackdrop();
    require(surround.Render(*graphBackdrop,*ImGui::GetDrawData(),{.06f,.07f,.08f,1},0) &&
        readBackdrop()==uncachedBackdrop,"Reusing the unchanged photo copy changed displayed pixels.");
    auto shifted=*graphBackdrop;
    for(auto& patch:shifted.patches) {patch.minimum.x+=10;patch.maximum.x+=10;}
    shifted.imageMinimum.x+=10;shifted.imageMaximum.x+=10;
    require(surround.Render(shifted,*ImGui::GetDrawData(),{.06f,.07f,.08f,1},0),"Moved photo GPU composition failed.");
    const auto shiftedBackdrop=readBackdrop();
    require(shiftedBackdrop!=uncachedBackdrop,"Moving the photo reused a stale screen-space copy.");
    shifted.presentationFingerprint=0;
    require(surround.Render(shifted,*ImGui::GetDrawData(),{.06f,.07f,.08f,1},0) &&
        readBackdrop()==shiftedBackdrop,"Cached photo positioning differs from unconditional rendering.");
    // Real accepted image, same resolution and full blur in both paths. Moving
    // the photo needs a new copy; moving only UI over a stationary photo does not.
    double backdropMs[2]{};
    for(int mode=0;mode<2;++mode) {
        require(surround.Render(*graphBackdrop,*ImGui::GetDrawData(),{.06f,.07f,.08f,1},0),"Backdrop timing warmup failed.");
        GLuint queries[2]{};glGenQueries(2,queries);glQueryCounter(queries[0],GL_TIMESTAMP);
        for(int n=0;n<16;++n) {
            auto sample=*graphBackdrop;
            if(mode && (n%2)==0) {
                for(auto& patch:sample.patches) {patch.minimum.x+=10;patch.maximum.x+=10;}
                sample.imageMinimum.x+=10;sample.imageMaximum.x+=10;
            }
            require(surround.Render(sample,*ImGui::GetDrawData(),{.06f,.07f,.08f,1},0),"Backdrop timing render failed.");
        }
        glQueryCounter(queries[1],GL_TIMESTAMP);GLuint64 begin=0,end=0;
        glGetQueryObjectui64v(queries[0],GL_QUERY_RESULT,&begin);glGetQueryObjectui64v(queries[1],GL_QUERY_RESULT,&end);
        backdropMs[mode]=double(end-begin)/16000000.0;glDeleteQueries(2,queries);
    }
    std::cout<<"Real-image backdrop GPU at 1000x700: stationary copy reuse "<<backdropMs[0]
        <<" ms/frame; moving photo with fresh copy "<<backdropMs[1]<<" ms/frame.\n";
    surround.Shutdown();
    io.AddKeyEvent(ImGuiKey_Space,true);io.AddMouseButtonEvent(0,true);graphFrame();
    io.AddMousePosEvent(940,590);graphFrame();io.AddMouseWheelEvent(0,1);graphFrame();
    io.AddMouseButtonEvent(0,false);io.AddKeyEvent(ImGuiKey_Space,false);graphFrame();
    graphBackdrop=editor.GetRawImageBackdrop();
    require(graphBackdrop && graphBackdrop->imageMinimum.x==graphMinimum.x && graphBackdrop->imageMinimum.y==graphMinimum.y &&
        graphBackdrop->imageMaximum.x==graphMaximum.x && graphBackdrop->imageMaximum.y==graphMaximum.y,
        "Graph input moved or zoomed the fixed image background.");
    require(editor.EnterRawWorkspaceRootTab(),"Cannot return to RAW after fixed Graph navigation.");
    for(int i=0;i<22;++i)frame();
    rawFrame=editor.GetRawImageBackdrop();
    require(rawFrame && std::abs(rawFrame->imageMinimum.x-rawMinimum.x)<.01f &&
        std::abs(rawFrame->imageMinimum.y-rawMinimum.y)<.01f &&
        std::abs(rawFrame->imageMaximum.x-rawMaximum.x)<.01f && std::abs(rawFrame->imageMaximum.y-rawMaximum.y)<.01f,
        "Visiting Graph changed the preserved RAW camera.");
    std::cout<<"RAW two-button image pan, fixed Graph fit, GPU surround and RAW view restoration passed.\n";
    }
    editor.RequestRawLayerToolIndex(0);
    for(int i=0;i<12;++i)frame();
    await([&] {
        const auto* image=editor.GetRawImageBackdrop();
        return image && !image->patches.empty() && image->imageMaximum.x>image->imageMinimum.x &&
            image->imageMaximum.y>image->imageMinimum.y;
    });
    const auto drawCapture=[&] {
        const auto beforeCapture=editor.QueryRawOperationMaskAvailability();
        require(beforeCapture.target.has_value(),"The C stroke has no operation target.");
        const auto* image=editor.GetRawImageBackdrop();require(image,"Capture has no RAW image.");
        const float left=std::max(0.f,image->imageMinimum.x),right=std::min(io.DisplaySize.x,image->imageMaximum.x);
        const float top=std::max(0.f,image->imageMinimum.y),bottom=std::min(io.DisplaySize.y,image->imageMaximum.y);
        const float x=left+(right-left)*.75f,y=top+(bottom-top)*.3f;
        io.AddMousePosEvent(x,y);io.AddKeyEvent(ImGuiKey_C,true);frame();
        io.AddMouseButtonEvent(0,true);frame();
        for(int i=1;i<=6;++i){io.AddMousePosEvent(x+i*4,y+i*3);frame();}
        io.AddMouseButtonEvent(0,false);frame();io.AddKeyEvent(ImGuiKey_C,false);frame();
        const auto started=std::chrono::steady_clock::now();
        require(editor.SubmitRawAssetCapture(Stack::RawAssetCapture::Output::LumaMatch,true),
            "The completed C stroke could not submit a brightness mask.");
        const auto selectedCapture=editor.QueryRawOperationMaskAvailability();
        require(selectedCapture.target && selectedCapture.target->layerId==beforeCapture.target->layerId &&
            selectedCapture.target->operationUuid!=beforeCapture.target->operationUuid &&
            selectedCapture.target->displayLabel.rfind("Local EV ",0)==0 && editor.GetRawLayerToolIndex()>=0,
            "Mask submission did not select its new Background tool synchronously.");
        std::cout<<"C-stroke masked-tool handoff "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<" ms before worker completion.\n";
    };
    const auto dismissPopup=[&] {
        io.AddMousePosEvent(950,680);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    };
    drawCapture();
    require(editor.UndoRawLayerEdit() && editor.GetRawLayerToolLabels()==defaultTools,
        "Undo did not remove the pending capture's adjustment.");
    dismissPopup();
    require(editor.GetRawLayerToolLabels()==defaultTools,"A canceled capture recreated its tool.");
    drawCapture();dismissPopup();
    const auto captureDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(!editor.QueryRawAssetCaptureAvailability().target && std::chrono::steady_clock::now()<captureDeadline) {
        frame();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(editor.QueryRawAssetCaptureAvailability().target.has_value(),"The native stroke capture did not finish.");
    int completedCaptures=1;
    const auto editMaskedTool=[&] {
        for(int i=0;i<8;++i) frame();
        const auto rect=bounds();
        const ImVec2 at(rect.Min.x+rect.GetWidth()*.23f,rect.Min.y+rect.GetHeight()*.7f);
        io.AddMousePosEvent(at.x,at.y);frame();io.AddMouseButtonEvent(0,true);frame();
        double peak=0,sum=0,pumpPeak=0,uiPeak=0,drawPeak=0,taskPeak=0;
        for(int i=1;i<=8;++i) {
            const auto start=std::chrono::steady_clock::now();
            io.AddMousePosEvent(at.x,at.y-i*2);frame();
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            peak=std::max(peak,ms);sum+=ms;
            pumpPeak=std::max(pumpPeak,pumpMilliseconds);uiPeak=std::max(uiPeak,uiMilliseconds);
            drawPeak=std::max(drawPeak,drawMilliseconds);taskPeak=std::max(taskPeak,taskMilliseconds);
        }
        io.AddMouseButtonEvent(0,false);frame();
        std::cout<<"Masked-tool editing after "<<completedCaptures
            <<" captures: full event/render-submission frame mean "<<sum/8<<" ms, peak "<<peak<<" ms; phase peaks pump "
            <<pumpPeak<<", UI "<<uiPeak<<", draw "<<drawPeak<<", tasks "<<taskPeak<<" ms.\n";
    };
    editMaskedTool();
    drawCapture();dismissPopup();
    const auto secondStart=std::chrono::steady_clock::now();
    const auto secondDeadline=secondStart+std::chrono::seconds(60);
    double completionPeak=0;
    while(!editor.QueryRawAssetCaptureAvailability().target && std::chrono::steady_clock::now()<secondDeadline) {
        const auto start=std::chrono::steady_clock::now();frame();
        completionPeak=std::max(completionPeak,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(editor.QueryRawAssetCaptureAvailability().target.has_value(),"The second native mask capture did not finish.");
    ++completedCaptures;
    std::cout<<"Second native capture completion "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-secondStart).count()
        <<" ms, peak foreground frame "<<completionPeak<<" ms.\n";
    editMaskedTool();
    editor.OpenRawLayerMaskGraph({});
    bool finished=false;int finishedMasks=0,blurCount=0,editedLocal=0;
    for(const auto& node:editor.GetNodeGraph().GetNodes()) if(node.kind==EditorNodeGraph::NodeKind::CustomMask) {
        finished=node.customMask.width>1 && node.customMask.height>1 && !node.rawCoverage.contains("captureToken");
        if(finished) ++finishedMasks;
    }
    for(const auto& node:editor.GetNodeGraph().GetNodes()) {
        if(node.kind==EditorNodeGraph::NodeKind::Layer && node.layerType==LayerType::GaussianBlur) ++blurCount;
        if(node.kind==EditorNodeGraph::NodeKind::RawOperation && node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::LocalEv &&
            Stack::RawRecipe::ReadGraphOperation(node.rawOperation).preToneExposureEv!=0) ++editedLocal;
    }
    require(finishedMasks==2 && blurCount==2,"Repeated C capture did not finish two masks with their own blur nodes.");
    require(editedLocal>=2,"The masked tools did not accept ordinary curve edits after capture.");
    std::cout<<"Real C-stroke immediate handoff, cancellation and native mask completion passed.\n";

    return true;
}

} // namespace

static bool RunRawGraphEditing(const std::filesystem::path& input, bool captureOnly, bool includeSharedCurves = true) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(1000, 700, "RAW graph editing validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return false; }
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Match the application's popup/Escape behavior.
    io.DisplaySize = {1000, 700};
    io.DeltaTime = 1.f / 60.f;
    ImGui_ImplOpenGL3_Init("#version 430");
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::EndFrame();
    Async::TaskSystem::Get().Initialize();
    auto editor = std::make_unique<EditorModule>();
    editor->SetDocumentPersistenceEnabled(false);
    editor->Initialize(window, nullptr, false);
    bool success=false;
    try {if(input.empty() && includeSharedCurves) ValidateSharedCurveInteractions(); success=input.empty() ? ValidateCanvas(*editor) : ValidateFloatingTools(*editor,input,captureOnly);}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';}
    editor->RequestWorkerShutdownForAppClose();
    Async::TaskSystem::Get().Shutdown();
    editor.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return success;
}
bool ValidateRawGraphEditing(const std::filesystem::path& input) {
    return RunRawGraphEditing(input,false);
}
bool ValidateRawAssetCapture(const std::filesystem::path& input) {
    return !input.empty() && RunRawGraphEditing(input,true);
}
bool ValidateRawGraphCanvas() {
    return RunRawGraphEditing({},false,false);
}
} // namespace Stack::Validation
