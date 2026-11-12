#include "App/UiPerformanceProbe.h"
#include "RawLabGalleryNavigator.h"
#include "App/AppHeaderStyle.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cstring>

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

void GalleryNavigator::Text(const char* text, ImVec2 minimum, ImVec2 maximum, bool secondary) {
    ImGui::PushStyleColor(ImGuiCol_Text, secondary ? m_Secondary : m_Text);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), minimum, maximum,
        maximum.x, text, text + std::strlen(text), nullptr);
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
    const bool hasLabel = label[0] != '\0' && (!control ||
        ImGui::CalcTextSize(label).x + 41 * m_Unit <= maximum.x - minimum.x);
    if (!hasLabel && label[0] && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
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
    const std::optional<std::string>& filter, ImVec4 surface, const std::function<void()>& footer,
    std::uint64_t fileTypes) {
    GalleryNavigatorAction result;
    m_Unit = ImGui::GetFontSize() / 16.f;
    PreparePalette(surface);

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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10 * m_Unit);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 5 * m_Unit);
    const float footerHeight = 90 * m_Unit;
    const float contentHeight = std::max(1.f, ImGui::GetContentRegionAvail().y-footerHeight-12*m_Unit);
    // On short windows only the content scrolls; view controls stay reachable.
    if (ImGui::BeginChild("NavigatorContent", {0, contentHeight}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
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
        if (Button("##ImageTypes", fileTypes == AllGalleryFileTypes ? "Images" : "Images (filtered)",
                Ui::Icon::Gallery, ImVec2(-1, rowHeight), false, nullptr, ButtonLayout::Sort))
            ImGui::OpenPopup("ImageTypes");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show image formats; projects use their reference image's format");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * m_Unit, 12 * m_Unit));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12 * m_Unit, 7 * m_Unit));
        if (ImGui::BeginPopup("ImageTypes")) {
            auto next = fileTypes;
            if (ImGui::SmallButton("All")) next = AllGalleryFileTypes;
            ImGui::SameLine();
            if (ImGui::SmallButton("None")) next = 0;
            ImGui::Separator();
            if (ImGui::BeginTable("Formats", 2)) {
                for (unsigned i = 0; i < GalleryFileTypeCount; ++i) {
                    ImGui::TableNextColumn();
                    const auto bit = std::uint64_t{1} << i;
                    bool checked = (next & bit) != 0;
                    if (ImGui::Checkbox(GalleryFileTypes[i].label, &checked))
                        next = checked ? next | bit : next & ~bit;
                }
                ImGui::EndTable();
            }
            if (next != fileTypes) result.fileTypes = next;
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        Space(6 * m_Unit);
        const auto heading = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(nullptr, 12 * m_Unit);
        ImGui::SetCursorScreenPos(ImVec2(heading.x + 4 * m_Unit, heading.y + 6 * m_Unit));
        ImGui::TextColored(m_Secondary, "Folders");
        ImGui::SetCursorScreenPos(ImVec2(heading.x + std::max(0.f, width - 88 * m_Unit), heading.y));
        if (Button("##OpenSource", "Browse", Ui::Icon::AllFolders, ImVec2(88 * m_Unit, 28 * m_Unit)))
            m_Filesystem.Open(filter && !tree.allSourceFolders ?
                tree.workspaceRoot/std::filesystem::u8path(*filter) : tree.workspaceRoot);
        Stack::UiPerformance::Probe::Item(Stack::UiPerformance::Probe::Visual::BrowseButton);
        const auto browse=ImGui::GetItemRectMin(),browseMax=ImGui::GetItemRectMax();
        result.browseMinimum={browse.x+10*m_Unit,(browse.y+browseMax.y-17*m_Unit)*.5f};
        result.browseMaximum={result.browseMinimum.x+17*m_Unit,result.browseMinimum.y+17*m_Unit};
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose or add a source folder");
        ImGui::PopFont();
    }
    ImGui::EndChild();
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
    const auto browser=m_Filesystem.RenderPopup();
    result.openPath=browser.folder;result.openImage=browser.image;
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
    const float half = std::max(1.f, (width - 10 * m_Unit) * .5f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 3 * m_Unit, origin.y + 3 * m_Unit));
    if (Button("##Grid", "Grid", Ui::Icon::View, ImVec2(half, 30 * m_Unit), grid, nullptr, ButtonLayout::Centered)) result = Action::Grid;
    Stack::UiPerformance::Probe::Item(Stack::UiPerformance::Probe::Visual::GridButton);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 7 * m_Unit + half, origin.y + 3 * m_Unit));
    if (Button("##Filmstrip", "Filmstrip", Ui::Icon::Filmstrip, ImVec2(half, 30 * m_Unit), !grid, nullptr, ButtonLayout::Centered)) result = Action::Filmstrip;
    Stack::UiPerformance::Probe::Item(Stack::UiPerformance::Probe::Visual::FilmstripButton);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 44 * m_Unit));
    const float sortWidth = std::max(32 * m_Unit, width - 72 * m_Unit);
    ImGui::BeginDisabled(!canSort);
    if (Button("##Sort", "View options", Ui::Icon::Sort, ImVec2(sortWidth, 32 * m_Unit), false, nullptr, ButtonLayout::Sort)) result = Action::Sort;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",
        canSort ? sortLabel : "Open a folder and switch to original images to sort them.");
    ImGui::SameLine(0, 4 * m_Unit);
    if (Button("##Properties", "", Ui::Icon::Details, ImVec2(32 * m_Unit, 32 * m_Unit), properties, nullptr, ButtonLayout::Centered)) result = Action::Properties;
    Stack::UiPerformance::Probe::Item(Stack::UiPerformance::Probe::Visual::PropertiesButton);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Image properties");
    ImGui::SameLine(0, 4 * m_Unit);
    if (Button("##More", "", Ui::Icon::MoreHorizontal, ImVec2(32 * m_Unit, 32 * m_Unit), false, nullptr, ButtonLayout::Centered)) result = Action::More;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("More gallery actions");
    ImGui::PopFont();
    return result;
}
} // namespace Stack::Editor::RawLabInternal
