#include "RenderCamera.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kDefaultYawDegrees = 18.0f;
constexpr float kDefaultPitchDegrees = -12.0f;
constexpr float kDefaultFieldOfViewDegrees = 50.0f;
constexpr float kDefaultFocusDistance = 6.0f;
constexpr float kDefaultApertureRadius = 0.0f;
constexpr float kDefaultExposure = 1.0f;
constexpr RenderFloat3 kLegacyOrbitTarget { 0.0f, 0.75f, 0.0f };

float NormalizeYawDegrees(float value) {
    while (value <= -180.0f) {
        value += 360.0f;
    }
    while (value > 180.0f) {
        value -= 360.0f;
    }
    return value;
}

RenderFloat3 BuildForwardVector(float yawDegrees, float pitchDegrees) {
    const float yawRadians = yawDegrees * 0.01745329252f;
    const float pitchRadians = pitchDegrees * 0.01745329252f;
    return Normalize(MakeRenderFloat3(
        std::cos(pitchRadians) * std::cos(yawRadians),
        std::sin(pitchRadians),
        std::cos(pitchRadians) * std::sin(yawRadians)));
}

RenderFloat3 BuildLegacyOrbitPosition(float yawDegrees, float pitchDegrees, float focusDistance) {
    const RenderFloat3 forward = BuildForwardVector(yawDegrees, pitchDegrees);
    return Subtract(kLegacyOrbitTarget, Scale(forward, std::max(focusDistance, 0.5f)));
}

} // namespace

RenderCamera::RenderCamera()
    : m_Name("Default Camera")
    , m_Position(BuildLegacyOrbitPosition(kDefaultYawDegrees, kDefaultPitchDegrees, kDefaultFocusDistance))
    , m_YawDegrees(kDefaultYawDegrees)
    , m_PitchDegrees(kDefaultPitchDegrees)
    , m_FieldOfViewDegrees(kDefaultFieldOfViewDegrees)
    , m_FocusDistance(kDefaultFocusDistance)
    , m_ApertureRadius(kDefaultApertureRadius)
    , m_Exposure(kDefaultExposure)
    , m_Revision(1)
    , m_LastChangeReason("Initial camera state.") {
}

RenderFloat3 RenderCamera::GetForwardVector() const {
    return BuildForwardVector(m_YawDegrees, m_PitchDegrees);
}

RenderFloat3 RenderCamera::GetRightVector() const {
    RenderFloat3 right = Cross(GetForwardVector(), MakeRenderFloat3(0.0f, 1.0f, 0.0f));
    if (Length(right) < 0.0001f) {
        right = MakeRenderFloat3(1.0f, 0.0f, 0.0f);
    }
    return Normalize(right);
}

RenderFloat3 RenderCamera::GetUpVector() const {
    return Normalize(Cross(GetRightVector(), GetForwardVector()));
}

bool RenderCamera::SetPosition(const RenderFloat3& value) {
    if (NearlyEqual(m_Position, value)) {
        return false;
    }

    m_Position = value;
    Touch("Camera position changed.");
    return true;
}

bool RenderCamera::SetYawDegrees(float value) {
    const float normalized = NormalizeYawDegrees(value);
    if (NearlyEqual(m_YawDegrees, normalized)) {
        return false;
    }

    m_YawDegrees = normalized;
    Touch("Camera yaw changed.");
    return true;
}

bool RenderCamera::SetPitchDegrees(float value) {
    const float clamped = std::clamp(value, -89.0f, 89.0f);
    if (NearlyEqual(m_PitchDegrees, clamped)) {
        return false;
    }

    m_PitchDegrees = clamped;
    Touch("Camera pitch changed.");
    return true;
}

bool RenderCamera::SetFieldOfViewDegrees(float value) {
    const float clamped = std::clamp(value, 20.0f, 110.0f);
    if (NearlyEqual(m_FieldOfViewDegrees, clamped)) {
        return false;
    }

    m_FieldOfViewDegrees = clamped;
    Touch("Camera field of view changed.");
    return true;
}

bool RenderCamera::SetFocusDistance(float value) {
    const float clamped = std::clamp(value, 0.1f, 100.0f);
    if (NearlyEqual(m_FocusDistance, clamped)) {
        return false;
    }

    m_FocusDistance = clamped;
    Touch("Camera focus distance changed.");
    return true;
}

bool RenderCamera::SetApertureRadius(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.5f);
    if (NearlyEqual(m_ApertureRadius, clamped)) {
        return false;
    }

    m_ApertureRadius = clamped;
    Touch("Camera aperture radius changed.");
    return true;
}

bool RenderCamera::SetExposure(float value) {
    const float clamped = std::clamp(value, 0.25f, 4.0f);
    if (NearlyEqual(m_Exposure, clamped)) {
        return false;
    }

    m_Exposure = clamped;
    Touch("Camera exposure changed.");
    return true;
}

bool RenderCamera::ResetToDefaultView(const std::string& reason) {
    return ApplySnapshot(
        BuildLegacyOrbitPosition(kDefaultYawDegrees, kDefaultPitchDegrees, kDefaultFocusDistance),
        kDefaultYawDegrees,
        kDefaultPitchDegrees,
        kDefaultFieldOfViewDegrees,
        kDefaultFocusDistance,
        kDefaultApertureRadius,
        kDefaultExposure,
        reason.empty() ? std::string("Camera reset to the default view.") : reason);
}

bool RenderCamera::ApplySnapshot(
    const RenderFloat3& position,
    float yawDegrees,
    float pitchDegrees,
    float fieldOfViewDegrees,
    float focusDistance,
    float apertureRadius,
    float exposure,
    const std::string& reason) {
    const float normalizedYaw = NormalizeYawDegrees(yawDegrees);
    const float clampedPitch = std::clamp(pitchDegrees, -89.0f, 89.0f);
    const float clampedFieldOfView = std::clamp(fieldOfViewDegrees, 20.0f, 110.0f);
    const float clampedFocusDistance = std::clamp(focusDistance, 0.1f, 100.0f);
    const float clampedApertureRadius = std::clamp(apertureRadius, 0.0f, 1.5f);
    const float clampedExposure = std::clamp(exposure, 0.25f, 4.0f);

    if (NearlyEqual(m_Position, position) &&
        NearlyEqual(m_YawDegrees, normalizedYaw) &&
        NearlyEqual(m_PitchDegrees, clampedPitch) &&
        NearlyEqual(m_FieldOfViewDegrees, clampedFieldOfView) &&
        NearlyEqual(m_FocusDistance, clampedFocusDistance) &&
        NearlyEqual(m_ApertureRadius, clampedApertureRadius) &&
        NearlyEqual(m_Exposure, clampedExposure)) {
        return false;
    }

    m_Position = position;
    m_YawDegrees = normalizedYaw;
    m_PitchDegrees = clampedPitch;
    m_FieldOfViewDegrees = clampedFieldOfView;
    m_FocusDistance = clampedFocusDistance;
    m_ApertureRadius = clampedApertureRadius;
    m_Exposure = clampedExposure;
    Touch(reason.empty() ? std::string("Camera snapshot applied.") : reason);
    return true;
}

bool RenderCamera::ApplySnapshot(
    float yawDegrees,
    float pitchDegrees,
    float fieldOfViewDegrees,
    float focusDistance,
    float apertureRadius,
    float exposure,
    const std::string& reason) {
    return ApplySnapshot(
        BuildLegacyOrbitPosition(yawDegrees, pitchDegrees, focusDistance),
        yawDegrees,
        pitchDegrees,
        fieldOfViewDegrees,
        focusDistance,
        apertureRadius,
        exposure,
        reason);
}

void RenderCamera::Touch(const std::string& reason) {
    ++m_Revision;
    m_LastChangeReason = reason;
}
