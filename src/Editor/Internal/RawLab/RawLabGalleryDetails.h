#pragma once

#include "Raw/RawGalleryActions.h"
#include <imgui.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Stack::RawWorkspace { struct SourceRecord; }
namespace Raw { struct RawMetadata; }

namespace Stack::Editor::RawLabInternal {

struct GalleryInspectorActions {
    std::size_t selectionCount = 0;
    const std::filesystem::path* focusedPath = nullptr;
    bool copyVisible = false;
    bool copyEnabled = false;
    bool pasteEnabled = false;
    bool locationEnabled = false;
    const char* copyReason = "Select an image first.";
    const char* pasteReason = "Select an image first.";
};

// Presentation only. Owns formatted metadata, native layout and finite hover
// motion; the caller retains thumbnail ownership and executes returned actions.
class GalleryDetails {
public:
    std::optional<RawGalleryActions::Action> Render(
        const RawWorkspace::SourceRecord* source,
        const std::shared_ptr<const Raw::RawMetadata>& metadata,
        const std::filesystem::path& displayedProject,
        const GalleryInspectorActions& actions, ImVec4 workspaceColor,
        const std::function<void(const ImVec2&)>& thumbnail = {});
    void EditsCopied();

private:
    void Prepare(const RawWorkspace::SourceRecord* source,
        const std::shared_ptr<const Raw::RawMetadata>& metadata,
        const std::filesystem::path& project);
    void PreparePalette(ImVec4 workspace);
    void PrepareLayout(float width, bool thumbnail);
    std::optional<RawGalleryActions::Action> RenderMetadata(
        const GalleryInspectorActions& actions,
        const std::function<void(const ImVec2&)>& thumbnail);
    std::optional<RawGalleryActions::Action> RenderEditActions(
        const GalleryInspectorActions& actions, bool stacked, bool different);
    bool Button(const char* id, const char* label, int glyph,
        bool enabled, ImVec2 size, bool quiet = false);

    std::filesystem::path m_SourcePath, m_ProjectPath, m_FocusedPath;
    std::string m_SourceName, m_Extension, m_FocusedName, m_SelectionScope;
    std::uintmax_t m_FileBytes = 0;
    std::int64_t m_Modified = 0;
    std::shared_ptr<const Raw::RawMetadata> m_Metadata;
    bool m_Prepared = false;
    std::string m_Name, m_Dimensions, m_Camera, m_Lens, m_Format, m_Size;
    std::array<std::string, 4> m_Exposure;
    std::size_t m_SelectionCount = static_cast<std::size_t>(-1);
    std::array<float, 3> m_Hover{};
    double m_CopiedUntil = 0;
    float m_Unit = 1, m_FontBase = 16;
    bool m_LayoutDirty = true, m_Condensed = false, m_LayoutThumbnail = false;
    float m_LayoutWidth = -1, m_LayoutFontSize = 0, m_LayoutFontBase = 0;
    float m_PreferredBodyHeight = 0, m_EditRowWidth = 200;
    ImFont* m_LayoutFont = nullptr;
    bool m_PaletteReady = false;
    std::array<ImVec4, 4> m_PaletteInputs{};
    ImVec4 m_Surface{}, m_Text{}, m_Secondary{}, m_Focus{};
    std::array<ImVec4, 3> m_Scrollbar{};
};

} // namespace Stack::Editor::RawLabInternal
