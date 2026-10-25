// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Animation/SliderMotion.h"
#include "Renderer/GlassChrome.h"
#include <imgui_internal.h>
#include <string>
#include <unordered_map>

namespace ImGuiExtras {
// One editing panel owns its resources and transient control state. No authored
// values, parameter pointers, window pointers or callbacks survive the frame.
struct SidePanelSliderState {
    struct Motion {
        Stack::Animation::SliderMotion slider;
        Stack::Animation::ScalarChannel width{1.f};
        int lastFrame=-1;
        bool held=false;
    };
    Stack::Renderer::GlassChrome material;
    ImDrawListSplitter splitter; // Retain channel buffers across panel frames.
    std::unordered_map<ImGuiID,Motion> controls;
    std::string identity;
    bool enabled=false, errorReported=false;
    void BeginFrame(bool requested, const std::string& owner);
    void Shutdown();
};

// Restricts presentation to the current panel window. Popups, floating tools,
// other windows and calls outside this scope retain their native sliders.
class SidePanelSliderScope {
public:
    explicit SidePanelSliderScope(SidePanelSliderState&, float opacity=1.f);
    ~SidePanelSliderScope();
    SidePanelSliderScope(const SidePanelSliderScope&)=delete;
    SidePanelSliderScope& operator=(const SidePanelSliderScope&)=delete;
    void Draw(const ImRect& frame, const ImRect& grab, double value, double minimum, double maximum);
private:
    SidePanelSliderScope* previous_;
    SidePanelSliderState& state_;
    ImGuiWindow* window_;
    ImDrawListSplitter& splitter_;
    bool split_=false, captured_=false;
    float opacity_=1.f;
    friend bool HasSidePanelSliderScope();
};

bool HasSidePanelSliderScope();
bool SidePanelSliderScalar(const char* label, ImGuiDataType type, void* value,
    const void* minimum, const void* maximum, const char* format=nullptr, ImGuiSliderFlags flags=0);
bool SidePanelSliderFloat(const char* label, float* value, float minimum, float maximum,
    const char* format="%.3f", ImGuiSliderFlags flags=0);
bool SidePanelSliderInt(const char* label, int* value, int minimum, int maximum,
    const char* format="%d", ImGuiSliderFlags flags=0);
}
