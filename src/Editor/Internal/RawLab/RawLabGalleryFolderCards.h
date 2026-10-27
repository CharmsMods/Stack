#pragma once

#include "Raw/RawWorkspace.h"
#include "Renderer/FolderPocketMaterial.h"
#include <imgui.h>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Editor::RawLabInternal {

// Presentation only. Catalog snapshots and borrowed thumbnail textures come
// from the editor; the cards never discover files or own image pixels.
class GalleryFolderCards {
public:
    struct Texture { unsigned int handle = 0; int width = 0, height = 0; };
    struct Action {
        struct Image { std::filesystem::path path, sourceFolder; };
        std::optional<std::filesystem::path> openFolder;
        std::optional<Image> openImage;
        bool addFolder = false;
    };
    using ResolveTexture = std::function<Texture(const std::filesystem::path&,
        const RawWorkspace::SourceRecord&)>;

    Action Render(const std::vector<RawWorkspace::SourceFolderCard>& folders,
        const ImVec2& size, ImVec4 surface, const ResolveTexture& texture);
    void Reset() { m_Motion.clear(); }
    void Shutdown() { m_Material.Shutdown(); }

private:
    struct Motion {
        float reveal = .16f, leanX = 0.f, leanY = 0.f;
        std::array<float, 4> pageLift{};
        int frame = -1;
        bool expanded = false;
    };
    std::unordered_map<std::string, Motion> m_Motion;
    Renderer::FolderPocketMaterial m_Material;
};

} // namespace Stack::Editor::RawLabInternal
