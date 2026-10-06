#pragma once

#include "Raw/RawGradingScope.h"

// Context-owned scratch storage. Each in-flight capture owns its output buffer.
class RawGradingScopeGpu {
public:
    bool Dispatch(unsigned int texture, int width, int height,
        Raw::RawWorkingSpace workingSpace, bool sceneLinear, bool encodedSrgb,
        unsigned int& outputBuffer);
    static bool Read(unsigned int outputBuffer, RawDevelopmentGradingScopeReadback& readback);
    void Shutdown();

private:
    unsigned int m_Program = 0;
    unsigned int m_Counts = 0;
    bool m_InitializationAttempted = false;
    int m_PhaseLocation = -1;
    int m_SizeLocation = -1;
    int m_SceneLinearLocation = -1;
    int m_EncodedLocation = -1;
    int m_Rec2020Location = -1;
};
