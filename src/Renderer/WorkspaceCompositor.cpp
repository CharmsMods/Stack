#include "WorkspaceCompositor.h"
#include "App/WorkspaceSwitcher.h"
#include "App/ToolSwitcher.h"
#include "ToolDrawerShaders.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "imgui_impl_opengl3.h"
#include <algorithm>
#include <cmath>
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

namespace Stack::Renderer {
namespace {
constexpr const char* vertex = R"(#version 430 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0, 1);
})";
constexpr const char* fragment = R"(#version 430 core
in vec2 uv; out vec4 color;
uniform sampler2D scene;
uniform sampler2D softScene;
uniform float amount, recess, curvature;
void main() {
    vec2 p = (uv - .5) * 2.0;
    float scale = mix(1.0, recess, amount);
    vec2 q = p / scale;
    // Vertical arcs bow inward at the center. Horizontal mapping follows a
    // cylindrical arc as well, so every texel bends with the screen.
    float k = .28 * amount;
    float x = abs(k) < .0001 ? q.x : asin(clamp(q.x * sin(k), -1., 1.)) / k;
    float height = 1.0 - curvature * amount * (1.0 - x*x);
    vec2 t = vec2(x, q.y / height) * .5 + .5;
    float edge = max(abs(x), abs(q.y / height));
    float aa = max(fwidth(edge), .0001);
    float coverage = 1.0 - smoothstep(1.0-aa, 1.0+aa, edge);
    vec3 backdrop = mix(vec3(.012,.016,.025), vec3(.045,.055,.080), exp(-dot(p,p)*1.5));
    vec2 texel = 1.0 / vec2(textureSize(softScene, 0));
    vec3 blur = texture(softScene, t).rgb * .28;
    blur += (texture(softScene,t+vec2(texel.x,0)).rgb + texture(softScene,t-vec2(texel.x,0)).rgb
           + texture(softScene,t+vec2(0,texel.y)).rgb + texture(softScene,t-vec2(0,texel.y)).rgb) * .18;
    vec3 c = mix(texture(scene,t).rgb, blur, amount * .7);
    vec2 split = vec2(1.2 / float(textureSize(scene,0).x),0) * amount;
    c.r = mix(c.r, texture(scene,t+split).r, .075*amount);
    c.b = mix(c.b, texture(scene,t-split).b, .075*amount);
    c *= mix(1.0, .76, amount);
    c += vec3(.13,.18,.27) * exp(-abs(1.0-edge)*230.0) * amount * .22;
    color = vec4(mix(backdrop,c,coverage),1.0);
})";

constexpr const char* blendFragment = R"(#version 430 core
in vec2 uv; out vec4 color;
uniform sampler2D scene, previous;
uniform float weight;
void main() { color = mix(texture(previous,uv), texture(scene,uv), weight); }
)";
constexpr const char* previewFragment = R"(#version 430 core
in vec2 uv; out vec4 color;
uniform sampler2D scene, preview;
uniform vec4 bounds, fittedBounds;
uniform vec3 backdrop;
uniform float amount;
void main() {
    vec4 live = texture(scene, uv);
    if (any(lessThan(uv, bounds.xy)) || any(greaterThan(uv, bounds.zw))) {
        color = live; return;
    }
    vec4 remembered = vec4(backdrop, 1.0);
    if (all(greaterThanEqual(uv, fittedBounds.xy)) && all(lessThanEqual(uv, fittedBounds.zw))) {
        vec2 sampleUv = (uv - fittedBounds.xy) / (fittedBounds.zw - fittedBounds.xy);
        remembered = texture(preview, sampleUv);
    }
    color = mix(live, remembered, amount);
})";

ImVec4 NormalizedBounds(ImVec2 min, ImVec2 max, const ImDrawData& data) {
    const float w = std::max(1.f, data.DisplaySize.x);
    const float h = std::max(1.f, data.DisplaySize.y);
    return ImVec4(
        std::clamp((min.x - data.DisplayPos.x) / w, 0.f, 1.f),
        std::clamp(1.f - (max.y - data.DisplayPos.y) / h, 0.f, 1.f),
        std::clamp((max.x - data.DisplayPos.x) / w, 0.f, 1.f),
        std::clamp(1.f - (min.y - data.DisplayPos.y) / h, 0.f, 1.f));
}
bool HasArea(const ImVec4& bounds) { return bounds.z > bounds.x && bounds.w > bounds.y; }
void SetBounds(unsigned int program, const char* name, const ImVec4& bounds) {
    glUniform4f(glGetUniformLocation(program, name), bounds.x, bounds.y, bounds.z, bounds.w);
}
struct State {
    GLState::FramebufferState fb{true};
    GLint program, vao, active, textures[2], samplers[2];
    GLboolean blend, scissor, depth, cull;
    GLfloat clear[4];
    State() {
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        for (int i=0;i<2;++i) { glActiveTexture(GL_TEXTURE0+i); glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]); glGetIntegerv(GL_SAMPLER_BINDING,&samplers[i]); }
        blend=glIsEnabled(GL_BLEND); scissor=glIsEnabled(GL_SCISSOR_TEST); depth=glIsEnabled(GL_DEPTH_TEST); cull=glIsEnabled(GL_CULL_FACE);
    }
    ~State() {
        glClearColor(clear[0],clear[1],clear[2],clear[3]);
        fb.Restore(true); glUseProgram(program); glBindVertexArray(vao);
        for (int i=0;i<2;++i) { glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,textures[i]); glBindSampler(i,samplers[i]); }
        glActiveTexture(active);
        auto restore=[](GLenum cap, bool enabled) { if(enabled) glEnable(cap); else glDisable(cap); };
        restore(GL_BLEND,blend); restore(GL_SCISSOR_TEST,scissor); restore(GL_DEPTH_TEST,depth); restore(GL_CULL_FACE,cull);
    }
};
void Append(ImDrawData& data, ImDrawList* list) {
    data.CmdLists.push_back(list); ++data.CmdListsCount;
    data.TotalVtxCount += list->VtxBuffer.Size; data.TotalIdxCount += list->IdxBuffer.Size;
}
}
bool WorkspaceCompositor::Ensure(int w, int h) {
    if (failed) return false;
    if (!program) {
        program = GLHelpers::CreateShaderProgram(vertex,fragment);
        blendProgram = GLHelpers::CreateShaderProgram(vertex,blendFragment);
        previewProgram = GLHelpers::CreateShaderProgram(vertex, previewFragment);
        toolProgram = GLHelpers::CreateShaderProgram(vertex, ToolDrawerShaders::Composite);
        blurProgram = GLHelpers::CreateShaderProgram(vertex, ToolDrawerShaders::Blur);
        if (!program || !blendProgram || !previewProgram || !toolProgram || !blurProgram) { failed=true; return false; }
        glGenVertexArrays(1,&vao);
    }
    if (width == w && height == h && framebuffer) return true;
    if (framebuffer) glDeleteFramebuffers(1,&framebuffer);
    if (lowFramebuffer) glDeleteFramebuffers(1,&lowFramebuffer);
    if (texture) glDeleteTextures(1,&texture);
    if (lowTexture) glDeleteTextures(1,&lowTexture);
    if (historyFramebuffer) glDeleteFramebuffers(1,&historyFramebuffer);
    if (presentedFramebuffer) glDeleteFramebuffers(1,&presentedFramebuffer);
    if (historyTexture) glDeleteTextures(1,&historyTexture);
    if (presentedTexture) glDeleteTextures(1,&presentedTexture);
    historyTexture = GLHelpers::CreateTextureFromPixels(nullptr,w,h,4,false);
    presentedTexture = GLHelpers::CreateTextureFromPixels(nullptr,w,h,4,false);
    historyFramebuffer = historyTexture ? GLHelpers::CreateFBO(historyTexture) : 0;
    presentedFramebuffer = presentedTexture ? GLHelpers::CreateFBO(presentedTexture) : 0;
    historyValid = false;
    texture = GLHelpers::CreateTextureFromPixels(nullptr,w,h,4,false);
    lowTexture = GLHelpers::CreateTextureFromPixels(nullptr,std::max(1,w/2),std::max(1,h/2),4,false);
    framebuffer = texture ? GLHelpers::CreateFBO(texture) : 0;
    lowFramebuffer = lowTexture ? GLHelpers::CreateFBO(lowTexture) : 0;
    width=w; height=h;
    failed = !framebuffer || !lowFramebuffer || !historyFramebuffer || !presentedFramebuffer;
    return !failed;
}
bool WorkspaceCompositor::EnsureToolBlur(int w, int h) {
    const int lowW = std::max(1, (w + 1) / 2), lowH = std::max(1, (h + 1) / 2);
    if (toolBlurWidth == lowW && toolBlurHeight == lowH && toolBlurFramebuffers[0] && toolBlurFramebuffers[1]) return true;
    glDeleteFramebuffers(2, toolBlurFramebuffers);
    glDeleteTextures(2, toolBlurTextures);
    for (int i = 0; i < 2; ++i) {
        toolBlurTextures[i] = GLHelpers::CreateTextureFromPixels(nullptr, lowW, lowH, 4, false);
        toolBlurFramebuffers[i] = toolBlurTextures[i] ? GLHelpers::CreateFBO(toolBlurTextures[i]) : 0;
    }
    toolBlurWidth = lowW; toolBlurHeight = lowH;
    return toolBlurFramebuffers[0] && toolBlurFramebuffers[1];
}
void WorkspaceCompositor::BlurTools(float amount, const ImVec4& bounds,
    const ImVec2& feather, float pixelScale) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, presentedFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, toolBlurFramebuffers[0]);
    glBlitFramebuffer(0, 0, width, height, 0, 0, toolBlurWidth, toolBlurHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glViewport(0, 0, toolBlurWidth, toolBlurHeight);
    glUseProgram(blurProgram);
    glUniform1i(glGetUniformLocation(blurProgram, "scene"), 0);
    SetBounds(blurProgram, "bounds", bounds);
    glUniform2f(glGetUniformLocation(blurProgram, "feather"), feather.x, feather.y);
    // Half-resolution pixels are filtered with contiguous Gaussian taps.
    // Cap support at 24 texels per side to keep high-DPI rendering bounded.
    glUniform1f(glGetUniformLocation(blurProgram, "sigma"),
        std::min(8.f, (2.f + 4.f * amount) * pixelScale));
    for (int pass = 0; pass < 2; ++pass) {
        glBindFramebuffer(GL_FRAMEBUFFER, toolBlurFramebuffers[1 - pass]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, toolBlurTextures[pass]);
        glUniform2f(glGetUniformLocation(blurProgram, "direction"),
            pass == 0 ? 1.f / toolBlurWidth : 0.f,
            pass == 1 ? 1.f / toolBlurHeight : 0.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}
void WorkspaceCompositor::CaptureLastProjectFrame() {
    if (lastProjectFrame.workspace) {
        previews.Capture(lastProjectFrame.framebuffer, lastProjectFrame.workspace,
            lastProjectFrame.rootView, lastProjectFrame.left, lastProjectFrame.bottom,
            lastProjectFrame.right, lastProjectFrame.top);
    }
    lastProjectFrame = {};
}

void WorkspaceCompositor::ForgetProjectPreview(std::uint64_t workspace) {
    previews.Forget(workspace);
    if (lastProjectFrame.workspace == workspace) lastProjectFrame = {};
}

void WorkspaceCompositor::PresentProjectPreview(const ProjectFrame& frame,
    const ImDrawData& data, const ImVec4& clear) {
    if (frame.previewAmount <= 0.f) return;
    const auto* entry = previews.Use(frame.previewWorkspace, frame.previewRootView);
    const ImVec4 bounds = NormalizedBounds(frame.bodyMin, frame.bodyMax, data);
    if (!entry || !HasArea(bounds)) return;
    // Keep the remembered layout's proportions after a window resize or
    // panel-width change. Never stretch photographs to fill the new bounds.
    const float targetWidth = (bounds.z - bounds.x) * width;
    const float targetHeight = (bounds.w - bounds.y) * height;
    const float scale = std::min(targetWidth / entry->sourceWidth, targetHeight / entry->sourceHeight);
    const float fitWidth = entry->sourceWidth * scale / width;
    const float fitHeight = entry->sourceHeight * scale / height;
    const float x = (bounds.x + bounds.z - fitWidth) * .5f;
    const float y = (bounds.y + bounds.w - fitHeight) * .5f;
    glUseProgram(previewProgram);
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, presentedTexture); glBindSampler(0, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, entry->texture); glBindSampler(1, 0);
    glUniform1i(glGetUniformLocation(previewProgram, "scene"), 0);
    glUniform1i(glGetUniformLocation(previewProgram, "preview"), 1);
    SetBounds(previewProgram, "bounds", bounds);
    SetBounds(previewProgram, "fittedBounds", ImVec4(x, y, x + fitWidth, y + fitHeight));
    glUniform3f(glGetUniformLocation(previewProgram, "backdrop"), clear.x, clear.y, clear.z);
    glUniform1f(glGetUniformLocation(previewProgram, "amount"), std::clamp(frame.previewAmount, 0.f, 1.f));
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

bool WorkspaceCompositor::Render(ImDrawData* data, const Workspace::Switcher& sw, const ImVec4& clear,
    const Tools::Switcher* tools, int toolIndex, std::uint64_t projectWorkspace,
    bool retainProjectFrame, const ProjectFrame* projectFrame) {
    if (!data) return false;
    const int w=static_cast<int>(data->DisplaySize.x*data->FramebufferScale.x);
    const int h=static_cast<int>(data->DisplaySize.y*data->FramebufferScale.y);
    if (w<=0 || h<=0) return false;
    const bool toolMode = tools && tools->Visible();
    const bool projectMode = projectWorkspace != 0 && !toolMode && !sw.Visible();
    const int mode = toolMode ? 2 : sw.Visible() ? 1 : projectMode ? 3 : 0;
    const bool visible = mode != 0;
    // Retain the last presented project body so switches can crossfade even
    // when an async close or native titlebar action changes the active owner.
    if (!visible || mode != previousMode) historyValid=false;
    previousMode = mode;
    if (!visible && (failed || (program && width==w && height==h))) return false;
    State saved;
    const ImVec4 projectBounds = projectFrame ?
        NormalizedBounds(projectFrame->bodyMin, projectFrame->bodyMax, *data) : ImVec4();
    const bool captureAllowed = projectMode && !retainProjectFrame && projectFrame &&
        projectFrame->captureAllowed && projectFrame->rootView >= 0 && HasArea(projectBounds);
    // A newly starting hover can capture the outgoing live body below. Keep
    // its chosen preview from being the entry evicted by that capture.
    if (projectMode && projectFrame && projectFrame->previewAmount > 0.f)
        previews.Use(projectFrame->previewWorkspace, projectFrame->previewRootView);
    // Capture the previous clean frame before a resize, owner change or
    // transient overlay can overwrite either of its render targets.
    if (lastProjectFrame.workspace && (!captureAllowed || width != w || height != h ||
        lastProjectFrame.workspace != projectWorkspace || lastProjectFrame.rootView != projectFrame->rootView))
        CaptureLastProjectFrame();
    if (!Ensure(w,h)) return false;
    if (!visible) return false;
    const ImVec4 blurBounds = NormalizedBounds(toolBlurMin, toolBlurMax, *data);
    const bool blurTools = toolMode && HasArea(blurBounds) && tools->Amount() > 0.f;
    if (blurTools && !EnsureToolBlur(w, h)) return false;
    body.Clear(); overlay.Clear();
    for (auto* out : {&body,&overlay}) {
        out->Valid=true; out->DisplayPos=data->DisplayPos; out->DisplaySize=data->DisplaySize;
        out->FramebufferScale=data->FramebufferScale; out->OwnerViewport=data->OwnerViewport;
        out->Textures=data->Textures;
    }
    for (auto* list : data->CmdLists) {
        const bool foreground = (toolMode && list == toolsFixed) ||
            std::find(fixed.begin(),fixed.end(),list)!=fixed.end() ||
            ((projectMode || toolMode) && std::find(chrome.begin(),chrome.end(),list)!=chrome.end());
        Append(foreground ? overlay : body,list);
    }
    if (projectMode && retainProjectFrame && historyValid && destination == projectWorkspace) {
        // Closing clears the editor before its tab can be retired next frame.
        // Hold the last image through that gap instead of presenting a blank.
        saved.fb.Restore(true);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, presentedFramebuffer);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        saved.fb.Restore(true);
        ImGui_ImplOpenGL3_RenderDrawData(&overlay);
        return true;
    }
    // Keep the last visible blend as the source when direction changes mid-fade.
    const double now = ImGui::GetTime();
    const bool reducedMotion = projectFrame && projectFrame->reducedMotion;
    const std::uint64_t nextDestination = projectMode ? projectWorkspace :
        static_cast<std::uint64_t>(toolMode ? toolIndex : sw.preview);
    const float fadeSeconds = toolMode || projectMode ? .18f : sw.config.previewFadeSeconds;
    if (historyValid && destination != nextDestination && !reducedMotion) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,presentedFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,historyFramebuffer);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        fadeStarted=now;
    }
    float weight=historyValid && !reducedMotion ? std::clamp(float(now-fadeStarted)/fadeSeconds,0.f,1.f) : 1.f;
    weight=weight*weight*(3.f-2.f*weight);
    if (!toolMode && !projectMode && !sw.held) weight=std::max(weight,1.f-sw.Amount());
    destination=nextDestination;
    if (!historyValid || reducedMotion) fadeStarted=now-fadeSeconds;
    // Steady project frames render directly into the retained scene. Only
    // transitions need a second render target and a fullscreen blend pass.
    const bool directProjectFrame = projectMode && weight >= 1.0f;
    glBindFramebuffer(GL_FRAMEBUFFER, directProjectFrame ? presentedFramebuffer : framebuffer);
    glViewport(0,0,w,h); glDisable(GL_SCISSOR_TEST);
    glClearColor(clear.x,clear.y,clear.z,1); glClear(GL_COLOR_BUFFER_BIT);
    const bool photoComposited = rawBackdropFrame && rawBackdrop.Render(*rawBackdropFrame, *data, clear);
    {
        RawPhotoInteriorClip photoClip(photoComposited ? rawBackdropFrame : nullptr);
        ImGui_ImplOpenGL3_RenderDrawData(&body);
    }
    if (captureAllowed) {
        // Remember the unblended body, even during project crossfades.
        // Hover never enters this texture and cannot contaminate a later snapshot.
        lastProjectFrame = {projectWorkspace, projectFrame->rootView,
            directProjectFrame ? presentedFramebuffer : framebuffer,
            int(std::ceil(projectBounds.x * w)), int(std::ceil(projectBounds.y * h)),
            int(std::floor(projectBounds.z * w)), int(std::floor(projectBounds.w * h))};
    }
    if (!directProjectFrame) {
        glBindFramebuffer(GL_FRAMEBUFFER,presentedFramebuffer); glViewport(0,0,w,h);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glUseProgram(blendProgram); glBindVertexArray(vao);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture); glBindSampler(0,0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,historyValid ? historyTexture : texture); glBindSampler(1,0);
        glUniform1i(glGetUniformLocation(blendProgram,"scene"),0);
        glUniform1i(glGetUniformLocation(blendProgram,"previous"),1);
        glUniform1f(glGetUniformLocation(blendProgram,"weight"),weight);
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    historyValid=true;
    if (toolMode) {
        const float dpi = data->OwnerViewport ? data->OwnerViewport->DpiScale : 1.f;
        const float amount = tools->Amount();
        const ImVec2 feather(28.f * dpi / std::max(1.f, data->DisplaySize.x),
            28.f * dpi / std::max(1.f, data->DisplaySize.y));
        if (blurTools) BlurTools(amount, blurBounds, feather, std::max(.5f, dpi * data->FramebufferScale.x));
        saved.fb.Restore(true);
        glUseProgram(toolProgram);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, presentedTexture); glBindSampler(0,0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, toolBlurTextures[0]); glBindSampler(1,0);
        glUniform1i(glGetUniformLocation(toolProgram, "scene"), 0);
        glUniform1i(glGetUniformLocation(toolProgram, "softScene"), 1);
        glUniform1f(glGetUniformLocation(toolProgram, "amount"), amount);
        SetBounds(toolProgram, "bounds", blurBounds);
        glUniform2f(glGetUniformLocation(toolProgram, "feather"), feather.x, feather.y);
    } else if (projectMode) {
        // No deformation or blur for ordinary project navigation.
        saved.fb.Restore(true);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, presentedFramebuffer);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        saved.fb.Restore(true);
        if (projectFrame) PresentProjectPreview(*projectFrame, *data, clear);
        ImGui_ImplOpenGL3_RenderDrawData(&overlay);
        return true;
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,presentedFramebuffer); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,lowFramebuffer);
        glBlitFramebuffer(0,0,w,h,0,0,std::max(1,w/2),std::max(1,h/2),GL_COLOR_BUFFER_BIT,GL_LINEAR);
        saved.fb.Restore(true);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,presentedTexture);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,lowTexture);
        glUniform1i(glGetUniformLocation(program,"scene"),0); glUniform1i(glGetUniformLocation(program,"softScene"),1);
        glUniform1f(glGetUniformLocation(program,"amount"),sw.Amount());
        glUniform1f(glGetUniformLocation(program,"recess"),sw.config.recessedScale);
        glUniform1f(glGetUniformLocation(program,"curvature"),sw.config.curvature);
    }
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES,0,3);
    ImGui_ImplOpenGL3_RenderDrawData(&overlay);
    return true;
}
void WorkspaceCompositor::Shutdown() {
    rawBackdrop.Shutdown();
    previews.Clear();
    lastProjectFrame = {};
    if(framebuffer) glDeleteFramebuffers(1,&framebuffer);
    if(lowFramebuffer) glDeleteFramebuffers(1,&lowFramebuffer);
    if(texture) glDeleteTextures(1,&texture);
    if(lowTexture) glDeleteTextures(1,&lowTexture);
    if(historyFramebuffer) glDeleteFramebuffers(1,&historyFramebuffer);
    if(presentedFramebuffer) glDeleteFramebuffers(1,&presentedFramebuffer);
    if(historyTexture) glDeleteTextures(1,&historyTexture);
    if(presentedTexture) glDeleteTextures(1,&presentedTexture);
    if(blendProgram) glDeleteProgram(blendProgram);
    if(previewProgram) glDeleteProgram(previewProgram);
    previewProgram = 0;
    historyFramebuffer=presentedFramebuffer=historyTexture=presentedTexture=blendProgram=0;
    historyValid=false;
    if(toolProgram) glDeleteProgram(toolProgram);
    if(blurProgram) glDeleteProgram(blurProgram);
    glDeleteFramebuffers(2, toolBlurFramebuffers);
    glDeleteTextures(2, toolBlurTextures);
    toolBlurTextures[0]=toolBlurTextures[1]=toolBlurFramebuffers[0]=toolBlurFramebuffers[1]=0;
    toolBlurWidth=toolBlurHeight=0; toolProgram=blurProgram=0;
    if(program) glDeleteProgram(program);
    if(vao) glDeleteVertexArrays(1,&vao);
    framebuffer=lowFramebuffer=texture=lowTexture=program=vao=0; width=height=0; failed=false;
}
}
