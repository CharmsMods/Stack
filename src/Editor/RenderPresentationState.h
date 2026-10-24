#pragma once

#include "Editor/GraphRenderPolicy.h"
#include <cstdint>
#include <string>

namespace Stack::EditorRendering {

// Processing completeness belongs to the accepted pixels, not to worker
// idleness or native dimensions. A continuation can replace them only while
// this exact presentation still awaits it, and only with a newer attempt.
struct RenderPresentationState {
    std::uint64_t ownerId = 0, generation = 0, schedulingSerial = 0;
    GraphRendering::RequestTag graphRequest;
    // Graph-disabled explicit packets have no document revision in their
    // emitted tag. Retain the submitted document fence with accepted pixels.
    bool hasExplicitDocumentContext = false;
    GraphRendering::RequestTag explicitDocumentContext;
    std::string sourceKey;
    std::uint64_t sourceHash = 0;
    std::size_t recipeRevision = 0;
    bool denoisePending = false, failed = false;
    std::string error;
};

struct FullQualityRenderRequestState {
    enum class Phase { None, Requested, Submitted, Ready, Failed, Canceled };
    Phase phase = Phase::None;
    std::uint64_t ownerId = 0, generation = 0;
    GraphRendering::RequestTag graphRequest;
    // The UI context fence and the emitted packet mode are distinct: RAW
    // explicit snapshots intentionally bypass regional Graph preview mode.
    GraphRendering::RequestTag emittedGraphRequest;
    std::string sourceKey;
    std::uint64_t sourceHash = 0;
    std::size_t recipeRevision = 0;
    std::string error;
    bool Pending() const { return phase == Phase::Requested || phase == Phase::Submitted; }
};

} // namespace Stack::EditorRendering
