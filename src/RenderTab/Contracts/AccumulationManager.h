#pragma once

#include "RenderContracts.h"

#include "RenderTab/Foundation/RenderFoundationTypes.h"

#include <chrono>
#include <cstdint>

namespace RenderContracts {

class AccumulationManager {
public:
    AccumulationManager() = default;

    struct SlotState {
        std::uint64_t epochId = 1;
        int sampleCount = 0;
        bool hasCompletedImage = false;
    };

    void ResetForScene(const RenderFoundation::Settings& settings);
    void ApplyChange(const SceneChangeSet& changeSet, const RenderFoundation::Settings& settings);
    void TickFrame(const RenderFoundation::Settings& settings);
    void UpdateViewportExtent(int width, int height, const RenderFoundation::Settings& settings);

    bool CanSubmitSample(const RenderFoundation::Settings& settings) const;
    int GetNextSampleIndex(const RenderFoundation::Settings& settings) const;
    void MarkSubmissionStarted();
    void MarkSampleCompleted(const RenderFoundation::Settings& settings);
    void ClearRenderSlotImage();

    int GetVisibleSlotIndex() const { return m_VisibleSlotIndex; }
    int GetRenderSlotIndex() const { return m_RenderSlotIndex; }
    const SlotState& GetSlotState(int index) const { return m_Slots[index < 0 ? 0 : index > 1 ? 1 : index]; }
    int GetCommittedViewportWidth() const { return m_CommittedViewportWidth; }
    int GetCommittedViewportHeight() const { return m_CommittedViewportHeight; }
    bool IsResizeInteractive() const { return m_ResizeInteractive; }
    bool IsSubmissionInFlight() const { return m_SubmissionInFlight; }

    int GetAccumulatedSamples() const { return m_AccumulatedSamples; }
    std::uint64_t GetTransportEpoch() const { return m_TransportEpoch; }
    std::uint64_t GetDisplayEpoch() const { return m_DisplayEpoch; }

private:
    void ClampSamplesForSettings(const RenderFoundation::Settings& settings);
    void ResetSlot(SlotState& slot);
    void BeginNewEpoch(const RenderFoundation::Settings& settings, ResetClass resetClass);

    std::uint64_t m_TransportEpoch = 1;
    std::uint64_t m_DisplayEpoch = 1;
    SlotState m_Slots[2] {};
    int m_VisibleSlotIndex = 0;
    int m_RenderSlotIndex = 0;
    bool m_PartialSwapPending = false;
    bool m_SubmissionInFlight = false;
    int m_CommittedViewportWidth = 0;
    int m_CommittedViewportHeight = 0;
    int m_PendingViewportWidth = 0;
    int m_PendingViewportHeight = 0;
    bool m_ResizeInteractive = false;
    std::chrono::steady_clock::time_point m_LastViewportExtentChange {};
    int m_AccumulatedSamples = 1;
};

} // namespace RenderContracts
