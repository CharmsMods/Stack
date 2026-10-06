#include "GraphCursor.h"
#include <vector>
#include "Utils/GraphNumericControls.h"

#include <imgui_internal.h>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <unordered_map>

namespace ImGuiExtras {
namespace {
struct WheelTarget {
    const void* graph = nullptr;
    int node = -1;
    ImGuiID control = 0;
    int frame = -1;
} wheelTarget;
struct NodeScopeState {
    const void* graph = nullptr;
    int node = -1;
    bool hovered = false;
    bool stacked = false;
    int valueBegin = -1;
    int valueEnd = -1;
    GraphNumericNodeColors colors;
    ImVec2 cursorAnchor{};
} nodeScope;
bool wheelFrameInteractive = false;



struct NumericState {
    float resetReveal=0, resetHover=0, resetPress=0, numberHover=0, numberPress=0, focusAlpha=0;
    bool textMode = false;
    bool focusRequested = false;
    bool dragging = false;
    bool cursorDragging=false;
    bool inputWasActive = false;
    std::array<char, 96> text {};
    double dragStart = 0.0;
    float lastMouseX=0, dragWidth=0;
    double remainder = 0.0;
    double invalidUntil = 0.0;
    ImVec2 anchor {};
    int lastFrame = 0;
};

std::unordered_map<ImGuiID, NumericState> states;

template <typename T>
void FormatExact(NumericState& state, T value) {
    if constexpr (std::is_integral_v<T>)
        std::snprintf(state.text.data(), state.text.size(), "%d", value);
    else
        std::snprintf(state.text.data(), state.text.size(), "%.9g", value);
}

template <typename T>
bool Parse(const char* text, T& value) {
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text) return false;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end || errno == ERANGE || !std::isfinite(parsed) ||
        parsed < std::numeric_limits<T>::lowest() ||
        parsed > std::numeric_limits<T>::max()) return false;
    if constexpr (std::is_integral_v<T>) {
        if (std::trunc(parsed) != parsed) return false;
    }
    value = static_cast<T>(parsed);
    return true;
}

void DrawReset(const ImRect& rect, float scale, float reveal, float hover, float press) {
    // Same small counterclockwise arrow as assets/icons/reset.svg.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 center = rect.GetCenter();
    const float radius = 4.3f * scale;
    ImVec4 color=nodeScope.graph ? nodeScope.colors.reset : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const auto target=nodeScope.graph ? nodeScope.colors.resetHovered : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    color=ImLerp(color,target,hover);
    if (nodeScope.graph) color=ImLerp(color,nodeScope.colors.resetActive,press);
    color.w*=reveal;
    const ImU32 ink=ImGui::GetColorU32(color);
    draw->PathArcTo(center, radius, -2.5f, 2.6f, 16);
    draw->PathStroke(ink, 0, 1.2f * scale);
    const ImVec2 tip(center.x - radius * 0.80f, center.y - radius * 0.60f);
    draw->AddLine(tip, ImVec2(tip.x, tip.y - 3.2f * scale), ink, 1.2f * scale);
    draw->AddLine(tip, ImVec2(tip.x + 3.2f * scale, tip.y), ink, 1.2f * scale);
}

void DrawValue(const ImRect& rect, const char* formatted, bool emphasized, float hover, float press) {
    // Split the formatted suffix so units stay quieter than the authored number.
    char* suffix = nullptr;
    std::strtod(formatted, &suffix);
    const char* numberEnd = suffix == formatted ? formatted + std::strlen(formatted) : suffix;
    const ImVec2 size = ImGui::CalcTextSize(formatted);
    const ImVec2 pos(std::max(rect.Min.x, rect.GetCenter().x - size.x * 0.5f),
        rect.GetCenter().y - size.y * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(rect.Min, rect.Max, true);
    const ImU32 numberColor=nodeScope.graph ? ImGui::GetColorU32(ImLerp(
        ImLerp(nodeScope.colors.number,nodeScope.colors.numberHovered,hover),nodeScope.colors.numberActive,press))
        : ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddText(pos, numberColor, formatted, numberEnd);
    if (*numberEnd) {
        const float offset = ImGui::CalcTextSize(formatted, numberEnd).x;
        draw->AddText(ImVec2(pos.x + offset, pos.y),
            ImGui::GetColorU32(emphasized ? ImGuiCol_Text : ImGuiCol_TextDisabled), numberEnd);
    }
    draw->PopClipRect();
}

void DrawInvalidFeedback(const ImRect& rect, float scale) {
    const ImU32 color = ImGui::GetColorU32(nodeScope.graph ? nodeScope.colors.error : ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(rect.Min.x, rect.Max.y), rect.Max, color, 1.4f * scale);
    ImGui::SetNextWindowPos(ImVec2(rect.Min.x, rect.Max.y + 4.0f * scale));
    ImGui::BeginTooltip();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "Invalid value. Previous value restored.");
    ImGui::EndTooltip();
}

template <typename T>
bool Numeric(const char* label, const char* id, T* value, T minimum, T maximum,
    const char* format, float width, const GraphNodeControlScopeConfig& config,
    std::optional<double> defaultValue, GraphNumericCapture capture) {
    ImGui::PushID(id);
    const ImGuiID controlId = ImGui::GetID("##value");
    const int frame = ImGui::GetFrameCount();
    if (frame % 300 == 0) {
        for (auto it = states.begin(); it != states.end();) {
            if (frame - it->second.lastFrame > 1200) it = states.erase(it);
            else ++it;
        }
    }
    NumericState& state = states[controlId];
    const bool eligible = !(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled);
    if (eligible && wheelFrameInteractive && IsSliderWheelModifierActive() && nodeScope.graph &&
        wheelTarget.node < 0 && nodeScope.hovered) {
        wheelTarget = {nodeScope.graph, nodeScope.node, controlId, frame};
    }
    const bool wheelFocused = eligible && wheelFrameInteractive && IsSliderWheelModifierActive() &&
        wheelTarget.graph == nodeScope.graph && wheelTarget.node == nodeScope.node &&
        wheelTarget.control == controlId;
    if (wheelFocused) wheelTarget.frame = frame;
    state.lastFrame = frame;
    const float scale = std::max(0.01f, config.scale);
    const float available = std::max(1.0f, width > 0.0f ? width : ImGui::CalcItemWidth());
    const float gap = 6.0f * scale;
    const float resetWidth = 20.0f * scale;
    char displayText[128];
    std::snprintf(displayText,sizeof(displayText),format,*value);
    const float textWidth=ImGui::CalcTextSize(displayText).x;
    const float valueWidth=std::min(std::max(1.0f,available-resetWidth-gap),
        state.dragging ? state.dragWidth : state.textMode ? std::max(60.0f*scale,textWidth+8.0f*scale) : textWidth+8.0f*scale);
    const float labelWidth = std::max(0.0f, available - valueWidth - resetWidth - gap);
    const bool stacked = nodeScope.stacked || ImGui::CalcTextSize(label).x > labelWidth;
    const float height = ImGui::GetFrameHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (stacked) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + available);
        ImGui::TextUnformatted(label);
        ImGui::PopTextWrapPos();
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
    }
    ImGui::PopStyleColor();
    const float rowY = stacked ? ImGui::GetCursorScreenPos().y : origin.y;
    const float numericWidth = valueWidth;
    const float numericX = origin.x + available - numericWidth;
    const ImRect valueRect(ImVec2(numericX, rowY), ImVec2(origin.x + available, rowY + height));
    const ImRect resetRect(ImVec2(numericX - gap - resetWidth, rowY), ImVec2(numericX - gap, rowY + height));
    bool changed = false;

    const bool validDefault = defaultValue && std::isfinite(*defaultValue) &&
        *defaultValue >= std::numeric_limits<T>::lowest() && *defaultValue <= std::numeric_limits<T>::max();
    const T baseline = validDefault ? static_cast<T>(*defaultValue) : T{};
    const float dt=std::min(ImGui::GetIO().DeltaTime,0.05f);
    const bool resetAvailable=validDefault && *value!=baseline;
    state.resetReveal=AnimateTowards(state.resetReveal,resetAvailable ? 1.0f : 0.0f,dt,20);
    if (resetAvailable || state.resetReveal>0.001f) {
        ImGui::BeginDisabled(!resetAvailable);
        ImGui::SetCursorScreenPos(resetRect.Min);
        if (ImGui::InvisibleButton("##reset", resetRect.GetSize(), ImGuiButtonFlags_EnableNav)) {
            *value = baseline;
            FormatExact(state, *value);
            state.invalidUntil = 0.0;
            ImGui::MarkItemEdited(ImGui::GetItemID());
            changed = true;
        }
        const bool resetHovered = ImGui::IsItemHovered();
        state.resetHover=AnimateTowards(state.resetHover,(resetHovered || ImGui::IsItemFocused()) ? 1.0f : 0.0f,dt,20);
        state.resetPress=AnimateTowards(state.resetPress,ImGui::IsItemActive() ? 1.0f : 0.0f,dt,26);
        DrawReset(resetRect,scale,state.resetReveal,state.resetHover,state.resetPress);
        ImGui::EndDisabled();
        if (resetHovered) ImGui::SetTooltip("Reset %s to %.9g", label, *defaultValue);
        capture(false, false);
    }

    ImGui::SetCursorScreenPos(valueRect.Min);
    const auto commit = [&]() {
        T parsed {};
        const bool valid = Parse(state.text.data(), parsed) &&
            (config.rangePolicy == GraphSliderRangePolicy::Unclamped ||
             (parsed >= std::min(minimum, maximum) && parsed <= std::max(minimum, maximum)));
        if (valid) {
            changed |= *value != parsed;
            *value = parsed;
            state.invalidUntil = 0.0;
        } else {
            state.invalidUntil = ImGui::GetTime() + 2.5;
        }
        FormatExact(state, *value);
    };

    // Preserve Stack's modifier-wheel adjustment through its routed wheel API.
    const bool localWheel = !nodeScope.graph && ImGui::IsWindowHovered() &&
        ImGui::IsMouseHoveringRect(valueRect.Min, valueRect.Max);
    const float wheel = eligible && (wheelFocused || localWheel) && !state.dragging &&
        !IsSliderWheelConsumed() ? GetSliderWheelDelta() : 0.0f;
    if (wheel != 0.0f) {
        ConsumeSliderWheel();
        double step = std::max(std::abs(static_cast<double>(maximum) - minimum) * 0.01,
            std::is_integral_v<T> ? 1.0 : 0.001);
        if (ImGui::GetIO().KeyShift) step *= 0.1;
        else if (ImGui::GetIO().KeyCtrl) step *= 10.0;
        double next = static_cast<double>(*value) + wheel * step;
        if (config.rangePolicy == GraphSliderRangePolicy::Bounded)
            next = std::clamp(next, static_cast<double>(std::min(minimum, maximum)), static_cast<double>(std::max(minimum, maximum)));
        if (std::isfinite(next) && next >= std::numeric_limits<T>::lowest() && next <= std::numeric_limits<T>::max()) {
            changed |= *value != static_cast<T>(next);
            *value = static_cast<T>(next);
        }
    }

    const int valueVertexBegin = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    bool rightClickConsumed = false;
    if (state.textMode) {
        if (!state.inputWasActive || state.focusRequested) FormatExact(state, *value);
        if (state.focusRequested) {
            ImGui::SetKeyboardFocusHere();
            state.focusRequested = false;
        }
        ImGui::SetNextItemWidth(valueRect.GetWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * scale);
        if (nodeScope.graph) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg,nodeScope.colors.surface);
            ImGui::PushStyleColor(ImGuiCol_Text,nodeScope.colors.number);
        }
        const bool enter = ImGui::InputText("##value_input", state.text.data(), state.text.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll |
            ImGuiInputTextFlags_CharsScientific);
        if (nodeScope.graph) ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        const bool active = ImGui::IsItemActive();
        const bool escape = active && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool toggle = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (escape) {
            FormatExact(state, *value);
            ImGui::ClearActiveID();
        } else if (enter || ImGui::IsItemDeactivated() || toggle) commit();
        if (toggle) {
            state.textMode = false;
            rightClickConsumed = true;
            ImGui::ClearActiveID();
        }
        state.inputWasActive = active && !escape && !toggle;
    } else {
        ImGui::InvisibleButton("##value", valueRect.GetSize());
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            state.textMode = true;
            state.focusRequested = true;
            state.dragging = false;
            rightClickConsumed = true;
            FormatExact(state, *value);
        } else {
            if (ImGui::IsItemActivated()) {
                state.dragging = true;
                state.cursorDragging=false;
                state.anchor = ImGui::GetIO().MousePos;
                state.lastMouseX=state.anchor.x;
                state.dragWidth=valueWidth;
                state.dragStart = *value;
                state.remainder = 0.0;
            }
            if (state.dragging && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                changed |= *value != static_cast<T>(state.dragStart);
                *value = static_cast<T>(state.dragStart);
                state.dragging = false;
                ImGui::ClearActiveID();
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !ImGui::IsItemActive()) state.dragging = false;
            if (state.dragging) {
                // Keep sensitivity independent of the smaller visible value target.
                const float sensitivityWidth = config.dragReferenceWidth > 0.0f ? config.dragReferenceWidth : std::max(38.0f * scale,
                    available - 68.0f * scale - 46.0f * scale - 2.0f * gap);
                double step = GraphSliderDragStepPerPixel(static_cast<float>(minimum),
                    static_cast<float>(maximum), sensitivityWidth, config.interactionScale, config.scrubSensitivity);
                if (ImGui::GetIO().KeyShift) step *= 0.1;
                else if (ImGui::GetIO().KeyCtrl) step *= 10.0;
                const double delta = (ImGui::GetIO().MousePos.x - state.lastMouseX) * step;
                state.lastMouseX=ImGui::GetIO().MousePos.x;
                state.cursorDragging |= std::abs(ImGui::GetIO().MousePos.x-state.anchor.x)>=3.0f;
                double next = static_cast<double>(*value) + delta;
                if constexpr (std::is_integral_v<T>) {
                    state.remainder += delta;
                    const double whole = std::trunc(state.remainder);
                    next = static_cast<double>(*value) + whole;
                    state.remainder -= whole;
                }
                next = std::clamp(next, static_cast<double>(std::numeric_limits<T>::lowest()),
                    static_cast<double>(std::numeric_limits<T>::max()));
                if (config.rangePolicy == GraphSliderRangePolicy::Bounded)
                    next = std::clamp(next, static_cast<double>(std::min(minimum, maximum)),
                        static_cast<double>(std::max(minimum, maximum)));
                if (std::isfinite(next)) {
                    changed |= *value != static_cast<T>(next);
                    *value = static_cast<T>(next);
                }
                SubmitCursorCaptureRequest({ CursorCaptureMode::LinearScrub, state.anchor, state.anchor });
            }
        }
        if (nodeScope.graph) RequestGraphValueCursor(nodeScope.graph,controlId,hovered,state.dragging && state.cursorDragging,
            nodeScope.cursorAnchor,nodeScope.colors.number);
        else if (hovered || state.dragging) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        char formatted[128];
        std::snprintf(formatted, sizeof(formatted), format, *value);
        state.numberHover=AnimateTowards(state.numberHover,hovered ? 1.0f : 0.0f,dt,20);
        state.numberPress=AnimateTowards(state.numberPress,state.dragging ? 1.0f : 0.0f,dt,26);
        DrawValue(valueRect, formatted, hovered || state.dragging,state.numberHover,state.numberPress);
        if (hovered && ImGui::CalcTextSize(formatted).x > valueRect.GetWidth()) ImGui::SetTooltip("%s", formatted);
    }
    state.focusAlpha=AnimateTowards(state.focusAlpha,state.textMode && ImGui::IsItemActive() ? 1.0f : 0.0f,dt,20);
    if (state.focusAlpha>0.001f) {
        ImVec4 outline=nodeScope.graph ? nodeScope.colors.focus : ImGui::GetStyleColorVec4(ImGuiCol_NavCursor);
        outline.w*=state.focusAlpha;
        ImGui::GetWindowDrawList()->AddRect(valueRect.Min,valueRect.Max,ImGui::GetColorU32(outline),3*scale,0,scale);
    }
    if (wheelFocused || (eligible && nodeScope.valueBegin<0)) {
        nodeScope.valueBegin = valueVertexBegin;
        nodeScope.valueEnd = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    }
    if (changed) ImGui::MarkItemEdited(ImGui::GetItemID());
    capture(false, rightClickConsumed);
    if (state.invalidUntil > ImGui::GetTime()) DrawInvalidFeedback(valueRect, scale);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, rowY + height));
    ImGui::Dummy(ImVec2(available, 0.0f));
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace

GraphNumericValueStyle::GraphNumericValueStyle() : applied_(nodeScope.graph!=nullptr) {
    if (applied_) ImGui::PushStyleColor(ImGuiCol_Text,nodeScope.colors.number);
}
GraphNumericValueStyle::~GraphNumericValueStyle() { if (applied_) ImGui::PopStyleColor(); }

void BeginGraphWheelFrame(const void* graph, bool interactive) {
    wheelFrameInteractive = interactive;
    if (!interactive) return;
    const int frame = ImGui::GetFrameCount();
    if (!ImGui::GetIO().KeyCtrl || ImGui::GetIO().AppFocusLost ||
        wheelTarget.graph != graph || frame - wheelTarget.frame > 1) wheelTarget = {};
}

int GraphWheelTargetNode(const void* graph) {
    return wheelFrameInteractive && wheelTarget.graph == graph ? wheelTarget.node : -1;
}

GraphNumericNodeScope::GraphNumericNodeScope(const void* graph, int node, bool hovered, bool stacked, const GraphNumericNodeColors* colors, const ImVec2* cursorAnchor)
    : draw_(ImGui::GetWindowDrawList()), start_(draw_->VtxBuffer.Size), preserveUntil_(start_) {
    nodeScope = {graph, node, hovered, stacked};
    static const auto fallback=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
    nodeScope.colors=colors ? *colors : fallback.nodeAppearance;
    surface_=nodeScope.colors.surface;
    nodeScope.cursorAnchor=cursorAnchor ? *cursorAnchor : ImGui::GetCursorScreenPos();
}

void GraphNumericNodeScope::PreserveCurrentDrawing() { preserveUntil_=draw_->VtxBuffer.Size; }

GraphNumericNodeScope::~GraphNumericNodeScope() {
    ImGui::PushID(nodeScope.graph); ImGui::PushID(nodeScope.node);
    const auto key=ImGui::GetID("##wheelFade");
    auto* storage=ImGui::GetStateStorage();
    const bool focused=wheelFrameInteractive && IsSliderWheelModifierActive() && wheelTarget.graph==nodeScope.graph && wheelTarget.node==nodeScope.node;
    const float amount=AnimateTowards(storage->GetFloat(key),focused ? 1.0f : 0.0f,std::min(ImGui::GetIO().DeltaTime,0.05f),18);
    storage->SetFloat(key,amount); ImGui::PopID(); ImGui::PopID();
    if (nodeScope.valueBegin >= 0 && amount>0.001f) {
        // Image preview vertices must retain their original tint during wheel focus.
        std::vector<bool> imageVertex(draw_->VtxBuffer.Size,false);
        for (const auto& cmd : draw_->CmdBuffer) {
            if (cmd.TexRef._TexData == ImGui::GetIO().Fonts->TexRef._TexData &&
                cmd.TexRef._TexID == ImGui::GetIO().Fonts->TexRef._TexID) continue;
            for (unsigned j=cmd.IdxOffset;j<cmd.IdxOffset+cmd.ElemCount;++j)
                imageVertex[cmd.VtxOffset+draw_->IdxBuffer[j]]=true;
        }
        for (int i = preserveUntil_; i < draw_->VtxBuffer.Size; ++i) {
            if (imageVertex[i] || (i >= nodeScope.valueBegin && i < nodeScope.valueEnd)) continue;
            ImVec4 color = ImGui::ColorConvertU32ToFloat4(draw_->VtxBuffer[i].col);
            color.x=color.x*(1-0.75f*amount)+surface_.x*(0.75f*amount);
            color.y=color.y*(1-0.75f*amount)+surface_.y*(0.75f*amount);
            color.z=color.z*(1-0.75f*amount)+surface_.z*(0.75f*amount);
            draw_->VtxBuffer[i].col = ImGui::ColorConvertFloat4ToU32(color);
        }
    }
    nodeScope = {};
}

bool GraphNumericFloat(const char* label, const char* id, float* value, float minimum,
    float maximum, const char* format, float width, const GraphNodeControlScopeConfig& config,
    std::optional<double> defaultValue, GraphNumericCapture capture) {
    return Numeric(label, id, value, minimum, maximum, format ? format : "%.2f", width, config, defaultValue, capture);
}

bool GraphNumericInt(const char* label, const char* id, int* value, int minimum,
    int maximum, const char* format, float width, const GraphNodeControlScopeConfig& config,
    std::optional<double> defaultValue, GraphNumericCapture capture) {
    return Numeric(label, id, value, minimum, maximum, format ? format : "%d", width, config, defaultValue, capture);
}

} // namespace ImGuiExtras
