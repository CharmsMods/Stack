// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "imgui.h"
#include "Glass/MaterialRenderer.h"
#include "Glass/ProximityJoins.h"
#include <array>
#include <deque>
#include <optional>
#include <string>

namespace Stack::Renderer {
// Main-window presentation only. Payloads and texture leases survive until
// BeginFrame, after the prior frame's deferred ImGui callbacks have completed.
class GlassChrome {
public:
    enum class MaterialRole { ClearControl, RegularSurface };
    struct SurfaceHandle {
        const GlassChrome* owner=nullptr;
        std::size_t index=0;
        std::uint64_t frame=0;
        explicit operator bool() const {return owner!=nullptr;}
    };
    struct Interaction {float press=0;ImVec2 tilt{},contact{},strain{};float contactGain=-1,elevation=0;};
    struct LightingFrame {ImVec2 min,max;};
    struct JoinGroup {ImGuiID set=0,item=0;float width=0;};
    void BeginFrame(bool enabled);
    SurfaceHandle AddBackdrop(ImDrawList*,ImVec2 min,ImVec2 max,ImVec4 tint,
        const LightingFrame* lighting=nullptr,std::uint64_t contentRevision=0);
    bool AddObject(ImDrawList*,SurfaceHandle,ImVec2 min,ImVec2 max,ImVec4 tint,float radius,
        float elevation=1.f,MaterialRole role=MaterialRole::ClearControl,float opacity=1.f,
        const Interaction* interaction=nullptr,const JoinGroup* join=nullptr);
    bool BeginInk(ImDrawList*,SurfaceHandle,ImVec2 min,ImVec2 max,ImGuiID id);
    void EndInk(ImDrawList*);
    const std::string& Error() const {return error.empty()?gpu.Error():error;}
    const Glass::MaterialRenderer::Counters& Diagnostics() const {return gpu.Diagnostics();}
    std::uint64_t JoinBuildCount() const {return joins.BuildCount();}
    void Shutdown();
private:
    struct CaptureLayout {int framebufferWidth=0,framebufferHeight=0,x=0,y=0,width=0,height=0;};
    struct Backdrop {
        GlassChrome* owner=nullptr;
        ImVec2 min{},max{},displayPos{},displaySize{},scale{1,1};
        ImVec4 tint{},clip{};
        float padding=0;
        CaptureLayout layout;
        std::uint64_t group=0,revision=0;
        std::uint32_t layer=0;
        unsigned remainingObjects=0;
        int left=0,bottom=0,right=0,top=0;
        Glass::MaterialRenderer::Target* target=nullptr;
        std::optional<stack::glass::BackdropSnapshot> snapshot;
    };
    struct Object {
        GlassChrome* owner;Backdrop* backdrop;
        stack::glass::gl::MaterialParameters material;
        ImVec2 center,halfSize;float radius,opacity;
        int left,bottom,width,height;
        Glass::MaterialEffects effects;
        JoinGroup join;
        std::array<float,4> clip{};
        int compound=-1;
        bool joinLeader=false;
    };
    struct Compound {
        Glass::JoinedGeometry geometry;
        float width=0;
        int left=0,bottom=0,right=0,top=0;
    };
    struct Ink {GlassChrome* owner;const Backdrop* backdrop;ImVec2 min,max;ImGuiID id;unsigned slot;ImVec4 clip;bool usable=false;};
    Backdrop* Resolve(SurfaceHandle);
    static void BuildLayout(Backdrop&);
    static void BackdropCallback(const ImDrawList*,const ImDrawCmd*);
    static void ObjectCallback(const ImDrawList*,const ImDrawCmd*);
    static void InkCallback(const ImDrawList*,const ImDrawCmd*);
    static void EndInkCallback(const ImDrawList*,const ImDrawCmd*);
    void CaptureBackdrop(Backdrop&);
    void PrepareJoins(Backdrop&);
    void DrawObject(const Object&);
    void DrawInk(const Ink&);
    void PrepareInk();
    bool EnsureInk();
    bool enabled=false;std::uint64_t frame=0;
    std::string error;Glass::MaterialRenderer gpu;
    std::deque<Backdrop> backdrops;std::deque<Object> objects;std::deque<Ink> inks;
    Glass::ProximityJoins joins;
    std::deque<Compound> compounds;
    std::size_t compoundCount=0;
    const Backdrop* activeBackdrop=nullptr;
    bool captureValid=false,inkReady=false;
    static constexpr unsigned MaxInkItems=256;
    std::array<ImGuiID,MaxInkItems> inkIdentity{};
    std::array<std::uint64_t,MaxInkItems> inkLastUsed{};
    std::uint64_t inkEpoch=0;
    unsigned programs[4]{},inkBuffers[2]{},sourceTexture=0;
    int width=0,height=0;
    ImVec2 captureOrigin{},captureSize{},captureSoftSize{},captureDisplayPos{},captureDisplaySize{},captureScale{};
};
}
