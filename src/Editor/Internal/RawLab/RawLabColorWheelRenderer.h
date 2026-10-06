#pragma once

#include "Raw/RawImageData.h"
#include <imgui.h>

namespace Stack::Editor::RawLabInternal {

class RawLabColorWheelRenderer {
public:
    RawLabColorWheelRenderer() = default;
    RawLabColorWheelRenderer(const RawLabColorWheelRenderer&) = delete;
    RawLabColorWheelRenderer& operator=(const RawLabColorWheelRenderer&) = delete;

    bool QueueDraw(ImDrawList& drawList, ImVec2 min, ImVec2 max,
        Raw::RawWorkingSpace workingSpace, float lightness, float extent);
    // The program is independent of the source image; retain it across source changes.
    void Shutdown();

private:
    struct DrawRequest {
        RawLabColorWheelRenderer* renderer = nullptr;
        ImVec2 min, size, displayPos, displaySize, framebufferScale;
        Raw::RawWorkingSpace workingSpace = Raw::RawWorkingSpace::LinearSrgbD65;
        float lightness = 0.7f, extent = 0.45f, alpha = 1.0f;
    };
    bool Initialize();
    static void DrawCallback(const ImDrawList*, const ImDrawCmd* command);
    unsigned int m_Program = 0;
    unsigned int m_VertexArray = 0;
    bool m_InitializationAttempted = false;
    int m_RectLocation = -1, m_DisplayLocation = -1, m_ParametersLocation = -1;
    int m_WorkingSpaceLocation = -1;
};

} // namespace Stack::Editor::RawLabInternal
