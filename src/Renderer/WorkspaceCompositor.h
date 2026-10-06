#pragma once
#include "imgui.h"
#include "WorkspacePreviewCache.h"
#include "RawImageBackdrop.h"
#include <cstdint>
#include <vector>
namespace Stack::Workspace { class Switcher; }
namespace Stack::Tools { struct Switcher; }
namespace Stack::Renderer {
class WorkspaceCompositor {
public:
    struct ProjectFrame {
        int rootView = -1;
        ImVec2 bodyMin{}, bodyMax{};
        bool captureAllowed = true;
        bool reducedMotion = false;
        std::uint64_t previewWorkspace = 0;
        int previewRootView = -1;
        float previewAmount = 0.f;
    };
    void BeginFrame() {
        fixed.clear(); chrome.clear(); toolsFixed = nullptr;
        toolBlurMin = toolBlurMax = ImVec2();
        rawBackdropFrame = nullptr;
    }
    void KeepFixed(ImDrawList* list) { fixed.push_back(list); }
    void SetRawImageBackdrop(const RawImageBackdropFrame* frame) { rawBackdropFrame = frame; }
    // Chrome stays live during project crossfades, but belongs to the scene
    // that recedes and bends when the Alt workspace switcher is visible.
    void KeepChrome(ImDrawList* list) { chrome.push_back(list); }
    void KeepToolsFixed(ImDrawList* list) { toolsFixed = list; }
    // Absolute ImGui coordinates, supplied by the owner of the active controls.
    // Unset or empty bounds leave the scene sharp.
    void SetToolBlurBounds(ImVec2 min, ImVec2 max) { toolBlurMin = min; toolBlurMax = max; }
    bool HasProjectPreview(std::uint64_t workspace, int rootView) const {
        return previews.Find(workspace, rootView) != nullptr;
    }
    // Call on the main GL context when retiring or replacing a project.
    void ForgetProjectPreview(std::uint64_t workspace);
    bool Render(ImDrawData* data, const Workspace::Switcher& switcher, const ImVec4& clear,
        const Tools::Switcher* tools = nullptr, int toolIndex = -1,
        std::uint64_t projectWorkspace = 0, bool retainProjectFrame = false,
        const ProjectFrame* projectFrame = nullptr);
    void Shutdown();
private:
    bool Ensure(int width, int height);
    bool EnsureToolBlur(int width, int height);
    void BlurTools(float amount, const ImVec4& bounds, const ImVec2& feather, float pixelScale);
    void CaptureLastProjectFrame();
    void PresentProjectPreview(const ProjectFrame& frame, const ImDrawData& data, const ImVec4& clear);
    unsigned int texture = 0, framebuffer = 0, lowTexture = 0, lowFramebuffer = 0;
    unsigned int program = 0, blendProgram = 0, previewProgram = 0, vao = 0;
    unsigned int toolProgram = 0, blurProgram = 0;
    unsigned int toolBlurTextures[2]{}, toolBlurFramebuffers[2]{};
    int toolBlurWidth = 0, toolBlurHeight = 0;
    unsigned int historyTexture = 0, historyFramebuffer = 0;
    unsigned int presentedTexture = 0, presentedFramebuffer = 0;
    bool historyValid = false;
    int previousMode = 0;
    std::uint64_t destination = 0;
    double fadeStarted = 0;
    int width = 0, height = 0;
    bool failed = false;
    std::vector<ImDrawList*> fixed;
    std::vector<ImDrawList*> chrome;
    ImDrawList* toolsFixed = nullptr;
    ImVec2 toolBlurMin{}, toolBlurMax{};
    WorkspacePreviewCache previews;
    RawImageBackdrop rawBackdrop;
    const RawImageBackdropFrame* rawBackdropFrame = nullptr;
    struct LastProjectFrame {
        std::uint64_t workspace = 0;
        int rootView = -1;
        unsigned int framebuffer = 0;
        int left = 0, bottom = 0, right = 0, top = 0;
    } lastProjectFrame;
    ImDrawData body, overlay;
};
}
