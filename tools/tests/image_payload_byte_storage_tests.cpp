#include "Project/ProjectGraphImageImportHistory.h"
#include "Graph/GraphDocumentRules.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace Stack::Project;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
EditorNodeGraph::Node Image() {
    EditorNodeGraph::Node node; node.id=1;node.instanceUuid="owned-image";node.kind=EditorNodeGraph::NodeKind::Image;
    node.image.pngBytes={1,2,3,4};node.image.previewPixels={5,6,7,8};
    node.image.pngStorage.Invalidate();node.image.previewStorage.Invalidate();return node;
}
ProjectGraphState Capture(ProjectGraphHistory& history,const EditorNodeGraph::Node& node) {
    ProjectGraphState state;
    auto& storage=state.images[node.instanceUuid];
    storage.png=history.RetainImageBytes(node.instanceUuid+"/png",node.image.pngBytes,node.image.pngStorage);
    storage.preview=history.RetainImageBytes(node.instanceUuid+"/preview",node.image.previewPixels,node.image.previewStorage);
    EditorNodeGraph::Node reduced;reduced.id=node.id;reduced.instanceUuid=node.instanceUuid;
    reduced.kind=node.kind;reduced.position=node.position;
    reduced.image.isEmbedding=node.image.isEmbedding;reduced.image.embeddingRequestId=node.image.embeddingRequestId;
    state.graph.EditNodes().push_back(std::move(reduced));return state;
}
}
int main() try {
    ProjectGraphHistory history;auto node=Image();
    auto initial=Capture(history,node);auto old=initial.images.at(node.instanceUuid).png;
    history.Begin(initial);
    for(int frame=0;frame<3;++frame) {
        node.position.x+=1;auto state=Capture(history,node);
        Require(state.images.at(node.instanceUuid).png==old,"drag retains immutable owner");
        history.Observe(std::move(state),true);
    }
    history.EndGesture();
    // Same-sized publication between drags must never reuse the preceding owner.
    node.image.pngStorage.Invalidate();node.image.pngBytes={9,8,7,6};
    auto changed=Capture(history,node);auto fresh=changed.images.at(node.instanceUuid).png;
    Require(fresh!=old && *old==std::vector<unsigned char>({1,2,3,4}),"old snapshot immutable");
    history.Observe(changed,false);
    Require(history.Candidate(false)->images.at(node.instanceUuid).png==old,"undo retains old owner");
    history.AcceptRestore(false);
    Require(history.Candidate(true)->images.at(node.instanceUuid).png==fresh,"redo retains new owner");
    node.image.pngBytes=*old;node.image.pngStorage.Seed(old);
    Require(Capture(history,node).images.at(node.instanceUuid).png==old,"restored owner seeded without copy");
    // Uncertified external payloads must still detect direct mutations.
    EditorNodeGraph::ImagePayload external;external.pngBytes={1,1};
    auto a=history.RetainImageBytes("external",external.pngBytes,external.pngStorage);
    external.pngBytes[0]=2;
    auto b=history.RetainImageBytes("external",external.pngBytes,external.pngStorage);
    Require(a!=b && (*a)[0]==1 && (*b)[0]==2,"uncertified comparison fallback");
    // Test the real candidate adoption boundary, including copied certification.
    EditorNodeGraph::Graph previous,candidate;
    previous.EditNodes().push_back(node);candidate.EditNodes().push_back(node);
    candidate.EditNodes()[0].image.pngBytes[0]=77;
    Stack::GraphModel::RetainImageStorage(candidate,previous);
    auto adopted=Capture(history,candidate.GetNodes()[0]).images.at(node.instanceUuid).png;
    Require((*adopted)[0]==77 && (*old)[0]==1,"adoption invalidates modified certified copy");
    // An undo candidate is explicitly seeded from its own retained snapshot.
    auto restoredOwner=std::make_shared<const std::vector<unsigned char>>(std::vector<unsigned char>{6,6,6,6});
    candidate=previous;candidate.EditNodes()[0].image.pngBytes=*restoredOwner;
    candidate.EditNodes()[0].image.pngStorage.Seed(restoredOwner);
    Stack::GraphModel::RetainImageStorage(candidate,previous);
    Require(Capture(history,candidate.GetNodes()[0]).images.at(node.instanceUuid).png==restoredOwner,"restore adoption preserves verified snapshot owner");
    candidate=previous;Stack::GraphModel::RetainImageStorage(candidate,previous);
    Require(Capture(history,candidate.GetNodes()[0]).images.at(node.instanceUuid).png==old,"equal adoption preserves owner");
    candidate=previous;candidate.EditNodes()[0].instanceUuid="equal-new-image";
    Stack::GraphModel::RetainImageStorage(candidate,previous);
    Require(Capture(history,candidate.GetNodes()[0]).images.at("equal-new-image").png==old,"new identity shares equality-proven immutable owner");
    candidate=previous;candidate.EditNodes()[0].instanceUuid="new-image";
    candidate.EditNodes()[0].image.pngBytes[0]=88;
    Stack::GraphModel::RetainImageStorage(candidate,previous);
    Require((*Capture(history,candidate.GetNodes()[0]).images.at("new-image").png)[0]==88,"new identity cannot retain copied stale owner");
    // Async embedding reconciliation while a layout gesture is open.
    ProjectGraphHistory async;node=Image();node.image.pngBytes.clear();node.image.pngStorage.Invalidate();
    node.image.isEmbedding=true;node.image.embeddingRequestId=42;
    async.Begin(Capture(async,node));node.position.x=5;async.Observe(Capture(async,node),true);
    const std::vector<unsigned char> completed{10,20,30,40};
    ResolveImageImportEmbedding(async,node.instanceUuid,42,completed);
    node.image.pngStorage.Invalidate();node.image.pngBytes=completed;
    node.image.isEmbedding=false;node.image.embeddingRequestId=0;
    auto next=Capture(async,node);
    Require(next.images.at(node.instanceUuid).png==async.CancelCandidate()->images.at(node.instanceUuid).png,"async between drags shares resolved owner");
    async.Observe(std::move(next),true);async.EndGesture();
    Require(*async.Candidate(false)->images.at(node.instanceUuid).png==completed,"undo does not resurrect pending bytes");
    // Actual decoder failures leave accepted payload and its certified owner intact.
    node=Image();old=Capture(history,node).images.at(node.instanceUuid).png;
    Require(!EditorNodeGraph::DecodeImagePayloadPngBytes({1,2,3},node.image),"malformed decode fails");
    Require(Capture(history,node).images.at(node.instanceUuid).png==old,"failed decode retains owner");
    const std::vector<unsigned char> png = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
        0x24, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47,
        0x42, 0x00, 0xae, 0xce, 0x1c, 0xe9, 0x00, 0x00,
        0x00, 0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c,
        0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81,
        0x34, 0x10, 0x30, 0xfc, 0x07, 0x00, 0x47, 0xca,
        0x08, 0xf8, 0x8b, 0x4e, 0x43, 0x85, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
        0x60, 0x82
    };
    Require(EditorNodeGraph::DecodeImagePayloadPngBytes(png,node.image),"valid decode");
    auto decoded=Capture(history,node).images.at(node.instanceUuid).png;
    Require(*decoded==png && decoded!=old && (*old)[0]==1,"successful decode replaces owner atomically");
    EditorNodeGraph::InvalidateImagePayloadRuntime(node.image);
    node.image.pngBytes.clear();node.image.previewPixels={4,4,4,4};
    auto rotated=Capture(history,node);
    Require(!rotated.images.at(node.instanceUuid).png && *rotated.images.at(node.instanceUuid).preview==node.image.previewPixels,"rotation invalidation");
    // Directly count conservative fallbacks instead of relying on timing.
    EditorNodeGraph::ImagePayloadByteStorage storage;std::vector<unsigned char> bytes{1,2};int checks=0;
    auto retain=[&] {++checks;return std::make_shared<const std::vector<unsigned char>>(bytes);};
    storage.Get(bytes,retain);storage.Get(bytes,retain);Require(checks==2,"uncertified checks each use");
    storage.Invalidate();auto owner=storage.Get(bytes,retain);storage.Get(bytes,retain);
    Require(checks==3,"certified unchanged bytes avoid comparison");
    bytes[0]=9;storage.Invalidate();Require((*storage.Get(bytes,retain))[0]==9 && (*owner)[0]==1,"same-sized new bytes replace immutable owner");
    std::cout<<"Image payload byte storage tests passed\n";
    return 0;
} catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
