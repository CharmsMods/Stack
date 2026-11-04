#pragma once
#include "Renderer/SceneColorGuidance.h"
#include "Renderer/ScopedGLObjects.h"
#include "Raw/RawColorCalibration.h"
#include "Raw/SceneColorGuide.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace Stack::Renderer {
// Color conversion and geometry actually applied by the RAW developer. Exposure
// and authored tone are deliberately absent: this guide describes source color.
struct SceneColorGuideTransform {
    std::array<float,9> matrix{1,0,0,0,1,0,0,0,1};
    int orientation=1;
    bool flipHorizontally=false,flipVertically=false;
    bool operator==(const SceneColorGuideTransform& other) const {
        return matrix==other.matrix && orientation==other.orientation &&
            flipHorizontally==other.flipHorizontally && flipVertically==other.flipVertically;
    }
};
SceneColorGuideTransform BuildSceneColorGuideTransform(const Raw::RawMetadata&,
    const Raw::RawDevelopSettings&,const RawRecipe::RawColorCalibrationTransform& = {});

// Renderer/context-owned, bounded to two prepared guides (at most 10 MiB GPU).
// Leases keep active handles alive during nested evaluation and eviction. No
// full-resolution image readback, or CPU guide validation on cache hits.
class SceneColorGuideCache {
public:
    static constexpr std::uint64_t kMaximumResidentBytes=kMaximumPreparedSceneColorGuideBytes;
    // Holds the GL allocation through nested graph/mask evaluations. Layers
    // borrow only view; the executing caller retains this lease until done.
    struct Lease {
        SceneColorGuidance view;
        std::shared_ptr<const void> owner;
    };
    Lease Prepare(const std::shared_ptr<const Raw::SceneColorGuide>&,
        const SceneColorGuideTransform&,const std::function<bool()>& shouldCancel = {});
    void Clear() { m_Entries.clear(); }
private:
    struct Entry {
        std::shared_ptr<const Raw::SceneColorGuide> source;
        SceneColorGuideTransform transform;
        ScopedGLTexture color,confidence;
        std::uint64_t revision=0;
    };
    std::vector<std::shared_ptr<Entry>> m_Entries;
};
}
