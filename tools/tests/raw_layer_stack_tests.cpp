#include "Project/RawCapturedMask.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Raw/RawAssetCapture.h"
#include "Project/RawLayerStack.h"
#include "Graph/LayerOperationParameters.h"
#include "Project/RawNodeEditing.h"
#include "Editor/NodeEditors/NodeEditorState.h"
#include "Project/RawProjectGraph.h"
#include "Editor/RawLayerTools.h"
#include "App/ToolSwitcher.h"
#include "Project/RawLayerSourceTransactions.h"
#include "Graph/GraphDocumentRules.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include "Raw/RawViewportDetail.h"
#include "Raw/RawRecipeCompatibility.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace Stack::Project;
void Check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
// Existing regression cases deliberately exercise a full legacy operation chain.
// New documents are tested separately with their smaller defaults.
void SeedOperationFixture(RawAdjustmentLayer& layer) {
    using Kind = Stack::RawRecipe::GraphOperationKind;
    using Role = Stack::GraphModel::NodeRole;
    int previous = FindRawRole(layer,Role::CurrentImage)->id;
    const int output = FindRawRole(layer,Role::LayerResult)->id;
    layer.graph.EditLinks().clear();
    int position = 1;
    for (auto kind : {Kind::Calibration,Kind::Exposure,Kind::LocalEv,Kind::ToneCurve,
        Kind::ColorWarp,Kind::DetailContrast}) {
        auto* node = FindRawOperation(layer,kind);
        if (!node) node = layer.graph.AddRawOperationNode(kind,{260.f*position,0});
        const int id=node->id;
        layer.graph.EditLinks().push_back({previous,"imageOut",id,"imageIn"});
        previous=id; ++position;
    }
    layer.graph.EditLinks().push_back({previous,"imageOut",output,"imageIn"});
}
RawMaskReference SampleMask(RawLayerStackState& state, const std::string& owner, Stack::GraphModel::Endpoint point) {
    const auto mask = AddRawGeneratedMask(state, owner, EditorNodeGraph::MaskGeneratorKind::Solid, "Sampled mask");
    auto* layer = FindRawAdjustmentLayer(state, owner);
    const int output = FindRawMaskOutput(state, mask)->nodeId;
    const auto originalLink = layer->graph.GetLinks().back();
    layer->graph.RemoveLink(originalLink.fromNodeId, originalLink.fromSocketId,
        originalLink.toNodeId, originalLink.toSocketId);
    const int source = layer->graph.AddImageNode({}, {0.0f, 300.0f})->id;
    const int channels = layer->graph.AddChannelSplitNode({150.0f, 300.0f})->id;
    std::string error;
    Check(layer->graph.TryConnectSockets(source, "imageOut", channels, "imageIn", &error), error);
    Check(layer->graph.TryConnectSockets(channels, "r", output, "imageIn", &error), error);
    layer->graph.FindNode(source)->role = Stack::GraphModel::NodeRole::Reference;
    layer->graph.FindNode(source)->reference = std::move(point);
    return mask;
}

void CheckNodeEditorTransactions() {
    using namespace Stack;
    using Kind=RawRecipe::GraphOperationKind;
    RawLayerStackDocument document;
    auto initial=document.State();
    auto* first=FindRawOperation(initial.background,Kind::Calibration);
    const auto firstId=first->instanceUuid;
    const int firstInt=first->id;
    const auto secondId=initial.background.graph.AddRawOperationNode(Kind::Calibration,{900,0})->instanceUuid;
    std::string error;
    Check(document.Apply(initial,error),error);
    const NodeEditTarget target{"project-a",kRawBackgroundId,secondId};
    RawLayerStackState candidate;
    auto edit=[](auto&,auto& node,auto&){node.rawOperation.parameters["editor-check"]=1;return true;};
    const auto revision=document.Revision();
    Check(!PrepareRawNodeEdit(document,"project-b",target,revision,candidate,edit,error),"A node edit crossed project ownership.");
    Check(!PrepareRawNodeEdit(document,"project-a",target,revision+1,candidate,edit,error),"A stale node edit was accepted.");
    Check(PrepareRawNodeEdit(document,"project-a",target,revision,candidate,edit,error),error);
    document.BeginGesture();
    Check(document.Apply(candidate,error),error);
    Check(!ResolveRawNode(document.State(),{"project-a",kRawBackgroundId,firstId},"project-a")->rawOperation.parameters.contains("editor-check"),
        "Editing the second instance changed the first.");
    document.EndGesture();
    Check(document.Undo(&error),error);
    Check(!ResolveRawNode(document.State(),target,"project-a")->rawOperation.parameters.contains("editor-check"),"Node edit undo lost the instance.");
    Check(document.Redo(&error),error);
    RawLayerStackDocument reopened;
    Check(reopened.Load(SerializeRawLayerStack(document.State()),error),error);
    Check(ResolveRawNode(reopened.State(),target,"project-a")->rawOperation.parameters.at("editor-check")==1,"Node edit did not survive persistence.");
    const auto input=RawNodeInput(initial,{"project-a",kRawBackgroundId,firstId},"project-a","imageIn");
    Check(input.has_value() && input->portId=="imageOut","Node analysis did not resolve the connected input.");
    Check(!RawNodeInput(initial,target,"project-b","imageIn"),"Node analysis crossed project ownership.");
    Editor::NodeEditorState views;
    struct State { int selected=0; };
    views.Get<State>(target).selected=7;
    const NodeEditTarget other{"project-a",kRawBackgroundId,firstId};
    Check(views.Get<State>(other).selected==0,"Repeated node editors share gesture state.");
    Check(views.Get<State>(target).selected==7,"Returning to a node lost its editor state.");
    Check(views.Get<State>({"project-b",kRawBackgroundId,secondId}).selected==0,"A second project inherited node editor state.");
    // A graph-native effect uses the same mask creation path and fresh defaults.
    auto state=initial;
    auto& layer=state.background;
    const int end=FindRawRole(layer,GraphModel::NodeRole::LayerResult)->id;
    const int lut=layer.graph.AddLutNode({},{1100,0})->id;
    layer.graph.FindNode(lut)->lut.label="Existing LUT selection";
    const auto lutId=layer.graph.FindNode(lut)->instanceUuid;
    Check(layer.graph.TryConnectSockets(firstInt,"imageOut",lut,"imageIn",&error),error);
    Check(layer.graph.TryConnectSockets(lut,"imageOut",end,"imageIn",&error),error);
    const auto* lutNode=layer.graph.FindNode(lut);
    const auto maskPort=EditorNodeGraphDefinitions::GetLivePortContract(layer.graph,*lutNode,"maskIn");
    if (!lutNode->definitionResolved) {
        std::cerr<<"LUT lookup kind="<<static_cast<int>(EditorNodeGraph::NodeKind::Lut)<<", variant0="
            <<bool(EditorNodeGraphDefinitions::FindLiveNodeDefinition(EditorNodeGraph::NodeKind::Lut,0))<<'\n';
        for (const auto& entry:EditorNodeGraphDefinitions::BuildRegisteredNodeCatalogEntries())
            if (entry.kind==EditorNodeGraph::NodeKind::Lut) std::cerr<<"  catalog variant="<<entry.value<<'\n';
        for (const auto& entry:EditorNodeGraphDefinitions::GetUnifiedNodeDefinitionRegistry())
            if (entry.kind==EditorNodeGraph::NodeKind::Lut) std::cerr<<"  registry variant="<<entry.variant<<'\n';
    }
    Check(RawNodeAcceptsMask(layer.graph,*lutNode),"A graph-native effect lost coverage capability: role="+
        std::to_string(static_cast<int>(lutNode->role))+", kind="+std::to_string(static_cast<int>(lutNode->kind))+
        ", resolved="+std::to_string(lutNode->definitionResolved)+
        ", definition error="+lutNode->definitionResolutionError+", imageIn="+std::to_string(layer.graph.FindSocket(lut,"imageIn"))+", imageOut="+
        std::to_string(layer.graph.FindSocket(lut,"imageOut"))+", mask="+std::to_string(bool(maskPort))+
        (maskPort ? ", optional="+std::to_string(maskPort->port.optional)+", type="+
            std::to_string(static_cast<int>(maskPort->port.logicalType)) : ""));
    const auto masked=AddRawLocalAdjustment(state,kRawBackgroundId,lutId,EditorNodeGraph::MaskGeneratorKind::RadialGradient,error);
    Check(!masked.operationUuid.empty(),error);
    const auto* added=ResolveRawNode(state,{"project-a",kRawBackgroundId,masked.operationUuid},"project-a");
    Check(added && added->kind==EditorNodeGraph::NodeKind::Lut && added->lut.label.empty(),
        "Masked graph-native effect did not use independent defaults.");
    auto editable=state.background.graph.FindNode(added->id);
    auto draft=*editable;draft.position={-123,-456};draft.title="Custom editor draft";
    const auto before=editable->position;
    Check(WriteRawNodeDraft(state.background,*editable,draft,{},error),error);
    Check(editable->position.x==before.x && editable->title=="Custom editor draft","A custom editor changed graph layout or lost its payload.");
    const int utility=state.background.graph.AddMaskUtilityNode(EditorNodeGraph::MaskUtilityKind::Invert,{})->id;
    Check(!RawNodeAcceptsMask(state.background.graph,*state.background.graph.FindNode(utility)),"A required mask operand was mistaken for optional coverage.");
    Check(ValidateRawLayerStack(state,error),error);
}

void CheckNeutralMaskedAdjustments() {
    using namespace Stack;
    using Kind = RawRecipe::GraphOperationKind;
    using Mask = EditorNodeGraph::MaskGeneratorKind;
    using Role = GraphModel::NodeRole;
    RawLayerStackState initial;
    std::string error;
    auto* original = FindRawOperation(initial.background,Kind::LocalEv);
    const auto originalUuid = original->instanceUuid;
    const int originalId = original->id;
    auto recipe = ReadRawLayerOperation(initial.background,originalUuid);
    recipe.preToneExposureEv = 1.25f;
    Check(WriteRawLayerOperation(initial.background,originalUuid,recipe,error),error);
    const auto originalSettings = RawRecipe::SerializeGraphOperation(original->rawOperation);
    const int driver = initial.background.graph.AddValueNode(NodeMath::MakeUniformScalar(.75),{})->id;
    Check(initial.background.graph.TryConnectSockets(driver,"valueOut",originalId,"param:strength",&error),error);
    RawLayerStackDocument document;
    Check(document.Apply(initial,error),error);
    const auto before = SerializeRawLayerStack(document.State());
    auto state = document.State();
    const auto first = AddRawLocalAdjustment(state,kRawBackgroundId,originalUuid,Mask::LinearGradient,error);
    Check(!first.operationUuid.empty(),error);
    Check(state.background.graph.GetNodes().size() == initial.background.graph.GetNodes().size()+2,
        "Drawing a mask created publication/reference helper nodes.");
    Check(first.layerId == kRawBackgroundId && state.layers.empty() && !state.background.layerMask,
        "Drawing a mask created a layer or changed whole-layer coverage.");
    auto* added = FindRawOperation(state.background,Kind::LocalEv,first.operationUuid);
    Check(added && RawRecipe::SerializeGraphOperation(added->rawOperation) ==
        RawRecipe::SerializeGraphOperation(RawRecipe::MakeGraphOperation(Kind::LocalEv)),
        "New masked adjustment did not start neutral.");
    Check(RawRecipe::SerializeGraphOperation(FindRawOperation(state.background,Kind::LocalEv,originalUuid)->rawOperation) == originalSettings,
        "Mask creation changed the earlier edit.");
    Check(state.background.graph.FindInputLink(originalId,"param:strength") &&
        !state.background.graph.FindInputLink(added->id,"param:strength"),"Mask creation copied or removed a parameter driver.");
    Check(state.background.graph.FindInputLink(added->id,"imageIn")->fromNodeId == originalId,
        "Masked adjustment was not inserted after the selected instance.");
    const auto* publication = FindRawPublishedNode(state.background,*FindRawMaskOutput(state,first.mask));
    const int generatorId = publication->id;
    Check(publication->kind == EditorNodeGraph::NodeKind::MaskTexture &&
        publication->rawCoverage.value("canvas",std::string{}) == "project" &&
        !state.background.graph.FindInputLink(generatorId,"matchExtent"),
        "A drawn mask still depends on adjustment pixels.");
    const auto sourceSockets = state.background.graph.GetSockets(*publication,false);
    Check(sourceSockets.size() == 1 && sourceSockets.front().direction == EditorNodeGraph::SocketDirection::Output,
        "The shape source exposes an image input.");
    const auto operationSockets = state.background.graph.GetSockets(*added,true);
    Check(std::none_of(operationSockets.begin(),operationSockets.end(),[](const auto& socket) { return socket.id == "inputImageOut"; }),
        "An unused operation-input tap is visible.");
    const auto* attachment = state.background.graph.FindInputLink(added->id,"maskIn");
    Check(attachment && attachment->fromNodeId == generatorId && attachment->fromSocketId == "maskOut",
        "The drawn shape does not connect directly to the adjustment.");
    auto backward = state;
    Check(backward.background.graph.TryConnectSockets(generatorId,"maskOut",originalId,"maskIn",&error),error);
    Check(ValidateRawLayerStack(backward,error),"Independent shape reuse caused a false feedback cycle: "+error);
    Check(GetRawOperationMask(state,state.background,first.operationUuid)->outputId == first.mask.outputId,
        "The RAW editor cannot find its new operation mask.");
    Check(document.Apply(state,error),error);
    const auto after = SerializeRawLayerStack(document.State());
    Check(document.Undo() && SerializeRawLayerStack(document.State()) == before,
        "Undo did not remove the adjustment and its mask together.");
    Check(document.Redo() && SerializeRawLayerStack(document.State()) == after,
        "Redo did not restore the adjustment and its mask together.");
    recipe = ReadRawLayerOperation(state.background,first.operationUuid);
    recipe.preToneExposureEv = -.5f;
    Check(WriteRawLayerOperation(state.background,first.operationUuid,recipe,error),error);
    const int firstId = FindRawOperation(state.background,Kind::LocalEv,first.operationUuid)->id;
    const auto second = AddRawLocalAdjustment(state,kRawBackgroundId,first.operationUuid,Mask::RadialGradient,error);
    Check(!second.operationUuid.empty(),error);
    const auto* next = FindRawOperation(state.background,Kind::LocalEv,second.operationUuid);
    Check(state.layers.empty() && second.mask.outputId != first.mask.outputId &&
        ReadRawLayerOperation(state.background,first.operationUuid).preToneExposureEv == -.5f &&
        ReadRawLayerOperation(state.background,second.operationUuid).preToneExposureEv == 0.f,
        "Repeated drawing copied settings, shared coverage or added a layer.");
    Check(state.background.graph.FindInputLink(next->id,"imageIn")->fromNodeId == firstId &&
        state.background.graph.FindInputLink(FindRawOperation(state.background,Kind::ToneCurve)->id,"imageIn")->fromNodeId == next->id,
        "A second mask did not preserve the intended serial order.");
    auto canvas = BuildRawProjectGraph(state);
    RawLayerStackState projected;
    Check(ApplyRawProjectGraph(state,canvas,canvas.graph,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,projected,error),error);
    Check(GetRawOperationMask(projected,projected.background,second.operationUuid)->outputId == second.mask.outputId,
        "The project graph lost the new mask attachment.");
    auto fromLibrary = canvas.graph;
    const auto libraryShape = fromLibrary.AddMaskGeneratorNode(Mask::Square,{})->instanceUuid;
    Check(ApplyRawProjectGraph(state,canvas,fromLibrary,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,projected,error),error);
    const auto libraryMask = std::find_if(projected.background.maskOutputs.begin(),projected.background.maskOutputs.end(),
        [&](const auto& mask) { return mask.nodeUuid == libraryShape; });
    Check(libraryMask != projected.background.maskOutputs.end(),"A library shape has no selectable mask identity.");
    Check(FindRawPublishedNode(projected.background,*libraryMask)->rawCoverage.value("canvas",std::string{}) == "project",
        "A library shape did not adopt its owning project canvas.");
    RawLayerStackState reopened;
    const auto saved = SerializeRawLayerStack(state);
    Check(DeserializeRawLayerStack(nlohmann::json::parse(saved.dump()),reopened,error),error);
    Check(SerializeRawLayerStack(reopened) == saved,"Same-layer masked edits changed during save/reopen.");
    auto legacyShapes = state;
    for (auto& node : legacyShapes.background.graph.EditNodes())
        if (node.kind == EditorNodeGraph::NodeKind::MaskTexture) {
            node.kind = EditorNodeGraph::NodeKind::MaskGenerator;
            EditorNodeGraphDefinitions::ApplyLiveDefinitionIdentity(node);
        }
    auto legacySaved = SerializeRawLayerStack(legacyShapes);
    legacySaved["version"] = 6;
    RawLayerStackState upgraded;
    Check(!DeserializeRawLayerStack(legacySaved,upgraded,error),"An obsolete mask-presentation model was converted during load.");
    Check(SetRawOperationMask(reopened,kRawBackgroundId,second.operationUuid,std::nullopt,error),error);
    Check(FindRawOperation(reopened.background,Kind::LocalEv,second.operationUuid) &&
        !GetRawOperationMask(reopened,reopened.background,second.operationUuid),"Removing coverage removed the adjustment.");
    const int extraOutput = state.background.graph.AddOutputNode({})->id;
    const int secondId = FindRawOperation(state.background,Kind::LocalEv,second.operationUuid)->id;
    Check(state.background.graph.TryConnectSockets(secondId,"imageOut",extraOutput,"imageIn",&error),error);
    const auto branched = SerializeRawLayerStack(state);
    Check(AddRawLocalAdjustment(state,kRawBackgroundId,second.operationUuid,Mask::Square,error).operationUuid.empty() &&
        SerializeRawLayerStack(state) == branched,"An ambiguous insertion changed the graph.");
    for (auto kind : {Kind::Calibration,Kind::Exposure,Kind::LocalEv,Kind::ToneCurve,Kind::ColorWarp,Kind::DetailContrast}) {
        RawLayerStackState other;
        const auto id = AddRawAdjustmentLayer(other);
        auto* layer = FindRawAdjustmentLayer(other,id);
        const int start = FindRawRole(*layer,Role::CurrentImage)->id;
        const int end = FindRawRole(*layer,Role::LayerResult)->id;
        auto* source = layer->graph.AddRawOperationNode(kind,{});
        const int sourceId = source->id;
        const auto uuid = source->instanceUuid;
        source->rawOperation.enabled = false;
        Check(layer->graph.TryConnectSockets(start,"imageOut",sourceId,"imageIn",&error),error);
        Check(layer->graph.TryConnectSockets(sourceId,"imageOut",end,"imageIn",&error),error);
        const auto created = AddRawLocalAdjustment(other,id,uuid,Mask::Square,error);
        Check(created.layerId == id && other.layers.size() == 1,error);
        const auto* neutral = FindRawOperation(other.layers.front(),kind,created.operationUuid);
        Check(neutral && RawRecipe::SerializeGraphOperation(neutral->rawOperation) ==
            RawRecipe::SerializeGraphOperation(RawRecipe::MakeGraphOperation(kind)),"Operation kind did not start fresh.");
    }
    std::cout << "Neutral same-layer masked adjustment, routing, history and round-trip checks passed.\n";
}

void CheckProjectCanvas() {
    using Kind=Stack::RawRecipe::GraphOperationKind;
    using Role=Stack::GraphModel::NodeRole;
    RawLayerStackState state;
    std::string error;
    {
        auto graph = state.background.graph;
        const int local = FindRawOperation(state.background, Kind::LocalEv)->id;
        Check(!graph.FindSocket(local, "referenceIn") && !graph.FindSocket(local, "measurementImageOut"),
            "Local EV still exposes the retired measurement reference ports.");
        const auto visibleParameter = [&] {
            const auto sockets = graph.GetSockets(*graph.FindNode(local), true);
            return std::any_of(sockets.begin(), sockets.end(), [](const auto& socket) { return socket.id == "param:strength"; });
        };
        Check(!visibleParameter(), "An unexposed RAW parameter is visible by default.");
        Check(graph.SetParameterExposed(local, "strength", true) && visibleParameter(), "Exposing a RAW parameter did not show its socket.");
        Check(graph.SetParameterExposed(local, "strength", false) && !visibleParameter(), "Hiding a RAW parameter did not hide its socket.");
        const int value = graph.AddValueNode(Stack::NodeMath::MakeUniformScalar(1.25), {})->id;
        Check(graph.TryConnectSockets(value, "valueOut", local, "param:strength", &error), error);
        Check(visibleParameter(), "A connected legacy RAW parameter was hidden.");
        Check(graph.SetParameterExposed(local, "strength", false) && !visibleParameter() &&
            !graph.FindInputLink(local, "param:strength"), "Hiding a connected legacy parameter left an invisible wire.");
    }
    const auto count=[](const RawAdjustmentLayer& layer) {
        return std::count_if(layer.graph.GetNodes().begin(),layer.graph.GetNodes().end(),
            [](const auto& n) { return n.kind==EditorNodeGraph::NodeKind::RawOperation; });
    };
    Check(count(state.background)==4,"Background must start with the four default RAW tools.");
    int previous=FindRawRole(state.background,Role::CurrentImage)->id;
    for (const auto kind : {Kind::LocalEv,Kind::ToneCurve,Kind::Calibration,Kind::ColorWarp}) {
        const auto* tool=FindRawOperation(state.background,kind);
        const auto* input=tool ? state.background.graph.FindInputLink(tool->id,"imageIn") : nullptr;
        Check(input && input->fromNodeId==previous,"Default RAW tools were not connected in order.");
        previous=tool->id;
    }
    Check(state.background.graph.FindInputLink(FindRawRole(state.background,Role::LayerResult)->id,"imageIn")->fromNodeId==previous,
        "The default RAW chain did not feed its layer result.");
    const auto originalUuid=FindRawOperation(state.background,Kind::LocalEv)->instanceUuid;
    auto recipe=ReadRawLayerOperation(state.background,originalUuid);
    recipe.preToneExposureEv=1.25f;
    Check(WriteRawLayerOperation(state.background,originalUuid,recipe,error),error);
    // Cap-order tests use explicitly authored layers. Drawing a mask no longer
    // creates a layer; that separate command is exercised below.
    const auto addLayer = [&](Kind kind, EditorNodeGraph::MaskGeneratorKind shape) {
        const auto id = AddRawAdjustmentLayer(state);
        auto* layer = FindRawAdjustmentLayer(state,id);
        const int start = FindRawRole(*layer,Role::CurrentImage)->id;
        const int end = FindRawRole(*layer,Role::LayerResult)->id;
        const auto* operation = layer->graph.AddRawOperationNode(kind,{260,0});
        const int operationId = operation->id;
        const auto uuid = operation->instanceUuid;
        Check(layer->graph.TryConnectSockets(start,"imageOut",operationId,"imageIn",&error),error);
        Check(layer->graph.TryConnectSockets(operationId,"imageOut",end,"imageIn",&error),error);
        const auto mask = AddRawGeneratedMask(state,id,shape,"Coverage");
        Check(SetRawOperationMask(state,id,uuid,mask,error),error);
        return RawLocalAdjustment{id,uuid,mask};
    };
    const auto first=addLayer(Kind::LocalEv,EditorNodeGraph::MaskGeneratorKind::LinearGradient);
    const auto second=addLayer(Kind::ToneCurve,EditorNodeGraph::MaskGeneratorKind::RadialGradient);
    auto canvas=BuildRawProjectGraph(state);
    const auto canvasProposal=Stack::GraphModel::ProposeEdit(canvas.graph,canvas.graph.GetStructureRevision(),[](auto&) {});
    Check(canvasProposal.analysis.valid,canvasProposal.analysis.errors.empty() ? "Project canvas rejected an unchanged edit." : canvasProposal.analysis.errors.front());
    const auto catalog=Stack::GraphModel::QueryAvailableNodes(canvas.graph,std::nullopt,
        EditorNodeGraphDefinitions::LiveGraphRole::RawLayer);
    Check(!catalog.empty(),"The project graph node library is empty.");
    Check(std::any_of(catalog.begin(),catalog.end(),[](const auto& entry) {
        return entry.kind==EditorNodeGraph::NodeKind::Layer && entry.label=="Saturation";
    }),"Saturation is missing from the project graph library.");
    auto toolsLayer=MakeRawLayer("tools","Tools");
    Check(Stack::Editor::RawLayerToolNodes(toolsLayer.graph).empty(),"Layer caps appeared in the RAW tool selector.");
    const int tool=toolsLayer.graph.AddRawOperationNode(Kind::Exposure,{})->id;
    Stack::Tools::Switcher picker;
    picker.count=static_cast<int>(Stack::Editor::RawLayerToolNodes(toolsLayer.graph).size());
    picker.Begin(true);
    Check(!picker.Open(),"A one-node layer opened the Alt selector.");
    const int helper=toolsLayer.graph.AddMaskGeneratorNode(EditorNodeGraph::MaskGeneratorKind::Solid,{})->id;
    picker.count=static_cast<int>(Stack::Editor::RawLayerToolNodes(toolsLayer.graph).size());
    picker.Begin(true);
    Check(picker.Open() && picker.count==2,
        "The settled View selector omitted an editable mask-building instance.");
    toolsLayer.graph.RemoveNode(helper);
    picker.Close(); picker.count=static_cast<int>(Stack::Editor::RawLayerToolNodes(toolsLayer.graph).size()); picker.Begin(true);
    Check(!picker.Open() && Stack::Editor::RawLayerToolNodes(toolsLayer.graph).front()->id==tool,
        "Deleting a node left a stale RAW selector choice.");
    const auto cap=[&](const std::string& owner,Role role) {
        if(role==Role::CurrentImage)for(const auto& hidden:canvas.hiddenStarts)
            if(hidden.owner==owner && !hidden.destinations.empty())return hidden.destinations.front().first;
        for (const auto& n:canvas.graph.GetNodes()) if(n.rawLayerOwnerId==owner && n.role==role) return n.id;
        return 0;
    };
    Check(std::none_of(canvas.graph.GetNodes().begin(),canvas.graph.GetNodes().end(),[](const auto& node) {
        return node.role==Role::CurrentImage;
    }),"The project canvas still exposes a redundant layer Start.");
    const int bgEnd=cap(kRawBackgroundId,Role::LayerResult), aStart=cap(first.layerId,Role::CurrentImage),
        aEnd=cap(first.layerId,Role::LayerResult),bStart=cap(second.layerId,Role::CurrentImage),bEnd=cap(second.layerId,Role::LayerResult);
    const bool capsConnect=canvas.graph.CanConnectSockets(aEnd,"imageOut",bStart,"imageIn",nullptr,&error);
    Check(capsConnect,"Projected caps cannot connect: "+error);
    RawLayerStackState accepted;
    Check(ApplyRawProjectGraph(state,canvas,canvas.graph,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,accepted,error),error);
    auto added=canvas.graph;
    auto addedSettings=Stack::GraphModel::LayerParameterRecords(canvas.graph);
    const int settingIndex=static_cast<int>(addedSettings.size());
    addedSettings.push_back({{"type","Saturation"},{"saturation",-.3f}});
    const int saturation=added.AddLayerNode(LayerType::Saturation,settingIndex,{500,250})->id;
    added.FindNode(saturation)->layerParameters["saturation"]=-.3f;
    const auto saturationUuid=added.FindNode(saturation)->instanceUuid;
    RawLayerStackState withTool;
    Check(ApplyRawProjectGraph(state,canvas,added,addedSettings,first.layerId,withTool,error),error);
    const auto* toolOwner=FindRawAdjustmentLayer(withTool,first.layerId);
    Check(std::any_of(toolOwner->graph.GetNodes().begin(),toolOwner->graph.GetNodes().end(),[&](const auto& node) {
        return node.instanceUuid==saturationUuid && node.layerIndex>=0 &&
            node.layerParameters.at("saturation")==-.3f;
    }),"Library insertion lost its selected owner or operation settings.");
    const auto ownedSettingsCount=Stack::GraphModel::LayerParameterRecords(toolOwner->graph).size();
    added.FindNode(saturation)->position={900,400};
    Check(ApplyRawProjectGraph(state,canvas,added,addedSettings,first.layerId,withTool,error),error);
    Check(Stack::GraphModel::LayerParameterRecords(FindRawAdjustmentLayer(withTool,first.layerId)->graph).size()==ownedSettingsCount,
        "Moving a node changed its parameter ownership.");
    added.RemoveLayerNode(settingIndex);
    addedSettings.erase(addedSettings.begin()+settingIndex);
    Check(!added.FindNode(saturation),"An ordinary library node could not be deleted.");
    Check(ApplyRawProjectGraph(state,canvas,added,addedSettings,first.layerId,withTool,error),error);
    const auto remaining=Stack::Editor::RawLayerToolNodes(FindRawAdjustmentLayer(withTool,first.layerId)->graph);
    Check(std::none_of(remaining.begin(),remaining.end(),[&](const auto* node) { return node->instanceUuid==saturationUuid; }),
        "Deleting a library node left a stale editor instance.");
    auto reordered=canvas.graph;
    // Set all three replacement edges together; intermediate gestures cannot introduce cycles.
    for(auto& edge:reordered.EditLinks()) {
        if(edge.toNodeId==aStart && edge.toSocketId=="imageIn") edge.fromNodeId=bEnd;
        if(edge.toNodeId==bStart && edge.toSocketId=="imageIn") edge.fromNodeId=bgEnd;
        if(edge.toNodeId==canvas.output) edge.fromNodeId=aEnd;
    }
    Check(ApplyRawProjectGraph(state,canvas,reordered,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,accepted,error),error);
    Check(accepted.layers.front().id==second.layerId && accepted.imageOutput->graphId==first.layerId,
        "Cap wiring did not change execution and displayed layer order.");
    RawLayerStackState reopened;
    const auto saved=SerializeRawLayerStack(accepted);
    Check(saved.at("version")==kRawLayerStackSchemaVersion && DeserializeRawLayerStack(nlohmann::json::parse(saved.dump()),reopened,error),error);
    Check(SerializeRawLayerStack(reopened)==saved,"Project canvas routing changed during save/reopen.");
    auto offCanvas=canvas.graph;
    offCanvas.FindNode(aEnd)->outputEnabled=false;
    RawLayerStackState off;
    Check(ApplyRawProjectGraph(state,canvas,offCanvas,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,off,error),error);
    Check(!FindRawAdjustmentLayer(off,first.layerId)->enabled,"End cap switch did not turn off its owning layer.");
    auto cycle=reordered;
    for(auto& edge:cycle.EditLinks()) if(edge.toNodeId==bStart && edge.toSocketId=="imageIn") edge.fromNodeId=aEnd;
    const auto stable=SerializeRawLayerStack(accepted);
    Check(!ApplyRawProjectGraph(state,canvas,cycle,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,accepted,error),"Cap feedback was accepted.");
    Check(SerializeRawLayerStack(accepted)==stable,"Rejected canvas edit changed the result document.");
    auto indirect=canvas.graph;
    int operationId=0;
    for(const auto& n:indirect.GetNodes()) if(n.instanceUuid==first.operationUuid) operationId=n.id;
    for(auto& edge:indirect.EditLinks()) if(edge.toNodeId==operationId && edge.toSocketId=="imageIn")
        edge.fromNodeId=canvas.preparedSource;
    Check(ApplyRawProjectGraph(state,canvas,indirect,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,accepted,error),error);
    Check(FindRawAdjustmentLayer(accepted,first.layerId)->imageInput==RawPreparedSourceEndpoint(),
        "Rewiring the first operation did not update its hidden layer ingress.");
    auto shared=canvas.graph;
    int generator=0,global=0;
    for(const auto& n:shared.GetNodes()) {
        if(n.rawLayerOwnerId==first.layerId && n.kind==EditorNodeGraph::NodeKind::MaskGenerator && n.role==Role::Ordinary) generator=n.id;
        if(n.instanceUuid==originalUuid) global=n.id;
    }
    Check(shared.TryConnectSockets(generator,"maskOut",global,"maskIn",&error),error);
    Check(ApplyRawProjectGraph(state,canvas,shared,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,accepted,error),error);
    const auto sharedSaved=SerializeRawLayerStack(accepted);
    RawLayerStackState repeated;
    Check(ApplyRawProjectGraph(accepted,canvas,shared,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,repeated,error),error);
    Check(SerializeRawLayerStack(repeated)==sharedSaved,"Repeated canvas commit replaced the shared reference identity.");
    auto* external=state.background.graph.AddImageNode({},{});
    external->role=Role::Reference;
    external->reference={"project","external-source","imageOut"};
    const auto externalUuid=external->instanceUuid;
    canvas=BuildRawProjectGraph(state);
    canvas.graph.EditNodes().front().position.x+=45.f;
    Check(ApplyRawProjectGraph(state,canvas,canvas.graph,Stack::GraphModel::LayerParameterRecords(canvas.graph),kRawBackgroundId,accepted,error),error);
    bool retained=false;
    for(const auto& n:accepted.background.graph.GetNodes()) if(n.instanceUuid==externalUuid)
        retained=n.reference.graphId=="project" && n.reference.nodeUuid=="external-source";
    Check(retained,"A layout edit lost an external reference.");
    auto legacy=SerializeRawLayerStack(RawLayerStackState{}); legacy["version"]=4;
    legacy.erase("explicitRouting"); legacy.erase("imageOutput");
    const auto beforeUnsupported=SerializeRawLayerStack(reopened);
    Check(!DeserializeRawLayerStack(legacy,reopened,error) && SerializeRawLayerStack(reopened)==beforeUnsupported,
        "An unsupported layer document changed the current routing draft: "+error);
    RawLayerStackState feedback;
    feedback.explicitRouting=true;
    auto& feedbackGraph=feedback.background.graph;
    const auto currentId=FindRawRole(feedback.background,Role::CurrentImage)->id;
    const auto feedbackNode=feedbackGraph.AddDataMathNode(EditorNodeGraph::DataMathMode::Multiply,{})->id;
    Check(feedbackGraph.TryConnectSockets(currentId,"imageOut",feedbackNode,"imageA",&error),error);
    feedback.background.imageInput=RawLayerEndpoint(feedback.background,*feedbackGraph.FindNode(feedbackNode));
    feedback.imageOutput=RawLayerEndpoint(feedback.background,*FindRawRole(feedback.background,Role::LayerResult));
    Check(!ValidateRawLayerStack(feedback,error),"An ordinary image endpoint hid Start feedback.");
    feedback.background.imageInput=RawPreparedSourceEndpoint();
    feedbackGraph.FindNode(feedbackNode)->dataMathSettings.domain=Stack::NodeMath::ArithmeticDomain::Values;
    feedbackGraph.EditLinks().erase(std::remove_if(feedbackGraph.EditLinks().begin(),feedbackGraph.EditLinks().end(),
        [&](const auto& link){return link.toNodeId==feedbackNode;}),feedbackGraph.EditLinks().end());
    feedback.imageOutput=RawLayerEndpoint(feedback.background,*feedbackGraph.FindNode(feedbackNode));
    Check(!ValidateRawLayerStack(feedback,error),"A uniform result was accepted as the main image route.");
    std::cout << "Project canvas defaults, local creation, cap order, image-route guards, cycle rejection and save/reopen passed.\n";
}

void CheckViewportContentIdentity() {
    const auto recipe = Stack::RawRecipe::MakeDefaultRecipe("layer-viewport-test");
    RenderGraphNode background;
    background.nodeId = 1;
    background.kind = RenderGraphNodeKind::RawDevelopment;
    background.rawDevelopment.recipe = recipe;
    RenderGraphSnapshot graph;
    graph.nodes.push_back(background);
    graph.outputNodeId = 1;
    graph.outputSocketId = "imageOut";
    RawLayerStackDocument document;
    RawLayerStackState state;
    AddRawAdjustmentLayer(state, "Exposure");
    SeedOperationFixture(state.layers.front());
    std::string error;
    Check(document.Apply(state, error), error);
    const auto lowered = [&]() {
        auto snapshot = graph;
        Check(LowerRawLayerStack(snapshot, document.State(), 1, document.ProcessingRevision(), error), error);
        return snapshot;
    };
    const auto identity = [&](const auto& snapshot) {
        return Stack::EditorRendering::BuildRawPresentationFingerprint(recipe, snapshot);
    };
    const auto native = lowered();
    const auto content = identity(native);
    Check(Raw::RetainViewportDetail(content, identity(lowered()), {}, 4000, 3000, {}, 1000, 750),
        "Unchanged layered proxy cannot retain matching native detail.");
    auto layoutOnly = document.State();
    layoutOnly.background.graph.EditNodes().front().position.x += 20;
    const auto pixelsBeforeLayout = document.ProcessingRevision();
    Check(document.Apply(layoutOnly, error, false), error);
    Check(document.ProcessingRevision() == pixelsBeforeLayout && identity(lowered()) == content,
        "Layout changes invalidated retained image pixels.");
    Check(document.Undo() && document.ProcessingRevision() == pixelsBeforeLayout,"Layout undo invalidated pixels.");
    Check(document.Redo() && document.ProcessingRevision() == pixelsBeforeLayout,"Layout redo invalidated pixels.");
    document.BeginGesture();
    auto cancelledLayout = document.State();
    cancelledLayout.background.graph.EditNodes().front().position.x += 10;
    Check(document.Apply(cancelledLayout,error,false),error);
    Check(document.CancelGesture() && document.ProcessingRevision() == pixelsBeforeLayout,"Layout cancellation invalidated pixels.");
    auto edited = document.State();
    FindRawOperation(edited.layers.front(), Stack::RawRecipe::GraphOperationKind::Exposure)->rawOperation.parameters["ev"] = 1.0f;
    Check(document.Apply(edited, error), error);
    Check(!Raw::RetainViewportDetail(content, identity(lowered()), {}, 4000, 3000, {}, 1000, 750),
        "Old native detail hides an adjustment-layer edit.");
    auto maskPreview = native;
    maskPreview.outputNodeId = native.rawLayerBackgroundNodeId;
    Check(identity(maskPreview) != content, "A changed layered output reused photo detail.");
    Check(identity(graph) == Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, 0).postOutputCrop,
        "Background-only presentation identity changed.");
}

void CheckCompoundReferenceCycle() {
    using namespace Stack;
    RawLayerStackState state;
    EditorNodeGraph::Graph internal;
    internal.SetAllowNoOutput(true);
    const auto compoundUuid = NodeMath::GenerateCanonicalUuid();
    auto* reference = internal.AddImageNode({},{});
    reference->role = GraphModel::NodeRole::Reference;
    reference->referenceType = NodeMath::LogicalValueType::ColorImage;
    reference->reference = {kRawBackgroundId,compoundUuid,"image"};
    NodeMath::CompoundDefinition definition;
    definition.definitionUuid = NodeMath::GenerateCanonicalUuid();
    definition.identity.id = "project:compound/" + definition.definitionUuid;
    definition.identity.version = {1,0,0};
    definition.label = "Reference loop";
    definition.ports.push_back({"image","Image",NodeMath::PortDirection::Output,
        NodeMath::LogicalValueType::ColorImage,false,reference->instanceUuid,"imageOut"});
    definition.canonicalGraph = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(),internal);
    NodeMath::RefreshCompoundDefinitionContentHash(definition);
    std::string error;
    Check(state.background.graph.AddCompoundDefinition(definition,&error),error);
    auto* compound = state.background.graph.AddCompoundNode(definition.identity,{});
    compound->instanceUuid = compoundUuid;
    compound->compound.instance.instanceUuid = compoundUuid;
    Check(!ValidateRawLayerStack(state,error) && error.find("dependency loop") != std::string::npos,
        "Disconnected compound reference feedback was not rejected with its cycle path: " + error);
}

void CheckCompoundOutputCompleteness() {
    using namespace Stack;
    using namespace EditorNodeGraph;
    const auto checkRawEnd = [&](const NodeMath::CompoundDefinition& definition,
                                 const std::vector<NodeMath::CompoundDefinition>& catalog,
                                 const char* label) {
        RawLayerStackState state;
        auto& graph=state.background.graph;
        std::string error;
        const auto closure=NodeMath::CollectCompoundDependencyClosure(catalog,{definition.identity},&error);
        Check(!closure.empty(),std::string(label)+": "+error);
        for (const auto& reference:closure)
            Check(graph.AddCompoundDefinition(*NodeMath::FindExactCompoundDefinition(catalog,reference),&error),error);
        const int end=FindRawRole(state.background,GraphModel::NodeRole::LayerResult)->id;
        const auto incoming=*graph.FindAnyInputLink(end,"imageIn");
        const int compound=graph.AddCompoundNode(definition.identity,{})->id;
        GraphOutputContext context;
        for (const auto& node:graph.GetNodes())
            if (node.role==GraphModel::NodeRole::OriginalImage || node.role==GraphModel::NodeRole::CurrentImage)
                context.sourceDescriptors[GraphOutputIdentity(node.id,"imageOut")]=RawLayerSceneDescriptor(Raw::RawWorkingSpace::LinearRec2020D65);
        ScopedGraphOutputAnalysis semantics(graph,context);
        Check(graph.TryConnectSockets(incoming.fromNodeId,incoming.fromSocketId,compound,"image-in",&error),error);
        Check(graph.TryConnectSockets(compound,"image-out",end,"imageIn",&error),error);
        const auto admitted=[&] {
            const auto& outputs=graph.GetConnectedOutputNodeIds();
            return std::find(outputs.begin(),outputs.end(),end)!=outputs.end();
        };
        Check(admitted(),std::string(label)+" packed RAW End unavailable: "+graph.GetOutputConnectionDiagnostic());
        std::vector<int> children;
        Check(graph.UnpackCompoundNode(compound,&children,&error),error);
        Check(admitted(),std::string(label)+" decompiled RAW End unavailable: "+graph.GetOutputConnectionDiagnostic());
    };

    Graph internal;
    internal.Clear();internal.SetAllowNoOutput(true);
    auto* difference=internal.AddDataMathNode(DataMathMode::Subtract,{});
    difference->dataMathSettings.constantA=1.f;
    NodeMath::CompoundDefinition subtract;
    subtract.definitionUuid=NodeMath::GenerateCanonicalUuid();
    subtract.identity.id="project:compound/"+subtract.definitionUuid;
    subtract.identity.version={1,0,0};subtract.label="One minus input";
    subtract.ports.push_back({"image-in","Image",NodeMath::PortDirection::Input,
        NodeMath::LogicalValueType::ColorImage,false,difference->instanceUuid,"imageB"});
    subtract.ports.push_back({"image-out","Result",NodeMath::PortDirection::Output,
        NodeMath::LogicalValueType::ColorImage,false,difference->instanceUuid,"imageOut"});
    subtract.canonicalGraph=SerializeGraphPayload(nlohmann::json::array(),internal);
    NodeMath::RefreshCompoundDefinitionContentHash(subtract);
    checkRawEnd(subtract,{subtract},"Constant A / connected B");

    const auto& templates=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    const auto halation=std::find_if(templates.begin(),templates.end(),[](const auto& definition) {
        return definition.identity.id=="stack:compound/halation";
    });
    Check(halation!=templates.end(),"Plain Halation template is missing.");
    checkRawEnd(*halation,templates,"Halation");
}

void CheckNativeDecomposition() {
    using namespace Stack;
    using namespace EditorNodeGraph;
    for (const auto& definition:EditorNodeGraphDefinitions::GetUnifiedNodeDefinitionRegistry()) {
        Check(definition.inspectability==NodeMath::Inspectability::OpaqueSpecialized ||
            !definition.canonicalRecipeId.empty(),"A native operation claims graph inspectability without a recipe.");
    }
    for (bool enabled:{true,false}) {
        Graph graph;graph.Clear();
        const int source=graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid,{})->id;
        const int native=graph.AddMaskUtilityNode(MaskUtilityKind::Invert,{})->id;
        graph.FindNode(native)->maskUtilitySettings.enabled=enabled;
        graph.FindNode(native)->rawLayerOwnerId="native-owner";
        const auto uuid=graph.FindNode(native)->instanceUuid;
        const int end=graph.AddOutputNode({},true)->id;
        std::string error;
        Check(graph.TryConnectSockets(source,"maskOut",native,"maskIn",&error),error);
        Check(graph.TryConnectSockets(native,"maskOut",end,"imageIn",&error),error);
        const int maskedGain=graph.AddDataMathNode(DataMathMode::Multiply,{})->id;
        Check(graph.TryConnectSockets(source,"maskOut",maskedGain,"imageA",&error),error);
        Check(graph.TryConnectSockets(native,"maskOut",maskedGain,"maskIn",&error),error);
        const auto nativeType=DescribeGraphOutput(graph,native,"maskOut").descriptor.logicalType;
        const auto capability=EditorNodeGraphDefinitions::GetNodeDecompositionCapability(graph,*graph.FindNode(native));
        Check(capability.canDecompile && capability.inspectability==NodeMath::Inspectability::GraphDefinedOptimizedEquivalent,
            "Native Invert Mask has no exact decomposition capability.");
        NodeMath::CompoundDefinition first,second;
        Check(EditorNodeGraphDefinitions::BuildNativeNodeDecomposition(graph,*graph.FindNode(native),first,&error),error);
        graph.FindNode(native)->title="Renamed inversion";
        Check(EditorNodeGraphDefinitions::BuildNativeNodeDecomposition(graph,*graph.FindNode(native),second,&error),error);
        Check(NodeMath::SameDefinitionReference(first.identity,second.identity),"A UI title changed the native recipe identity.");
        Check(first.ports.size()==2 && first.ports[0].id=="mask-in" && first.ports[1].id=="mask-out",
            "A native recipe reused noncanonical native socket IDs as compound ports.");
        std::vector<int> children;
        std::vector<CompoundOutputBinding> bindings;
        Check(graph.UnpackCompoundNode(native,&children,&error,&bindings),error);
        Check(children.size()==1 && graph.FindNode(source),"Native decompilation duplicated its source or extra operations.");
        Check(bindings.size()==1 && bindings.front().authoredNodeId==native &&
            bindings.front().authoredSocketId=="maskOut" && bindings.front().expandedNodeId==children.front() &&
            bindings.front().expandedSocketId=="imageOut",
            "Native decompilation lost the authored output socket alias.");
        auto* child=graph.FindNode(children.front());
        Check(child && child->kind==NodeKind::DataMath && child->rawLayerOwnerId=="native-owner" &&
            child->dataMathSettings.domain==NodeMath::ArithmeticDomain::Channel &&
            child->dataMathMode==(enabled ? DataMathMode::Subtract : DataMathMode::Multiply),
            "Native enabled/bypass setting or graph owner changed during decompilation.");
        const auto* input=graph.FindAnyInputLink(child->id,enabled ? "imageB" : "imageA");
        const auto* output=graph.FindAnyInputLink(end,"imageIn");
        const auto* coverage=graph.FindAnyInputLink(maskedGain,"maskIn");
        Check(input && output && coverage && input->fromNodeId==source &&
            output->fromNodeId==child->id && coverage->fromNodeId==child->id,
            "Native decompilation lost an exact external endpoint.");
        Check(DescribeGraphOutput(graph,child->id,"imageOut").descriptor.logicalType==nativeType,
            "A decomposed native Mask changed its logical type at an effect-mask consumer.");
        const auto leaf=EditorNodeGraphDefinitions::GetNodeDecompositionCapability(graph,*child);
        Check(!leaf.canDecompile && !leaf.reason.empty(),"An arithmetic primitive is advertised as a fake compound.");
        graph.EditNodes();
        child=graph.FindNode(children.front());
        if (enabled) child->dataMathSettings.constantA=2.f;
        else child->dataMathSettings.constantB=2.f;
        int repacked=-1;
        Check(graph.GetGroups().size()==1 && graph.RepackCompoundGroup(graph.GetGroups().front().id,&repacked,&error),error);
        Check(graph.FindNode(repacked)->instanceUuid==uuid,"Native repack lost the original instance identity.");
        Graph restored;
        DeserializeGraphPayload(SerializeGraphPayload(nlohmann::json::array(),graph),restored,0);
        children.clear();
        Check(restored.UnpackCompoundNode(repacked,&children,&error),error);
        const auto* edited=restored.FindNode(children.front());
        Check(edited && (enabled ? edited->dataMathSettings.constantA : edited->dataMathSettings.constantB)==2.f,
            "Repacking/reloading a native decomposition restored the original recipe over an edit.");
    }
}

void CheckMergedSourceHistory() {
    using namespace Stack;
    for (const auto intent : {MultiFrameOperationIntent::RawBurstDenoise,MultiFrameOperationIntent::RawBurstHdr}) {
        RawProjectSnapshot snapshot;
        snapshot.projectId = "merged-source-history";
        snapshot.pipelineData["rawLayerStack"] = nlohmann::json::object();
        MultiFrameSourceSet sourceSet;
        sourceSet.sourceSetId = "first-source";
        sourceSet.operationIntent = intent;
        sourceSet.name = "First";
        snapshot.sourceSets.push_back(sourceSet);
        sourceSet.sourceSetId = "second-source";
        sourceSet.name = "Second";
        snapshot.sourceSets.push_back(sourceSet);
        snapshot.activeSourceSetId = "first-source";
        std::string error;
        RawProjectEditRecipeBinding binding;
        Check(ResolveRawProjectEditRecipe(snapshot,"first-source",binding,&error),error);
        Check(StoreRawProjectEditRecipe(snapshot,binding,binding.recipe).success,"Could not seed merged source settings.");
        const auto initial = binding.recipe;
        RawLayerStackDocument document;
        document.BeginGesture();
        for (float gain : {2.f,3.f}) {
            auto next = initial;
            next.whiteBalance.hasMultipliers = true;
            next.whiteBalance.multipliers = {gain,1.f,1.f};
            const auto update = ApplyMergedRawLayerSourceEdit(document,snapshot,"first-source",next,nullptr,0);
            Check(update.success && update.changed,update.errorMessage);
        }
        document.EndGesture();
        snapshot.activeSourceSetId = "second-source";
        auto singleSource = RawRecipe::MakeDefaultRecipe("unrelated-single-source");
        auto update = RestoreRawLayerHistory(document,RawLayerHistoryAction::Undo,singleSource,&snapshot,nullptr,0);
        Check(update.success && update.sourceSetId == "first-source" && !document.CanUndo(),
            "Merged source gesture did not create exactly one history entry for its own source.");
        Check(ResolveRawProjectEditRecipe(snapshot,"first-source",binding,&error),error);
        Check(binding.recipe.whiteBalance.multipliers == initial.whiteBalance.multipliers,
            "Merged source undo did not restore camera settings.");
        update = RestoreRawLayerHistory(document,RawLayerHistoryAction::Redo,singleSource,&snapshot,nullptr,0);
        Check(update.success,"Merged source redo failed: "+update.errorMessage);
        Check(ResolveRawProjectEditRecipe(snapshot,"first-source",binding,&error),error);
        Check(binding.recipe.whiteBalance.multipliers[0] == 3.f,"Merged source redo restored another source's settings.");
        document.BeginGesture();
        auto cancelled = binding.recipe;
        cancelled.whiteBalance.multipliers[0] = 4.f;
        Check(ApplyMergedRawLayerSourceEdit(document,snapshot,"first-source",cancelled,nullptr,0).success,
            "Could not preview the merged source gesture.");
        update = RestoreRawLayerHistory(document,RawLayerHistoryAction::CancelGesture,singleSource,&snapshot,nullptr,0);
        Check(update.success && !document.GestureActive(),"Merged source gesture cancellation failed.");
        Check(ResolveRawProjectEditRecipe(snapshot,"first-source",binding,&error),error);
        Check(binding.recipe.whiteBalance.multipliers[0] == 3.f,"Cancellation left preview settings in the merged source.");
        Check(ResolveRawProjectEditRecipe(snapshot,"second-source",binding,&error),error);
        Check(binding.recipe.whiteBalance.multipliers[0] == 1.f,"History changed another merged source.");
        const auto revision = document.Revision();
        snapshot.sourceSets.erase(snapshot.sourceSets.begin());
        update = RestoreRawLayerHistory(document,RawLayerHistoryAction::Undo,singleSource,&snapshot,nullptr,0);
        Check(!update.success && document.Revision() == revision,"An unresolved history source changed the layer document.");
    }
}

void CheckViewportCoordinates() {
    using Mapping = Stack::Editor::RawLabInternal::RawLabImageMapping;
    Stack::NodeMath::SpatialDescriptor source;
    source.fullWindow = {0,0,600,400};
    auto displayed = source;
    displayed.sourceTransform = {-1,0,1,0,1,0,0,0,1};
    Stack::RawRecipe::RawCropRotationRecipe crop;
    crop.cropEnabled=true; crop.cropX=.1f; crop.cropY=.2f; crop.cropWidth=.5f; crop.cropHeight=.6f;
    const auto flipped=Mapping::FromSpatial(source,displayed,crop);
    Check(flipped.has_value(),"A cropped horizontal flip has no viewport mapping.");
    const auto painted=flipped->Local({.2f,.3f});
    Check(std::abs(painted.x-.8f)<1e-6f && std::abs(painted.y-.38f)<1e-6f,
        "Viewport painting ignored the displayed crop or horizontal flip.");
    const auto handle=flipped->Canvas(painted);
    Check(std::abs(handle.x-.2f)<1e-6f && std::abs(handle.y-.3f)<1e-6f,
        "A painted point and its viewport handle disagree.");
    source.sourceTransform={0,-1,1,1,0,0,0,0,1};
    displayed.sourceTransform={1,0,0,0,1,0,0,0,1};
    const auto rotated=Mapping::FromSpatial(source,displayed,{});
    Check(rotated.has_value(),"A rotated operation input has no viewport mapping.");
    const auto local=rotated->Local({.2f,.3f});
    Check(std::abs(local.x-.7f)<1e-6f && std::abs(local.y-.2f)<1e-6f,
        "Painting used display coordinates instead of the rotated input.");
    source.sourceTransform={0,0,0,0,1,0,0,0,1};
    Check(!Mapping::FromSpatial(source,displayed,{}),"A singular coordinate transform was treated as identity.");
}

void CheckTonalCurvePoints() {
    using namespace Stack::RawRecipe;
    using Result = TonalCurveEditResult;
    const std::vector<RawBezierCurvePoint> identity{{0,0,{1},{1}},{1,1,{1},{1}}};
    auto points = identity;
    Check(SetTonalCurveSlider(points,0,.25f,0,64)==Result::Unchanged && SameCurvePoints(points,identity),
        "A zero slider created a hidden curve point.");
    Check(SetTonalCurveSlider(points,0,.25f,.5f,64)==Result::Changed,
        "A nonzero slider did not add its curve point.");
    Check(points.size()==3 && points[1].x==.25f && points[1].y==.625f &&
        points[1].tonal.baselineY==.25f && TonalCurveSliderValue(points,0)==.5f,
        "A slider did not use the current curve baseline and positive headroom.");
    Check(SetTonalCurveSlider(points,0,-2,-.5f,64)==Result::Changed &&
        points[1].x==.25f && points[1].y==.125f && TonalCurveSliderValue(points,0)==-.5f,
        "Updating a slider moved its fixed x or used the wrong negative headroom.");
    Check(SetTonalCurveSlider(points,0,.9f,-.5f,64)==Result::Unchanged,
        "Repeating an identical slider value invalidated the curve.");
    Check(SetTonalCurveSlider(points,0,.9f,1,64)==Result::Changed && points[1].y==1 &&
        TonalCurveSliderValue(points,0)==1,"A positive slider endpoint did not reach available white headroom.");
    Check(SetTonalCurveSlider(points,0,.9f,-1,64)==Result::Changed && points[1].y==0 &&
        TonalCurveSliderValue(points,0)==-1,"A negative slider endpoint did not reach available black headroom.");
    Check(SetTonalCurveSlider(points,0,-2,0,64)==Result::Changed && SameCurvePoints(points,identity),
        "Zero failed to remove an owned point after its proposed domain position changed.");

    const float tiny = std::numeric_limits<float>::epsilon();
    Check(SetTonalCurveSlider(points,2,.5f,tiny,64)==Result::Changed &&
        TonalCurveSliderValue(points,2)>0 && points[1].y!=points[1].tonal.baselineY,
        "A small representable nonzero edit was treated as zero.");
    Check(!RemoveNeutralTonalCurvePoints(points),"Neutral cleanup swallowed a small intentional edit.");
    points[1].y=points[1].tonal.baselineY;
    Check(RemoveNeutralTonalCurvePoints(points) && SameCurvePoints(points,identity),
        "Returning an owned point exactly to baseline did not remove it.");

    const std::vector<RawBezierCurvePoint> custom{
        {0,0,{1},{1}}, {.25f,.3f,{.8f,true,-.03f,-.02f},{.7f,true,.04f,.02f}},
        {.5005f,.7f,{.8f,true,-.04f,-.06f},{.9f,true,.05f,.03f}}, {1,1,{1},{1}}};
    points=custom;
    Check(SetTonalCurveSlider(points,2,.5f,.5f,64)==Result::Changed && points.size()==custom.size(),
        "A colliding slider added a duplicate x rather than retaining the custom point.");
    Check(points[2].x==custom[2].x && points[2].tonal.replacesPoint &&
        points[2].tonal.originalX==custom[2].x && points[2].tonal.baselineY==custom[2].y,
        "A near-collision moved the original anchor or lost its restoration baseline.");
    Check(SameCurvePoints({points[0],points[1],points[3]},{custom[0],custom[1],custom[3]}),
        "A slider changed unrelated custom points or manual handles.");
    const auto collided=points;
    Check(SetTonalCurveSlider(points,1,.5004f,.5f,64)==Result::Conflict && SameCurvePoints(points,collided),
        "A second slider replaced another slider's fixed point.");
    Check(SetTonalCurveSlider(points,2,.5f,0,64)==Result::Changed && SameCurvePoints(points,custom),
        "Zero did not restore the displaced custom point and its manual handles exactly.");
    Check(SetTonalCurveSlider(points,2,.5f,-.25f,64)==Result::Changed,
        "A custom point could not be reacquired after restoring it.");
    points[2].y=points[2].tonal.baselineY;
    Check(RemoveNeutralTonalCurvePoints(points) && SameCurvePoints(points,custom),
        "Baseline drag cleanup did not restore the original custom point.");
    Check(SetTonalCurveSlider(points,2,.5f,.5f,64)==Result::Changed &&
        RemoveTonalCurvePoint(points,2) && SameCurvePoints(points,custom),
        "Explicit owned-point removal lost the displaced ordinary point.");
    auto full=identity;
    Check(SetTonalCurveSlider(full,0,.25f,.5f,2)==Result::CapacityReached && SameCurvePoints(full,identity),
        "A full curve silently discarded an existing point to make room for a slider.");

    points=custom;
    Check(SetTonalCurveSlider(points,2,.5f,.5f,64)==Result::Changed &&
        SetTonalCurveSlider(points,0,.1f,-.25f,64)==Result::Changed,"Could not author persistence fixtures.");
    const auto genericSaved=CurvePointsJson(points);
    Check(SameCurvePoints(ReadCurvePoints(nlohmann::json::parse(genericSaved.dump())),points),
        "Generic curve JSON lost slider ownership or the custom point snapshot.");
    const CurveAmountState locked{true,0,points};
    const auto effective=AttenuateCurve(locked.reference,locked.amount,true);
    Check(std::none_of(effective.begin(),effective.end(),[](const auto& p){return IsTonalCurvePoint(p);}),
        "Zero curve amount left hidden neutral slider-owned points.");
    Check(CurvePointsJson(locked.reference)==genericSaved,
        "Attenuation changed the authored locked reference.");
    auto partial=AttenuateCurve(locked.reference,.5f,true);
    for (std::size_t i=partial.size();i-- > 0;) if (IsTonalCurvePoint(partial[i])) RemoveTonalCurvePoint(partial,i);
    Check(SameCurvePoints(partial,AttenuateCurve(custom,.5f,true)),
        "Partial attenuation did not transform restoration baselines and manual handles consistently.");
    Check(CurveAmountJson(ReadCurveAmount(nlohmann::json::parse(CurveAmountJson(locked).dump())))==CurveAmountJson(locked),
        "Locked reference persistence lost owned points.");
    SceneTone scene;
    scene.curve=points;scene.curveAmount=locked;
    const auto sceneSaved=SerializeSceneTone(scene);
    Check(SerializeSceneTone(ReadSceneTone(nlohmann::json::parse(sceneSaved.dump())))==sceneSaved,
        "Direct luminance persistence lost slider points or amount references.");

    auto toneOperation=MakeGraphOperation(GraphOperationKind::ToneCurve);
    auto toneRecipe=ReadGraphOperation(toneOperation);
    RawPointCurveComponent component;
    for (const auto& p:points) component.points.push_back({p.x,p.y,1,p.incoming,p.outgoing,p.tonal});
    StorePointCurveComponentInFinishToneJson(toneRecipe.finishTone.layerJson,RawPointCurveChannel::Composite,component);
    toneRecipe.finishTone.layerJson["curveAmounts"]["composite"]=CurveAmountJson(locked);
    WriteGraphOperation(toneOperation,toneRecipe);
    const auto toneSaved=SerializeGraphOperation(toneOperation);
    auto toneReopened=DeserializeGraphOperation(nlohmann::json::parse(toneSaved.dump()));
    Check(SerializeGraphOperation(toneReopened)==toneSaved,
        "Component curve operation persistence changed its slider metadata.");
    Check(SameCurvePoints(RawPointCurveBezierPoints(PointCurveComponentFromFinishToneJson(
        toneReopened.parameters,RawPointCurveChannel::Composite)),points),
        "Component conversion lost owned-point restoration metadata.");

    auto localRecipe=MakeDefaultRecipe({});
    auto localPoints=RawLocalRangeBezierPoints(localRecipe.localRange);
    const float localMid=localPoints[1].x;
    Check(SetTonalCurveSlider(localPoints,2,localMid,.25f,12)==Result::Changed,
        "Local EV's default midpoint could not be owned without an extra point.");
    localRecipe.localRange.points.clear();
    for (const auto& p:localPoints) localRecipe.localRange.points.push_back({
        localRecipe.localRange.minEv+p.x*(localRecipe.localRange.maxEv-localRecipe.localRange.minEv),
        -4+8*p.y,p.incoming,p.outgoing,p.tonal});
    localRecipe.localRange.enabled=true;
    localRecipe.localRange.curveAmount={true,.5f,localPoints};
    const auto localSaved=SerializeRecipe(localRecipe);
    const auto localReopened=DeserializeRecipe(nlohmann::json::parse(localSaved.dump()));
    Check(SerializeRecipe(localReopened)==localSaved,"Local EV persistence changed its slider or lock state.");
    Check(SameCurvePoints(RawLocalRangeBezierPoints(localReopened.localRange),localPoints),
        "Local EV EV/normalized conversion changed ownership or the normalized original baseline.");

    RawLayerStackDocument document;
    auto initial=document.State();SeedOperationFixture(initial.background);
    std::string error;
    Check(document.Apply(initial,error),error);
    const auto before=SerializeRawLayerStack(document.State());
    auto candidate=document.State();
    const auto localId=FindRawOperation(candidate.background,GraphOperationKind::LocalEv)->instanceUuid;
    const auto toneId=FindRawOperation(candidate.background,GraphOperationKind::ToneCurve)->instanceUuid;
    Check(WriteRawLayerOperation(candidate.background,localId,localRecipe,error),error);
    Check(WriteRawLayerOperation(candidate.background,toneId,toneRecipe,error),error);
    document.BeginGesture();
    Check(document.Apply(candidate,error),error);document.EndGesture();
    const auto after=SerializeRawLayerStack(document.State());
    Check(document.Undo(&error) && SerializeRawLayerStack(document.State())==before,
        "Undo did not restore the curves before the slider gesture.");
    Check(document.Redo(&error) && SerializeRawLayerStack(document.State())==after,
        "Redo did not restore both nodes' slider points and ownership.");
    RawLayerStackDocument reopened;
    Check(reopened.Load(nlohmann::json::parse(after.dump()),error),error);
    Check(SerializeRawLayerStack(reopened.State())==after,
        "Serialized project reopen changed tagged Local EV/Tone nodes or history-visible parameters.");
    const auto reopenedLocal=ReadRawLayerOperation(reopened.State().background,localId);
    const auto reopenedTone=ReadRawLayerOperation(reopened.State().background,toneId);
    Check(SameCurvePoints(RawLocalRangeBezierPoints(reopenedLocal.localRange),localPoints) &&
        SameCurvePoints(RawPointCurveBezierPoints(PointCurveComponentFromFinishToneJson(
            reopenedTone.finishTone.layerJson,RawPointCurveChannel::Composite)),points),
        "Project reopen did not restore the actual tagged point collections.");
    std::cout << "Tonal slider ownership, zero cleanup, collision restoration, amount, persistence and history checks passed.\n";
}
void CheckPreparedGraphTransactions() {
    RawLayerStackDocument document, other;
    std::string error;
    const auto original = SerializeRawLayerStack(document.State());
    const auto projection = BuildRawProjectGraph(document.State());
    auto edited = projection.graph.CloneForAnalysis();
    for (auto& node : edited.GetNodes())
        if (node.role == Stack::GraphModel::NodeRole::LayerResult) node.position.x += 42.f;
    auto prepared = document.PrepareGraphEdit(projection,edited,
        Stack::GraphModel::LayerParameterRecords(edited),kRawBackgroundId,error);
    Check(prepared.has_value(),error);
    // The draft remains caller-owned. Mutating it after preparation cannot
    // change the opaque validated candidate installed by the document.
    edited.EditNodes().clear();
    Check(document.Commit(std::move(*prepared),error),error);
    const auto committed = SerializeRawLayerStack(document.State());
    Check(committed != original && !document.State().background.graph.GetNodes().empty(),
        "Prepared graph retained mutable caller storage or lost its edit.");
    Check(document.Undo(&error) && SerializeRawLayerStack(document.State()) == original,
        "Prepared graph commit did not retain one exact undo state.");
    Check(document.Redo(&error) && SerializeRawLayerStack(document.State()) == committed,
        "Prepared graph commit did not retain its exact redo state.");

    auto wrongOwner = document.Prepare(document.State(),error);
    Check(wrongOwner.has_value(),error);
    const auto otherBefore = SerializeRawLayerStack(other.State());
    Check(!other.Commit(std::move(*wrongOwner),error) &&
        SerializeRawLayerStack(other.State()) == otherBefore,
        "A prepared edit was accepted by another document.");
    Check(!document.Commit(std::move(*wrongOwner),error) &&
        SerializeRawLayerStack(document.State()) == committed,
        "A consumed prepared edit was accepted again.");

    auto stale = document.Prepare(document.State(),error);
    Check(stale.has_value(),error);
    auto newer = document.State(); newer.background.name = "Newer edit";
    Check(document.Apply(std::move(newer),error),error);
    const auto newerSaved = SerializeRawLayerStack(document.State());
    Check(!document.Commit(std::move(*stale),error) &&
        SerializeRawLayerStack(document.State()) == newerSaved,
        "A prepared edit replaced a newer document revision.");

    EditorNodeGraph::Graph composition;
    const int source = composition.AddRawDevelopmentNode({},{})->id;
    auto dependency = document.Prepare(document.State(),error,&composition,source,7);
    Check(dependency.has_value(),error);
    Check(!document.Commit(std::move(*dependency),error,true,false,8,&composition),
        "A prepared edit accepted a changed dependency revision.");
    dependency = document.Prepare(document.State(),error,&composition,source,7);
    Check(dependency.has_value(),error);
    composition.AddRawDevelopmentNode({},{});
    Check(!document.Commit(std::move(*dependency),error,true,false,7,&composition) &&
        SerializeRawLayerStack(document.State()) == newerSaved,
        "A prepared edit accepted changed composition topology.");

    auto invalid = document.State(); invalid.background.graph.EditNodes().clear();
    Check(!document.Prepare(std::move(invalid),error).has_value() &&
        SerializeRawLayerStack(document.State()) == newerSaved,
        "Invalid preparation changed the live document.");
    std::cout << "Prepared RAW graph ownership, immutability, stale dependencies, history and rejection checks passed.\n";
}
} // namespace

void TestDirectCurvesAndCapture() {
    using namespace Stack::RawRecipe;
    SceneTone tone;
    Check(!IsSceneToneActive(tone),"Default direct tone must bypass.");
    for(float ev:{-30.f,-16.f,-2.f,0.f,12.f,16.f,30.f})
        Check(std::abs(EvaluateSceneToneEv(tone,ev)-ev)<.001f,"Direct neutral curve is not identity.");
    tone.curve={{0,0,{1},{1}},{.3f,.15f,{1},{1}},{.65f,.9f,{1},{1}},{1,1,{1},{1}}};
    for(const auto& p:tone.curve)Check(std::abs(EvaluateSceneToneEv(tone,-16+32*p.x)-(-16+32*p.y))<.001f,"A tone point does not pin its output.");
    for(int i=0;i<=200;++i) {float x=float(i)/200;const float y=EvaluateRawBezierCurve(tone.curve,x);Check(y>=0 && y<=1,"Smooth curve overshot its bounds.");}
    tone.curveAmount={true,.35f,tone.curve};tone.curve=AttenuateCurve(tone.curveAmount.reference,.35f,true);
    const auto saved=SerializeSceneTone(tone);Check(SerializeSceneTone(ReadSceneTone(saved))==saved,"Locked tone reference did not survive save/reopen.");
    const auto neutral=AttenuateCurve(tone.curveAmount.reference,0,true);
    for(float x:{0.f,.2f,.5f,.8f,1.f})Check(std::abs(EvaluateRawBezierCurve(neutral,x)-x)<1e-5f,"Tone 0% must be diagonal.");
    auto recipe=MakeDefaultRecipe({});recipe.localRange.curveAmount={true,.4f,RawLocalRangeBezierPoints(recipe.localRange)};
    auto reopened=DeserializeRecipe(SerializeRecipe(recipe));
    Check(CurveAmountJson(recipe.localRange.curveAmount)==CurveAmountJson(reopened.localRange.curveAmount),"Local EV lock did not persist.");
    auto flat=AttenuateCurve(tone.curveAmount.reference,0,false);
    for(float x:{0.f,.2f,.5f,.8f,1.f})Check(std::abs(EvaluateRawBezierCurve(flat,x)-.5f)<1e-5f,"Local EV 0% must be zero delta.");
    // Use a small explicit set of pixels, checking orientation, intervening
    // samples, HDR values and invalid alpha without a generated image suite.
    using namespace Stack::RawAssetCapture;
    const std::vector<float> pixels={.1f,.2f,.3f,1, 2,3,4,1, .4f,.5f,.6f,0, .7f,.8f,.9f,1};
    Selection selection;selection.shape=Shape::Stroke;selection.path={{0,0},{1,0},{1,1}};
    auto capture=Capture(pixels,2,2,Raw::RawWorkingSpace::LinearSrgbD65,selection,{});
    Check(capture.kind==Stack::Assets::Kind::DataRange && capture.samples->width==2,"Stroke traversal or alpha exclusion failed.");
    Check(capture.samples->values[0]>.5f && capture.samples->values[1]<.5f,"Stroke sample orientation is wrong.");
    Check(capture.samples->values[12]>1,"HDR samples were clipped.");
    capture.id="stroke";capture.name="Stroke";Stack::Assets::Library restored;std::string error;
    Check(Stack::Assets::DeserializeLibrary(Stack::Assets::SerializeLibrary({capture}),restored,error),"Stroke sample persistence failed.");
    Check(restored.front().samples->values==capture.samples->values,"Saved samples changed.");
    selection.output=Output::SpatialMask;
    auto mask=Capture(pixels,2,2,Raw::RawWorkingSpace::LinearSrgbD65,selection,{});
    Check(mask.kind==Stack::Assets::Kind::Mask && mask.data->channels==1 && mask.data->Valid(true),"Spatial mask is not coverage.");
    Stack::Project::RawLayerStackState stack;
    auto* rawMask=stack.background.graph.AddCustomMaskNode(Stack::Project::CapturedMaskPayload(*mask.data),{0,300});
    rawMask->rawCoverage={{"canvas","project"}};
    const auto maskUuid=rawMask->instanceUuid;
    Stack::Project::RegisterRawMaskOutputs(stack,Stack::Project::kRawBackgroundId);
    const auto publication=std::find_if(stack.background.maskOutputs.begin(),stack.background.maskOutputs.end(),[&](const auto& item){return item.nodeUuid==maskUuid;});
    Check(publication!=stack.background.maskOutputs.end(),"Captured mask was not published.");
    const auto* local=Stack::Project::FindRawOperation(stack.background,GraphOperationKind::LocalEv);
    Check(Stack::Project::SetRawOperationMask(stack,Stack::Project::kRawBackgroundId,local->instanceUuid,
        Stack::Project::RawMaskReference{Stack::Project::kRawBackgroundId,publication->id},error),"Captured mask cannot attach to an operation.");
    Check(Stack::Project::ValidateRawLayerStack(stack,error),"Captured mask invalidated its layer graph.");
    std::cout<<"Direct curve identity, point outputs, amount references and native stroke capture checks passed.\n";
}

void TestSceneToneAndDetailMath() {
    using namespace Stack::RawRecipe;
    SceneTone tone; tone.direct=false;
    Check(!IsSceneToneActive(tone), "Neutral luminance tone is active.");
    for (float ev : {-40.f, -16.f, -4.f, 0.f, 4.f, 16.f, 40.f})
        Check(std::abs(EvaluateSceneToneEv(tone, ev)-ev)<1e-5f, "Neutral tone is not identity.");
    tone.contrast=1.5f;
    const float freeTail=EvaluateSceneToneEv(tone,8)-8;
    Check(std::abs(freeTail-1)<1e-5f, "Unprotected transition integral is incorrect.");
    for (float protection : {0.f,.25f,.5f,.75f,1.f}) {
        tone.outerProtection=protection;
        Check(std::abs(EvaluateSceneToneEv(tone,0))<1e-6f, "Middle gray moved.");
        Check(std::abs(EvaluateSceneToneEv(tone,8)-8-(1-protection)*freeTail)<1e-5f,
            "Outer-tone protection is not an EV blend.");
        Check(std::abs(EvaluateSceneToneContrast(tone,0)-1.5f)<1e-5f, "Feasible central contrast changed with protection.");
    }
    for(float contrast : {.05f,1.5f,4.f}) for(float width : {.25f,2.f,12.f}) {
        tone.contrast=contrast; tone.transitionEv=width;
        for(float ev=-20;ev<=20;ev+=.013f) {
            Check(EvaluateSceneToneContrast(tone,ev)>=kSceneToneSlopeFloor-1e-5f,"Tone slope reversed or collapsed.");
            const float slope=(EvaluateSceneToneEv(tone,ev+.001f)-EvaluateSceneToneEv(tone,ev-.001f))/.002f;
            Check(std::abs(slope-EvaluateSceneToneContrast(tone,ev))<.006f,"Curve and Contrast disagree.");
        }
    }
    tone=SceneTone{}; tone.direct=false; tone.contrast=1.4f; tone.pivotEv=-2; tone.outerProtection=1;
    tone.points[3].contrast=1.2f;
    auto baseline=tone; baseline.contrast=1;
    for(float ev : {-20.f,10.f}) Check(std::abs(EvaluateSceneToneEv(tone,ev)-EvaluateSceneToneEv(baseline,ev))<1e-5f,
        "Protection erased the existing tone curve.");
    Check(SerializeSceneTone(ReadSceneTone(SerializeSceneTone(tone)))==SerializeSceneTone(tone),"Tone round-trip changed its model.");
    DetailContrast detail;
    Check(!IsDetailContrastActive(detail),"Neutral detail is active.");
    AdjustDetailMacro(detail,3,5,.4f);
    detail.targetEnabled=true;detail.evScaleResidual[8*kDetailBands+4]=.2f;
    Check(std::abs(EvaluateDetailGain(detail,4,0)-1.6f)<1e-6f,"Detail field and macro do not combine.");
    Check(EvaluateDetailGain(detail,4,12)==1,"Detail targeting leaks outside its EV range.");
    Check(SerializeDetailContrast(ReadDetailContrast(SerializeDetailContrast(detail)))==SerializeDetailContrast(detail),"Detail round-trip changed its model.");
    detail.targetEnabled=false;
    detail.evScaleResidual[8*kDetailBands+4]=-3;
    detail.evScaleResidual[9*kDetailBands+4]=2;
    const auto map=BuildDetailGainMap(detail);
    const float interpolated=std::clamp((map[8*kDetailBands+4]+map[9*kDetailBands+4])*.5f,0.f,3.f);
    Check(std::abs(interpolated-EvaluateDetailGain(detail,4,.75f))<1e-6f,"Compiled detail field clips before interpolation.");
    std::cout << "Luminance integration, protection, monotonicity and detail response checks passed.\n";
}

void TestRawLayerStackContracts() {
    CheckPreparedGraphTransactions();
    CheckTonalCurvePoints();
    TestDirectCurvesAndCapture();
    TestSceneToneAndDetailMath();
    CheckNodeEditorTransactions();
    CheckNeutralMaskedAdjustments();
    CheckProjectCanvas();
    CheckViewportContentIdentity();
    CheckCompoundReferenceCycle();
    CheckCompoundOutputCompleteness();
    CheckNativeDecomposition();
    CheckMergedSourceHistory();
    CheckViewportCoordinates();
    const auto offers = Stack::GraphModel::QueryAvailableNodes(RawLayerStackState{}.background.graph);
    Check(std::any_of(offers.begin(),offers.end(),[](const auto& entry) { return entry.kind == EditorNodeGraph::NodeKind::Compound; }),
        "The node browser lost shipped compounds.");
    using namespace Stack::RawRecipe;
    using Role = Stack::GraphModel::NodeRole;
    RawLayerStackDocument document;
    RawLayerStackState state;
    const auto first = AddRawAdjustmentLayer(state, "Lift shadows");
    const auto second = AddRawAdjustmentLayer(state, "Soften highlights");
    SeedOperationFixture(state.background);
    for (auto& layer : state.layers) SeedOperationFixture(layer);
    std::string error;
    auto* creative = FindRawAdjustmentLayer(state, first);
    FindRawOperation(*creative, GraphOperationKind::Exposure)->rawOperation.parameters["ev"] = 1.5f;
    const auto toolMask = AddRawGeneratedMask(state, first, EditorNodeGraph::MaskGeneratorKind::RadialGradient, "Face");
    const auto layerMask = AddRawGeneratedMask(state, first, EditorNodeGraph::MaskGeneratorKind::LinearGradient, "Fade");
    const auto attach = [&](RawLayerStackState& target, const std::string& id, GraphOperationKind kind, RawMaskReference mask) {
        const auto* operation = FindRawOperation(*FindRawAdjustmentLayer(target, id), kind);
        Check(SetRawOperationMask(target, id, operation->instanceUuid, mask, error), error);
    };
    SceneTone tone; tone.direct=false; tone.contrast = 1.3f; tone.outerProtection = .6f;
    auto authoredTone=Stack::RawRecipe::ReadGraphOperation(FindRawOperation(*creative, GraphOperationKind::ToneCurve)->rawOperation);
    authoredTone.finishTone.layerJson["curveMode"]="contrast";
    authoredTone.finishTone.layerJson["luminanceTone"]=SerializeSceneTone(tone);
    Stack::RawRecipe::WriteGraphOperation(FindRawOperation(*creative, GraphOperationKind::ToneCurve)->rawOperation,authoredTone);
    DetailContrast detail; detail.scaleGains[4] = 1.25f; detail.evScaleResidual[63] = -.2f;
    FindRawOperation(*creative, GraphOperationKind::DetailContrast)->rawOperation.parameters = SerializeDetailContrast(detail);
    attach(state, first, GraphOperationKind::ToneCurve, toolMask);
    attach(state, first, GraphOperationKind::LocalEv, toolMask);
    attach(state, first, GraphOperationKind::DetailContrast, toolMask);
    attach(state, second, GraphOperationKind::Exposure, toolMask);
    creative->layerMask = layerMask;
    Check(document.Apply(state, error), error);
    const auto original = SerializeRawLayerStack(document.State());
    RawLayerStackDocument reopened;
    Check(reopened.Load(nlohmann::json::parse(original.dump()), error), error);
    Check(SerializeRawLayerStack(reopened.State()) == original, "Reopen changed authored graphs.");
    auto reordered = reopened.State();
    Check(MoveRawAdjustmentLayer(reordered, first, 1, error), error);
    const auto* reorderedTone = FindRawOperation(reordered.layers.back(), GraphOperationKind::ToneCurve);
    Check(GetRawOperationMask(reordered, reordered.layers.back(), reorderedTone->instanceUuid)->outputId == toolMask.outputId,
        "Reorder changed mask identity.");
    Check(reopened.Apply(reordered, error), error);
    Check(reopened.Undo() && SerializeRawLayerStack(reopened.State()) == original, "Undo did not restore graph topology.");
    Check(reopened.Redo() && reopened.State().layers.back().id == first, "Reorder redo failed.");
    Check(document.State().layers.front().id == first, "Another document changed this document.");
    Check(reopened.Undo(), "Second undo failed.");
    reopened.BeginGesture();
    auto candidate = reopened.State(); candidate.layers.front().opacity = .2f;
    Check(reopened.Apply(candidate, error), error);
    Check(reopened.CancelGesture() && reopened.CanRedo(), "Cancel lost the redo branch.");
    reopened.BeginGesture();
    candidate = reopened.State(); candidate.layers.front().opacity = .3f;
    Check(reopened.Apply(candidate, error), error);
    candidate.layers.front().opacity = .6f;
    Check(reopened.Apply(candidate, error), error);
    reopened.EndGesture();
    Check(!reopened.CanRedo(), "Committed edit retained stale redo.");
    Check(reopened.Undo() && reopened.State().layers.front().opacity == 1.f, "Gesture split history.");

    auto feedback = state;
    auto* owner = FindRawAdjustmentLayer(feedback, first);
    const auto input = RawLayerEndpoint(*owner, *FindRawRole(*owner, Role::CurrentImage));
    const auto sampled = SampleMask(feedback, first, input);
    attach(feedback, first, GraphOperationKind::Exposure, sampled);
    Check(ValidateRawLayerStack(feedback, error), error);
    const auto exposure = RawLayerEndpoint(*owner, *FindRawOperation(*owner, GraphOperationKind::Exposure));
    for (auto& node : owner->graph.GetNodes())
        if (node.role == Role::Reference && node.reference == input) node.reference = exposure;
    Check(!ValidateRawLayerStack(feedback, error), "Masked-result feedback was accepted.");
    const auto unchanged = SerializeRawLayerStack(document.State());
    Check(!document.Apply(feedback, error) && SerializeRawLayerStack(document.State()) == unchanged,
        "Rejected edit changed the document.");
    owner->enabled = false;
    Check(!ValidateRawLayerStack(feedback, error), "Disabled feedback was accepted.");

    auto cross = state;
    const auto* producer = FindRawAdjustmentLayer(cross, first);
    const auto crossMask = SampleMask(cross, second, RawLayerEndpoint(*producer, *FindRawRole(*producer, Role::LayerResult)));
    attach(cross, second, GraphOperationKind::ToneCurve, crossMask);
    Check(ValidateRawLayerStack(cross, error), error);
    const auto beforeMove = SerializeRawLayerStack(cross);
    Check(!MoveRawAdjustmentLayer(cross, second, 0, error) && SerializeRawLayerStack(cross) == beforeMove,
        "Reorder introduced feedback or changed a rejected candidate.");
    auto independent = state;
    const auto laterMask = AddRawGeneratedMask(independent, second, EditorNodeGraph::MaskGeneratorKind::Solid, "Independent");
    attach(independent, first, GraphOperationKind::Exposure, laterMask);
    Check(ValidateRawLayerStack(independent, error), "Independent later generator rejected: " + error);

    EditorNodeGraph::Graph composition;
    const int outerSource = composition.AddRawDevelopmentNode({}, {})->id;
    const std::string outerUuid = composition.FindNode(outerSource)->instanceUuid;
    auto outerFeedback = state;
    const auto outerSample = SampleMask(outerFeedback, first, {"project", outerUuid, "imageOut"});
    attach(outerFeedback, first, GraphOperationKind::Exposure, outerSample);
    Check(!ValidateRawLayerStack(outerFeedback, error, &composition, outerSource),
        "A feedback loop through outer composition was accepted.");
    const int generator = composition.AddImageGeneratorNode(EditorNodeGraph::ImageGeneratorKind::SolidColor, {})->id;
    for (auto& node : FindRawAdjustmentLayer(outerFeedback, first)->graph.GetNodes())
        if (node.role == Role::Reference && node.reference.graphId == "project")
            node.reference.nodeUuid = composition.FindNode(generator)->instanceUuid;
    const bool outerAccepted=ValidateRawLayerStack(outerFeedback,error,&composition,outerSource);
    Check(outerAccepted,
        "An independent outer generator was rejected: " + error);
    auto authoredSource = MakeDefaultRecipe({});
    authoredSource.preToneExposureEv = 2.f;
    authoredSource.whiteBalance.hasMultipliers = true;
    authoredSource.whiteBalance.multipliers = {2.f,1.f,1.5f};
    const auto savedSource = SerializeWorkspaceSourceRecipe(authoredSource);
    Check(IsCanonicalRawRecipeDocument(savedSource) && IsCanonicalWorkspaceSourceRecipeDocument(savedSource),
        "The source-settings writer and current-format validator disagree.");
    auto duplicateCreative = savedSource;
    duplicateCreative["exposureEv"] = 2.f;
    Check(!IsCanonicalRawRecipeDocument(duplicateCreative),"A source record accepted duplicate creative settings.");
    Check(!savedSource.contains("exposureEv") && !savedSource.contains("finishTone") && !savedSource.contains("localRange"),
        "The source record retained duplicate creative parameters.");
    const auto sourceRestored = DeserializeRecipe(savedSource);
    Check(sourceRestored.preToneExposureEv == 0.f && sourceRestored.whiteBalance.multipliers == authoredSource.whiteBalance.multipliers,
        "Source-only persistence lost camera preparation or restored creative edits.");
    {
        RawLayerStackDocument combined;
        auto source = MakeDefaultRecipe("immutable-source.dng");
        const auto initialSource = SerializeWorkspaceSourceRecipe(source);
        const auto initialGraph = SerializeRawLayerStack(combined.State());
        auto edited = combined.State();
        SeedOperationFixture(edited.background);
        FindRawOperation(edited.background,GraphOperationKind::Exposure)->rawOperation.parameters["ev"] = 1.f;
        Check(combined.ApplySourceEdit(edited,source,authoredSource,error),error);
        Check(source.source.sourcePath == "immutable-source.dng","A settings edit replaced the source asset.");
        Check(combined.Undo(&error,nullptr,0,&source) && SerializeWorkspaceSourceRecipe(source) == initialSource &&
            SerializeRawLayerStack(combined.State()) == initialGraph,"Undo split the source and creative settings.");
        Check(combined.Redo(&error,nullptr,0,&source) && source.whiteBalance.multipliers == authoredSource.whiteBalance.multipliers &&
            FindRawOperation(combined.State().background,GraphOperationKind::Exposure)->rawOperation.parameters.at("ev") == 1.f,
            "Redo split the source and creative settings.");
        edited = combined.State(); edited.background.id.clear();
        const auto beforeReject = SerializeWorkspaceSourceRecipe(source);
        const auto revision = combined.Revision();
        Check(!combined.ApplySourceEdit(edited,source,MakeDefaultRecipe({}),error) && combined.Revision() == revision &&
            SerializeWorkspaceSourceRecipe(source) == beforeReject,"Rejected combined edit changed source settings or history.");
    }

    auto serial = state.background.graph;
    const auto exposureId = FindRawOperation(state.background,GraphOperationKind::Exposure)->id;
    const auto localIdBeforeMove = FindRawOperation(state.background,GraphOperationKind::LocalEv)->id;
    auto moved = Stack::GraphModel::ProposeSerialMove(serial,serial.GetStructureRevision(),exposureId,true);
    Check(Stack::GraphModel::ApplyEdit(serial,serial.GetStructureRevision(),std::move(moved),error),error);
    Check(serial.FindInputLink(exposureId,"imageIn")->fromNodeId == localIdBeforeMove,"Serial move changed layout without changing execution.");
    auto curves = state;
    const auto afterCurve = FindRawOperation(curves.background,GraphOperationKind::ToneCurve)->instanceUuid;
    const auto curveUuid = AddRawLocalAdjustment(curves,kRawBackgroundId,afterCurve,
        EditorNodeGraph::MaskGeneratorKind::LinearGradient,error).operationUuid;
    Check(!curveUuid.empty() && ValidateRawLayerStack(curves,error),"Masked sequential curve failed: " + error);
    const auto* addedCurve = FindRawOperation(curves.background,GraphOperationKind::ToneCurve,curveUuid);
    Check(addedCurve && curves.background.graph.FindInputLink(addedCurve->id,"maskIn"),"The local curve has no authored mask attachment.");
    auto published = state;
    const auto* publishedExposure = FindRawOperation(published.background,GraphOperationKind::Exposure);
    Check(PublishRawLayerResult(published,RawLayerEndpoint(published.background,*publishedExposure),first,error),error);
    Check(ValidateRawLayerStack(published,error),error);
    auto typed = state;
    const auto* valueNode = typed.background.graph.AddValueNode(Stack::NodeMath::MakeUniformScalar(1.25),{0,700});
    const auto valueEndpoint = RawLayerEndpoint(typed.background,*valueNode,"valueOut");
    Check(PublishRawLayerResult(typed,valueEndpoint,first,error),error);
    auto* typedLayer = FindRawAdjustmentLayer(typed,first);
    const auto typedReferenceId = typedLayer->graph.GetNodes().back().id;
    Check(typedLayer->graph.TryConnectSockets(typedReferenceId,"imageOut",
        FindRawOperation(*typedLayer,GraphOperationKind::Exposure)->id,"param:ev",&error),error);
    Check(reopened.Load(SerializeRawLayerStack(typed),error),error);
    Check(ValidateRawLayerStack(reopened.State(),error),error);
    // Cross-graph semantic checks must resolve the producer rather than trust
    // the reference box's declared image socket.
    auto invalidReference = state;
    auto* producerLayer = FindRawAdjustmentLayer(invalidReference,second);
    const int viewId = producerLayer->graph.AddLayerNode(LayerType::ViewTransform,0,{0,600})->id;
    const auto currentId = FindRawRole(*producerLayer,Role::OriginalImage)->id;
    Check(producerLayer->graph.TryConnectSockets(currentId,"imageOut",viewId,"imageIn",&error),error);
    const auto displayEndpoint = RawLayerEndpoint(*producerLayer,*producerLayer->graph.FindNode(viewId));
    auto* consumerLayer = FindRawAdjustmentLayer(invalidReference,first);
    const int referenceId = consumerLayer->graph.AddImageNode({}, {0,600})->id;
    consumerLayer->graph.FindNode(referenceId)->role = Role::Reference;
    consumerLayer->graph.FindNode(referenceId)->reference = displayEndpoint;
    const int consumerId = FindRawOperation(*consumerLayer,GraphOperationKind::Exposure)->id;
    Check(consumerLayer->graph.TryConnectSockets(referenceId,"imageOut",consumerId,"imageIn",&error),error);
    const bool acceptedDisplayReference = ValidateRawLayerStack(invalidReference,error);
    Check(!acceptedDisplayReference && error.find("Scene-linear") != std::string::npos,
        "A display-referred cross-graph image entered a scene-linear operation: " + error);

    auto unfinished = state;
    owner = FindRawAdjustmentLayer(unfinished, first);
    owner->graph.RemoveNode(FindRawMaskOutput(unfinished, toolMask)->nodeId);
    Check(reopened.Load(SerializeRawLayerStack(unfinished), error), error);
    const auto* restoredTone = FindRawOperation(reopened.State().layers.front(), GraphOperationKind::ToneCurve);
    Check(reopened.State().layers.front().graph.FindInputLink(restoredTone->id, "maskIn") != nullptr,
        "Broken attached mask became unmasked on reopen.");
    auto malformed = original;
    malformed["layers"][0]["graph"]["nodeGraph"]["links"][0]["fromNodeId"] = 999999;
    const auto beforeBadLoad = SerializeRawLayerStack(reopened.State());
    Check(!reopened.Load(malformed, error) && SerializeRawLayerStack(reopened.State()) == beforeBadLoad,
        "Malformed load changed the live document.");
    auto repeated = state;
    owner = FindRawAdjustmentLayer(repeated, first);
    const auto* originalExposure = FindRawOperation(*owner, GraphOperationKind::Exposure);
    const std::string firstUuid = originalExposure->instanceUuid;
    auto* added = owner->graph.AddRawOperationNode(GraphOperationKind::Exposure, {100, 400});
    added->rawOperation.parameters["ev"] = -2.f;
    Check(added->instanceUuid != firstUuid && FindRawOperation(*owner, GraphOperationKind::Exposure, firstUuid)->rawOperation.parameters["ev"] == 1.5f,
        "Repeated instances share settings or identity.");
    Check(ValidateRawLayerStack(repeated, error), "Independent unfinished operation rejected: " + error);
    const int sourceId = FindRawRole(*owner, Role::OriginalImage)->id;
    owner->graph.RemoveNode(sourceId);
    Check(owner->graph.FindNode(sourceId) != nullptr, "Protected source was deleted.");
    auto covered = state;
    auto* coveredLayer = FindRawAdjustmentLayer(covered, first);
    const auto localUuid = FindRawOperation(*coveredLayer, GraphOperationKind::LocalEv)->instanceUuid;
    auto localRecipe = ReadRawLayerOperation(*coveredLayer, localUuid);
    RawGradientEvAdjustment gradient; gradient.mask.id = "coverage-gradient";
    gradient.mask.centerU = .27f;
    localRecipe.evGradients.push_back(gradient);
    RawZoneArea area; area.id = "coverage-area"; area.sourceAspect = 1.5f;
    RawZoneBrushStroke stroke; stroke.path.push_back({.5f, .5f}); area.strokes.push_back(stroke);
    localRecipe.localRange.areas.push_back(area);
    Check(WriteRawLayerOperation(*coveredLayer, localUuid, localRecipe, error), error);
    Check(ValidateRawLayerStack(covered, error), "Input-derived coverage rejected: " + error);
    const auto* coveredOp = FindRawOperation(*coveredLayer, GraphOperationKind::LocalEv);
    Check(coveredOp->rawOperation.parameters.at("evGradients")[0].at("mask").size() == 1 &&
        !coveredOp->rawOperation.parameters.at("localRange").at("areas")[0].contains("strokes"),
        "Local EV retained duplicate authored coverage.");
    auto hydrated = ReadRawLayerOperation(*coveredLayer, localUuid);
    Check(hydrated.evGradients[0].mask.centerU == .27f && hydrated.localRange.areas[0].strokes.size() == 1,
        "Ordinary controls did not bind to graph-owned coverage.");
    auto currentState = covered;
    SampleMask(currentState, second, RawLayerEndpoint(*coveredLayer, *coveredOp, "inputImageOut"));
    auto currentPayload = SerializeRawLayerStack(currentState);
    RawLayerStackState currentReopened;
    Check(DeserializeRawLayerStack(currentPayload, currentReopened, error), error);
    bool retainedReference = false;
    for (const auto& node : FindRawAdjustmentLayer(currentReopened, second)->graph.GetNodes())
        if (node.reference.nodeUuid == localUuid) {
            Check(node.reference.portId == "inputImageOut", "Current input reference changed on reopen.");
            retainedReference = true;
        }
    Check(retainedReference && ReadRawLayerOperation(*FindRawAdjustmentLayer(currentReopened, first), localUuid)
        .localRange.areas[0].strokes.size() == 1, "Current reopen lost its cross-layer input or painted coverage.");
    auto obsolete = currentPayload;
    for (auto& node : obsolete["layers"][0]["graph"]["nodeGraph"]["nodes"])
        if (node.at("id") == coveredOp->id) {
            node["definition"]["version"] = "1.0.0";
            node["definition"]["contentHash"] = "sha256:c57faa3c23a155f4cb602cb1db173b8019201a86023d42406e63bee1ea8e5016";
        }
    const auto beforeObsolete = SerializeRawLayerStack(currentReopened);
    Check(!DeserializeRawLayerStack(obsolete, currentReopened, error) &&
        SerializeRawLayerStack(currentReopened) == beforeObsolete,
        "An obsolete RAW definition was migrated or changed the current document.");
    const auto* coverageLink = coveredLayer->graph.FindInputLink(coveredOp->id, "gradient:coverage-gradient");
    Check(coverageLink != nullptr, "Gradient mask was not exposed as a graph input.");
    const int generatorId = coverageLink->fromNodeId, localId = coveredOp->id;
    Check(!coveredLayer->graph.TryConnectSockets(localId, "imageOut", generatorId,
        EditorNodeGraph::kMatchExtentInputSocketId, &error), "Finished Local EV feedback was accepted.");
    auto processedCoverage = covered;
    auto* processedOwner = FindRawAdjustmentLayer(processedCoverage,first);
    const int invertId = processedOwner->graph.AddMaskUtilityNode(EditorNodeGraph::MaskUtilityKind::Invert,{0,500})->id;
    Check(processedOwner->graph.TryConnectSockets(generatorId,"maskOut",invertId,"maskIn",&error),error);
    Check(processedOwner->graph.TryConnectSockets(invertId,"maskOut",localId,"gradient:coverage-gradient",&error),error);
    auto processedRecipe = ReadRawLayerOperation(*processedOwner,localUuid);
    Check(processedRecipe.evGradients[0].mask.centerU == .27f,"Processed gradient lost its authored geometry owner.");
    processedRecipe.evGradients[0].mask.centerU = .41f;
    Check(WriteRawLayerOperation(*processedOwner,localUuid,processedRecipe,error),error);
    Check(processedOwner->graph.FindInputLink(localId,"gradient:coverage-gradient")->fromNodeId == invertId &&
        ReadRawLayerOperation(*processedOwner,localUuid).evGradients[0].mask.centerU == .41f,
        "Editing a processed mask replaced its branch or failed to update its generator.");
    coveredLayer->graph.RemoveLink(generatorId, "maskOut", localId, "gradient:coverage-gradient");
    Check(WriteRawLayerOperation(*coveredLayer, localUuid, hydrated, error), error);
    Check(!coveredLayer->graph.FindInputLink(localId, "gradient:coverage-gradient"), "Editing a curve recreated deleted coverage.");
    const auto* exposureNode = FindRawOperation(*coveredLayer, GraphOperationKind::Exposure);
    const Stack::Timeline::AnimatableParameterTarget animated{exposureNode->id, "ev", coveredLayer->id, exposureNode->instanceUuid};
    Stack::Timeline::SetOrReplaceKeyframe(coveredLayer->animation, animated, 0, 0);
    Stack::Timeline::SetOrReplaceKeyframe(coveredLayer->animation, animated, 10, 2);
    auto otherTarget = animated; otherTarget.graphId = second;
    Check(!Stack::Timeline::SameTarget(animated, otherTarget), "Animation targets alias across graphs.");
    Check(reopened.Load(SerializeRawLayerStack(covered), error), error);
    const auto* track = Stack::Timeline::FindTimelineTrack(reopened.State().layers.front().animation, animated);
    float animatedValue = 0;
    Check(track && Stack::Timeline::EvaluateTimelineTrackAtFrame(*track, 5, animatedValue) && animatedValue == 1,
        "Graph-qualified animation did not survive save/reopen.");
    std::cout << "RAW graph ownership, instances, references, persistence, history and dependency checks passed.\n";
}


