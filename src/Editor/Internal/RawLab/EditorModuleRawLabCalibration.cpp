#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Raw/RawColorCalibration.h"

#include <algorithm>
#include <cmath>

using namespace Stack::Editor::RawLabInternal;

namespace {
void DrawPrimaryDiagram(const Stack::RawRecipe::RawColorCalibrationRecipe& recipe) {
    using namespace Stack::RawRecipe;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = std::clamp(width * 0.62f, 130.0f, 200.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    auto* draw = ImGui::GetWindowDrawList();
    // A radius of 1.0 xy units encloses twice the longest reference primary.
    const float scale = (std::min(width, height) - 26.0f) * 0.5f;
    const ImVec2 center(origin.x + width * 0.5f, origin.y + height * 0.5f);
    const auto position = [&](const std::array<double, 2>& xy) {
        return ImVec2(center.x + float(xy[0] - kCalibrationWhite[0]) * scale,
            center.y - float(xy[1] - kCalibrationWhite[1]) * scale);
    };
    const auto adjusted = ColorCalibrationPrimaries(recipe);
    const ImU32 colors[] = {IM_COL32(232,104,99,255), IM_COL32(112,194,133,255), IM_COL32(103,155,237,255)};
    const char* labels[] = {"R", "G", "B"};
    const ImU32 guide = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
    for (int i = 0; i < 3; ++i) {
        const ImVec2 original = position(kCalibrationPrimaries[i]);
        const ImVec2 current = position(adjusted[i]);
        draw->AddLine(original, position(kCalibrationPrimaries[(i + 1) % 3]), guide);
        draw->AddLine(original, current, colors[i]);
        draw->AddCircle(original, 5.5f, colors[i], 20, 1.0f);
        draw->AddCircleFilled(current, 3.5f, colors[i]);
        draw->AddText(ImVec2(current.x + 7.0f, current.y - 7.0f), colors[i], labels[i]);
    }
    draw->AddCircleFilled(center, 2.5f, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    LabTooltip("Primary positions around neutral. Outlines show the originals; filled markers show your settings.");
}

bool CalibrationSlider(const char* label, float& value, const char* tooltip, bool& active) {
    const float before = value;
    ImGui::PushID(label);
    const float rowStart = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(rowStart + ImGui::CalcTextSize("Saturation").x + 10.0f);
    const float numericWidth = ImGui::CalcTextSize("-100.0").x + 14.0f;
    const float trackWidth = std::max(20.0f, ImGui::GetContentRegionAvail().x -
        numericWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InvisibleButton("##track", ImVec2(trackWidth, ImGui::GetFrameHeight()));
    const auto& io = ImGui::GetIO();
    const bool dragging = ImGui::IsItemActive();
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    const float travel = std::max(1.0f, trackWidth - 8.0f);
    if (dragging) {
        if (ImGui::IsItemActivated()) {
            if (!io.KeyShift) value = (io.MousePos.x - lo.x - 4.0f) / travel * 200.0f - 100.0f;
        } else {
            value += io.MouseDelta.x * 200.0f / travel * (io.KeyShift ? 0.1f : 1.0f);
        }
        value = std::clamp(value, -100.0f, 100.0f);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        value = 0.0f;
        ImGui::ClearActiveID();
    }
    active |= ImGui::IsItemActive();
    auto* draw = ImGui::GetWindowDrawList();
    const float mid = (lo.y + hi.y) * 0.5f;
    draw->AddLine(ImVec2(lo.x + 4.0f, mid), ImVec2(hi.x - 4.0f, mid),
        ImGui::GetColorU32(ImGuiCol_Separator), 2.0f);
    draw->AddLine(ImVec2((lo.x+hi.x)*0.5f, mid-3), ImVec2((lo.x+hi.x)*0.5f, mid+3),
        ImGui::GetColorU32(ImGuiCol_TextDisabled));
    draw->AddCircleFilled(ImVec2(lo.x + 4.0f + (value + 100.0f) / 200.0f * travel, mid),
        4.0f, ImGui::GetColorU32(dragging ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab));
    LabTooltip(tooltip);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(numericWidth);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::InputFloat("##number", &value, 0, 0, "%+.1f");
    active |= ImGui::IsItemActive();
    value = std::isfinite(value) ? std::clamp(value, -100.0f, 100.0f) : 0.0f;
    LabTooltip("Type a value. Shift-drag the slider for fine adjustment; double-click the slider to reset.");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopID();
    return value != before;
}
} // namespace

bool EditorModule::RenderRawWorkspaceLabCalibrationSurface(RawWorkspaceEditContext& context) {
    auto& calibration = context.recipe.colorCalibration;
    auto& ui = m_RawWorkspaceLabUi;
    const auto before = calibration;
    const auto source = GetActiveRawWorkspacePreviewIdentity();
    if (ui.calibrationGestureSource != source) {
        ui.calibrationGestureSource = source;
        ui.calibrationGestureActive = false;
    }
    bool active = false;
    bool changed = false;
    if (BypassEyeButton("calibration-bypass", calibration.enabled,
            "Bypass Calibration", "Enable Calibration")) {
        calibration.enabled = !calibration.enabled;
        changed = true;
    }
    ImGui::SameLine();
    if (BareTextButton("Reset")) {
        for (auto& primary : calibration.primaries) primary = {};
        changed = true;
    }
    ImGui::BeginDisabled(!calibration.enabled);
    DrawPrimaryDiagram(calibration);
    const char* labels[] = {"Red Primary", "Green Primary", "Blue Primary"};
    const char* directions[] = {
        "Negative moves red toward magenta; positive toward yellow.",
        "Negative moves green toward yellow; positive toward cyan.",
        "Negative moves blue toward cyan; positive toward magenta."
    };
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ImGui::Spacing();
        ImGui::TextUnformatted(labels[i]);
        ImGui::BeginDisabled(IsRawParameterDriven("hue-" + std::to_string(i)));
        changed |= CalibrationSlider("Hue", calibration.primaries[i].hue, directions[i], active);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(IsRawParameterDriven("saturation-" + std::to_string(i)));
        changed |= CalibrationSlider("Saturation", calibration.primaries[i].saturation,
            "Changes primary distance from neutral. -100 halves it; +100 doubles it. Grays stay neutral.", active);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("Shift: fine adjustment");
    LabTooltip("Click a value to type. Double-click a slider to reset it. Escape cancels the current edit.");
    if (active && !ui.calibrationGestureActive) ui.calibrationGestureStart = before;
    // Multi-frame edits persist during a drag, so retain its starting values for Escape too.
    if (ui.calibrationGestureActive && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        calibration = ui.calibrationGestureStart;
        ImGui::ClearActiveID();
        active = false;
        changed = true;
    }
    ui.calibrationGestureActive = active;
    return changed;
}
