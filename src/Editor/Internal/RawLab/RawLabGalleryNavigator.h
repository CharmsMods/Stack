#pragma once

#include "RawLabGalleryFolderTree.h"
#include "GalleryFilesystemBrowser.h"
#include "GalleryFileTypeFilter.h"
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
    std::optional<std::filesystem::path> openPath;
    std::optional<std::filesystem::path> openImage;
    std::optional<std::uint64_t> fileTypes;
    ImVec2 browseMinimum{},browseMaximum{};
};

enum class GalleryNavigatorFooterAction { None, Grid, Filmstrip, Sort, Properties, More };

// Presentation only: prepared counts/paths in, navigation intent out. The
// editor owns folder/category changes; the shell owns panel width and saving.
class GalleryNavigator {
public:
    GalleryNavigatorAction Render(GalleryFolderTree& tree,
        RawWorkspace::GalleryContentMode category, const std::optional<std::string>& folder,
        ImVec4 surface, const std::function<void()>& footer,
        std::uint64_t fileTypes = AllGalleryFileTypes);
    GalleryNavigatorFooterAction RenderFooter(bool grid, bool properties,
        bool canSort, const char* sortLabel);

private:
    GalleryFilesystemBrowser m_Filesystem;
    enum class ButtonLayout { Navigation, Folder, Centered, Sort };
    void PreparePalette(ImVec4 surface);
    bool Button(const char* id, const char* label, LibraryUi::Icon icon,
        ImVec2 size, bool selected = false, const char* count = nullptr,
        ButtonLayout layout = ButtonLayout::Navigation);
    void Text(const char* text, ImVec2 minimum, ImVec2 maximum, bool secondary = false);
    float m_Unit = 1.f;
    bool m_PaletteReady = false;
    std::array<ImVec4, 3> m_PaletteInputs{};
    ImVec4 m_Surface{}, m_Text{}, m_Secondary{}, m_Hover{}, m_Selected{}, m_Control{};
};
} // namespace Stack::Editor::RawLabInternal
