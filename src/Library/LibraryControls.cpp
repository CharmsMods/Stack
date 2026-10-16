#include "LibraryControls.h"

#include "App/AppHeaderStyle.h"
#include "Notifications/NotificationPresenter.h"

#include <imgui_internal.h>
#include <algorithm>

namespace Stack::LibraryUi {
namespace {

float Motion(const char* name, float target, float initial, float duration) {
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(name);
    float& value = *storage->GetFloatRef(id, initial);
    if (value == target) return value;
    if (ReducedMotion()) value = target;
    else Header::ApproachMotion(value, target, duration);
    return value;
}

void DrawIconShape(Icon icon, ImVec2 origin, float size, ImU32 color) {
    auto* draw = ImGui::GetWindowDrawList();
    const float s = size / 24.f;
    const auto p = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };
    const auto line = [&](float x, float y, float x2, float y2) {
        draw->AddLine(p(x, y), p(x2, y2), color, 1.65f * s);
    };
    switch (icon) {
    case Icon::Gallery:
        draw->AddRect(p(3, 4), p(21, 20), color, 2.f * s, 0, 1.65f * s);
        draw->AddCircleFilled(p(8, 9), 1.6f * s, color, 12);
        line(4, 18, 11, 12); line(11, 12, 15, 16); line(15, 16, 18, 13); line(18, 13, 21, 16);
        break;
    case Icon::Projects:
    case Icon::Folder:
        draw->AddRect(p(3, 7), p(21, 20), color, 2.f * s, 0, 1.65f * s);
        line(3, 7, 3, 4); line(3, 4, 10, 4); line(10, 4, 13, 7);
        break;
    case Icon::AllFolders:
        draw->AddRect(p(5, 7), p(22, 20), color, 2.f * s, 0, 1.65f * s);
        line(5, 7, 5, 4); line(5, 4, 11, 4); line(11, 4, 14, 7);
        line(2, 8, 2, 23); line(2, 23, 18, 23);
        break;
    case Icon::OpenFolder:
        line(2, 8, 2, 4); line(2, 4, 9, 4); line(9, 4, 12, 7); line(12, 7, 20, 7);
        line(2, 8, 2, 20); line(2, 20, 19, 20); line(19, 20, 23, 10);
        line(23, 10, 7, 10); line(7, 10, 2, 20);
        break;
    case Icon::Filmstrip:
        draw->AddRect(p(5, 4), p(19, 20), color, 1.5f * s, 0, 1.65f * s);
        line(1, 5, 1, 19); line(23, 5, 23, 19);
        break;
    case Icon::SavedEdits:
        line(2, 8, 12, 3); line(12, 3, 22, 8); line(22, 8, 12, 13); line(12, 13, 2, 8);
        line(2, 13, 12, 18); line(12, 18, 22, 13);
        line(2, 18, 12, 23); line(12, 23, 22, 18);
        break;
    case Icon::Bracket:
        draw->AddRect(p(8, 3), p(21, 16), color, 2.f * s, 0, 1.65f * s);
        line(5, 7, 5, 19); line(5, 19, 17, 19);
        line(2, 10, 2, 22); line(2, 22, 14, 22);
        break;
    case Icon::Sort:
        line(5, 4, 5, 20); line(2, 17, 5, 20); line(5, 20, 8, 17);
        line(11, 6, 22, 6); line(11, 12, 19, 12); line(11, 18, 16, 18);
        break;
    case Icon::View:
        for (int row = 0; row < 2; ++row) for (int column = 0; column < 2; ++column) {
            const float x = 3.f + column * 10.f, y = 3.f + row * 10.f;
            draw->AddRect(p(x, y), p(x + 7, y + 7), color, 1.f * s, 0, 1.65f * s);
        }
        break;
    case Icon::More:
        for (float y : {5.f, 12.f, 19.f}) draw->AddCircleFilled(p(12, y), 1.6f * s, color, 12);
        break;
    case Icon::MoreHorizontal:
        for (float x : {5.f, 12.f, 19.f}) draw->AddCircleFilled(p(x, 12), 1.6f * s, color, 12);
        break;
    case Icon::ChevronDown:
        line(7, 10, 12, 15); line(12, 15, 17, 10);
        break;
    case Icon::Details:
        draw->AddRect(p(3, 4), p(21, 20), color, 2.f * s, 0, 1.65f * s);
        line(14, 4, 14, 20); line(17, 9, 19, 9); line(17, 13, 19, 13);
        break;
    }
}

} // namespace

void DrawIcon(Icon icon, ImVec2 origin, float size, ImU32 color) {
    DrawIconShape(icon, origin, size, color);
}

bool ReducedMotion() {
    // The preference is application-wide. Query Windows once for all these
    // controls in a frame, rather than once per icon or animation channel.
    static int frame = -1;
    static bool reduced = false;
    if (frame != ImGui::GetFrameCount()) {
        frame = ImGui::GetFrameCount();
        reduced = Notifications::SystemReducedMotion();
    }
    return reduced;
}

bool IconButton(const char* label, Icon icon, bool selected, bool enabled) {
    const float unit = ImGui::GetFontSize() / 13.f;
    ImGui::PushID(label);
    const ImVec4 transparent(0, 0, 0, 0);
    ImGui::PushStyleColor(ImGuiCol_Button, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, transparent);
    ImGui::PushStyleColor(ImGuiCol_Text, transparent);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(label, ImVec2(34.f * unit, 34.f * unit));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    const float hover = Motion("Hover", hovered || held ? 1.f : 0.f, 0.f, .16f);
    const float active = Motion("Selected", selected ? 1.f : 0.f, selected ? 1.f : 0.f, .20f);
    ImVec4 tint = Header::Blend(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        ImGui::GetStyleColorVec4(ImGuiCol_Text), std::max(hover, active));
    if (!enabled) tint.w *= ImGui::GetStyle().DisabledAlpha;
    const float size = (20.f + hover - (held ? 1.f : 0.f)) * unit;
    DrawIcon(icon, ImVec2((minimum.x + maximum.x - size) * .5f,
        (minimum.y + maximum.y - size) * .5f - active * unit), size, ImGui::GetColorU32(tint));
    if (active > 0.f) {
        tint.w *= active;
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2((minimum.x + maximum.x) * .5f, maximum.y - 2.f * unit),
            1.4f * unit, ImGui::GetColorU32(tint), 12);
    }
    if (hovered) ImGui::SetTooltip("%s", label);
    ImGui::PopID();
    return pressed;
}

bool TextButton(const char* label, bool enabled, const ImVec2& size) {
    ImGui::PushID(label);
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID lastFrame = ImGui::GetID("LastFrame");
    if (storage->GetInt(lastFrame, -2) != ImGui::GetFrameCount() - 1)
        storage->SetFloat(ImGui::GetID("Appear"), 0.f);
    storage->SetInt(lastFrame, ImGui::GetFrameCount());
    const float appear = Motion("Appear", 1.f, 0.f, .18f);
    const float hover = storage->GetFloat(ImGui::GetID("Hover"), 0.f);
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * appear);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * .5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(text.x, text.y, text.z, hover * .07f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(text.x, text.y, text.z, hover * .07f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(text.x, text.y, text.z, .13f));
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(label, size);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    Motion("Hover", hovered ? 1.f : 0.f, 0.f, .14f);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    ImGui::PopID();
    return pressed;
}

int ViewSwitch(bool gallery, ViewSwitchMotion& motion, bool enabled, float scale) {
    const float unit = scale > 0.f ? scale : ImGui::GetFontSize() / 13.f;
    const float target = gallery ? 1.f : 0.f;
    if (motion.leading < 0.f) motion.leading = motion.trailing = target;
    if (motion.leading != target || motion.trailing != target) {
        if (ReducedMotion()) motion.leading = motion.trailing = target;
        else {
            Header::ApproachMotion(motion.leading, target, .18f);
            Header::ApproachMotion(motion.trailing, target, .29f);
        }
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(1.f, ImGui::GetContentRegionAvail().x);
    const float height = 36.f * unit, inset = 3.f * unit;
    const float segment = std::max(0.f, width - inset * 2.f) * .5f;
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
        ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, .035f)), height * .5f);
    const float left = std::min(motion.leading, motion.trailing);
    const float right = std::max(motion.leading, motion.trailing);
    draw->AddRectFilled(ImVec2(origin.x + inset + segment * left, origin.y + inset),
        ImVec2(origin.x + inset + segment * (right + 1.f), origin.y + height - inset),
        ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, .13f)), (height - inset * 2.f) * .5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, height * .5f);
    ImGui::BeginDisabled(!enabled);
    int requested = -1;
    for (int view = 0; view != 2; ++view) {
        if (view) ImGui::SameLine(0, 0);
        const float amount = view ? (motion.leading + motion.trailing) * .5f
            : 1.f - (motion.leading + motion.trailing) * .5f;
        ImGui::PushStyleColor(ImGuiCol_Text, Header::Blend(
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), text, amount));
        if (ImGui::Button(view ? "Gallery" : "Projects", ImVec2(width * .5f, height))) requested = view;
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    return requested;
}

} // namespace Stack::LibraryUi
