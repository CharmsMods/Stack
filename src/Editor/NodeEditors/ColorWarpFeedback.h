#pragma once

#include "Raw/RawColorWarpSelection.h"
#include "Renderer/RenderPipeline.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace Stack::Editor {

struct ColorWarpFeedbackData {
    std::size_t selectionFingerprint = 0;
    RawRecipe::RawColorWarpMaskGuide guide;
    std::vector<RawRecipe::RawColorWarpMaskFields> fields;

    void SampleMasks(const RawRecipe::RawColorWarpRecipe& recipe,
        const RawRecipe::RawColorWarpCoordinate& input, float u, float v,
        std::vector<RawRecipe::RawColorWarpMaskSample>& samples) const;
};

std::vector<unsigned char> BuildColorWarpFeedbackOverlay(const ColorWarpFeedbackData& data,
    const RawRecipe::RawColorWarpRecipe& recipe,
    const std::vector<RawRecipe::RawColorWarpPin>& inspectedPins);

// Per-node presentation cache, shared by the Graph and RAW hosts. Jobs own
// copies of their inputs and publish only into their own request object.
class ColorWarpFeedback {
    struct Request {
        std::size_t key = 0;
        std::atomic<bool> cancelled { false };
        std::atomic<bool> done { false };
        std::mutex mutex;
        std::shared_ptr<const ColorWarpFeedbackData> data;
    };
    std::size_t m_DesiredKey = 0;
    std::shared_ptr<Request> m_Request;
public:
    ~ColorWarpFeedback() { if (m_Request) m_Request->cancelled = true; }
    void Update(const RawDevelopmentGraphScopeReadback& scope,
        const std::string& targetIdentity, std::size_t inputFingerprint,
        const RawRecipe::RawColorWarpRecipe& recipe, Raw::RawWorkingSpace workingSpace,
        const std::function<bool(std::function<void()>)>& submit);
    std::shared_ptr<const ColorWarpFeedbackData> Current() const;
    std::size_t Key() const { return m_DesiredKey; }
};

} // namespace Stack::Editor
