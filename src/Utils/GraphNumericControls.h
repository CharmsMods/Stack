#pragma once

#include "Utils/ImGuiExtras.h"
#include <optional>
#include "App/settings/CreamPalette.h"

namespace ImGuiExtras {

// A node scope owns the Ctrl-wheel target and the drawing ranges used to dim
// everything except its primary value. No parameter pointers survive a frame.
void BeginGraphWheelFrame(const void* graph, bool interactive);
int GraphWheelTargetNode(const void* graph);
using GraphNumericNodeColors = StackAppearance::NodeAppearance;
class GraphNumericNodeScope {
public:
    GraphNumericNodeScope(const void* graph, int node, bool hovered, bool stacked, const GraphNumericNodeColors* colors = nullptr, const ImVec2* cursorAnchor = nullptr);
    ~GraphNumericNodeScope();
    void PreserveCurrentDrawing();
private:
    ImDrawList* draw_;
    int start_;
    int preserveUntil_;
    ImVec4 surface_;
};

// Color existing native numeric fields without changing their gestures or layout.
class GraphNumericValueStyle {
public:
    GraphNumericValueStyle();
    ~GraphNumericValueStyle();
private:
    bool applied_=false;
};

using GraphNumericCapture = void (*)(bool popupOpen, bool rightClickConsumed);

bool GraphNumericFloat(const char* label, const char* id, float* value,
    float minimum, float maximum, const char* format, float width,
    const GraphNodeControlScopeConfig& config, std::optional<double> defaultValue,
    GraphNumericCapture capture);
bool GraphNumericInt(const char* label, const char* id, int* value,
    int minimum, int maximum, const char* format, float width,
    const GraphNodeControlScopeConfig& config, std::optional<double> defaultValue,
    GraphNumericCapture capture);

} // namespace ImGuiExtras
