#pragma once

#include "Renderer/MaskRenderTypes.h"

namespace Stack::Renderer {

// Accumulate only requested dependencies. Document membership is separately
// used for resource ownership and must not select an unrelated output's format.
struct GraphResourcePolicy {
    bool rawStages = false;
    bool float32Targets = false;
    bool sourceColorGuide = false;

    void Include(const RenderGraphNode& node, bool rawSceneGraph) {
        using K = RenderGraphNodeKind;
        const bool raw = node.kind == K::RawSource || node.kind == K::RawDecode ||
            node.kind == K::RawDevelop || node.kind == K::RawDevelopment ||
            node.kind == K::RawProjectSourceSet || node.kind == K::RawOperation;
        rawStages |= raw;
        // Preserve the established RAW and math precision; do not promote a
        // separate output merely because its document contains such a node.
        float32Targets |= rawSceneGraph || node.kind == K::RawDevelopment || node.kind == K::DataMath;
        sourceColorGuide |= node.rawDevelopment.embeddedRawData &&
            bool(node.rawDevelopment.embeddedRawData->sceneColorGuide);
    }

    unsigned int TargetBytesPerPixel(bool retainedFloat32 = false) const {
        return float32Targets || retainedFloat32 ? 16u : 8u;
    }
};

} // namespace Stack::Renderer
