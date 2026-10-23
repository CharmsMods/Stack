#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Editor::RawLabInternal {

// Stores geometry only. The thumbnail cache owns textures and resolves them
// at draw time, so a folder change or eviction cannot leave a stale GL handle.
class GalleryLayoutAnimation {
public:
    struct Texture {
        unsigned int handle = 0;
        int width = 0, height = 0;
    };
    void BeginFrame(const std::string& workspace, bool grid, int frame, double time,
        std::uint64_t orderRevision, bool reducedMotion);
    void EndFrame();
    bool Observe(const std::string& key, const ImRect& image, const ImRect& clip,
        float opacity = 1.0f);
    void ObserveSlot(const std::string& key, const ImRect& image, const ImRect& clip);
    void SetViewport(const ImRect& viewport) { m_Viewport = viewport; }
    void PrepareSwitch(const std::string& preferredKey);
    bool AnchorPending() const { return m_AnchorPending; }
    const std::string& AnchorKey() const { return m_AnchorKey; }
    ImVec2 AnchorFraction() const { return m_AnchorFraction; }
    void AnchorApplied() { m_AnchorPending = false; }
    void Draw(ImDrawList* draw, const ImRect& bounds,
        const std::function<Texture(const std::string&)>& texture);
    // Map a tile's decoration/input geometry with its image. Layout itself
    // stays at the destination so motion cannot change scrolling extents.
    bool DisplayRect(const std::string& key, const ImRect& target, ImRect& displayed) const;
    std::string ImageAt(const ImVec2& point) const;
    bool Protects(const std::string& key) const;
    bool Active() const { return m_Active; }
    void Reset();

private:
    struct Pose {
        ImRect image;
        ImRect clip;
        float opacity = 1.0f;
        bool rendered = false;
        std::size_t order = 0;
        int frame = -1;
    };
    using Layout = std::unordered_map<std::string, Pose>;
    Layout Sample() const;
    Pose SamplePose(const std::string& key, const Pose& destination) const;
    static Pose Interpolate(const Pose& from, const Pose& to, float amount);
    static Pose OffscreenEndpoint(const Pose& pose, bool grid);
    bool Participates(const std::string& key) const;

    std::string m_Workspace;
    Layout m_Layout;
    Layout m_From;
    std::vector<std::pair<std::string, Pose>> m_Presented;
    int m_Frame = -1;
    double m_StartedAt = 0.0;
    double m_Duration = 0.22;
    float m_Progress = 1.0f;
    bool m_Grid = false;
    std::uint64_t m_OrderRevision = 0;
    bool m_Active = false;
    std::size_t m_NextOrder = 0;
    std::string m_AnchorKey;
    ImVec2 m_AnchorFraction = ImVec2(.5f, .5f);
    bool m_AnchorPending = false;
    ImRect m_Viewport;
};

}
