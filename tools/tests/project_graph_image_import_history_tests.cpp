#include "Project/ProjectGraphImageImportHistory.h"
#include "Async/TaskGroup.h"
#include <iostream>
#include <stdexcept>
using namespace Stack::Project;
namespace {
void Require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
ProjectGraphState Pending(float x=0,std::string uuid="image",std::uint64_t request=7) {
    ProjectGraphState state; EditorNodeGraph::Node node;
    node.id=1;node.instanceUuid=std::move(uuid);node.kind=EditorNodeGraph::NodeKind::Image;
    node.position.x=x;node.title="authored title";node.notes="authored note";node.image.sourcePath="same-source.png";node.image.isLoading=true;node.image.importRequestId=request;
    state.graph.EditNodes().push_back(std::move(node));return state;
}
EditorNodeGraph::ImagePayload Decoded(){EditorNodeGraph::ImagePayload p;p.label="source";p.width=1;p.height=1;p.channels=4;p.pixels={1,2,3,4};p.previewPixels={4,3,2,1};p.previewWidth=1;p.previewHeight=1;p.isEmbedding=true;p.importRequestId=7;p.embeddingRequestId=7;return p;}
const EditorNodeGraph::Node& Node(const ProjectGraphState* s){Require(s && s->graph.GetNodes().size()==1,"missing image");return s->graph.GetNodes()[0];}
void CheckReady(const ProjectGraphState* s,float x){const auto& n=Node(s);Require(!n.image.isLoading&&!n.image.isEmbedding,"completed request resurrected pending flags");Require(n.position.x==x&&n.notes=="authored note"&&n.title=="authored title","resolution changed authored fields");const auto& bytes=s->images.at("image");Require(bytes.pixels.bytes&&*bytes.pixels.bytes==std::vector<unsigned char>({1,2,3,4}),"lost decoded pixels");Require(bytes.png&&*bytes.png==std::vector<unsigned char>({9,8}),"lost storage bytes");}
void Finish(ProjectGraphHistory& h){auto p=Decoded();ResolveImageImportDecode(h,"image",7,p);ResolveImageImportEmbedding(h,"image",7,{9,8});}
// Source-image loading starts history after decode, while embedding is still
// pending. Unlike Add Slice it has no earlier placeholder/import undo action.
ProjectGraphState SourceBaseline() {
    ProjectGraphHistory history;history.Reset(Pending());
    const auto decoded=Decoded();ResolveImageImportDecode(history,"image",7,decoded);
    history.Observe(Pending(1),false);
    return *history.Candidate(false);
}
void CheckSourceHistory() {
    const auto baseline=SourceBaseline();
    for(const bool failed:{false,true}) {
        ProjectGraphHistory history;history.Reset(baseline);
        const std::vector<unsigned char> encoded=failed?std::vector<unsigned char>{}:std::vector<unsigned char>{9,8};
        // Completion before any authored edit must update the initial baseline.
        ResolveImageImportEmbedding(history,"image",7,encoded);
        auto edited=baseline;edited.graph.EditNodes()[0].position.x=12;
        history.Observe(std::move(edited),false);
        // An edit made during encoding is reconciled along with earlier state.
        ResolveImageImportEmbedding(history,"image",7,encoded);
        const auto ready=[&](const ProjectGraphState* state,float x) {
            const auto& node=Node(state);
            Require(!node.image.isEmbedding && node.image.embeddingRequestId==0,"source undo restored pending embedding");
            Require(node.position.x==x && node.title=="authored title" && node.notes=="authored note","source embedding changed authored metadata");
            const auto& image=state->images.at("image");
            Require(image.pixels.bytes && *image.pixels.bytes==std::vector<unsigned char>({1,2,3,4}),"source embedding lost decoded pixels");
            Require(failed?!image.png:(image.png && *image.png==encoded),"source embedding stored the wrong success/failure payload");
        };
        ready(history.Candidate(false),0);history.AcceptRestore(false);
        ready(history.Candidate(true),12);history.AcceptRestore(true);
    }
    // Deletion/undo does not cancel owned completion in retained snapshots.
    ProjectGraphHistory absent;absent.Reset(baseline);absent.Observe({},false);
    ResolveImageImportEmbedding(absent,"image",7,{9,8});CheckReady(absent.Candidate(false),0);
    absent.AcceptRestore(false);Require(absent.CanRedo(),"source completion erased deletion redo");
    // Different generation or UUID remains pending and preserves all bytes.
    ProjectGraphHistory replacement;auto newer=baseline;
    newer.graph.EditNodes()[0].image.embeddingRequestId=8;
    replacement.Reset(newer);replacement.Observe(newer,false);
    ResolveImageImportEmbedding(replacement,"image",7,{9,8});
    Require(Node(replacement.Candidate(false)).image.isEmbedding && !replacement.Candidate(false)->images.at("image").png,"stale source request changed replacement history");
}
}
int main() try {
    CheckSourceHistory();
    ProjectGraphHistory h;h.Begin({});h.Observe(Pending(),false);h.Observe(Pending(10),true);h.EndGesture();h.Observe(Pending(20),true);h.EndGesture();
    Finish(h);CheckReady(h.Candidate(false),10);h.AcceptRestore(false);CheckReady(h.Candidate(true),20);h.AcceptRestore(true);
    // Result arrives while node is absent (undo import or delete node).
    h.Reset({});h.Observe(Pending(),false);Require(h.Candidate(false)->graph.GetNodes().empty(),"bad import baseline");h.AcceptRestore(false);
    Finish(h);Require(h.CanRedo(),"async completion discarded redo");CheckReady(h.Candidate(true),0);h.AcceptRestore(true);
    h.Observe({},false);Finish(h);CheckReady(h.Candidate(false),0);h.AcceptRestore(false);Require(h.Candidate(true)->graph.GetNodes().empty(),"resolution resurrected deleted node");
    // Gesture baseline and current both resolve without committing the drag.
    h.Reset(Pending());h.Observe(Pending(30),true);Finish(h);Require(h.GestureActive(),"completion ended gesture");CheckReady(h.CancelCandidate(),0);h.AcceptCancel();
    // UUID and request identity, not reused integer node ID, own the result.
    h.Reset(Pending(1,"replacement",7));h.Observe(Pending(2,"image",8),false);Finish(h);Require(Node(h.Candidate(false)).image.isLoading,"completion changed reused integer ID");h.AcceptRestore(false);Require(Node(h.Candidate(true)).image.isLoading,"completion changed newer request");
    // Re-importing the same path is a new UUID/request, not the old completion.
    h.Reset(Pending(0,"replacement",8));h.Observe(Pending(1,"replacement",8),false);
    Finish(h);Require(Node(h.Candidate(false)).instanceUuid=="replacement" && Node(h.Candidate(false)).image.isLoading,
        "old same-path import completed the replacement");
    // Tab retirement stops new work but retains already-owned completions. A
    // different project may even contain the same persisted node UUID/request.
    ProjectGraphHistory closedProject, otherProject;
    closedProject.Reset(Pending());closedProject.Observe(Pending(1),false);
    otherProject.Reset(Pending());otherProject.Observe(Pending(2),false);
    auto& pool=Async::TaskSystem::Get();pool.Initialize();Async::TaskGroup closingTasks;
    closingTasks.Stop();
    Require(closingTasks.PostToMain([&]{Finish(closedProject);}),"retired completion was rejected");
    Require(!closingTasks.IsIdle(),"retired owner released before completion");
    pool.PumpMainThreadTasks();
    Require(closingTasks.IsIdle(),"retired completion failed to drain");
    CheckReady(closedProject.Candidate(false),0);
    Require(Node(otherProject.Candidate(false)).image.isLoading,"completion crossed project history owners");
    pool.Shutdown();
    // Decode failure removes only pending imported nodes, including redo.
    h.Reset({});h.Observe(Pending(),false);h.Candidate(false);h.AcceptRestore(false);ResolveImageImportFailure(h,"image",7);
    Require(h.CanRedo() && h.Candidate(true)->graph.GetNodes().empty(),"failed import resurrected through redo");
    h.Reset(Pending(0,"other",7));h.Observe(Pending(0,"image",8),false);ResolveImageImportFailure(h,"image",7);Require(Node(h.Candidate(false)).image.isLoading,"failure removed unrelated image");
    // Encoding failure retains decoded pixels but releases the saving gate.
    h.Reset(Pending());auto p=Decoded();ResolveImageImportDecode(h,"image",7,p);h.Observe(Pending(5),true);ResolveImageImportEmbedding(h,"image",7,{});
    const auto* failed=h.CancelCandidate();Require(!Node(failed).image.isEmbedding && failed->images.at("image").pixels.bytes,"encoding failure lost pixels or kept pending");
    std::cout<<"Async image history: success, failure, absent nodes, gesture, UUID/request ownership passed.\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
