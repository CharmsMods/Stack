#pragma once

#include <imgui.h>
#include <string>

namespace Stack::Renderer {

// Display-only frost over a borrowed thumbnail, not a framebuffer backdrop or
// physical refraction. The caller owns the pocket mesh and its thumbnail lease.
// Begin/Shutdown require the main UI GL context; queued draws must complete
// before Shutdown. Resetting a view needs no GL work and retains this one program.
class FolderPocketMaterial {
public:
    FolderPocketMaterial() = default;
    FolderPocketMaterial(const FolderPocketMaterial&) = delete;
    FolderPocketMaterial& operator=(const FolderPocketMaterial&) = delete;

    // UV spacing between Gaussian samples (about six displayed pixels).
    // gradientUv contains source UV.y at the upper lip and bottom, respectively.
    // On false, draw an opaque ordinary-ImGui pocket instead. On true, draw the
    // borrowed texture with white vertices (including normal AA edge alpha),
    // then End. The mesh may be warped; its UVs keep frost attached to the face.
    bool Begin(ImDrawList* draw, ImVec2 uvStep, ImVec4 topTint, ImVec4 bottomTint,
        float transmission = .12f, ImVec2 gradientUv = ImVec2(1, 0));
    static void End(ImDrawList* draw);
    void Shutdown();
    const std::string& Error() const { return m_Error; }

private:
    bool EnsureProgram();
    static void Bind(const ImDrawList*, const ImDrawCmd*);
    unsigned int m_Program = 0;
    bool m_Failed = false;
    std::string m_Error;
};

} // namespace Stack::Renderer
