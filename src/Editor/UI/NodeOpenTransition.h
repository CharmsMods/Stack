#pragma once
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Editor {
struct NodeAppearanceTexture {
    unsigned int name = 0;
    ~NodeAppearanceTexture();
};
// Presentation only: event-time node geometry and small immutable copies of
// textured node details. The main image stays with the live viewport owner.
class NodeOpenTransition {
public:
    struct Mesh {
        ImVec4 clip{};
        std::vector<ImDrawVert> vertices;
        std::shared_ptr<NodeAppearanceTexture> texture; // Null uses the font atlas.
    };
    struct Appearance {
        ImVec2 minimum{}, maximum{};
        std::string nodeUuid;
        std::vector<Mesh> meshes;
        bool Valid() const { return maximum.x > minimum.x && maximum.y > minimum.y; }
        // firstIndex is the triangle boundary recorded before drawing this node.
        void Capture(const ImDrawList& list, int firstVertex, ImVec2 low, ImVec2 high, unsigned firstIndex = 0);
    };
    void Begin(Appearance appearance, std::string document, double now) {
        if (active && hasImage && identity == document && appearance.Valid()) {
            // A new opening replaces the destination, not the visible frame.
            // Keep the outgoing card until it fades rather than substituting
            // another node's pixels underneath a partially transparent photo.
            const double visibleTime = presentedAt >= 0 ? presentedAt : now;
            openingPhoto = ImageOpacity(visibleTime);
            openingBlur = surround ? BlurOpacity(visibleTime) : 0.f;
            openingControls = ControlsOpacity(visibleTime);
            openingNode = NodeOpacity(visibleTime);
            retargetLow = minimum; retargetHigh = maximum;
            requested = now; started = -1;
            closing = closed = false; surround = retargeting = true;
            returnOffset = correctionLow = correctionHigh = {};
            return;
        }
        Reset(); node = std::move(appearance); identity = std::move(document);
        requested = now; active = node.Valid(); surround = active; minimum = node.minimum; maximum = node.maximum;
    }
    void Reset() {
        ReleaseSubmittedDraw();
        active = surround = closing = closed = hasImage = retargeting = false;
        started = presentedAt = -1; node = {}; identity.clear();
        openingPhoto = openingBlur = openingControls = 0.f; openingNode = 1.f;
        minimum=maximum=targetLow=targetHigh={};
    }
    // ImGui commands borrow texture names until this frame reaches the backend.
    // Navigation may replace the appearance after DrawNode, in the same frame.
    void ReleaseSubmittedDraw() const {
        if (!ImGui::GetCurrentContext() || ImGui::GetFrameCount() != drawnFrame)
            drawnTextures.clear();
    }
    // The editor calls this only on its current GL context, between UI frames.
    void Shutdown() { Reset(); drawnTextures.clear(); }
    void Tick(const std::string& document, double now, bool reducedMotion) {
        ReleaseSubmittedDraw();
        if (!active) return;
        if (identity != document) {
            // A cancelled return belongs to the old document; it must not
            // switch the newly activated document to Graph.
            Reset();
        } else if (reducedMotion || (started < 0 && now-requested>2.0)) {
            const bool returning = closing;
            Reset(); closed = returning;
        } else if (closing ? now-started >= reverseFrom : started >= 0 && now-started >= .76) {
            active=false; closed=closing; node.meshes.clear();
        }
    }
    bool Active() const { return active; }
    bool Closing() const { return closing; }
    bool Closed() const { return closed; }
    bool Close(Appearance destination, const std::string& document, double now, float backdropOpacity = 0.f) {
        if (closing) return true;
        if (!destination.Valid() || !hasImage || (!identity.empty() && identity != document)) return false;
        const ImVec2 visibleLow=minimum, visibleHigh=maximum;
        const double visibleTime = presentedAt >= 0 ? presentedAt : now;
        closingPhoto = ImageOpacity(visibleTime);
        closingControls = ControlsOpacity(visibleTime);
        closingNode = NodeOpacity(visibleTime);
        reverseBlur = surround ? BlurOpacity(visibleTime) : std::clamp(backdropOpacity,0.f,1.f);
        reverseFrom = active && started >= 0 && !retargeting
            ? std::clamp(visibleTime-started, 0.0, .76) : .76;
        identity=document;
        node = std::move(destination);
        ExpandNode(targetLow, targetHigh);
        started=now; closing=active=true;
        surround=true;
        returnOffset=correctionLow=correctionHigh={};
        ImVec2 nextLow=targetLow, nextHigh=targetHigh;
        Present(nextLow,nextHigh,now);
        // Retargeting an interrupted opening must start at its visible bounds.
        correctionLow={visibleLow.x-nextLow.x,visibleLow.y-nextLow.y};
        correctionHigh={visibleHigh.x-nextHigh.x,visibleHigh.y-nextHigh.y};
        minimum=visibleLow; maximum=visibleHigh;
        return true;
    }
    void SetReturnOffset(ImVec2 offset) { returnOffset = offset; }
    bool SurroundRequested() const { return surround; }
    float Progress(double now, double begin, double duration) const {
        if (!active) return closed ? 0.f : 1.f;
        if (started < 0) return 0.f;
        const double elapsed = closing ? reverseFrom-(now-started) : now-started;
        const float t = static_cast<float>(std::clamp((elapsed - begin) / duration, 0.0, 1.0));
        return t*t*t*(t*(6.f*t-15.f)+10.f);
    }
    float ImageOpacity(double now) const {
        if (!active) return closed ? 0.f : 1.f;
        return closing ? ClosingFade(now,.015,.165,closingPhoto)
            : openingPhoto+(1.f-openingPhoto)*Progress(now,.015,.165);
    }
    float BlurOpacity(double now) const {
        if (!active) return closed ? 0.f : 1.f;
        return closing ? ClosingFade(now,.46,.14,reverseBlur)
            : openingBlur+(1.f-openingBlur)*Progress(now,.46,.14);
    }
    float ControlsOpacity(double now) const {
        if (!active) return closed ? 0.f : 1.f;
        return closing ? ClosingFade(now,.58,.18,closingControls)
            : openingControls+(1.f-openingControls)*Progress(now,.58,.18);
    }
    void Present(ImVec2& low, ImVec2& high, double now) {
        hasImage = high.x>low.x && high.y>low.y;
        if (!closing) { targetLow=low; targetHigh=high; }
        presentedAt = now;
        if (!active) { minimum=low; maximum=high; return; }
        if (started < 0) {
            started = now;
            if (!retargeting) ExpandNode(low, high);
        }
        const auto mix = [](ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t); };
        if (retargeting && !closing) {
            const float redirected = Progress(now,0,.32);
            minimum = mix(retargetLow,targetLow,redirected);
            maximum = mix(retargetHigh,targetHigh,redirected);
            low=minimum; high=maximum;
            return;
        }
        const float shape = Progress(now, 0, .18);
        const float travel = Progress(now, .16, .32);
        minimum = mix(mix(node.minimum,expandedLow,shape),targetLow,travel);
        maximum = mix(mix(node.maximum,expandedHigh,shape),targetHigh,travel);
        if (closing) {
            minimum.x += returnOffset.x * (1.f-travel); minimum.y += returnOffset.y * (1.f-travel);
            maximum.x += returnOffset.x * (1.f-travel); maximum.y += returnOffset.y * (1.f-travel);
            const float t=float(std::clamp((now-started)/std::max(.001,reverseFrom),0.0,1.0));
            const float correction=1.f-t*t*(3.f-2.f*t);
            minimum.x+=correctionLow.x*correction; minimum.y+=correctionLow.y*correction;
            maximum.x+=correctionHigh.x*correction; maximum.y+=correctionHigh.y*correction;
        }
        low=minimum; high=maximum;
    }
    void FinishPhoto(ImDrawList& list, int firstVertex, double now) const {
        if (!active) return;
        const float opacity = ImageOpacity(now);
        if (opacity >= 1.f) return;
        for (int i=firstVertex; i<list.VtxBuffer.Size; ++i) {
            auto& color=list.VtxBuffer[i].col;
            color=(color & ~IM_COL32_A_MASK) | (static_cast<ImU32>((color >> IM_COL32_A_SHIFT)*opacity) << IM_COL32_A_SHIFT);
        }
    }
    void DrawNode(ImDrawList& list, double now) const {
        ReleaseSubmittedDraw();
        if (!active) return;
        const float opacity=NodeOpacity(now);
        if (opacity<=0.f) return;
        const float scaleX=(maximum.x-minimum.x)/(node.maximum.x-node.minimum.x);
        const float scaleY=(maximum.y-minimum.y)/(node.maximum.y-node.minimum.y);
        const auto transform=[&](ImVec2 p) { return ImVec2(
            minimum.x+(p.x-node.minimum.x)*scaleX,
            minimum.y+(p.y-node.minimum.y)*scaleY); };
        for (const auto& mesh:node.meshes) {
            if (mesh.texture && std::find(drawnTextures.begin(), drawnTextures.end(),
                    mesh.texture) == drawnTextures.end()) {
                drawnTextures.push_back(mesh.texture);
                drawnFrame = ImGui::GetFrameCount();
            }
            list.PushTextureID(mesh.texture ? ImTextureRef((ImTextureID)(intptr_t)mesh.texture->name) : ImGui::GetIO().Fonts->TexRef);
            list.PushClipRect(transform({mesh.clip.x,mesh.clip.y}),transform({mesh.clip.z,mesh.clip.w}),true);
            // Bound each reservation for backends using 16-bit draw indices.
            for (std::size_t begin=0; begin<mesh.vertices.size(); begin+=12000) {
                const auto count=static_cast<int>(std::min<std::size_t>(12000,mesh.vertices.size()-begin));
                list.PrimReserve(count,count);
                for(int i=0;i<count;++i) {
                    const auto& v=mesh.vertices[begin+i];
                    const auto color=(v.col & ~IM_COL32_A_MASK) |
                        (static_cast<ImU32>((v.col >> IM_COL32_A_SHIFT)*opacity) << IM_COL32_A_SHIFT);
                    list.PrimVtx(transform(v.pos),v.uv,color);
                }
            }
            list.PopClipRect();
            list.PopTextureID();
        }
    }
private:
    float ClosingFade(double now, double begin, double duration, float from) const {
        if (from <= 0.f) return 0.f;
        const float start = Progress(started,begin,duration);
        if (start > 0.f) return from*std::clamp(Progress(now,begin,duration)/start,0.f,1.f);
        // A retargeted opening may retain visible pixels before this phase has
        // started. Fade those over the remaining return, never drop them.
        const float t = float(std::clamp((now-started)/std::max(.001,reverseFrom),0.0,1.0));
        return from*(1.f-t*t*(3.f-2.f*t));
    }
    float NodeOpacity(double now) const {
        return closing ? 1.f-ClosingFade(now,.025,.155,1.f-closingNode)
            : openingNode*(1.f-Progress(now,.025,.155));
    }
    void ExpandNode(ImVec2 low, ImVec2 high) {
        const float aspect = std::max(.001f, (high.x-low.x) / std::max(1.f, high.y-low.y));
        const float width = std::max(node.maximum.x-node.minimum.x,
            (node.maximum.y-node.minimum.y)*aspect)*1.12f;
        const ImVec2 center((node.minimum.x+node.maximum.x)*.5f, (node.minimum.y+node.maximum.y)*.5f);
        expandedLow = {center.x-width*.5f, center.y-width/aspect*.5f};
        expandedHigh = {center.x+width*.5f, center.y+width/aspect*.5f};
    }
    Appearance node;
    mutable std::vector<std::shared_ptr<NodeAppearanceTexture>> drawnTextures;
    mutable int drawnFrame = -1;
    std::string identity;
    ImVec2 minimum{},maximum{},expandedLow{},expandedHigh{},targetLow{},targetHigh{},returnOffset{},correctionLow{},correctionHigh{},retargetLow{},retargetHigh{};
    double requested=0,started=-1,presentedAt=-1,reverseFrom=0;
    float reverseBlur=0.f, closingPhoto=0.f, closingControls=0.f, closingNode=0.f;
    float openingPhoto=0.f, openingBlur=0.f, openingControls=0.f, openingNode=1.f;
    bool active=false, surround=false, closing=false, closed=false, hasImage=false, retargeting=false;
};
}
