#pragma once

#include "Editor/NodeGraph/NodeGraphTypes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

namespace Stack::Editor {

// Excludes RAW/multiframe sources, compounds with potentially hidden sources,
// and unknown future node kinds.
inline bool IsRasterToneProbeCacheNode(EditorNodeGraph::NodeKind kind) {
    using K = EditorNodeGraph::NodeKind;
    switch (kind) {
        case K::Image: case K::Lut: case K::Layer: case K::Output:
        case K::Composite: case K::Scope: case K::MaskGenerator:
        case K::MaskCombine: case K::Mix: case K::Preview: case K::MaskUtility:
        case K::ImageToMask: case K::ImageGenerator: case K::ChannelSplit:
        case K::ChannelCombine: case K::ConstantChannel: case K::CustomMask:
        case K::DataMath: case K::Value: case K::TechnicalImage:
        case K::FieldMean: case K::Reformat:
            return true;
        default: return false;
    }
}

inline bool CanCacheRasterToneProbe(bool rawWorkspace, bool maskWorkspace,
    bool liveTimelineDraft, bool rasterNodesOnly) {
    return !rawWorkspace && !maskWorkspace && !liveTimelineDraft && rasterNodesOnly;
}

struct ToneCurveProbeKey {
    std::weak_ptr<const void> owner;
    std::string documentId, nodeInstance;
    int nodeId = 0;
    std::uint64_t editRevision = 0, renderRevision = 0, structureRevision = 0;
    std::uint64_t dirtyGeneration = 0, sourceLoadGeneration = 0, acceptedRenderGeneration = 0;
    std::size_t sourceIdentity = 0;
    int frame = 0, duration = 0, fps = 0, livePreviewFrame = -1;
    float u = 0, v = 0;

    bool operator==(const ToneCurveProbeKey& other) const {
        if (owner.expired() || other.owner.expired() ||
            owner.owner_before(other.owner) || other.owner.owner_before(owner)) return false;
        const auto fields = [](const ToneCurveProbeKey& k) {
            return std::tie(k.documentId, k.nodeInstance, k.nodeId, k.editRevision,
                k.renderRevision, k.structureRevision, k.dirtyGeneration,
                k.sourceLoadGeneration, k.acceptedRenderGeneration, k.sourceIdentity,
                k.frame, k.duration, k.fps, k.livePreviewFrame, k.u, k.v);
        };
        return fields(*this) == fields(other);
    }
};

// One successful CPU sample only. No document, pixels or GPU resources retained.
class ToneCurveProbeCache {
public:
    void Clear() { m_Key.reset(); }
    bool Find(const ToneCurveProbeKey& key, std::array<float, 4>& rgba) {
        if (m_Key && *m_Key == key) { rgba = m_Rgba; return true; }
        Clear(); // A later failure must not resurrect an earlier result.
        return false;
    }
    void StoreSuccess(const ToneCurveProbeKey& key, const std::array<float, 4>& rgba) {
        if (key.owner.expired()) { Clear(); return; }
        m_Key = key;
        m_Rgba = rgba;
    }
private:
    std::optional<ToneCurveProbeKey> m_Key;
    std::array<float, 4> m_Rgba{};
};

} // namespace Stack::Editor
