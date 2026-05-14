#pragma once

#include <imgui.h>

namespace ImGuiExtras {
    struct NodeControlState {
        bool hovered = false;
        bool active = false;
        bool edited = false;
        bool popupOpen = false;
        ImGuiID id = 0;
    };

    float AnimateTowards(float current, float target, float deltaTime, float speed = 18.0f);
    float EaseOutCubic(float value);
    void DrawSpinner(const char* label, float radius, int thickness, ImU32 color);
    void RenderBusyOverlay(const char* message);
    void ResetNodeControlState();
    const NodeControlState& GetNodeControlState();

    // Node UI Helpers
    bool NodeSliderFloat(const char* label, const char* id, float* v, float v_min, float v_max, const char* format = "%.2f", float controlWidth = 0.0f);
    bool NodeSliderInt(const char* label, const char* id, int* v, int v_min, int v_max, const char* format = "%d", float controlWidth = 0.0f);
    bool NodeCheckbox(const char* label, const char* id, bool* v, float controlWidth = 0.0f);
    bool NodeCombo(const char* label, const char* id, int* current_item, const char* const items[], int items_count, float controlWidth = 0.0f);
    bool NodeColorEdit3(const char* label, const char* id, float color[3], ImGuiColorEditFlags flags = 0, float controlWidth = 0.0f);
    bool NodeColorEdit4(const char* label, const char* id, float color[4], ImGuiColorEditFlags flags = 0, float controlWidth = 0.0f);
    bool NodeInputFloat(const char* label, const char* id, float* v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", float controlWidth = 0.0f);
    bool NodeInputInt(const char* label, const char* id, int* v, int step = 1, int step_fast = 100, float controlWidth = 0.0f);
}
