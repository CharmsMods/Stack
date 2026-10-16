#include "Editor/NodeGraph/Definitions/OperationInventory.h"

namespace EditorNodeGraphDefinitions {
namespace OperationDefinitions {
using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;
using LogicalValue = LogicalValueType;
using TechnicalOperation = Stack::NodeMath::TechnicalImageOperation;
void AppendSourceOperations(std::vector<OperationRegistration>& operations) {
    const auto add = [&](LiveNodeDefinition definition) {
        switch (definition.kind) {
            case NodeKind::RawSource: case NodeKind::RawProjectFrame:
            case NodeKind::MultiFrameDenoise: case NodeKind::MultiFrameHdr:
            case NodeKind::RawProjectSourceSet: case NodeKind::RawDevelopment:
            case NodeKind::RawDecode: case NodeKind::RawDevelop:
            case NodeKind::RawDetailAutoMask: case NodeKind::RawDetailFusion:
                definition.inspectability = Stack::NodeMath::Inspectability::OpaqueSpecialized; break;
            default: break;
        }
        if (definition.kind == NodeKind::RawDetailAutoMask || definition.kind == NodeKind::RawDetailFusion)
            definition.bypassBindings = {{"imageOut", "imageIn"}};
        if (definition.kind == NodeKind::RawDevelop)
            definition.outputDependencies.push_back({EditorNodeGraph::kPreFinishImageOutputSocketId, {EditorNodeGraph::kRawInputSocketId}});
        operations.push_back({OperationFamily::SourcePreparation, std::move(definition)});
    };
    add(MakeOperation(EditorNodeGraph::NodeKind::Output, 0, "Output", "Input / Output", "output", NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::Histogram), "Histogram", "Input / Output", "scope:histogram", NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::Vectorscope), "Vectorscope", "Input / Output", "scope:vectorscope", NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::RGBParade), "RGB Parade", "Input / Output", "scope:rgbparade", NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Preview, 0, "Preview", "Input / Output", "preview", NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::RawDevelopment, 0, "RAW Development", "Input / Output", "raw-development", NodeCatalogPreviewStrategy::NoPreview, 1));
    add(MakeOperation(EditorNodeGraph::NodeKind::RawDecode, 0, "RAW Decode", "Input / Output", "raw-decode", NodeCatalogPreviewStrategy::NoPreview, 2));
    add(MakeOperation(EditorNodeGraph::NodeKind::RawDevelop, 0, "Develop (Advanced Auto)", "Advanced RAW", "develop", NodeCatalogPreviewStrategy::NoPreview, 2));
    add(MakeOperation(EditorNodeGraph::NodeKind::HdrMerge, 0, "HDR Merge", "Input / Output", "hdr-merge", NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Mfsr, 0, "MFSR", "Input / Output", "mfsr", NodeCatalogPreviewStrategy::FallbackOnly));
    // Manifest/source ownership controls creation of these nodes. Hiding their
    // browser rows does not remove their canonical definitions.
    add(MakeOperation(NodeKind::Image, 0, "Image Source", "Internal", "source:image", NodeCatalogPreviewStrategy::NoPreview, 1, false));
    add(MakeOperation(NodeKind::RawSource, 0, "RAW Source", "Internal", "source:raw", NodeCatalogPreviewStrategy::NoPreview, 1, false));
    add(MakeOperation(NodeKind::RawProjectFrame, 0, "RAW Project Frame", "Internal", "managed:raw-project-frame", NodeCatalogPreviewStrategy::NoPreview, 1, false));
    add(MakeOperation(NodeKind::MultiFrameDenoise, 0, "Multi-Frame Denoise", "Internal", "managed:multi-frame-denoise", NodeCatalogPreviewStrategy::NoPreview, 1, false));
    add(MakeOperation(NodeKind::MultiFrameHdr, 0, "Multi-Frame HDR", "Internal", "managed:multi-frame-hdr", NodeCatalogPreviewStrategy::NoPreview, 1, false));
    add(MakeOperation(NodeKind::RawProjectSourceSet, 0, "RAW Project Source Set", "Internal", "managed:raw-project-source-set", NodeCatalogPreviewStrategy::NoPreview, 1, false));
    add(MakeOperation(NodeKind::RawDetailAutoMask, 0, "RAW Detail Auto Mask", "Internal", "raw-detail:auto-mask", NodeCatalogPreviewStrategy::NoPreview, 1, false));
    add(MakeOperation(NodeKind::RawDetailFusion, 0, "Pre-Local Exposure", "Internal", "raw-detail:fusion", NodeCatalogPreviewStrategy::NoPreview, 1, false));
    add(MakeOperation(NodeKind::Composite, 0, "Composite", "Internal", "composite", NodeCatalogPreviewStrategy::NoPreview, 1, false));
}
} // namespace OperationDefinitions
} // namespace EditorNodeGraphDefinitions
