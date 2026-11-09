#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Editor::RawLabInternal {

// Visible-content summary and shrink hysteresis belong to one Gallery owner.
struct FilmstripHeightState {
    int first=-1,last=-1;
    std::uint64_t revision=~std::uint64_t{0},organization=~std::uint64_t{0},similarity=~std::uint64_t{0};
    std::size_t maximum=1;
    float scroll=-1,target=0,pending=0;
    double holdUntil=0,shrinkAt=0;
    void ObserveScroll(float value,double now,float seconds) {
        if(scroll>=0 && seconds>0 && std::abs(value-scroll)/seconds>650.f) holdUntil=now+.24;
        scroll=value;
    }
    float Target(float desired,double now) {
        if(desired>=target) {target=desired;pending=desired;shrinkAt=0;}
        else {
            if(pending!=desired) {pending=desired;shrinkAt=now+.12;}
            if(now>=std::max(shrinkAt,holdUntil)) target=desired;
        }
        return target;
    }
};

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
    void PrepareRemoval() { m_RemovalPending = true; }
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
    bool PresentedBounds(const std::string& key,ImRect& bounds) const;
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
    Pose OffscreenEndpoint(const Pose& pose, bool grid) const;
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
    bool m_RemovalPending = false, m_Removing = false;
    std::size_t m_NextOrder = 0;
    std::string m_AnchorKey;
    ImVec2 m_AnchorFraction = ImVec2(.5f, .5f);
    bool m_AnchorPending = false;
    ImRect m_Viewport;
};

}
