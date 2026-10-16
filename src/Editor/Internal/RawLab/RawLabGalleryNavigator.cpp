#include "RawLabGalleryNavigator.h"
#include "App/AppHeaderStyle.h"

#include <imgui_internal.h>
#include <algorithm>

namespace Stack::Editor::RawLabInternal {
namespace {
namespace Ui = Stack::LibraryUi;
using Mode = RawWorkspace::GalleryContentMode;
constexpr const char* CategoryLabels[]{"Original images", "Saved projects", "Brackets"};
constexpr Ui::Icon CategoryIcons[]{Ui::Icon::Gallery, Ui::Icon::SavedEdits, Ui::Icon::Bracket};
void Space(float height) { if (height > 0) ImGui::Dummy(ImVec2(0, height)); }
bool Equal(ImVec4 a, ImVec4 b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
}

void GalleryNavigator::PreparePalette(ImVec4 surface) {
    surface.w = 1.f;
    const std::array inputs{surface, ImGui::GetStyleColorVec4(ImGuiCol_Text),
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)};
    if (m_PaletteReady && Equal(inputs[0], m_PaletteInputs[0]) &&
        Equal(inputs[1], m_PaletteInputs[1]) && Equal(inputs[2], m_PaletteInputs[2])) return;
    m_PaletteReady = true;
    m_PaletteInputs = inputs;
    m_Surface = surface;
    m_Text = inputs[1];
    m_Secondary = Header::Blend(surface, m_Text, .72f);
    m_Hover = Header::Blend(surface, m_Text, .065f);
    m_Selected = Header::Blend(surface, m_Text, .14f);
    // A small neutral backing distinguishes the two view choices; no card.
    m_Control = Header::Blend(surface, m_Text, .035f);
}

void GalleryNavigator::PrepareRows(GalleryFolderTree& tree) {
    if (m_TreeSerial == tree.serial && !m_RowsDirty) return;
    m_TreeSerial = tree.serial;
    m_RowsDirty = false;
    m_Rows.clear();
    m_Rows.push_back({&tree.root, 0});
    const auto append = [&](const auto& self, GalleryFolderTree::Folder& parent, int depth) -> void {
        for (auto& [label, child] : parent.children) {
            m_Rows.push_back({&child, depth});
            if (child.expanded) self(self, child, depth + 1);
        }
    };
    append(append, tree.root, 0);
}

void GalleryNavigator::Text(const char* text, ImVec2 minimum, ImVec2 maximum, bool secondary) {
    ImGui::PushStyleColor(ImGuiCol_Text, secondary ? m_Secondary : m_Text);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), minimum, maximum,
        maximum.x, text, nullptr, nullptr);
    ImGui::PopStyleColor();
}

bool GalleryNavigator::Button(const char* id, const char* label, Ui::Icon icon,
    ImVec2 size, bool selected, const char* count, ButtonLayout layout) {
    const bool control = layout == ButtonLayout::Centered;
    const float leading = layout == ButtonLayout::Folder ? 14.f : 0.f;
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? m_Selected : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? m_Selected : m_Hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_Selected);
    const bool clicked = ImGui::Button(id, size);
    ImGui::PopStyleColor(3);
    if (!ImGui::IsItemVisible()) return clicked;
    const auto minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    const float alpha = ImGui::GetStyle().Alpha;
    const auto ink = [&](ImVec4 color) { color.w *= alpha; return ImGui::ColorConvertFloat4ToU32(color); };
    if (selected && !control) draw->AddRectFilled(ImVec2(minimum.x, minimum.y + 9 * m_Unit),
        ImVec2(minimum.x + 2 * m_Unit, maximum.y - 9 * m_Unit), ink(m_Text), m_Unit);
    const bool hasLabel = label[0] != '\0';
    const float iconLeft = control ? ((maximum.x - minimum.x) -
        (hasLabel ? ImGui::CalcTextSize(label).x + 25 * m_Unit : 17 * m_Unit)) * .5f : (10 + leading) * m_Unit;
    Ui::DrawIcon(icon, ImVec2(minimum.x + iconLeft, (minimum.y + maximum.y - 17 * m_Unit) * .5f),
        17 * m_Unit, ink(m_Text));
    float textRight = maximum.x - 10 * m_Unit;
    if (layout == ButtonLayout::Sort) {
        textRight -= 16 * m_Unit;
        Ui::DrawIcon(Ui::Icon::ChevronDown, ImVec2(maximum.x - 20 * m_Unit,
            (minimum.y + maximum.y - 12 * m_Unit) * .5f), 12 * m_Unit, ink(m_Secondary));
    }
    if (count) {
        ImGui::PushFont(nullptr, 12 * m_Unit);
        const auto measured = ImGui::CalcTextSize(count);
        draw->AddText(ImVec2(textRight - measured.x, (minimum.y + maximum.y - measured.y) * .5f),
            ink(selected ? m_Text : m_Secondary), count);
        textRight -= measured.x + 12 * m_Unit;
        ImGui::PopFont();
    }
    const ImVec2 textMin(minimum.x + iconLeft + 26 * m_Unit,
        (minimum.y + maximum.y - ImGui::GetFontSize()) * .5f);
    if (hasLabel) Text(label, textMin, ImVec2(std::max(textMin.x, textRight), maximum.y));
    return clicked;
}

GalleryNavigatorAction GalleryNavigator::Render(GalleryFolderTree& tree, Mode category,
    const std::optional<std::string>& filter, ImVec4 surface, const std::function<void()>& footer) {
    GalleryNavigatorAction result;
    m_Unit = ImGui::GetFontSize() / 16.f;
    PreparePalette(surface);
    PrepareRows(tree);
    const bool compact = ImGui::GetWindowHeight() < 560 * m_Unit;
    const bool hasSource = !tree.workspaceRoot.empty();
    const float rowHeight = (compact ? 30.f : 32.f) * m_Unit;
    const auto* selectedFolder = filter ? tree.Find(*filter) : &tree.root;
    ImGui::PushFont(nullptr, 14 * m_Unit);
    ImGui::PushStyleColor(ImGuiCol_Text, m_Text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, m_Secondary);
    ImGui::PushStyleColor(ImGuiCol_Separator, Header::Blend(m_Surface, m_Text, .14f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5 * m_Unit);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    for (int index = 0; index < 3; ++index) {
        ImGui::PushID(index);
        if (Button("##Category", CategoryLabels[index], CategoryIcons[index],
                ImVec2(-1, rowHeight), static_cast<int>(category) == index,
                hasSource ? (selectedFolder ? selectedFolder->countLabels[index].c_str() : "0") : nullptr))
            result.category = static_cast<Mode>(index);
        ImGui::PopID();
        if (index != 2) Space(2 * m_Unit);
    }
    Space((compact ? 10 : 20) * m_Unit);
    const auto heading = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(nullptr, 12 * m_Unit);
    ImGui::SetCursorScreenPos(ImVec2(heading.x + 4 * m_Unit, heading.y + 6 * m_Unit));
    ImGui::TextColored(m_Secondary, "Source folder");
    ImGui::SetCursorScreenPos(ImVec2(heading.x + std::max(0.f, width - 88 * m_Unit), heading.y));
    if (Button("##OpenSource", "Open...", Ui::Icon::OpenFolder, ImVec2(88 * m_Unit, 28 * m_Unit)))
        result.openFolder = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open a different source folder");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(heading.x, heading.y + (compact ? 28 : 34) * m_Unit));
    if (hasSource) {
        const auto origin = ImGui::GetCursorScreenPos();
        Ui::DrawIcon(Ui::Icon::Folder, ImVec2(origin.x + 8 * m_Unit, origin.y + 5 * m_Unit),
            18 * m_Unit, ImGui::GetColorU32(m_Secondary));
        Text(tree.sourceName.c_str(), ImVec2(origin.x + 34 * m_Unit, origin.y),
            ImVec2(origin.x + width - 8 * m_Unit, origin.y + 20 * m_Unit));
        ImGui::Dummy(ImVec2(width, 20 * m_Unit));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tree.fullPath.c_str());
        if (!compact) {
            ImGui::PushFont(nullptr, 12 * m_Unit);
            Text(tree.parentPath.c_str(), ImVec2(origin.x + 34 * m_Unit, origin.y + 22 * m_Unit),
                ImVec2(origin.x + width - 8 * m_Unit, origin.y + 38 * m_Unit), true);
            ImGui::PopFont();
            Space(18 * m_Unit);
        }
        Space((compact ? 8 : 14) * m_Unit);
    }
    const float footerHeight = 88 * m_Unit;
    const float bodyHeight = std::max(1.f, ImGui::GetContentRegionAvail().y - footerHeight - 12 * m_Unit);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10 * m_Unit);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 5 * m_Unit);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Header::Blend(m_Surface, m_Text, .26f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, Header::Blend(m_Surface, m_Text, .4f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, Header::Blend(m_Surface, m_Text, .55f));
    if (ImGui::BeginChild("FolderBrowser", ImVec2(0, bodyHeight), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground)) {
        if (!hasSource) {
            ImGui::TextWrapped("Choose a folder to browse its original images, saved projects, and brackets.");
            Space(12 * m_Unit);
            if (Button("##ChooseFolder", "Open folder...", Ui::Icon::OpenFolder, ImVec2(-1, 32 * m_Unit)))
                result.openFolder = true;
        } else {
            ImGuiListClipper clipper;
            const float folderHeight = 32 * m_Unit;
            clipper.Begin(static_cast<int>(m_Rows.size()), folderHeight);
            while (clipper.Step()) for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto& row = m_Rows[index];
                auto& folder = *row.folder;
                const bool all = row.folder == &tree.root;
                const auto origin = ImGui::GetCursorScreenPos();
                const float indent = std::min((all ? 0.f : row.depth * 16.f) * m_Unit,
                    std::max(0.f, ImGui::GetContentRegionAvail().x - 144 * m_Unit));
                const float available = ImGui::GetContentRegionAvail().x;
                ImGui::PushID(all ? "AllFolders" : folder.key.c_str());
                ImGui::SetCursorScreenPos(ImVec2(origin.x + indent, origin.y));
                const bool selected = all ? !filter : filter && *filter == folder.key;
                const bool clicked = Button("##Folder", folder.label.c_str(), all ? Ui::Icon::AllFolders : Ui::Icon::Folder,
                        ImVec2(std::max(1.f, available - indent), folderHeight), selected,
                        folder.countLabels[static_cast<int>(category)].c_str(), ButtonLayout::Folder);
                const bool expandable = !all && !folder.children.empty();
                const bool arrowHovered = expandable && ImGui::IsItemHovered() &&
                    ImGui::GetMousePos().x < origin.x + indent + 24 * m_Unit;
                const bool disclosureClicked = clicked && arrowHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                if (clicked && !disclosureClicked) {
                    result.folderChanged = true;
                    result.folder = all ? std::nullopt : std::optional<std::string>(folder.key);
                }
                if (ImGui::IsItemHovered()) {
                    if (arrowHovered) ImGui::SetTooltip("%s folder", folder.expanded ? "Collapse" : "Expand");
                    else ImGui::SetTooltip("%s\nIncludes subfolders", folder.fullPath.c_str());
                }
                if (expandable) {
                    const bool focused = ImGui::IsItemFocused() &&
                        !(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled);
                    const bool right = focused && ImGui::IsKeyPressed(ImGuiKey_RightArrow);
                    const bool left = focused && ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
                    if (disclosureClicked || (right && !folder.expanded) || (left && folder.expanded)) {
                        // Defer rebuilding the flat list until the next frame.
                        folder.expanded = !folder.expanded;
                        m_RowsDirty = true;
                    }
                    const auto point = ImVec2(origin.x + indent + 4 * m_Unit, origin.y + 13 * m_Unit);
                    auto* draw = ImGui::GetWindowDrawList();
                    // Arrow sits before the folder glyph, within its own rail.
                    draw->AddTriangleFilled(point,
                        ImVec2(point.x + (folder.expanded ? 6 : 0) * m_Unit, point.y + (folder.expanded ? 0 : 6) * m_Unit),
                        ImVec2(point.x + (folder.expanded ? 3 : 4) * m_Unit, point.y + (folder.expanded ? 4 : 3) * m_Unit),
                        ImGui::GetColorU32(m_Secondary));
                }
                ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + folderHeight));
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    Space(12 * m_Unit);
    if (ImGui::BeginChild("NavigatorFooter", ImVec2(0, footerHeight), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::Separator();
        Space(10 * m_Unit);
        if (footer) footer();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
    return result;
}

GalleryNavigatorFooterAction GalleryNavigator::RenderFooter(bool grid, bool properties,
    bool canSort, const char* sortLabel) {
    using Action = GalleryNavigatorFooterAction;
    Action result = Action::None;
    const float width = ImGui::GetContentRegionAvail().x;
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin,
        ImVec2(origin.x + width, origin.y + 36 * m_Unit), ImGui::GetColorU32(m_Control), 7 * m_Unit);
    ImGui::PushFont(nullptr, 12 * m_Unit);
    const float half = (width - 10 * m_Unit) * .5f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 3 * m_Unit, origin.y + 3 * m_Unit));
    if (Button("##Grid", "Grid", Ui::Icon::View, ImVec2(half, 30 * m_Unit), grid, nullptr, ButtonLayout::Centered)) result = Action::Grid;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 7 * m_Unit + half, origin.y + 3 * m_Unit));
    if (Button("##Filmstrip", "Filmstrip", Ui::Icon::Filmstrip, ImVec2(half, 30 * m_Unit), !grid, nullptr, ButtonLayout::Centered)) result = Action::Filmstrip;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 44 * m_Unit));
    const float sortWidth = std::max(32 * m_Unit, width - 72 * m_Unit);
    ImGui::BeginDisabled(!canSort);
    if (Button("##Sort", sortLabel, Ui::Icon::Sort, ImVec2(sortWidth, 32 * m_Unit), false, nullptr, ButtonLayout::Sort)) result = Action::Sort;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",
        canSort ? sortLabel : "Open a folder and switch to original images to sort them.");
    ImGui::SameLine(0, 4 * m_Unit);
    if (Button("##Properties", "", Ui::Icon::Details, ImVec2(32 * m_Unit, 32 * m_Unit), properties, nullptr, ButtonLayout::Centered)) result = Action::Properties;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Image properties");
    ImGui::SameLine(0, 4 * m_Unit);
    if (Button("##More", "", Ui::Icon::MoreHorizontal, ImVec2(32 * m_Unit, 32 * m_Unit), false, nullptr, ButtonLayout::Centered)) result = Action::More;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("More gallery actions");
    ImGui::PopFont();
    return result;
}
} // namespace Stack::Editor::RawLabInternal
