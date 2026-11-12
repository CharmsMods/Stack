#include "GraphView.h"
#include "Renderer/MaskRenderTypes.h"
#include "NodeMath/DescriptorSerialization.h"
#include <algorithm>
namespace Stack::Hdr {
namespace {
RenderGraphNode* DirectPhotoSource(RenderGraphSnapshot& graph) {
    int endpoint=graph.outputNodeId;
    std::string socket=graph.outputSocketId.empty()?"imageOut":graph.outputSocketId;
    for(size_t depth=0;depth<graph.nodes.size();++depth) {
        if(socket!="imageOut") return nullptr;
        auto node=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.nodeId==endpoint;});
        if(node==graph.nodes.end()) return nullptr;
        if(node->kind==RenderGraphNodeKind::Image) return node->image.hdrPixels ? &*node : nullptr;
        if(node->kind!=RenderGraphNodeKind::Output) return nullptr;
        const auto link=std::find_if(graph.links.begin(),graph.links.end(),[&](const auto& l){return l.toNodeId==endpoint && l.toSocketId=="imageIn";});
        if(link==graph.links.end()) return nullptr;
        endpoint=link->fromNodeId;socket=link->fromSocketId;
    }
    return nullptr;
}
void DescribeOutput(RenderGraphSnapshot& graph,bool encodeSrgb,bool hdr) {
    using namespace NodeMath;
    auto& descriptor=graph.outputDescriptor;
    descriptor.color=SemanticField<ColorIdentity>::Known({"srgb-d65",{},ColorRelation::Standard});
    descriptor.transfer=SemanticField<TransferDescriptor>::Known({encodeSrgb ? TransferKind::Srgb:TransferKind::Linear,0.f,{}});
    descriptor.reference=SemanticField<ReferenceState>::Known(ReferenceState::Display);
    descriptor.range=SemanticField<NumericRange>::Known({0,1,hdr,hdr,NonFinitePolicy::Forbidden});
    graph.outputDescriptorIdentity=DescriptorContentIdentity(descriptor);
}
}
bool NeedsSdrPhotoView(const RenderGraphSnapshot& graph) {
    using namespace NodeMath;
    const auto& d=graph.outputDescriptor;
    return d.logicalType==LogicalValueType::ColorImage &&
        d.reference.state==KnowledgeState::Known && d.reference.value==ReferenceState::Display &&
        d.transfer.state==KnowledgeState::Known && d.transfer.value.kind==TransferKind::Linear &&
        d.range.state==KnowledgeState::Known && d.range.value.allowsAboveNominal &&
        std::any_of(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==RenderGraphNodeKind::Image && bool(n.image.hdrPixels);});
}
bool ConfigureView(RenderGraphSnapshot& graph,float headroom,bool encodeSrgb) {
    using namespace NodeMath;
    using K=RenderGraphNodeKind;
    auto& descriptor=graph.outputDescriptor;
    if(descriptor.logicalType!=LogicalValueType::ColorImage || graph.outputNodeId<=0) return false;
    int endpoint=graph.outputNodeId;
    bool found=false;
    for(size_t depth=0;depth<graph.nodes.size();++depth) {
        auto it=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.nodeId==endpoint;});
        if(it==graph.nodes.end()) break;
        auto& node=*it;
        nlohmann::json* view=nullptr;
        if(node.kind==K::Layer && node.layerJson.value("type",std::string{})=="ViewTransform" &&
            node.layerJson.value("visible",true)) view=&node.layerJson;
        if(node.kind==K::RawDevelopment && !graph.rawLayerBackgroundNodeId)
            view=&node.rawDevelopment.recipe.viewTransform.layerJson;
        if(view && view->is_object()) {
            if(view->value("debugFalseColor",false)) return false;
            if(view->value("enabled",true)) {
                (*view)["_presentationHeadroom"]=headroom;
                (*view)["encodeSrgbOutput"]=encodeSrgb;
                found=true;
            }
            break;
        }
        if(node.kind!=K::Output && !(node.kind==K::Layer && node.layerJson.value("type",std::string{})=="RawOutputCrop")) break;
        auto link=std::find_if(graph.links.begin(),graph.links.end(),[&](const auto& l){return l.toNodeId==endpoint && l.toSocketId=="imageIn";});
        if(link==graph.links.end()) break;
        endpoint=link->fromNodeId;
    }
    bool useMaster=headroom>1.f;
    if(!found) {
        // Already-finished SDR must not receive a second display transform.
        if(!useMaster && encodeSrgb && descriptor.reference.state==KnowledgeState::Known &&
            descriptor.reference.value==ReferenceState::Display && descriptor.transfer.state==KnowledgeState::Known &&
            descriptor.transfer.value.kind==TransferKind::Srgb && descriptor.color.state==KnowledgeState::Known &&
            descriptor.color.value.identity=="srgb-d65") {
            for(auto& node:graph.nodes) if(node.kind==K::Image && node.image.hdrPixels) node.image.useHdrPixels=true;
            return true;
        }
        const auto linear=descriptor.transfer.state==KnowledgeState::Known && descriptor.transfer.value.kind==TransferKind::Linear;
        // Processing such as Tone may clear the range without changing the
        // explicitly declared display-linear representation.
        const bool displayLinearInput=linear && descriptor.reference.state==KnowledgeState::Known &&
            descriptor.reference.value==ReferenceState::Display;
        if(!linear || descriptor.color.state!=KnowledgeState::Known || descriptor.reference.state!=KnowledgeState::Known ||
            (!displayLinearInput && descriptor.reference.value!=ReferenceState::Scene)) return false;
        const auto& basis=descriptor.color.value.identity;
        if(basis!="srgb-d65" && basis!="linear-srgb-d65" && basis!="rec2020-d65" && basis!="linear-rec2020-d65") return false;
        auto* directBase=displayLinearInput && descriptor.range.state==KnowledgeState::Known &&
            descriptor.range.value.allowsAboveNominal ? DirectPhotoSource(graph) : nullptr;
        // The source decoder retains the encoded SDR rendition for gain-map
        // JPEGs and prepares one for PQ PNGs. Use it directly for the base.
        if(directBase && !useMaster && encodeSrgb) {
            directBase->image.useHdrPixels=false;
            DescribeOutput(graph,true,false);
            return true;
        }
        RenderGraphNode view;
        for(const auto& n:graph.nodes) view.nodeId=std::max(view.nodeId,n.nodeId);
        ++view.nodeId; view.kind=K::Layer;
        view.layerJson={{"type","ViewTransform"},{"_displayLinearInput",displayLinearInput},{"_presentationHeadroom",headroom},
            {"sdrHighlightDetail",1.f},
            {"inputWorkingSpace",basis.find("2020")!=std::string::npos ? "linear-rec2020-d65":"linear-srgb-d65"},{"encodeSrgbOutput",encodeSrgb}};
        RenderGraphLink link; link.fromNodeId=graph.outputNodeId; link.fromSocketId=graph.outputSocketId.empty()?"imageOut":graph.outputSocketId;
        link.toNodeId=view.nodeId; link.toSocketId="imageIn";
        graph.links.push_back(std::move(link)); graph.outputNodeId=view.nodeId; graph.outputSocketId="imageOut";
        graph.nodes.push_back(std::move(view));
    }
    // Processing and the view transform consume the declared linear source.
    // Headroom selects the presentation, not the source's transfer encoding.
    for(auto& node:graph.nodes) if(node.kind==K::Image && node.image.hdrPixels) node.image.useHdrPixels=true;
    DescribeOutput(graph,encodeSrgb,useMaster);
    return true;
}
}
