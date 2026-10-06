#include "Queue/QueueModule.h"
#include "imgui.h"

#include <algorithm>
#include <array>

namespace Stack::Queue {
namespace {

bool IsActive(ItemState state) {
    return state == ItemState::Preparing || state == ItemState::Rendering || state == ItemState::Writing;
}

bool MatchesFilter(ItemState state, int filter) {
    switch (filter) {
    case 1: return state == ItemState::Waiting;
    case 2: return IsActive(state);
    case 3: return state == ItemState::Complete;
    case 4: return state == ItemState::Failed;
    default: return true;
    }
}

} // namespace

void QueueModule::RenderSectionPanel() {
    const auto& items = FrameItems();
    ImGui::TextUnformatted("Jobs");
    ImGui::SameLine();
    ImGui::TextDisabled("%llu", static_cast<unsigned long long>(items.size()));
    if (items.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("Add images or projects from Gallery to prepare an export.");
        return;
    }

    std::array<int, 5> counts{};
    counts[0] = static_cast<int>(items.size());
    for (const Item& item : items) {
        for (int filter = 1; filter < 5; ++filter)
            if (MatchesFilter(item.state, filter)) ++counts[filter];
    }
    constexpr const char* labels[] = {"All jobs", "Waiting", "Active", "Complete", "Failed"};
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::BeginCombo("##JobStateFilter", labels[m_StateFilter])) {
        for (int filter = 0; filter < 5; ++filter) {
            ImGui::PushID(filter);
            const std::string label = std::string(labels[filter]) + "  " + std::to_string(counts[filter]);
            if (ImGui::Selectable(label.c_str(), m_StateFilter == filter)) m_StateFilter = filter;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("%d active  /  %d waiting", counts[2], counts[1]);
    ImGui::Spacing();

    const float lineHeight = ImGui::GetTextLineHeight();
    const float rowHeight = lineHeight * 2.f + 12.f;
    const float listHeight = std::clamp(ImGui::GetContentRegionAvail().y * .48f, 96.f, 340.f);
    ImGui::BeginChild("QueueJobList", ImVec2(0.f, listHeight), false);
    bool anyVisible = false;
    for (const Item& item : items) {
        if (!MatchesFilter(item.state, m_StateFilter)) continue;
        anyVisible = true;
        ImGui::PushID(static_cast<int>(item.id));
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const ImVec2 rowMax(rowMin.x + ImGui::GetContentRegionAvail().x, rowMin.y + rowHeight);
        if (ImGui::Selectable("##Job", m_DetailItem == item.id, ImGuiSelectableFlags_None,
                ImVec2(0.f, rowHeight)))
            m_DetailItem = item.id;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(rowMin, rowMax, true);
        draw->AddText(ImVec2(rowMin.x + 8.f, rowMin.y + 5.f),
            ImGui::GetColorU32(ImGuiCol_Text), item.displayName.c_str());
        draw->AddText(ImVec2(rowMin.x + 8.f, rowMin.y + 5.f + lineHeight),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), StateLabel(item.state));
        draw->PopClipRect();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(item.displayName.c_str());
            if (!item.status.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.f);
                ImGui::TextUnformatted(item.status.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    if (!anyVisible) ImGui::TextDisabled("No matching jobs.");
    ImGui::EndChild();

    const auto selected = std::find_if(items.begin(), items.end(),
        [this](const Item& item) { return item.id == m_DetailItem; });
    if (selected == items.end()) return;
    const Item& item = *selected;
    ImGui::Spacing();
    ImGui::TextDisabled("Job details");
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(item.displayName.c_str());
    ImGui::TextDisabled("%s / %s", item.kind == ItemKind::Project ? "Project" : "Image",
        StateLabel(item.state));
    if (IsActive(item.state) || item.state == ItemState::Complete)
        ImGui::ProgressBar(item.progress, ImVec2(-1.f, 4.f), "");
    if (!item.status.empty()) ImGui::TextUnformatted(item.status.c_str());
    ImGui::Spacing();
    bool included = item.selected;
    if (ImGui::Checkbox("Include in export", &included)) m_Model.SetSelected(item.id, included);
    ImGui::TextDisabled("Location");
    ImGui::TextUnformatted(item.path.u8string().c_str());
    ImGui::PopTextWrapPos();
}

} // namespace Stack::Queue
