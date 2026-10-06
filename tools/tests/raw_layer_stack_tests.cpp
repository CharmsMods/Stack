#include "Project/RawLayerStack.h"
#include "Project/RawLayerSourceTransactions.h"
#include "Graph/GraphDocumentRules.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include "Raw/RawViewportDetail.h"
#include "Raw/RawRecipeCompatibility.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Stack::Project;
void Check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
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
} // namespace

void TestSceneToneAndDetailMath() {
    using namespace Stack::RawRecipe;
    SceneTone tone;
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
    tone=SceneTone{}; tone.contrast=1.4f; tone.pivotEv=-2; tone.outerProtection=1;
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
    TestSceneToneAndDetailMath();
    CheckViewportContentIdentity();
    CheckCompoundReferenceCycle();
    CheckMergedSourceHistory();
    CheckViewportCoordinates();
    const auto offers = Stack::GraphModel::QueryAvailableNodes(RawLayerStackState{}.background.graph);
    Check(std::any_of(offers.begin(),offers.end(),[](const auto& entry) { return entry.kind == EditorNodeGraph::NodeKind::Compound; }),
        "The node browser lost shipped compounds.");
    Check(std::any_of(offers.begin(),offers.end(),[](const auto& entry) { return entry.kind == EditorNodeGraph::NodeKind::FrequencyFilter && entry.value == static_cast<int>(EditorNodeGraph::FrequencyFilterMode::NotchReject); }),
        "The node browser lost frequency presets.");
    using namespace Stack::RawRecipe;
    using Role = Stack::GraphModel::NodeRole;
    RawLayerStackDocument document;
    RawLayerStackState state;
    const auto first = AddRawAdjustmentLayer(state, "Lift shadows");
    const auto second = AddRawAdjustmentLayer(state, "Soften highlights");
    std::string error;
    auto* creative = FindRawAdjustmentLayer(state, first);
    FindRawOperation(*creative, GraphOperationKind::Exposure)->rawOperation.parameters["ev"] = 1.5f;
    const auto toolMask = AddRawGeneratedMask(state, first, EditorNodeGraph::MaskGeneratorKind::RadialGradient, "Face");
    const auto layerMask = AddRawGeneratedMask(state, first, EditorNodeGraph::MaskGeneratorKind::LinearGradient, "Fade");
    const auto attach = [&](RawLayerStackState& target, const std::string& id, GraphOperationKind kind, RawMaskReference mask) {
        const auto* operation = FindRawOperation(*FindRawAdjustmentLayer(target, id), kind);
        Check(SetRawOperationMask(target, id, operation->instanceUuid, mask, error), error);
    };
    SceneTone tone; tone.contrast = 1.3f; tone.outerProtection = .6f;
    FindRawOperation(*creative, GraphOperationKind::LuminanceTone)->rawOperation.parameters = SerializeSceneTone(tone);
    DetailContrast detail; detail.scaleGains[4] = 1.25f; detail.evScaleResidual[63] = -.2f;
    FindRawOperation(*creative, GraphOperationKind::DetailContrast)->rawOperation.parameters = SerializeDetailContrast(detail);
    attach(state, first, GraphOperationKind::LuminanceTone, toolMask);
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
    const auto* reorderedTone = FindRawOperation(reordered.layers.back(), GraphOperationKind::LuminanceTone);
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
    attach(cross, second, GraphOperationKind::LuminanceTone, crossMask);
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
    Check(ValidateRawLayerStack(outerFeedback, error, &composition, outerSource),
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
    const auto afterCurve = FindRawOperation(curves.background,GraphOperationKind::LuminanceTone)->instanceUuid;
    const auto curveUuid = AddRawMaskedCurve(curves,kRawBackgroundId,afterCurve,GraphOperationKind::LuminanceTone,
        EditorNodeGraph::MaskGeneratorKind::LinearGradient,error);
    Check(!curveUuid.empty() && ValidateRawLayerStack(curves,error),"Masked sequential curve failed: " + error);
    const auto* addedCurve = FindRawOperation(curves.background,GraphOperationKind::LuminanceTone,curveUuid);
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
    producerLayer->processingSettings.push_back({{"type","ViewTransform"}});
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
    const auto* restoredTone = FindRawOperation(reopened.State().layers.front(), GraphOperationKind::LuminanceTone);
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


