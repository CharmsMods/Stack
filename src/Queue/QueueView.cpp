#include "Queue/QueueView.h"
#include "App/AppHeaderStyle.h"
#include "App/settings/PrimaryAction.h"
#include "Library/LibraryControls.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cstring>

namespace Stack::Queue {
namespace {
bool Active(ItemState state) {
    return state == ItemState::Queued || state == ItemState::Preparing ||
        state == ItemState::Rendering || state == ItemState::Writing;
}
const char* State(ItemState state) {
    switch (state) {
    case ItemState::Queued: return "Queued";
    case ItemState::Preparing: return "Preparing";
    case ItemState::Rendering: return "Rendering";
    case ItemState::Writing: return "Writing";
    case ItemState::Complete: return "Complete";
    case ItemState::Failed: return "Failed";
    default: return "Waiting";
    }
}
int Category(ItemState state) {
    if (Active(state)) return 2;
    if (state == ItemState::Complete) return 3;
    if (state == ItemState::Failed) return 4;
    return 1;
}
void Space(float height) { if (height > 0) ImGui::Dummy(ImVec2(0, height)); }
void Ellipsis(const char* text, ImVec2 at, float right, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), at,
        ImVec2(std::max(at.x, right), at.y + ImGui::GetFontSize() + 2), right, text, text + std::strlen(text), nullptr);
    ImGui::PopStyleColor();
}
ImVec4 Secondary() {
    return Header::Blend(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg),
        ImGui::GetStyleColorVec4(ImGuiCol_Text), .72f);
}
void StatusMark(ImVec2 center, int category, float unit, ImU32 color) {
    auto* draw = ImGui::GetWindowDrawList();
    if (category == 3) {
        draw->AddLine(ImVec2(center.x - 5 * unit, center.y),
            ImVec2(center.x - unit, center.y + 4 * unit), color, 1.5f * unit);
        draw->AddLine(ImVec2(center.x - unit, center.y + 4 * unit),
            ImVec2(center.x + 6 * unit, center.y - 4 * unit), color, 1.5f * unit);
    } else if (category == 4) {
        draw->AddCircle(center, 6 * unit, color, 12, 1.3f * unit);
        draw->AddLine(ImVec2(center.x, center.y - 3 * unit), ImVec2(center.x, center.y + unit), color, 1.3f * unit);
        draw->AddCircleFilled(ImVec2(center.x, center.y + 3.5f * unit), .8f * unit, color, 8);
    } else if (category == 0) {
        for (int y = -1; y <= 1; ++y)
            draw->AddLine(ImVec2(center.x - 6 * unit, center.y + y * 4 * unit),
                ImVec2(center.x + 6 * unit, center.y + y * 4 * unit), color, 1.3f * unit);
    } else {
        draw->AddCircle(center, 5.5f * unit, color, 12, 1.3f * unit);
        if (category == 2) draw->AddCircleFilled(center, 2.3f * unit, color, 10);
    }
}
bool QuietButton(const char* label, ImVec2 size = ImVec2()) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    return clicked;
}
}

void QueueView::PrepareFilter(const std::vector<Item>& items) {
    if (m_PreparedFilter == m_Filter) return;
    m_PreparedFilter = m_Filter;
    m_Visible.clear();
    for (std::size_t i = 0; i < items.size(); ++i)
        if (m_Filter == 0 || Category(items[i].state) == m_Filter) m_Visible.push_back(i);
}

void QueueView::Prepare(const std::vector<Item>& items, std::uint64_t revision) {
    if (revision != m_Revision) {
        m_Revision = revision;
        m_Counts.fill(0);
        m_Ready = 0;
        m_Rows.resize(items.size());
        bool detailExists = false;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            auto& row = m_Rows[i];
            if (row.id != item.id) {
                row = {};
                row.id = item.id;
                row.name = item.displayName;
                row.kind = item.kind == ItemKind::SourceImage ? "Original image" :
                    item.kind == ItemKind::ProjectSnapshot ? "Project - current edits" : "Saved project";
                row.location = item.path.u8string();
            }
            if (row.outputPath != item.outputPath) {
                row.outputPath = item.outputPath;
                row.output = item.outputPath.u8string();
            }
            ++m_Counts[0];
            ++m_Counts[Category(item.state)];
            if (item.selected && (item.state == ItemState::Waiting || item.state == ItemState::Failed)) ++m_Ready;
            detailExists |= item.id == m_DetailItem;
            if (item.id == m_DetailItem) m_DetailIndex = i;
        }
        if (!detailExists) {
            m_DetailItem = items.empty() ? 0 : items.front().id;
            m_DetailIndex = 0;
        }
        for (std::size_t i = 0; i < m_Counts.size(); ++i) m_CountLabels[i] = std::to_string(m_Counts[i]);
        m_Summary = std::to_string(items.size()) + (items.size() == 1 ? " job" : " jobs");
        m_ExportLabel = m_Ready ? "Export selected (" + std::to_string(m_Ready) + ")" : "Export selected";
        m_PreparedFilter = -1;
    }
    PrepareFilter(items);
}

ViewAction QueueView::RenderNavigator(const std::vector<Item>& items) {
    ViewAction action;
    const float s = ImGui::GetFontSize() / 16.f;
    const auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const auto secondary = Secondary();
    const auto surface = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, secondary);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::TextUnformatted("Queue");
    Space(20 * s);
    ImGui::PushFont(nullptr, 14 * s);
    constexpr const char* labels[]{"All jobs", "Waiting", "Active", "Complete", "Failed"};
    for (int filter = 0; filter < 5; ++filter) {
        ImGui::PushID(filter);
        const bool selected = m_Filter == filter;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? Header::Blend(surface, text, .14f) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Header::Blend(surface, text, selected ? .14f : .07f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Header::Blend(surface, text, .14f));
        if (ImGui::Button("##State", ImVec2(-1, 32 * s))) {
            m_Filter = filter;
            PrepareFilter(items);
        }
        ImGui::PopStyleColor(3);
        const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
        if (ImGui::IsItemVisible()) {
            const auto ink = ImGui::GetColorU32(text);
            StatusMark(ImVec2(low.x + 18 * s, low.y + 16 * s), filter, s, ink);
            if (selected) ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(low.x, low.y + 9 * s),
                ImVec2(low.x + 2 * s, high.y - 9 * s), ink, s);
            Ellipsis(labels[filter], ImVec2(low.x + 36 * s, low.y + 9 * s), high.x - 48 * s, ink);
            ImGui::PushFont(nullptr, 12 * s);
            const char* count = m_CountLabels[filter].c_str();
            const auto size = ImGui::CalcTextSize(count);
            ImGui::GetWindowDrawList()->AddText(ImVec2(high.x - 10 * s - size.x, low.y + 10 * s),
                ImGui::GetColorU32(selected ? text : secondary), count);
            ImGui::PopFont();
        }
        ImGui::PopID();
        Space(2 * s);
    }
    Space(24 * s);
    const float detailHeight = std::max(1.f, ImGui::GetContentRegionAvail().y - 56 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * s, 0));
    if (ImGui::BeginChild("QueueDetails", ImVec2(0, detailHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoBackground)) {
        if (m_DetailIndex < items.size() && items[m_DetailIndex].id == m_DetailItem) {
            const auto index = m_DetailIndex;
            const auto* found = &items[index];
            const auto& row = m_Rows[index];
            ImGui::PushFont(nullptr, 12 * s);
            ImGui::TextDisabled("Selected job");
            ImGui::PopFont();
            Space(8 * s);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(row.name.c_str());
            Space(6 * s);
            ImGui::PushFont(nullptr, 12 * s);
            ImGui::TextColored(secondary, "%s", row.kind.c_str());
            Space(16 * s);
            ImGui::TextUnformatted(State(found->state));
            if (!found->status.empty() && found->status != State(found->state)) {
                Space(6 * s);
                ImGui::TextColored(secondary, "%s", found->status.c_str());
            }
            ImGui::PopFont();
            if (!Active(found->state) && found->state != ItemState::Complete) {
                Space(12 * s);
                bool included = found->selected;
                if (ImGui::Checkbox("Include in export", &included)) action = {ViewCommand::Include, found->id, included};
            }
            if (!row.location.empty()) {
                Space(20 * s);
                ImGui::PushFont(nullptr, 12 * s);
                ImGui::TextDisabled("Source location");
                Space(6 * s);
                ImGui::TextColored(secondary, "%s", row.location.c_str());
                ImGui::PopFont();
            }
            if (!row.output.empty()) {
                Space(16 * s);
                if (QuietButton("Show output", ImVec2(-1, 30 * s))) action = {ViewCommand::RevealOutput, found->id};
            }
            ImGui::PopTextWrapPos();
        } else {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(secondary, "Stage originals, saved projects or brackets from Gallery.");
            Space(12 * s);
            ImGui::TextColored(secondary, "In Graph, right-click Image Output to queue its current edits.");
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    Space(16 * s);
    if (QuietButton("Open Gallery", ImVec2(-1, 36 * s))) action.command = ViewCommand::OpenGallery;
    ImGui::PopFont();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    return action;
}

ViewAction QueueView::RenderWorkspace(const std::vector<Item>& items,
    const ExportPresentation& output, const Thumbnail& thumbnail) {
    ViewAction action;
    const float s = ImGui::GetFontSize() / 16.f;
    const auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const auto secondary = Secondary();
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, secondary);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushFont(nullptr, 14 * s);
    const auto header = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(nullptr, 18 * s);
    ImGui::TextUnformatted(m_Filter == 0 ? "Render queue" :
        m_Filter == 1 ? "Waiting" : m_Filter == 2 ? "Active jobs" : m_Filter == 3 ? "Completed jobs" : "Failed jobs");
    ImGui::PopFont();
    Space(6 * s);
    const auto summaryAt = ImGui::GetCursorScreenPos();
    Ellipsis(output.status.empty() ? m_Summary.c_str() : output.status.c_str(), summaryAt,
        header.x + width - 44 * s, ImGui::GetColorU32(secondary));
    ImGui::Dummy(ImVec2(std::max(1.f, width - 44 * s), 18 * s));
    if (ImGui::IsItemHovered() && !output.status.empty()) ImGui::SetTooltip("%s", output.status.c_str());
    ImGui::SetCursorScreenPos(ImVec2(header.x + std::max(0.f, width - 34 * s), header.y));
    if (QuietButton("...##QueueActions", ImVec2(34 * s, 30 * s))) ImGui::OpenPopup("QueueActions");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 6 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * s, 12 * s));
    if (ImGui::BeginPopup("QueueActions")) {
        if (ImGui::MenuItem("Include all eligible jobs")) action.command = ViewCommand::SelectAll;
        if (ImGui::MenuItem("Exclude all eligible jobs")) action.command = ViewCommand::SelectNone;
        ImGui::Separator();
        if (ImGui::MenuItem("Clear completed", nullptr, false, m_Counts[3] != 0)) action.command = ViewCommand::ClearCompleted;
        if (ImGui::MenuItem("Open Gallery")) action.command = ViewCommand::OpenGallery;
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(ImVec2(header.x, header.y + 66 * s));
    const float footerHeight = (width < 550 * s ? 142.f : 92.f) * s;
    const float listHeight = std::max(1.f, ImGui::GetContentRegionAvail().y - footerHeight);
    if (ImGui::BeginChild("QueueJobs", ImVec2(0, listHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
        if (m_Visible.empty()) {
            Space(std::max(20 * s, listHeight * .26f));
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(items.empty() ? "Ready when you are" : "No jobs in this view");
            Space(10 * s);
            ImGui::TextColored(secondary, "%s", items.empty() ?
                "Select images, saved projects or brackets in Gallery, then choose Add to queue." :
                "Choose another status in the Queue navigator.");
            if (items.empty()) {
                Space(8 * s);
                ImGui::TextColored(secondary, "For current edits, right-click the Image Output node in Graph.");
                Space(20 * s);
                if (QuietButton("Open Gallery", ImVec2(132 * s, 36 * s))) action.command = ViewCommand::OpenGallery;
            }
            ImGui::PopTextWrapPos();
        } else {
            const float rowHeight = 76 * s;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_Visible.size()), rowHeight);
            while (clipper.Step()) for (int visible = clipper.DisplayStart; visible < clipper.DisplayEnd; ++visible) {
                const auto index = m_Visible[visible];
                const auto& item = items[index];
                const auto& row = m_Rows[index];
                ImGui::PushID(static_cast<int>(item.id));
                const auto low = ImGui::GetCursorScreenPos();
                const float rowWidth = ImGui::GetContentRegionAvail().x;
                const bool compact = rowWidth < 620 * s;
                const float actionWidth = 76 * s;
                ImGui::SetCursorScreenPos(ImVec2(low.x + 30 * s, low.y + 2 * s));
                ImGui::PushStyleColor(ImGuiCol_Button, m_DetailItem == item.id ?
                    Header::Blend(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), text, .075f) : ImVec4(0, 0, 0, 0));
                if (ImGui::Button("##Inspect", ImVec2(std::max(1.f, rowWidth - actionWidth - 30 * s), rowHeight - 6 * s))) {
                    m_DetailItem = item.id;
                    m_DetailIndex = index;
                }
                const bool hovered = ImGui::IsItemHovered();
                ImGui::PopStyleColor();
                if (hovered) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(row.name.c_str());
                    if (!item.status.empty()) {
                        ImGui::PushTextWrapPos(420 * s);
                        ImGui::TextUnformatted(item.status.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::EndTooltip();
                }
                ImGui::SetCursorScreenPos(ImVec2(low.x, low.y + 25 * s));
                bool included = item.selected;
                ImGui::BeginDisabled(Active(item.state) || item.state == ItemState::Complete);
                if (ImGui::Checkbox("##Include", &included)) action = {ViewCommand::Include, item.id, included};
                ImGui::EndDisabled();
                const ImVec2 thumbMin(low.x + 38 * s, low.y + 10 * s);
                const ImVec2 thumbSize(54 * s, 50 * s);
                thumbnail(item, thumbMin, thumbSize);
                const float nameX = low.x + 106 * s;
                const float right = low.x + rowWidth - actionWidth - 10 * s;
                Ellipsis(row.name.c_str(), ImVec2(nameX, low.y + 13 * s),
                    compact ? right : right - 152 * s, ImGui::GetColorU32(text));
                ImGui::PushFont(nullptr, 12 * s);
                Ellipsis(compact ? State(item.state) : row.kind.c_str(), ImVec2(nameX, low.y + 37 * s),
                    compact ? right : right - 152 * s, ImGui::GetColorU32(secondary));
                if (!compact) {
                    const float stateX = right - 136 * s;
                    StatusMark(ImVec2(stateX + 7 * s, low.y + 22 * s), Category(item.state), s, ImGui::GetColorU32(secondary));
                    Ellipsis(State(item.state), ImVec2(stateX + 22 * s, low.y + 16 * s), right,
                        ImGui::GetColorU32(text));
                    if (Active(item.state) && item.state != ItemState::Queued) {
                        ImGui::SetCursorScreenPos(ImVec2(stateX + 22 * s, low.y + 43 * s));
                        ImGui::ProgressBar(item.progress, ImVec2(110 * s, 3 * s), "");
                    }
                } else if (Active(item.state) && item.state != ItemState::Queued) {
                    ImGui::SetCursorScreenPos(ImVec2(nameX, low.y + 59 * s));
                    ImGui::ProgressBar(item.progress, ImVec2(std::max(1.f, right - nameX), 3 * s), "");
                }
                ImGui::PopFont();
                ImGui::SetCursorScreenPos(ImVec2(low.x + rowWidth - actionWidth, low.y + 21 * s));
                if (item.state == ItemState::Complete && !row.output.empty()) {
                    if (QuietButton("Show", ImVec2(actionWidth, 30 * s))) action = {ViewCommand::RevealOutput, item.id};
                } else {
                    ImGui::BeginDisabled(Active(item.state));
                    if (QuietButton("Remove", ImVec2(actionWidth, 30 * s))) action = {ViewCommand::Remove, item.id};
                    ImGui::EndDisabled();
                }
                ImGui::SetCursorScreenPos(low);
                ImGui::Dummy(ImVec2(rowWidth, rowHeight));
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    Space(16 * s);
    const auto footer = ImGui::GetCursorScreenPos();
    const bool compact = width < 550 * s;
    const float controlsWidth = 182 * s;
    const float locationRight = footer.x + width - (compact ? 0 : controlsWidth + 24 * s);
    ImGui::PushFont(nullptr, 12 * s);
    ImGui::TextDisabled("Export destination");
    ImGui::PopFont();
    const ImVec2 destinationPos(footer.x, footer.y + 22 * s);
    ImGui::SetCursorScreenPos(destinationPos);
    ImGui::BeginDisabled(output.busy);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button("##Destination", ImVec2(std::max(1.f, locationRight - footer.x), 26 * s))) action.command = ViewCommand::ChooseDestination;
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    const bool destinationHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    LibraryUi::DrawIcon(LibraryUi::Icon::Folder, ImVec2(footer.x + 2 * s, destinationPos.y + 5 * s), 16 * s, ImGui::GetColorU32(text));
    Ellipsis(output.destination.empty() ? "Choose a folder..." : output.destination.c_str(),
        ImVec2(footer.x + 26 * s, destinationPos.y + 6 * s), locationRight - 4 * s, ImGui::GetColorU32(text));
    if (destinationHovered && !output.destination.empty()) ImGui::SetTooltip("%s", output.destination.c_str());
    ImGui::SetCursorScreenPos(ImVec2(footer.x, footer.y + 54 * s));
    ImGui::PushFont(nullptr, 12 * s);
    ImGui::TextColored(secondary, "PNG  /  Full quality  /  Unique filenames");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(compact ? footer.x : footer.x + width - controlsWidth,
        footer.y + (compact ? 82.f : 16.f) * s));
    if (output.busy) {
        ImGui::BeginDisabled(output.stopping);
        if (QuietButton(output.stopping ? "Stopping after current" : "Stop after current", ImVec2(controlsWidth, 36 * s))) action.command = ViewCommand::StopAfterCurrent;
        ImGui::EndDisabled();
    } else {
        ImGui::BeginDisabled(!m_Ready || !output.available);
        if (StackAppearance::PrimaryActionButton(m_ExportLabel.c_str(), ImVec2(controlsWidth, 36 * s), output.primaryColors)) action.command = ViewCommand::Export;
        ImGui::EndDisabled();
    }
    ImGui::SetCursorScreenPos(ImVec2(footer.x, footer.y + footerHeight - 16 * s));
    ImGui::Dummy(ImVec2(0, 1));
    ImGui::PopFont();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    return action;
}

void QueueView::Reset() {
    m_Rows.clear();
    m_Visible.clear();
    m_Counts.fill(0);
    m_Revision = m_DetailItem = 0;
    m_PreparedFilter = -1;
}
} // namespace Stack::Queue
