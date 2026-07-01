#pragma once

#include <imgui.h>
#include <string>

namespace ImGuiExtras {
    struct NodeControlState {
        bool hovered = false;
        bool active = false;
        bool edited = false;
        bool popupOpen = false;
        bool rightClickConsumed = false;
        bool lastHovered = false;
        bool lastActive = false;
        bool lastEdited = false;
        bool lastPopupOpen = false;
        bool lastRightClickConsumed = false;
        ImGuiID id = 0;
    };

    enum class GraphSliderRangePolicy {
        Bounded,
        Unclamped
    };

    struct GraphNodeControlScopeConfig {
        float labelWidth = 70.0f;
        float valueWidth = 48.0f;
        float minSliderWidth = 78.0f;
        float scale = 1.0f;
        bool allowSliderTextEntry = false;
        bool useScrubHandles = false;
        GraphSliderRangePolicy rangePolicy = GraphSliderRangePolicy::Bounded;
        float scrubSensitivity = 1.0f;
    };

    enum class CursorCaptureMode {
        None,
        LockedScrub,
        LockedPan
    };

    struct CursorCaptureRequest {
        CursorCaptureMode mode = CursorCaptureMode::None;
        ImVec2 anchorScreenPos = ImVec2(0.0f, 0.0f);
        ImVec2 restoreScreenPos = ImVec2(0.0f, 0.0f);
    };

    float AnimateTowards(float current, float target, float deltaTime, float speed = 18.0f);
    float EaseOutCubic(float value);
    void BeginFrameInputRouting();
    bool IsSliderWheelModifierActive();
    bool IsSliderWheelConsumed();
    float GetSliderWheelDelta();
    void DrawSpinner(const char* label, float radius, int thickness, ImU32 color);
    void DrawSpinnerOnly(float radius, int thickness, ImU32 color);
    void RenderSpinnerOnlyOverlay(float alpha = 1.0f);
    void RenderBusyOverlay(const char* message);
    void RenderProgressOverlay(const char* message, float progress);
    void ResetNodeControlState();
    const NodeControlState& GetNodeControlState();
    void BeginGraphNodeControlScope(const GraphNodeControlScopeConfig& config = {});
    void EndGraphNodeControlScope();
    bool IsGraphNodeControlScopeActive();
    void SubmitCursorCaptureRequest(const CursorCaptureRequest& request);
    bool ConsumeCursorCaptureRequest(CursorCaptureRequest* outRequest);
    void RichSectionLabel(const char* label, float spacingAfter = 0.0f);
    bool RichFullWidthButton(const char* label, float width, float height = 0.0f);
    void RichColorSwatchRow(
        const char* swatchId,
        const float color[3],
        float swatchWidth,
        float swatchHeight,
        const char* valueText,
        float totalWidth,
        float spacing = 0.0f);

    // Node UI Helpers
    bool NodeSliderFloat(const char* label, const char* id, float* v, float v_min, float v_max, const char* format = "%.2f", float controlWidth = 0.0f);
    bool NodeSliderInt(const char* label, const char* id, int* v, int v_min, int v_max, const char* format = "%d", float controlWidth = 0.0f);
    bool NodeCheckbox(const char* label, const char* id, bool* v, float controlWidth = 0.0f);
    bool NodeCombo(const char* label, const char* id, int* current_item, const char* const items[], int items_count, float controlWidth = 0.0f);
    bool NodeColorEdit3(const char* label, const char* id, float color[3], ImGuiColorEditFlags flags = 0, float controlWidth = 0.0f);
    bool NodeColorEdit4(const char* label, const char* id, float color[4], ImGuiColorEditFlags flags = 0, float controlWidth = 0.0f);
    bool NodeInputFloat(const char* label, const char* id, float* v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", float controlWidth = 0.0f);
    bool NodeInputInt(const char* label, const char* id, int* v, int step = 1, int step_fast = 100, float controlWidth = 0.0f);
    bool NodeTextMultiline(const char* label, const char* id, std::string& value, float controlWidth = 0.0f, int lineCount = 4);
    bool GradeWheel3(const char* label, const char* id, float color[3], float width = 0.0f);
}
