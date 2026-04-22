#include "RenderTab/Contracts/AccumulationManager.h"

#include <algorithm>
#include <limits>

namespace RenderContracts {

namespace {

constexpr auto kViewportResizeCommitDelay = std::chrono::milliseconds(150);
constexpr int kMaxAccumulatedSampleCount = std::numeric_limits<int>::max() - 1;

} // namespace

void AccumulationManager::ResetSlot(SlotState& slot) {
    slot.epochId = m_TransportEpoch;
    slot.sampleCount = 0;
    slot.hasCompletedImage = false;
}

void AccumulationManager::ResetForScene(const RenderFoundation::Settings& settings) {
    m_TransportEpoch = 1;
    m_DisplayEpoch = 1;
    m_VisibleSlotIndex = 0;
    m_RenderSlotIndex = 0;
    m_PartialSwapPending = false;
    m_SubmissionInFlight = false;
    m_CommittedViewportWidth = 0;
    m_CommittedViewportHeight = 0;
    m_PendingViewportWidth = 0;
    m_PendingViewportHeight = 0;
    m_ResizeInteractive = false;
    m_LastViewportExtentChange = {};
    ResetSlot(m_Slots[0]);
    ResetSlot(m_Slots[1]);

    if (settings.viewMode != RenderFoundation::ViewMode::PathTrace) {
        m_Slots[0].sampleCount = 1;
        m_Slots[0].hasCompletedImage = true;
        m_AccumulatedSamples = 1;
    } else {
        m_AccumulatedSamples = 0;
    }

    ClampSamplesForSettings(settings);
}

void AccumulationManager::BeginNewEpoch(const RenderFoundation::Settings& settings, ResetClass resetClass) {
    ++m_TransportEpoch;
    m_SubmissionInFlight = false;

    if (settings.viewMode != RenderFoundation::ViewMode::PathTrace) {
        ResetSlot(m_Slots[0]);
        ResetSlot(m_Slots[1]);
        m_Slots[0].sampleCount = 1;
        m_Slots[0].hasCompletedImage = true;
        m_VisibleSlotIndex = 0;
        m_RenderSlotIndex = 0;
        m_PartialSwapPending = false;
        m_AccumulatedSamples = 1;
        return;
    }

    const int nextSlot = 1 - m_VisibleSlotIndex;
    ResetSlot(m_Slots[nextSlot]);
    m_Slots[nextSlot].epochId = m_TransportEpoch;
    m_RenderSlotIndex = nextSlot;

    if (resetClass == ResetClass::PartialAccumulation) {
        m_PartialSwapPending = true;
    } else {
        m_VisibleSlotIndex = nextSlot;
        m_PartialSwapPending = false;
        m_AccumulatedSamples = 0;
    }
}

void AccumulationManager::ApplyChange(const SceneChangeSet& changeSet, const RenderFoundation::Settings& settings) {
    switch (changeSet.resetClass) {
        case ResetClass::None:
            break;
        case ResetClass::DisplayOnly:
            ++m_DisplayEpoch;
            break;
        case ResetClass::PartialAccumulation:
        case ResetClass::FullAccumulation:
            BeginNewEpoch(settings, changeSet.resetClass);
            break;
    }

    ClampSamplesForSettings(settings);
}

void AccumulationManager::TickFrame(const RenderFoundation::Settings& settings) {
    ClampSamplesForSettings(settings);
}

void AccumulationManager::UpdateViewportExtent(
    int width,
    int height,
    const RenderFoundation::Settings& settings) {

    if (width <= 0 || height <= 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_CommittedViewportWidth <= 0 || m_CommittedViewportHeight <= 0) {
        m_CommittedViewportWidth = width;
        m_CommittedViewportHeight = height;
        m_PendingViewportWidth = width;
        m_PendingViewportHeight = height;
        m_LastViewportExtentChange = now;
        m_ResizeInteractive = false;
        return;
    }

    if (width == m_CommittedViewportWidth && height == m_CommittedViewportHeight) {
        m_PendingViewportWidth = width;
        m_PendingViewportHeight = height;
        m_ResizeInteractive = false;
        return;
    }

    if (width != m_PendingViewportWidth || height != m_PendingViewportHeight) {
        m_PendingViewportWidth = width;
        m_PendingViewportHeight = height;
        m_LastViewportExtentChange = now;
        m_ResizeInteractive = true;
        return;
    }

    if ((now - m_LastViewportExtentChange) >= kViewportResizeCommitDelay) {
        m_CommittedViewportWidth = width;
        m_CommittedViewportHeight = height;
        m_ResizeInteractive = false;
        ApplyChange(
            { DirtyFlags::Viewport, ResetClass::FullAccumulation, "Viewport extent committed." },
            settings);
        return;
    }

    m_ResizeInteractive = true;
}

bool AccumulationManager::CanSubmitSample(const RenderFoundation::Settings& settings) const {
    if (settings.viewMode != RenderFoundation::ViewMode::PathTrace) {
        return false;
    }

    if (m_SubmissionInFlight || m_CommittedViewportWidth <= 0 || m_CommittedViewportHeight <= 0) {
        return false;
    }

    const SlotState& renderSlot = m_Slots[m_RenderSlotIndex];
    const int sampleLimit = settings.accumulationEnabled ? kMaxAccumulatedSampleCount : 1;
    return renderSlot.sampleCount < sampleLimit;
}

int AccumulationManager::GetNextSampleIndex(const RenderFoundation::Settings& settings) const {
    if (settings.viewMode != RenderFoundation::ViewMode::PathTrace) {
        return 0;
    }
    return std::max(0, m_Slots[m_RenderSlotIndex].sampleCount);
}

void AccumulationManager::MarkSubmissionStarted() {
    m_SubmissionInFlight = true;
}

void AccumulationManager::MarkSampleCompleted(const RenderFoundation::Settings& settings) {
    m_SubmissionInFlight = false;

    SlotState& renderSlot = m_Slots[m_RenderSlotIndex];
    renderSlot.hasCompletedImage = true;
    if (settings.accumulationEnabled) {
        renderSlot.sampleCount = std::min(
            renderSlot.sampleCount + 1,
            kMaxAccumulatedSampleCount);
    } else {
        renderSlot.sampleCount = 1;
    }

    if (m_PartialSwapPending) {
        m_VisibleSlotIndex = m_RenderSlotIndex;
        m_PartialSwapPending = false;
    }

    ClampSamplesForSettings(settings);
}

void AccumulationManager::ClearRenderSlotImage() {
    m_Slots[m_RenderSlotIndex].sampleCount = 0;
    m_Slots[m_RenderSlotIndex].hasCompletedImage = false;
    if (m_VisibleSlotIndex == m_RenderSlotIndex) {
        m_AccumulatedSamples = 0;
    }
}

void AccumulationManager::ClampSamplesForSettings(const RenderFoundation::Settings& settings) {
    if (settings.viewMode != RenderFoundation::ViewMode::PathTrace) {
        m_AccumulatedSamples = 1;
        return;
    }

    const int sampleLimit = settings.accumulationEnabled ? kMaxAccumulatedSampleCount : 1;
    m_Slots[0].sampleCount = std::clamp(m_Slots[0].sampleCount, 0, sampleLimit);
    m_Slots[1].sampleCount = std::clamp(m_Slots[1].sampleCount, 0, sampleLimit);
    m_AccumulatedSamples = std::clamp(m_Slots[m_VisibleSlotIndex].sampleCount, 0, sampleLimit);
}

} // namespace RenderContracts
