#include "Editor/UI/FusionWorkspaceUi.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
namespace {
bool CurveEditor(const char* id, std::array<float, 7>& curve) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(180.0f, ImGui::GetContentRegionAvail().x), 130);
    ImGui::InvisibleButton("curve", size);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 5);
    const auto point = [&](float x, float y) {
        return ImVec2(pos.x + 10 + (x + 12) / 18 * (size.x - 20),
                      pos.y + 10 + (6 - y) / 12 * (size.y - 20));
    };
    for (int v = -6; v <= 6; v += 3) {
        draw->AddLine(point(-12, static_cast<float>(v)), point(6, static_cast<float>(v)),
            ImGui::GetColorU32(v == 0 ? ImGuiCol_TextDisabled : ImGuiCol_Border));
    }
    ImVec2 previous = point(-12, Raw::Hdr::EvaluateFusionCurve(curve, -12));
    for (int i = 1; i <= 100; ++i) {
        const float x = -12 + 18.0f * i / 100;
        const ImVec2 next = point(x, Raw::Hdr::EvaluateFusionCurve(curve, x));
        draw->AddLine(previous, next, ImGui::GetColorU32(ImGuiCol_PlotLines), 2);
        previous = next;
    }
    bool changed = false;
    for (std::size_t i = 0; i < curve.size(); ++i)
        draw->AddCircleFilled(point(Raw::Hdr::kFusionCurveEv[i], curve[i]), 4, ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const auto mouse = ImGui::GetIO().MousePos;
        const float x = -12 + (mouse.x - pos.x - 10) / (size.x - 20) * 18;
        std::size_t nearest = 0;
        for (std::size_t i = 1; i < curve.size(); ++i)
            if (std::abs(x - Raw::Hdr::kFusionCurveEv[i]) < std::abs(x - Raw::Hdr::kFusionCurveEv[nearest])) nearest = i;
        curve[nearest] = std::clamp(6 - (mouse.y - pos.y - 10) / (size.y - 20) * 12, -6.0f, 6.0f);
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag a knot. Horizontal axis: fixed neutral scene EV relative to 18%% gray. Vertical axis: stops.");
    ImGui::TextDisabled("Shadows                         Highlights");
    if (ImGui::SmallButton("Reset curve")) { curve.fill(0); changed = true; }
    ImGui::PopID(); return changed;
}
}

bool DrawFusionControls(FusionWorkspaceUiState& s, const Raw::Hdr::FusionPreview* preview) {
    auto& p = s.controls;
    bool changed = false;
    ImGui::TextUnformatted("HDR Fusion");
    ImGui::TextWrapped("Set brightness, then choose which captures support it. Clipped and rejected measurements stay excluded.");
    changed |= ImGui::SliderFloat("Target exposure", &p.targetEv, -6, 6, "%+.2f EV");
    changed |= CurveEditor("Target exposure by luminance", p.targetCurve);
    ImGui::Spacing();
    changed |= ImGui::Checkbox("Guide source preference", &p.preferSources);
    ImGui::BeginDisabled(!p.preferSources);
    changed |= ImGui::SliderFloat("Preferred source offset", &p.preferredEv, -6, 6, "%+.2f EV");
    changed |= ImGui::SliderFloat("Follow target exposure", &p.targetFollow, 0, 1, "%.2f");
    changed |= ImGui::SliderFloat("Source blend width", &p.blendWidthEv, 0.25f, 8, "%.2f EV");
    changed |= CurveEditor("Source preference by luminance", p.preferenceCurve);
    ImGui::EndDisabled();
    if (preview && ImGui::CollapsingHeader("Exposure groups", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& info : preview->sources) {
            auto it = std::find_if(p.sources.begin(), p.sources.end(), [&](const auto& v) { return v.sourceId == info.sourceId; });
            Raw::Hdr::FusionSourcePreference value {info.sourceId};
            if (it != p.sources.end()) value = *it;
            ImGui::PushID(info.sourceId.c_str());
            const auto label = s.sourceLabels.find(info.sourceId);
            ImGui::TextWrapped("%s", label == s.sourceLabels.end() ? info.sourceId.c_str() : label->second.c_str());
            ImGui::TextDisabled("Capture %+.2f EV", info.captureEv);
            bool edit = ImGui::Checkbox("Include", &value.enabled);
            ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
            edit |= ImGui::SliderFloat("##bias", &value.biasStops, -6, 6, "Bias %+.2f stops");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("+1 stop doubles relative trust before normalization. It does not double brightness.");
            if (edit) { if (it == p.sources.end()) p.sources.push_back(value); else *it = value; changed = true; }
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Local exposure and source masks", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(p.masks.size() >= 32);
        if (ImGui::Button("Add local mask")) {
            p.masks.emplace_back(); s.selectedMask = static_cast<int>(p.masks.size()) - 1;
            s.placeMask = true; changed = true;
        }
        ImGui::EndDisabled();
        for (std::size_t i = 0; i < p.masks.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const std::string label = "Mask " + std::to_string(i + 1);
            if (ImGui::Selectable(label.c_str(), s.selectedMask == static_cast<int>(i))) s.selectedMask = static_cast<int>(i);
            ImGui::PopID();
        }
        if (s.selectedMask >= 0 && s.selectedMask < static_cast<int>(p.masks.size())) {
            auto& m = p.masks[s.selectedMask];
            changed |= ImGui::Checkbox("Mask enabled", &m.enabled);
            ImGui::SameLine(); if (ImGui::SmallButton("Place in preview")) s.placeMask = true;
            changed |= ImGui::SliderFloat("Center X", &m.centerX, 0, 1, "%.3f");
            changed |= ImGui::SliderFloat("Center Y", &m.centerY, 0, 1, "%.3f");
            changed |= ImGui::SliderFloat("Horizontal radius", &m.radiusX, 0.005f, 1, "%.3f");
            changed |= ImGui::SliderFloat("Vertical radius", &m.radiusY, 0.005f, 1, "%.3f");
            changed |= ImGui::SliderFloat("Mask feather", &m.feather, 0.01f, 1, "%.2f");
            changed |= ImGui::SliderFloat("Local target", &m.targetEv, -6, 6, "%+.2f EV");
            ImGui::BeginDisabled(!p.preferSources);
            changed |= ImGui::SliderFloat("Local source offset", &m.preferredEv, -6, 6, "%+.2f EV");
            ImGui::EndDisabled();
            if (ImGui::SmallButton("Delete mask")) { p.masks.erase(p.masks.begin() + s.selectedMask); s.selectedMask = -1; changed = true; }
        }
        if (s.placeMask) ImGui::TextWrapped("Click the preview to place the selected mask.");
    }
    if (ImGui::Button("Reset Fusion controls")) { p = {}; s.selectedMask = -1; changed = true; }
    s.dirty |= changed;
    return changed;
}
} // namespace Stack::Editor
