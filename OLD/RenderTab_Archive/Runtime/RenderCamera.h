#pragma once

#include "RenderTab/Runtime/Geometry/RenderMath.h"

#include <cstdint>
#include <string>

class RenderCamera {
public:
    RenderCamera();

    const std::string& GetName() const { return m_Name; }
    const RenderFloat3& GetPosition() const { return m_Position; }
    float GetYawDegrees() const { return m_YawDegrees; }
    float GetPitchDegrees() const { return m_PitchDegrees; }
    float GetFieldOfViewDegrees() const { return m_FieldOfViewDegrees; }
    float GetFocusDistance() const { return m_FocusDistance; }
    float GetApertureRadius() const { return m_ApertureRadius; }
    float GetExposure() const { return m_Exposure; }
    std::uint64_t GetRevision() const { return m_Revision; }
    const std::string& GetLastChangeReason() const { return m_LastChangeReason; }

    RenderFloat3 GetForwardVector() const;
    RenderFloat3 GetRightVector() const;
    RenderFloat3 GetUpVector() const;

    bool SetPosition(const RenderFloat3& value);
    bool SetYawDegrees(float value);
    bool SetPitchDegrees(float value);
    bool SetFieldOfViewDegrees(float value);
    bool SetFocusDistance(float value);
    bool SetApertureRadius(float value);
    bool SetExposure(float value);
    bool ResetToDefaultView(const std::string& reason = {});
    bool ApplySnapshot(
        const RenderFloat3& position,
        float yawDegrees,
        float pitchDegrees,
        float fieldOfViewDegrees,
        float focusDistance,
        float apertureRadius,
        float exposure,
        const std::string& reason);
    bool ApplySnapshot(
        float yawDegrees,
        float pitchDegrees,
        float fieldOfViewDegrees,
        float focusDistance,
        float apertureRadius,
        float exposure,
        const std::string& reason);

private:
    void Touch(const std::string& reason);

    std::string m_Name;
    RenderFloat3 m_Position {};
    float m_YawDegrees = 18.0f;
    float m_PitchDegrees = -12.0f;
    float m_FieldOfViewDegrees = 50.0f;
    float m_FocusDistance = 6.0f;
    float m_ApertureRadius = 0.0f;
    float m_Exposure = 1.0f;
    std::uint64_t m_Revision = 0;
    std::string m_LastChangeReason;
};
