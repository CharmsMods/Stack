#pragma once

#include "Editor/Internal/RawLab/RawLabColorCloudRenderer.h"
#include "Raw/RawImageData.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct RawDevelopmentGraphScopeReadback;
namespace Stack::RawRecipe { struct RawColorWarpRecipe; }

namespace Stack::Editor {
struct ColorWarpFeedbackData;

struct ColorWarpCloudDemand {
    std::size_t inputKey = 0, feedbackKey = 0;
    std::uint64_t nodeRevision = 0, localRevision = 0;
    Raw::RawWorkingSpace workingSpace = Raw::RawWorkingSpace::LinearSrgbD65;
    int selected = -1;
    bool live = false, preview = false;
};

// Per-node derived presentation. Workers own immutable samples and recipes;
// only the caller uploads completed vertices on its existing GL context.
class ColorWarpCloudPreparation {
public:
    using Vertices = std::vector<RawLabInternal::RawLabColorCloudGpuVertex>;
    using Submit = std::function<bool(std::function<void()>)>;
    ColorWarpCloudPreparation();
    ~ColorWarpCloudPreparation();
    ColorWarpCloudPreparation(const ColorWarpCloudPreparation&) = delete;
    ColorWarpCloudPreparation& operator=(const ColorWarpCloudPreparation&) = delete;

    std::optional<Vertices> Update(const RawDevelopmentGraphScopeReadback& scope,
        const RawRecipe::RawColorWarpRecipe& recipe,
        std::shared_ptr<const ColorWarpFeedbackData> feedback,
        const ColorWarpCloudDemand& demand, const Submit& submit);
    bool InputChanged() const;
    bool SelectionCurrent() const;

private:
    struct State;
    std::unique_ptr<State> m_State;
};
}
