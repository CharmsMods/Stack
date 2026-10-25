#include "Utils/FloatingSlider.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/SidePanelSlider.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace ImGuiExtras {
namespace {
// A drawing scope only, restored on exit. Animation state belongs to its caller.
thread_local FloatingSliderState* active = nullptr;

template<class T>
bool Slider(const char* label, const char* id, T* value, T minimum, T maximum,
    const char* format, float requestedWidth, bool bounded = true) {
    IM_ASSERT(active);
    // ImGui reserves half the scalar domain for its slider arithmetic. Values
    // outside that track remain intact and can still be entered exactly.
    minimum = std::max(minimum, std::numeric_limits<T>::lowest()/2);
    maximum = std::min(maximum, std::numeric_limits<T>::max()/2);
    ImGui::PushID(id);
    const ImGuiID key = ImGui::GetID("##value");
    const float scale = ImGui::GetFontSize() / 16.f;
    const float width = std::max(60.f * scale, requestedWidth > 0.f
        ? requestedWidth : ImGui::GetContentRegionAvail().x);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float radius = 7.f * scale, thickness = 5.f * scale;
    const float inset = 12.f * scale;
    const float top = 36.f * scale;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + inset, origin.y + top));
    ImGui::SetNextItemWidth(width - inset * 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 6.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, radius * 2.f);
    constexpr auto flags = ImGuiSliderFlags_NoInput | ImGuiSliderFlags_NoRoundToFormat;
    for (const auto color : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
             ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Border})
        ImGui::PushStyleColor(color, ImVec4(0,0,0,0));
    bool changed;
    if constexpr (std::is_same_v<T, int>)
        changed = SidePanelSliderInt("##value", value, minimum, maximum, "", flags);
    else if constexpr (std::is_same_v<T, float>)
        changed = SidePanelSliderFloat("##value", value, minimum, maximum, "", flags);
    else
        changed = SidePanelSliderScalar("##value", std::is_integral_v<T> ? ImGuiDataType_S64 : ImGuiDataType_Double,
            value, &minimum, &maximum, "", flags);
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);
    const ImVec2 low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && !ImGui::GetIO().KeyAlt && !IsSliderWheelConsumed() &&
        ImGui::GetIO().MouseWheel != 0.f && !(ImGui::GetItemFlags() & ImGuiItemFlags_Disabled)) {
        T next = *value;
        if constexpr (std::is_integral_v<T>) {
            if (ImGui::GetIO().MouseWheel>0 && next<maximum) ++next;
            if (ImGui::GetIO().MouseWheel<0 && next>minimum) --next;
        } else {
            const double step = (static_cast<double>(maximum)-minimum) * (ImGui::GetIO().KeyShift ? .001 : .01);
            next = static_cast<T>(std::clamp(static_cast<double>(*value) + ImGui::GetIO().MouseWheel*step,
                static_cast<double>(minimum),static_cast<double>(maximum)));
        }
        changed |= next != *value;
        *value = next;
        ConsumeSliderWheel();
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    }
    if (changed) ImGui::MarkItemEdited(key);
    if (hovered && label && *label) ImGui::SetTooltip("%s", label);

    const float ratio = maximum > minimum
        ? std::clamp(static_cast<float>((static_cast<double>(*value)-minimum) /
            (static_cast<double>(maximum)-minimum)), 0.f, 1.f) : 0.f;
    const float left = low.x + radius + 2.f, right = high.x - radius - 2.f;
    const ImVec2 center(left + (right-left)*ratio, (low.y+high.y)*.5f);
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    const auto line = [&](float a, float b) {
        if (b <= a) return;
        draw->AddLine(ImVec2(a,center.y), ImVec2(b,center.y), ink, thickness);
        draw->AddCircleFilled(ImVec2(a,center.y),thickness*.5f,ink,16);
        draw->AddCircleFilled(ImVec2(b,center.y),thickness*.5f,ink,16);
    };
    // Leave the circle's interior transparent even over the image background.
    if (!HasSidePanelSliderScope()) {
        line(left,center.x-radius-2.f*scale);
        line(center.x+radius+2.f*scale,right);
        draw->AddCircle(center,radius,ink,32,2.f*scale);
    }

    char text[96];
    const char* numericFormat = format ? format : std::is_same_v<T,int> ? "%d" :
        std::is_integral_v<T> ? "%lld" : "%.3f";
    std::snprintf(text,sizeof(text),numericFormat,*value);
    auto& readout = active->readouts[key];
    readout.lastFrame = ImGui::GetFrameCount();
    readout.label.Update(text,ImGui::GetTime(),false);
    const float textWidth = readout.label.Width();
    const float textLeft = std::clamp(center.x-textWidth*.5f,origin.x,
        std::max(origin.x,origin.x+width-textWidth));
    readout.label.Draw(*draw,ImVec2(textLeft,origin.y+6.f*scale),textLeft+textWidth,ink);
    if (ImGui::BeginPopupContextItem("##exact-value")) {
        ImGui::TextUnformatted(label);
        T edited = *value;
        constexpr auto type = std::is_same_v<T,float> ? ImGuiDataType_Float :
            std::is_same_v<T,double> ? ImGuiDataType_Double :
            std::is_same_v<T,int> ? ImGuiDataType_S32 : ImGuiDataType_S64;
        ImGui::SetNextItemWidth(180.f*scale);
        if (ImGui::InputScalar("##exact",type,&edited,nullptr,nullptr,numericFormat) && std::isfinite(static_cast<double>(edited))) {
            // Uniform double/int64 values have no authored hard domain. Other
            // operation controls retain the range supplied by their parameter.
            if constexpr (std::is_same_v<T,float> || std::is_same_v<T,int>)
                if (bounded) edited = std::clamp(edited,minimum,maximum);
            changed |= *value != edited;
            *value = edited;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}
}

FloatingSliderScope::FloatingSliderScope(FloatingSliderState& state) : previous_(active) {
    active = &state;
    if (state.readouts.size() > 128) {
        const int oldest = ImGui::GetFrameCount()-600;
        for (auto it=state.readouts.begin(); it!=state.readouts.end();)
            if (it->second.lastFrame < oldest) it=state.readouts.erase(it); else ++it;
    }
}
FloatingSliderScope::~FloatingSliderScope() { active = previous_; }
bool HasFloatingSliderScope() { return active != nullptr; }
bool FloatingSliderFloat(const char* label,const char* id,float* value,float minimum,float maximum,const char* format,float width,bool bounded) {
    return Slider(label,id,value,minimum,maximum,format,width,bounded);
}
bool FloatingSliderInt(const char* label,const char* id,int* value,int minimum,int maximum,const char* format,float width) {
    return Slider(label,id,value,minimum,maximum,format,width);
}
bool FloatingSliderDouble(const char* label,const char* id,double* value,double minimum,double maximum,const char* format,float width) {
    return Slider(label,id,value,minimum,maximum,format,width);
}
bool FloatingSliderInteger(const char* label,const char* id,std::int64_t* value,std::int64_t minimum,std::int64_t maximum,const char* format,float width) {
    return Slider(label,id,value,minimum,maximum,format,width);
}
} // namespace ImGuiExtras
