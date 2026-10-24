#!/usr/bin/env python3
"""Generate a CPU test of the actual source-load completion and history capture.

Only the EditorModule/project shell is reduced. Completion, capture, and the
source-generation reset prefix are copied unchanged from production source.
No UI, real image, source decoder, encoder, or filesystem load is involved.
Link with the existing async-image-history CPU sources plus
src/Editor/NodeGraph/EditorNodeGraphCompound.cpp for the real analysis clone.
"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]

    def read(path):
        return (root / 'src' / path).read_text()

    source = read('Editor/Internal/EditorModulePersistence.cpp')
    request = source.split('void EditorModule::RequestLoadSourceImage(', 1)[1]
    marker = '            const auto completeEmbedding = '
    assert request.count(marker) == 1
    start = request.index(marker)
    callback = request[start:request.index('\n            auto postLoadFailure', start)]
    # Confirm both terminal paths use this callback, and that decode binds its
    # stable target before publishing the payload rather than reading selection
    # again when the encoder finishes.
    assert request.count('completeEmbedding(std::move(pngBytes));') == 1
    assert request.count('completeEmbedding({});') == 1
    assert 'published->nodeId = imageNode->id;' in request
    assert 'published->uuid = imageNode->instanceUuid;' in request
    assert request.index('published->uuid = imageNode->instanceUuid;') < request.index('imageNode->image = std::move(payload);')
    assert 'GetActiveImageNodeId' not in callback
    reset_source = read('Editor/Internal/Project/EditorModuleProjectInteraction.cpp')
    reset = reset_source.split('void EditorModule::ResetProjectInteractionState() {', 1)[1].split('    m_PreviewRequestCandidates', 1)[0]
    assert '++m_SourceLoadGeneration;' in reset
    assert '++m_SourceLoadProjectGeneration;' in reset
    history = read('Editor/Internal/EditorModuleGraphHistory.cpp')
    method = history.split('Stack::Project::ProjectGraphState EditorModule::CaptureProjectGraphState() const {', 1)[1].split('\nvoid EditorModule::BeginProjectGraphHistory()', 1)[0]
    text = r'''
#include "Project/ProjectGraphImageImportHistory.h"
#include "Async/TaskGroup.h"
#include "Async/TaskState.h"
#include <iostream>
#include <stdexcept>
using namespace Stack::Project;
enum class UiNotificationSeverity { Error };
struct Project { EditorNodeGraph::Graph graph; ProjectGraphHistory graphHistory; Stack::Timeline::TimelineAnimationState timeline; };
void Require(bool b,const char* why) {if(!b)throw std::runtime_error(why);}
struct EditorModule {
    std::unique_ptr<Project> m_Project=std::make_unique<Project>();
    std::uint64_t m_SourceLoadGeneration=7;
    std::uint64_t m_SourceLoadProjectGeneration=0;
    Async::TaskState m_SourceLoadTaskState=Async::TaskState::Idle;
    std::string m_SourceLoadStatusText;
    void PostNotification(UiNotificationSeverity,const std::string&,const std::string&) {}
    ProjectGraphState CaptureProjectGraphState() const {'''+method+r'''
    void ResetGeneration() {'''+reset+r'''}
    auto Completion(int nodeId=1,const std::string& uuid="source") {
        const auto generation=m_SourceLoadGeneration;
        const auto projectGeneration=m_SourceLoadProjectGeneration;
        struct PublishedImage {int nodeId=-1;std::string uuid;};
        const auto published=std::make_shared<PublishedImage>();
        published->nodeId=nodeId;published->uuid=uuid;
'''+callback+r'''
        return completeEmbedding;
    }
};
struct Fixture {
    EditorModule editor;
    Fixture() {
        EditorNodeGraph::Node n;n.id=1;n.instanceUuid="source";n.kind=EditorNodeGraph::NodeKind::Image;
        n.title="My title";n.notes="My note";n.position.x=3;
        n.image.width=n.image.height=1;n.image.channels=4;n.image.pixels={1,2,3,4};n.image.previewPixels={4,3,2,1};
        n.image.isEmbedding=true;n.image.embeddingRequestId=7;n.image.importRequestId=7;
        n.image.pngStorage.Invalidate();n.image.previewStorage.Invalidate();
        editor.m_Project->graph.EditNodes().push_back(n);editor.m_Project->graph.SetActiveImageNodeId(1);
        editor.m_Project->graphHistory.Reset(editor.CaptureProjectGraphState());
    }
    void Edit() {
        editor.m_Project->graph.EditNodes()[0].position.x=9;
        editor.m_Project->graphHistory.Observe(editor.CaptureProjectGraphState(),false);
    }
};
void Check(const ProjectGraphState* state,bool failed=false,float expectedX=3) {
    Require(state && state->graph.GetNodes().size()==1,"missing source snapshot");
    const auto& n=state->graph.GetNodes()[0];
    Require(!n.image.isEmbedding && n.image.embeddingRequestId==0,"pending source resurrected");
    Require(n.title=="My title" && n.notes=="My note" && n.position.x==expectedX,"completion changed authored metadata");
    const auto& bytes=state->images.at("source");
    Require(bytes.pixels.bytes && *bytes.pixels.bytes==std::vector<unsigned char>({1,2,3,4}),"source pixels changed");
    Require(failed?!bytes.png:(bytes.png && *bytes.png==std::vector<unsigned char>({9,8})),"wrong saved source bytes");
}
int main() try {
    for(bool failed:{false,true}) {
        Fixture f;auto complete=f.editor.Completion();complete(failed?std::vector<unsigned char>{}:std::vector<unsigned char>{9,8});
        f.Edit();auto& h=f.editor.m_Project->graphHistory;Check(h.Candidate(false),failed);h.AcceptRestore(false);Check(h.Candidate(true),failed,9);
        if(!failed) {
            const auto first=f.editor.CaptureProjectGraphState();
            const auto second=f.editor.CaptureProjectGraphState();
            Require(first.images.at("source").png==second.images.at("source").png && first.images.at("source").png==h.Candidate(true)->images.at("source").png,"source capture lost its certified immutable owner");
        }
    }
    {
        Fixture f;auto complete=f.editor.Completion();f.Edit();
        auto& h=f.editor.m_Project->graphHistory;h.Candidate(false);h.AcceptRestore(false);
        complete({9,8});Require(h.CanRedo(),"completion erased redo");Check(h.Candidate(true),false,9);
    }
    {
        Fixture f;auto complete=f.editor.Completion();auto& graph=f.editor.m_Project->graph;
        auto other=graph.GetNodes()[0];other.id=2;other.instanceUuid="other";graph.EditNodes().push_back(other);graph.SetActiveImageNodeId(2);
        complete({9,8});Require(!graph.FindNode(1)->image.isEmbedding && graph.FindNode(2)->image.isEmbedding,"completion followed active selection");
        Require(graph.FindNode(2)->image.pngBytes.empty(),"completion wrote another image");
    }
    {
        Fixture f;auto complete=f.editor.Completion();f.editor.m_Project->graph.EditNodes().clear();
        auto& h=f.editor.m_Project->graphHistory;h.Observe(f.editor.CaptureProjectGraphState(),false);
        complete({9,8});Check(h.Candidate(false));Require(f.editor.m_Project->graph.GetNodes().empty(),"completion resurrected deleted live node");
    }
    for(bool failed:{false,true}) for(bool encodeFailed:{false,true}) {
        Fixture f;auto complete=f.editor.Completion();
        ++f.editor.m_SourceLoadGeneration;
        // A remains the accepted source while B loads or after B decode fails.
        f.editor.m_SourceLoadStatusText=failed?"B failed":"Loading B";
        f.editor.m_SourceLoadTaskState=failed?Async::TaskState::Failed:Async::TaskState::Queued;
        complete(encodeFailed?std::vector<unsigned char>{}:std::vector<unsigned char>{9,8});
        Require(!f.editor.m_Project->graph.FindNode(1)->image.isEmbedding,"newer failed/pending request stranded accepted source");
        Require(f.editor.m_SourceLoadStatusText==(failed?"B failed":"Loading B") && f.editor.m_SourceLoadTaskState==(failed?Async::TaskState::Failed:Async::TaskState::Queued),"older completion overwrote current request status");
        f.Edit();Check(f.editor.m_Project->graphHistory.Candidate(false),encodeFailed);
    }
    {
        Fixture f;auto completeA=f.editor.Completion();++f.editor.m_SourceLoadGeneration;
        // B successfully replaces A on the same node. Its request identity,
        // not merely the shared UUID, must own both live and retained bytes.
        auto& image=f.editor.m_Project->graph.EditNodes()[0].image;
        image.embeddingRequestId=f.editor.m_SourceLoadGeneration;
        auto& h=f.editor.m_Project->graphHistory;h.Reset(f.editor.CaptureProjectGraphState());
        auto completeB=f.editor.Completion();completeA({11,12});
        Require(image.isEmbedding && image.pngBytes.empty(),"old source completed its replacement");
        completeB({9,8});completeA({11,12});
        Require(image.pngBytes==std::vector<unsigned char>({9,8}),"old source overwrote replacement bytes");
        f.Edit();Check(h.Candidate(false));
    }
    for(bool aFirst:{false,true}) {
        Fixture f;auto completeA=f.editor.Completion();++f.editor.m_SourceLoadGeneration;
        // B can publish into another active node, leaving A live. Resetting
        // the workspace history for B must not cancel A's accepted image.
        auto& graph=f.editor.m_Project->graph;auto other=graph.GetNodes()[0];
        other.id=2;other.instanceUuid="other";other.image.embeddingRequestId=f.editor.m_SourceLoadGeneration;
        graph.EditNodes().push_back(other);graph.SetActiveImageNodeId(2);
        auto& h=f.editor.m_Project->graphHistory;h.Reset(f.editor.CaptureProjectGraphState());
        auto completeB=f.editor.Completion(2,"other");
        if(aFirst) {completeA({11,12});completeB({9,8});}
        else {completeB({9,8});completeA({11,12});}
        Require(!graph.FindNode(1)->image.isEmbedding && graph.FindNode(1)->image.pngBytes==std::vector<unsigned char>({11,12}),"B's separate node stranded A");
        Require(!graph.FindNode(2)->image.isEmbedding && graph.FindNode(2)->image.pngBytes==std::vector<unsigned char>({9,8}),"A corrupted B's separate node");
        h.Observe(f.editor.CaptureProjectGraphState(),false);
        Require(*h.Candidate(false)->images.at("source").png==std::vector<unsigned char>({11,12}) && *h.Candidate(false)->images.at("other").png==std::vector<unsigned char>({9,8}),"two-source history kept stale bytes");
    }
    {
        Fixture f;auto complete=f.editor.Completion();f.editor.ResetGeneration();
        // Deliberately retain identical UUID/request data after the actual
        // production reset prefix, as if the same document were reopened.
        complete({9,8});Require(f.editor.m_Project->graph.FindNode(1)->image.isEmbedding,"stale completion mutated replacement");
        f.Edit();Require(f.editor.m_Project->graphHistory.Candidate(false)->graph.GetNodes()[0].image.isEmbedding,"stale completion reconciled replacement history");
    }
    {
        Fixture f;auto complete=f.editor.Completion();f.editor.m_Project->graph.EditNodes()[0].instanceUuid="replacement";
        complete({9,8});Require(f.editor.m_Project->graph.FindNode(1)->image.isEmbedding,"completion mutated reused node ID");
    }
    {
        Fixture closed,other;auto complete=closed.editor.Completion();auto& pool=Async::TaskSystem::Get();pool.Initialize();
        Async::TaskGroup group;group.Stop();Require(group.PostToMain([complete]{complete({9,8});}),"retired owner rejected tracked completion");
        pool.PumpMainThreadTasks();Require(group.IsIdle(),"retired source callback retained lease");
        Require(!closed.editor.m_Project->graph.FindNode(1)->image.isEmbedding && other.editor.m_Project->graph.FindNode(1)->image.isEmbedding,"retired source crossed owners");pool.Shutdown();
    }
    std::cout<<"Actual source embedding callback/capture: success, failure, undo/redo, selection, deletion, overlapping requests, same-document reset, reused ID, retired-owner isolation passed.\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == '__main__':
    main()
