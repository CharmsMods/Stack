#pragma once
#include <imgui.h>
#include <deque>

namespace Raw {
class ViewportFadeRenderer {
public:
    ~ViewportFadeRenderer();
    bool Initialize();
    void Shutdown();
    void Bind(unsigned int previous, float amount, bool encoded, const float* projection, bool vertexColor = true,
        ImVec2 previousOffset = ImVec2(0,0), ImVec2 previousScale = ImVec2(1,1)) const;
    bool Draw(ImDrawList* drawList, unsigned int previous, unsigned int current,
        ImVec2 minimum, ImVec2 maximum, float amount, bool encoded,
        ImVec2 uvMinimum = ImVec2(0, 1), ImVec2 uvMaximum = ImVec2(1, 0),
        ImU32 tint = IM_COL32_WHITE, float rounding = 0,
        ImVec2 previousOffset = ImVec2(0,0), ImVec2 previousScale = ImVec2(1,1));
private:
    struct Command {
        const ViewportFadeRenderer* renderer = nullptr;
        unsigned int previous = 0;
        float amount = 1;
        bool encoded = false;
        ImVec2 displayPosition, displaySize;
        ImVec2 previousOffset, previousScale;
    };
    std::deque<Command> m_Commands;
    int m_CommandFrame = -1;
    unsigned int m_Program = 0;
};
}
