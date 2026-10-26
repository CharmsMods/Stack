// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Animation/SliderMotion.h"
#include "Renderer/GlassChrome.h"
#include "Renderer/Glass/Engine/motion_presets.hpp"
#include "Renderer/Glass/Engine/morph.hpp"
#include <imgui_internal.h>
#include <string>
#include <unordered_map>

namespace ImGuiExtras {
enum class GlassElements { Controls, FloatingTools };
enum class GlassMotionRole { GraphPoint, Slider, Selection };
struct GlassSelectionInteraction {
    Stack::Animation::ControlInput input;
    float position=0;
    bool direct=false,cancelled=false;
};
// One editing surface owns its resources and transient control state. No authored
// values, parameter pointers, window pointers or callbacks survive the frame.
struct GlassControlState {
    struct Motion {
        Stack::Animation::SliderMotion slider;
        Stack::Animation::ScalarChannel activation,strain,tilt,contact;
        float previousX=0;
        double previousTime=0;
        int lastFrame=-1;
        bool held=false;
    };
    Stack::Renderer::GlassChrome material;
    ImDrawListSplitter splitter; // Retain channel buffers across panel frames.
    ImDrawListSplitter inlineSplitter; // Short scope, wholly inside one table channel.
    std::unordered_map<ImGuiID,Motion> controls;
    struct PointMotion {
        std::optional<stack::glass::motion::MotionState> visual;
        stack::glass::motion::MotionOutput output;
        std::optional<stack::glass::engine::ShapeMotion> selection;
        GlassMotionRole role=GlassMotionRole::GraphPoint;
        double time=0;
        int frame=-1;
    };
    std::unordered_map<ImGuiID,PointMotion> points;
    struct Feedback { Stack::Animation::ControlMotion motion; int lastFrame=-1; };
    std::unordered_map<ImGuiID,Feedback> feedback;
    std::string identity;
    bool enabled=false, errorReported=false;
    void BeginFrame(bool requested, const std::string& owner);
    void Shutdown();
};

// Restricts presentation to the opted-in window. Other windows and calls outside
// this scope retain their native presentation; this never creates hit targets.
class GlassControlScope {
public:
    explicit GlassControlScope(GlassControlState&, float opacity=1.f, bool inlineDrawing=false,
        GlassElements elements=GlassElements::Controls);
    ~GlassControlScope();
    GlassControlScope(const GlassControlScope&)=delete;
    GlassControlScope& operator=(const GlassControlScope&)=delete;
    void Draw(const ImRect& frame, const ImRect& grab, double value, double minimum, double maximum);
    bool DrawControl(const ImRect&, Stack::Animation::ControlInput, float radius);
    bool DrawPoint(ImVec2 point,float radius,Stack::Animation::ControlInput,
        GlassMotionRole role=GlassMotionRole::GraphPoint,float aspect=1.f,
        const Stack::Renderer::GlassChrome::JoinGroup* join=nullptr);
    bool DrawSelection(ImVec2 first,float step,int selected,float radius,ImGuiID identity,
        const GlassSelectionInteraction* interaction=nullptr);
    float SelectionPosition(ImGuiID identity,float fallback) const;
    bool PrepareForeground(ImDrawList&);
private:
    bool Prepare(ImDrawList&, int channel);
    bool Ready() const;
    bool Capture(const ImRect* bounds=nullptr);
    GlassControlScope* previous_;
    GlassControlState& state_;
    ImGuiWindow* window_;
    ImDrawListSplitter& splitter_;
    bool split_=false, captured_=false,inline_=false;
    Stack::Renderer::GlassChrome::SurfaceHandle backdrop_;
    float opacity_=1.f;
    GlassElements elements_=GlassElements::Controls;
    friend bool HasGlassControlScope();
    friend bool HasGlassPointScope();
    friend bool IsGraphPointPresentation();
    friend class GlassBackgroundScope;
    friend class GlassForegroundInkScope;
    friend class AutomaticGlassScope;
    friend bool DrawGlassSurface(ImDrawList&,ImVec2,ImVec2,ImVec4,float,bool,Stack::Renderer::GlassChrome::MaterialRole);
};

// Application-owned fallback state makes shared wrappers work in child windows,
// tables and popups. No state or project selection is owned by this pointer.
void SetGlassFrameState(GlassControlState*);
bool GlassEnabledForCurrentWindow();
bool GlassFrameEnabled();
bool DrawGlassSurface(ImDrawList&,ImVec2 min,ImVec2 max,ImVec4 tint,float radius,bool attachedLeft=false,
    Stack::Renderer::GlassChrome::MaterialRole role=Stack::Renderer::GlassChrome::MaterialRole::RegularSurface);
class AutomaticGlassScope {
public:
    AutomaticGlassScope();
private:
    std::optional<GlassControlScope> scope_;
};

// Plot backgrounds, curves and slider tracks precede the shared capture. Sharp
// text/icons and semantic point marks remain in the foreground channel.
class GlassBackgroundScope {
public:
    explicit GlassBackgroundScope(ImDrawList&);
    ~GlassBackgroundScope();
    void End();
    GlassBackgroundScope(const GlassBackgroundScope&)=delete;
    GlassBackgroundScope& operator=(const GlassBackgroundScope&)=delete;
private:
    GlassControlScope* scope_=nullptr;
    ImDrawList& draw_;
    int previousChannel_=2;
};
class GlassForegroundInkScope {
public:
    GlassForegroundInkScope();
    ~GlassForegroundInkScope();
    GlassForegroundInkScope(const GlassForegroundInkScope&)=delete;
    GlassForegroundInkScope& operator=(const GlassForegroundInkScope&)=delete;
private:
    Stack::Renderer::GlassChrome* material_=nullptr;
    ImDrawList* draw_=nullptr;
};

bool HasGlassControlScope();
bool HasGlassPointScope();
bool DrawGlassSelection(ImDrawList&,ImVec2 first,float step,int selected,float radius,ImGuiID identity,
    const GlassSelectionInteraction* interaction=nullptr);
float GlassSelectionPosition(ImGuiID identity,float fallback);
bool IsGraphPointPresentation();
bool DrawGlassLastItem(bool selected=false, float radius=-1.f);
bool DrawGlassPoint(ImDrawList&, ImVec2 point, float radius, ImGuiID identity,
    bool selected, bool hovered, bool held,
    GlassMotionRole role=GlassMotionRole::GraphPoint,float aspect=1.f,
    const Stack::Animation::ControlInput* nativeInput=nullptr,
    const Stack::Renderer::GlassChrome::JoinGroup* join=nullptr);
bool GlassButton(const char* label, const ImVec2& size=ImVec2(0,0), bool selected=false);
bool GlassSmallButton(const char* label);
bool GlassCheckbox(const char* label, bool* value);
bool GlassRadioButton(const char* label, bool selected);
bool GlassRadioButton(const char* label, int* value, int buttonValue);
struct SliderSemanticEdit { bool (*apply)(void*)=nullptr; void* user=nullptr; };
bool GlassSliderScalar(const char* label, ImGuiDataType type, void* value,
    const void* minimum, const void* maximum, const char* format=nullptr, ImGuiSliderFlags flags=0,
    const SliderSemanticEdit* semanticEdit=nullptr);
bool GlassSliderFloat(const char* label, float* value, float minimum, float maximum,
    const char* format="%.3f", ImGuiSliderFlags flags=0);
bool GlassSliderInt(const char* label, int* value, int minimum, int maximum,
    const char* format="%d", ImGuiSliderFlags flags=0);
bool GlassBegin(const char* name,bool* open=nullptr,ImGuiWindowFlags flags=0,bool attachedLeft=false);
bool GlassBeginChild(const char* id,const ImVec2& size=ImVec2(0,0),ImGuiChildFlags childFlags=0,ImGuiWindowFlags flags=0);
bool GlassBeginChild(ImGuiID id,const ImVec2& size=ImVec2(0,0),ImGuiChildFlags childFlags=0,ImGuiWindowFlags flags=0);
bool GlassBeginPopup(const char* id,ImGuiWindowFlags flags=0);
bool GlassBeginPopupModal(const char* name,bool* open=nullptr,ImGuiWindowFlags flags=0);
bool GlassBeginPopupContextItem(const char* id=nullptr,ImGuiPopupFlags flags=ImGuiPopupFlags_MouseButtonRight);
bool GlassBeginPopupContextWindow(const char* id=nullptr,ImGuiPopupFlags flags=ImGuiPopupFlags_MouseButtonRight);
bool GlassBeginPopupContextVoid(const char* id=nullptr,ImGuiPopupFlags flags=ImGuiPopupFlags_MouseButtonRight);
bool GlassBeginMenu(const char* label,bool enabled=true);
}
