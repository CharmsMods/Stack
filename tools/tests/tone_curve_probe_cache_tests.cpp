#include "Editor/ToneCurveProbeCache.h"
#include <stdexcept>
#include <iostream>
#include <limits>

namespace {
void Require(bool condition) {
    if (!condition) throw std::runtime_error("Tone curve probe cache check failed");
}
}

int main() {
    using namespace Stack::Editor;
    using K = EditorNodeGraph::NodeKind;
    Require(IsRasterToneProbeCacheNode(K::Image));
    Require(IsRasterToneProbeCacheNode(K::Layer));
    for (auto kind : {K::RawSource, K::RawDecode, K::RawDevelop, K::RawDevelopment,
            K::RawProjectFrame, K::RawProjectSourceSet, K::RawOperation,
            K::MultiFrameDenoise, K::MultiFrameHdr, K::Mfsr, K::HdrMerge, K::Compound, K::Count})
        Require(!IsRasterToneProbeCacheNode(kind));
    Require(CanCacheRasterToneProbe(false, false, false, true));
    Require(!CanCacheRasterToneProbe(true, false, false, true));
    Require(!CanCacheRasterToneProbe(false, true, false, true));
    Require(!CanCacheRasterToneProbe(false, false, true, true));
    Require(!CanCacheRasterToneProbe(false, false, false, false));
    auto owner = std::make_shared<int>(1);
    ToneCurveProbeKey key;
    key.owner = owner; key.documentId = "document"; key.nodeInstance = "node";
    key.nodeId = 3; key.u = .25f; key.v = .5f;
    ToneCurveProbeCache cache;
    std::array<float, 4> result{}, sample{.1f, .2f, .3f, 1.f};
    Require(!cache.Find(key, result)); // failures are not stored
    cache.StoreSuccess(key, sample);
    for (int frame = 0; frame < 120; ++frame) {
        Require(cache.Find(key, result)); Require(result == sample);
    }
    const auto miss = [&](auto change) {
        cache.StoreSuccess(key, sample);
        auto changed = key; change(changed);
        Require(!cache.Find(changed, result));
        Require(!cache.Find(key, result)); // failed changed request cannot revive old success
    };
    miss([](auto& k) { k.documentId += "new"; });
    miss([](auto& k) { k.nodeInstance += "new"; });
    miss([](auto& k) { ++k.nodeId; });
    miss([](auto& k) { ++k.editRevision; });
    miss([](auto& k) { ++k.renderRevision; });
    miss([](auto& k) { ++k.structureRevision; });
    miss([](auto& k) { ++k.dirtyGeneration; });
    miss([](auto& k) { ++k.sourceLoadGeneration; });
    miss([](auto& k) { ++k.acceptedRenderGeneration; });
    miss([](auto& k) { ++k.sourceIdentity; });
    miss([](auto& k) { ++k.frame; });
    miss([](auto& k) { ++k.duration; });
    miss([](auto& k) { ++k.fps; });
    miss([](auto& k) { ++k.livePreviewFrame; });
    miss([](auto& k) { k.u += .01f; });
    miss([](auto& k) { k.v += .01f; });
    miss([](auto& k) { k.u = std::numeric_limits<float>::quiet_NaN(); });
    auto otherOwner = std::make_shared<int>(1);
    miss([&](auto& k) { k.owner = otherOwner; });
    cache.StoreSuccess(key, sample); cache.Clear(); Require(!cache.Find(key, result));
    cache.StoreSuccess(key, sample); owner.reset(); Require(!cache.Find(key, result));
    cache.StoreSuccess(key, sample); Require(!cache.Find(key, result));
    std::cout << "Tone curve probe cache tests passed\n";
}
