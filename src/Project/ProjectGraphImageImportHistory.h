#pragma once

#include "Project/ProjectGraphHistory.h"
#include "Project/GraphImagePayload.h"

namespace Stack::Project {
inline bool OwnsPendingImageImport(const EditorNodeGraph::Node& node,
    const std::string& uuid, std::uint64_t request, bool loading) {
    return node.kind==EditorNodeGraph::NodeKind::Image && node.instanceUuid==uuid &&
        (loading ? node.image.isLoading && node.image.importRequestId==request :
                   node.image.isEmbedding && node.image.embeddingRequestId==request);
}

inline void ResolveImageImportDecode(ProjectGraphHistory& history, const std::string& uuid,
    std::uint64_t request, const EditorNodeGraph::ImagePayload& payload) {
    ProjectGraphState::ImageStorage storage;
    storage.pixels=EnsureSharedImagePixels(payload);
    storage.preview=history.RetainImageBytes(uuid+"/preview",payload.previewPixels,payload.previewStorage);
    storage.previewWidth=payload.previewWidth; storage.previewHeight=payload.previewHeight;
    storage.previewChannels=payload.previewChannels;
    // Copy metadata only. Full bytes have one immutable owner shared by every
    // intervening history entry; never copy the image once per snapshot.
    EditorNodeGraph::ImagePayload reduced;
    reduced.label=payload.label; reduced.sourcePath=payload.sourcePath;
    reduced.managedAssetId=payload.managedAssetId; reduced.projectAssetPath=payload.projectAssetPath;
    reduced.width=payload.width; reduced.height=payload.height; reduced.channels=payload.channels;
    reduced.originalChannels=payload.originalChannels; reduced.sourceColorMetadata=payload.sourceColorMetadata;
    reduced.previewWidth=payload.previewWidth; reduced.previewHeight=payload.previewHeight;
    reduced.previewChannels=payload.previewChannels;
    reduced.isLoading=payload.isLoading; reduced.isEmbedding=payload.isEmbedding;
    reduced.importRequestId=payload.importRequestId; reduced.embeddingRequestId=payload.embeddingRequestId;
    reduced.sharedPixels=storage.pixels.bytes; reduced.pixelsFingerprint=storage.pixels.fingerprint;
    history.ReconcileImageImport([&](ProjectGraphState& state) {
        for (auto& node:state.graph.GetNodes()) if (OwnsPendingImageImport(node,uuid,request,true)) {
            node.image=reduced;
            state.images[uuid]=storage;
        }
    });
}

inline void ResolveImageImportEmbedding(ProjectGraphHistory& history, const std::string& uuid,
    std::uint64_t request, const std::vector<unsigned char>& png) {
    const auto bytes=history.RetainImageBytes(uuid+"/png",png);
    history.ReconcileImageImport([&](ProjectGraphState& state) {
        for (auto& node:state.graph.GetNodes()) if (OwnsPendingImageImport(node,uuid,request,false)) {
            node.image.isEmbedding=false; node.image.embeddingRequestId=0;
            state.images[uuid].png=bytes;
        }
    });
}

inline void ResolveImageImportFailure(ProjectGraphHistory& history, const std::string& uuid,
    std::uint64_t request) {
    history.ReconcileImageImport([&](ProjectGraphState& state) {
        int pending=-1;
        for (const auto& node:state.graph.GetNodes())
            if (OwnsPendingImageImport(node,uuid,request,true)) { pending=node.id; break; }
        if (pending>0) { state.graph.RemoveNode(pending); state.images.erase(uuid); }
    });
}
} // namespace Stack::Project
