#include "StartupReveal.h"

#include <algorithm>
#include <cmath>

namespace Stack::StartupReveal {
namespace {

constexpr double kFormDiskSeconds = 0.80;
constexpr double kExpandDiskSeconds = 0.65;
constexpr double kHoldSurfaceSeconds = 0.35;
constexpr double kRevealInterfaceSeconds = 0.45;
constexpr double kRevealCaptionButtonsSeconds = 0.30;
constexpr float kRestingDiskFraction = 0.15f;
constexpr float kMinimumFeatherPixels = 1.0f;

float Saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseOutCubic(float value) {
    const float t = Saturate(value) - 1.0f;
    return 1.0f + t * t * t;
}

float Lerp(float from, float to, float amount) {
    return from + (to - from) * amount;
}

float FullCoverageRadius(const ImVec2& viewportSize) {
    return std::sqrt(
        viewportSize.x * viewportSize.x + viewportSize.y * viewportSize.y) * 0.5f + 2.0f;
}

} // namespace

bool Visual::IsActive() const {
    return phase != Phase::Disabled && phase != Phase::Complete;
}

bool Visual::HasTransparentBackdrop() const {
    return phase == Phase::FormDisk || phase == Phase::ExpandDisk;
}

bool Visual::ShouldRenderInterface() const {
    return phase == Phase::RevealInterface ||
        phase == Phase::RevealCaptionButtons ||
        phase == Phase::Complete ||
        phase == Phase::Disabled;
}

bool Visual::AllowsInput() const {
    return phase == Phase::Complete || phase == Phase::Disabled;
}

void Controller::Enable() {
    m_Enabled = true;
    m_StartedAt = -1.0;
    m_Visual = {};
    m_Visual.phase = Phase::FormDisk;
    m_Visual.interfaceOpacity = 0.0f;
    m_Visual.captionButtonOpacity = 0.0f;
}

void Controller::Disable() {
    m_Enabled = false;
    m_StartedAt = -1.0;
    m_Visual = {};
    m_Visual.phase = Phase::Disabled;
}

void Controller::Start(double nowSeconds) {
    if (!m_Enabled) {
        return;
    }
    m_StartedAt = nowSeconds;
}

Visual Controller::Update(double nowSeconds, const ImVec2& viewportSize) {
    if (!m_Enabled) {
        m_Visual = {};
        m_Visual.phase = Phase::Disabled;
        return m_Visual;
    }

    const float shortEdge = std::max(1.0f, std::min(viewportSize.x, viewportSize.y));
    const float restingRadius = shortEdge * kRestingDiskFraction;
    const float fullCoverageRadius = FullCoverageRadius(viewportSize);
    const float maximumFeather = std::max(18.0f, restingRadius * 0.55f);
    const double elapsed = m_StartedAt < 0.0 ? 0.0 : std::max(0.0, nowSeconds - m_StartedAt);

    Visual visual;
    visual.interfaceOpacity = 0.0f;
    visual.captionButtonOpacity = 0.0f;

    if (elapsed < kFormDiskSeconds) {
        const float progress = EaseOutCubic(static_cast<float>(elapsed / kFormDiskSeconds));
        visual.phase = Phase::FormDisk;
        visual.diskOpacity = progress;
        visual.diskRadius = restingRadius * progress;
        visual.diskFeather = Lerp(maximumFeather, kMinimumFeatherPixels, progress);
    } else if (elapsed < kFormDiskSeconds + kExpandDiskSeconds) {
        const float progress = EaseOutCubic(static_cast<float>(
            (elapsed - kFormDiskSeconds) / kExpandDiskSeconds));
        visual.phase = Phase::ExpandDisk;
        visual.diskOpacity = 1.0f;
        visual.diskRadius = Lerp(restingRadius, fullCoverageRadius, progress);
        visual.diskFeather = kMinimumFeatherPixels;
    } else if (elapsed < kFormDiskSeconds + kExpandDiskSeconds + kHoldSurfaceSeconds) {
        visual.phase = Phase::HoldSurface;
        visual.diskOpacity = 1.0f;
        visual.diskRadius = fullCoverageRadius;
        visual.diskFeather = kMinimumFeatherPixels;
    } else if (elapsed < kFormDiskSeconds + kExpandDiskSeconds +
        kHoldSurfaceSeconds + kRevealInterfaceSeconds) {
        const float progress = EaseOutCubic(static_cast<float>(
            (elapsed - kFormDiskSeconds - kExpandDiskSeconds - kHoldSurfaceSeconds) /
            kRevealInterfaceSeconds));
        visual.phase = Phase::RevealInterface;
        visual.diskOpacity = 1.0f;
        visual.diskRadius = fullCoverageRadius;
        visual.diskFeather = kMinimumFeatherPixels;
        visual.interfaceOpacity = progress;
    } else if (elapsed < kFormDiskSeconds + kExpandDiskSeconds +
        kHoldSurfaceSeconds + kRevealInterfaceSeconds + kRevealCaptionButtonsSeconds) {
        const float progress = EaseOutCubic(static_cast<float>(
            (elapsed - kFormDiskSeconds - kExpandDiskSeconds - kHoldSurfaceSeconds -
            kRevealInterfaceSeconds) /
            kRevealCaptionButtonsSeconds));
        visual.phase = Phase::RevealCaptionButtons;
        visual.diskOpacity = 1.0f;
        visual.diskRadius = fullCoverageRadius;
        visual.diskFeather = kMinimumFeatherPixels;
        visual.interfaceOpacity = 1.0f;
        visual.captionButtonOpacity = progress;
    } else {
        visual.phase = Phase::Complete;
        visual.interfaceOpacity = 1.0f;
        visual.captionButtonOpacity = 1.0f;
    }

    m_Visual = visual;
    return m_Visual;
}

bool Controller::IsEnabled() const {
    return m_Enabled;
}

bool Controller::IsActive() const {
    return m_Visual.IsActive();
}

void Render(
    const Visual& visual,
    const ImVec2& viewportPos,
    const ImVec2& viewportSize,
    const ImVec4& surfaceColor,
    ImDrawList* drawList) {
    if (!drawList || visual.diskOpacity <= 0.001f || visual.diskRadius <= 0.001f) {
        return;
    }

    const ImVec2 center(
        viewportPos.x + viewportSize.x * 0.5f,
        viewportPos.y + viewportSize.y * 0.5f);
    ImVec4 solid = surfaceColor;
    solid.w *= visual.diskOpacity;

    constexpr int kFeatherRingCount = 12;
    if (visual.diskFeather > kMinimumFeatherPixels + 0.01f) {
        for (int ring = 0; ring < kFeatherRingCount; ++ring) {
            const float amount = static_cast<float>(ring + 1) /
                static_cast<float>(kFeatherRingCount);
            ImVec4 ringColor = solid;
            ringColor.w *= amount * amount;
            drawList->AddCircleFilled(
                center,
                visual.diskRadius + visual.diskFeather * (1.0f - amount),
                ImGui::ColorConvertFloat4ToU32(ringColor),
                96);
        }
    }

    drawList->AddCircleFilled(
        center,
        visual.diskRadius,
        ImGui::ColorConvertFloat4ToU32(solid),
        96);
}

} // namespace Stack::StartupReveal
