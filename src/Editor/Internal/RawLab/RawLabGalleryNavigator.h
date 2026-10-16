#pragma once

#include "RawLabGalleryFolderTree.h"
#include "Library/LibraryControls.h"
#include "Raw/RawWorkspace.h"
#include <functional>
#include <optional>
#include <vector>

namespace Stack::Editor::RawLabInternal {

struct GalleryNavigatorAction {
    std::optional<RawWorkspace::GalleryContentMode> category;
    bool folderChanged = false, openFolder = false;
    std::optional<std::string> folder;
};

enum class GalleryNavigatorFooterAction { None, Grid, Filmstrip, Sort, Properties, More };

// Presentation only: prepared counts/paths in, navigation intent out. The
// editor owns folder/category changes; the shell owns panel width and saving.
class GalleryNavigator {
public:
    GalleryNavigatorAction Render(GalleryFolderTree& tree,
        RawWorkspace::GalleryContentMode category, const std::optional<std::string>& folder,
        ImVec4 surface, const std::function<void()>& footer);
    GalleryNavigatorFooterAction RenderFooter(bool grid, bool properties,
        bool canSort, const char* sortLabel);

private:
    enum class ButtonLayout { Navigation, Folder, Centered, Sort };
    struct Row { GalleryFolderTree::Folder* folder; int depth; };
    void PreparePalette(ImVec4 surface);
    void PrepareRows(GalleryFolderTree& tree);
    bool Button(const char* id, const char* label, LibraryUi::Icon icon,
        ImVec2 size, bool selected = false, const char* count = nullptr,
        ButtonLayout layout = ButtonLayout::Navigation);
    void Text(const char* text, ImVec2 minimum, ImVec2 maximum, bool secondary = false);
    float m_Unit = 1.f;
    bool m_PaletteReady = false, m_RowsDirty = true;
    std::uint64_t m_TreeSerial = 0;
    std::array<ImVec4, 3> m_PaletteInputs{};
    ImVec4 m_Surface{}, m_Text{}, m_Secondary{}, m_Hover{}, m_Selected{}, m_Control{};
    std::vector<Row> m_Rows;
};
} // namespace Stack::Editor::RawLabInternal
