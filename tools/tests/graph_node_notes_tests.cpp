#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Project/ProjectGraphHistory.h"
#include "Project/RawProjectGraph.h"

#include <iostream>
#include <stdexcept>

namespace {
void RequireNotes(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

void RunGraphNodeNotesTests() {
    using namespace EditorNodeGraph;
    using namespace Stack::Project;
    std::string error;
    Graph graph;
    graph.Clear();
    const int input=graph.AddImageGeneratorNode(ImageGeneratorKind::SolidColor,{})->id;
    const int output=graph.AddOutputNode({600,0},true)->id;
    const auto& definitions=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    const Stack::NodeMath::CompoundDefinition* percent=nullptr;
    for (const auto& definition:definitions) {
        Graph internal;
        DeserializeGraphPayload(definition.canonicalGraph,internal,0);
        RequireNotes(!internal.GetNodes().empty(),"A template has no canonical nodes.");
        for (const auto& node:internal.GetNodes())
            RequireNotes(!node.notes.empty(),"A shipped decompile template is missing a node purpose note.");
        RequireNotes(graph.AddCompoundDefinition(definition,&error),error.c_str());
        if (definition.identity.id=="stack:compound/percent") percent=&definition;
    }
    RequireNotes(percent!=nullptr,"Percent template is missing.");
    const int packed=graph.AddCompoundNode(percent->identity,{300,0})->id;
    RequireNotes(!graph.FindNode(packed)->notes.empty(),"Compound creation did not initialize its purpose note.");
    graph.FindNode(packed)->notes="My outer note.\nKeep the customized explanation.";
    graph.FindNode(input)->notes="Source note";
    RequireNotes(graph.TryConnectSockets(input,"imageOut",packed,"image-in",&error),error.c_str());
    RequireNotes(graph.TryConnectSockets(packed,"image-out",output,"imageIn",&error),error.c_str());
    const auto structure=graph.GetStructureRevision();
    graph.FindNode(output)->notes="Final output";
    RequireNotes(graph.GetStructureRevision()==structure,"Annotation changed executable structure revision.");
    Graph loaded;
    DeserializeGraphPayload(SerializeGraphPayload(nlohmann::json::array(),graph),loaded,0);
    RequireNotes(loaded.FindNode(packed)->notes==graph.FindNode(packed)->notes,"Notes did not round-trip.");
    RequireNotes(loaded.CloneForAnalysis().FindNode(input)->notes=="Source note","Reduced graph copy lost source notes.");
    Graph imageGraph;
    const int image=imageGraph.AddImageNode({},{})->id;
    imageGraph.FindNode(image)->notes="Image-source annotation";
    RequireNotes(imageGraph.CloneForAnalysis().FindNode(image)->notes=="Image-source annotation",
        "Image-specific reduced clone lost its annotation.");
    std::vector<int> unpacked;
    RequireNotes(loaded.UnpackCompoundNode(packed,&unpacked,&error,nullptr,true),error.c_str());
    RequireNotes(!unpacked.empty(),"Percent did not decompile.");
    loaded.FindNode(unpacked.front())->notes="Edited child note";
    loaded.FindNode(unpacked.back())->notes.clear();
    const int group=loaded.GetGroups().back().id;
    int repacked=0;
    RequireNotes(loaded.RepackCompoundGroup(group,&repacked,&error),error.c_str());
    RequireNotes(loaded.FindNode(repacked)->notes==graph.FindNode(packed)->notes,"Repack lost the outer annotation.");
    unpacked.clear();
    RequireNotes(loaded.UnpackCompoundNode(repacked,&unpacked,&error,nullptr,true),error.c_str());
    RequireNotes(loaded.FindNode(unpacked.front())->notes=="Edited child note","Repack lost edited child notes.");
    RequireNotes(loaded.FindNode(unpacked.back())->notes.empty(),"Repack replaced an intentionally empty note.");

    RawLayerStackDocument raw;
    auto initialRaw=raw.State();
    const int toneId=initialRaw.background.graph.AddRawOperationNode(
        Stack::RawRecipe::GraphOperationKind::ToneCurve,{})->id;
    initialRaw.background.graph.FindNode(toneId)->rawOperation.parameters["advanced"]["autoCalibratePending"]=true;
    RequireNotes(raw.Apply(std::move(initialRaw),error),error.c_str());
    auto projection=BuildRawProjectGraph(raw.State());
    auto edited=projection.graph;
    const auto processing=raw.ProcessingRevision();
    edited.FindNode(projection.preparedSource)->notes="RAW source note";
    edited.FindNode(projection.output)->notes="RAW output note";
    int ownedId=0;
    for (auto& node:edited.GetNodes()) if (!node.rawLayerOwnerId.empty()) {
        node.notes="Owned layer note";ownedId=node.id;break;
    }
    RequireNotes(ownedId!=0,"RAW projection has no owned node.");
    RequireNotes(raw.ApplyGraphLayout(projection,edited,false,error,true),error.c_str());
    RequireNotes(raw.PendingHistoryIsNotesOnly(RawLayerHistoryAction::Undo),"RAW annotation history lost its edit type.");
    RequireNotes(raw.ProcessingRevision()==processing,"Notes invalidated RAW processing.");
    const auto revision=raw.Revision();
    RequireNotes(raw.ApplyGraphLayout(projection,edited,false,error,true) && raw.Revision()==revision,
        "Unchanged notes created an authored revision.");
    RawLayerStackState restored;
    RequireNotes(DeserializeRawLayerStack(SerializeRawLayerStack(raw.State()),restored,error),error.c_str());
    auto restoredGraph=BuildRawProjectGraph(restored);
    RequireNotes(restoredGraph.graph.FindNode(restoredGraph.preparedSource)->notes=="RAW source note" &&
        restoredGraph.graph.FindNode(restoredGraph.output)->notes=="RAW output note" &&
        restoredGraph.graph.FindNode(ownedId)->notes=="Owned layer note","RAW note ownership did not persist.");
    // A processing result may arrive after the annotation. Its accepted values
    // and outstanding requests must survive annotation undo/redo.
    auto completed=raw.State();
    completed.background.graph.FindNode(toneId)->rawOperation.parameters["logMaxEv"]=8.f;
    RequireNotes(raw.Apply(std::move(completed),error,true,true),error.c_str());
    const auto acceptedProcessing=raw.ProcessingRevision();
    RequireNotes(raw.Undo(&error) && raw.ProcessingRevision()==acceptedProcessing,error.c_str());
    RequireNotes(raw.State().sourceCanvasNotes.empty(),"RAW undo did not restore source annotation.");
    const auto pendingSurvived=[&] {
        const auto& parameters=raw.State().background.graph.FindNode(toneId)->rawOperation.parameters;
        return parameters.at("logMaxEv")==8.f && parameters.at("advanced").at("autoCalibratePending")==true;
    };
    RequireNotes(pendingSurvived(),"RAW note undo replaced accepted parameters or canceled pending analysis.");
    RequireNotes(raw.Redo(&error) && raw.ProcessingRevision()==acceptedProcessing,error.c_str());
    RequireNotes(pendingSurvived(),"RAW note redo replaced accepted parameters or canceled pending analysis.");
    RequireNotes(raw.State().sourceCanvasNotes=="RAW source note","RAW redo did not restore annotation.");

    ProjectGraphHistory history;
    ProjectGraphState before;before.graph=graph;
    history.Begin(before);
    auto after=before;after.graph.FindNode(output)->notes="New output note";
    history.Observe(after,false,false);
    RequireNotes(history.Candidate(false) && !history.CandidateAffectsPixels(false),"Notes history is marked pixel-affecting.");
    history.AcceptRestore(false);
    RequireNotes(history.Candidate(true) && !history.CandidateAffectsPixels(true),"Notes redo is marked pixel-affecting.");
    history.AcceptRestore(true);
    auto processingEdit=after;processingEdit.graph.FindNode(output)->outputEnabled=false;
    history.Observe(processingEdit,false);
    RequireNotes(history.Candidate(false) && history.CandidateAffectsPixels(false),"An ordinary graph edit lost processing invalidation.");
    std::cout << "Node notes: defaults, save/load, reduced copies, decompile/repack, empty notes, RAW ownership/history and processing revisions passed.\n";
}
