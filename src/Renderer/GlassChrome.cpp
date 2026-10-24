// SPDX-License-Identifier: GPL-3.0-only
#include "GlassChrome.h"
#include "GlassChromeShaders.h"
#include "GLHelpers.h"
#include "GLStateGuards.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>

namespace Stack::Renderer {
void GlassChrome::BeginFrame(bool requested) {
    surfaces.clear(); inks.clear(); captureValid=false; inkReady=false;
    if (!requested && enabled) { Shutdown(); }
    enabled = requested;
}
bool GlassChrome::AddSurface(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 tint, float radius) {
    if (!enabled || !error.empty() || !draw || max.x <= min.x || max.y <= min.y) return false;
    auto* viewport = ImGui::GetMainViewport();
    const ImVec2 clipMin = draw->GetClipRectMin(), clipMax = draw->GetClipRectMax();
    surfaces.push_back({this,min,max,viewport->Pos,viewport->Size,ImGui::GetIO().DisplayFramebufferScale,
        tint,ImVec4(clipMin.x,clipMin.y,clipMax.x,clipMax.y),radius,viewport->DpiScale});
    draw->AddCallback(Callback,&surfaces.back());
    draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    return true;
}
void GlassChrome::Callback(const ImDrawList*, const ImDrawCmd* command) {
    const auto* surface = static_cast<const Surface*>(command->UserCallbackData);
    if (surface) surface->owner->Draw(*surface);
}
bool GlassChrome::BeginInk(ImDrawList* draw, ImVec2 min, ImVec2 max, ImGuiID id) {
    if (!enabled || !error.empty() || !draw || max.x<=min.x || max.y<=min.y ||
        ImGui::GetWindowViewport()!=ImGui::GetMainViewport()) return false;
    if (inks.size()>=MaxInkItems) {
        error="Glass navigation exceeded its foreground GPU budget. Turn the mode off to use standard navigation.";
        return false;
    }
    const ImVec2 clipMin=draw->GetClipRectMin(), clipMax=draw->GetClipRectMax();
    inks.push_back({this,min,max,id,static_cast<unsigned int>(inks.size()),
        ImVec4(clipMin.x,clipMin.y,clipMax.x,clipMax.y),false});
    draw->AddCallback(InkCallback,&inks.back());
    return true;
}
void GlassChrome::EndInk(ImDrawList* draw) {
    draw->AddCallback(EndInkCallback,nullptr);
    draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
}
void GlassChrome::InkCallback(const ImDrawList*, const ImDrawCmd* command) {
    const auto* ink=static_cast<const Ink*>(command->UserCallbackData);
    if (ink) ink->owner->DrawInk(*ink);
}
void GlassChrome::EndInkCallback(const ImDrawList*, const ImDrawCmd*) {
    // This UI pass owns texture unit 1. Never leave it active: the backend
    // binds each following draw command's atlas on the current texture unit.
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,0); glBindSampler(1,0);
    glActiveTexture(GL_TEXTURE0); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,0);
}
void GlassChrome::DrawInk(const Ink& ink) {
    // Draw order supplies a fresh successful material capture first. A hidden
    // rail or failed material must not reuse last frame's unrelated backdrop.
    if (!captureValid || !inkReady || !ink.usable || !error.empty() || !programs[2]) return;
    const float left=captureDisplayPos.x, right=left+captureDisplaySize.x;
    const float top=captureDisplayPos.y, bottom=top+captureDisplaySize.y;
    const float projection[16]={2.f/(right-left),0,0,0, 0,2.f/(top-bottom),0,0,
        0,0,-1,0, (right+left)/(left-right),(top+bottom)/(bottom-top),0,1};
    glUseProgram(programs[2]);
    glUniformMatrix4fv(0,1,GL_FALSE,projection);
    glUniform1i(4,int(ink.slot));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,inkBuffers[1]);
    glActiveTexture(GL_TEXTURE0);
    // Keep the ImGui backend's VAO, VBO/EBO, clip and blend state. Stack pins
    // its GLSL430 backend layout to Position/UV/Color locations 0/1/2.
}
void GlassChrome::PrepareInk() {
    inkReady=false;
    if (!captureValid || inks.empty()) return;
    struct Item { float region[4]; std::uint32_t identity[4]; };
    static_assert(sizeof(Item)==32,"std430 ink item layout");
    std::array<Item,MaxInkItems> items{};
    const float softW=float((width+1)/2), softH=float((height+1)/2);
    const float uScale=captureSoftSize.x/(captureSize.x*softW);
    const float vScale=captureSoftSize.y/(captureSize.y*softH);
    for (Ink& ink : inks) {
        const ImVec2 min(std::max(ink.min.x,ink.clip.x),std::max(ink.min.y,ink.clip.y));
        const ImVec2 max(std::min(ink.max.x,ink.clip.z),std::min(ink.max.y,ink.clip.w));
        const float leftPx=(min.x-captureDisplayPos.x)*captureScale.x;
        const float rightPx=(max.x-captureDisplayPos.x)*captureScale.x;
        const float topPx=(captureDisplaySize.y-(min.y-captureDisplayPos.y))*captureScale.y;
        const float bottomPx=(captureDisplaySize.y-(max.y-captureDisplayPos.y))*captureScale.y;
        ink.usable=max.x>min.x && max.y>min.y && leftPx>=captureOrigin.x &&
            rightPx<=captureOrigin.x+captureSize.x && bottomPx>=captureOrigin.y &&
            topPx<=captureOrigin.y+captureSize.y;
        if (!ink.usable) continue; // Never infer off-capture ink from its edge.
        const float centerX=((min.x+max.x)*.5f-captureDisplayPos.x)*captureScale.x;
        const float centerY=(captureDisplaySize.y-((min.y+max.y)*.5f-captureDisplayPos.y))*captureScale.y;
        items[ink.slot]={{(centerX-captureOrigin.x)*uScale,(centerY-captureOrigin.y)*vScale,
            (max.x-min.x)*captureScale.x*.3f*uScale,
            (max.y-min.y)*captureScale.y*.3f*vScale},{ink.id,1,0,0}};
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,inkBuffers[0]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,inks.size()*sizeof(Item),items.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,inkBuffers[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,inkBuffers[1]);
    glActiveTexture(GL_TEXTURE1); glBindSampler(1,0); glBindTexture(GL_TEXTURE_2D,textures[2]);
    glUseProgram(programs[3]);
    glUniform1i(0,int(inks.size()));
    glUniform2f(1,captureSoftSize.x/softW,captureSoftSize.y/softH);
    // Finish prior foreground buffer accesses before updating history, then
    // publish this dispatch to the following fragment-shader reads.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glDispatchCompute((static_cast<unsigned int>(inks.size())+63)/64,1,1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,0); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
    glBindTexture(GL_TEXTURE_2D,0); glActiveTexture(GL_TEXTURE0); glUseProgram(0);
    inkReady=true;
}
void GlassChrome::ReleaseTargets() {
    captureValid=false; inkReady=false;
    glDeleteFramebuffers(3,framebuffers); glDeleteTextures(3,textures);
    for (auto& f : framebuffers) f = 0;
    for (auto& t : textures) t = 0;
    width = height = 0;
}
bool GlassChrome::Ensure(int w, int h) {
    if (!error.empty()) return false;
    if (!programs[0]) {
        programs[0] = GLHelpers::CreateShaderProgram(GlassChromeShaders::Vertex,GlassChromeShaders::Blur);
        programs[1] = GLHelpers::CreateShaderProgram(GlassChromeShaders::Vertex,GlassChromeShaders::Material);
        programs[2] = GLHelpers::CreateShaderProgram(GlassChromeShaders::InkVertex,GlassChromeShaders::InkFragment);
        programs[3] = GLHelpers::CreateComputeProgram(GlassChromeShaders::InkMeasure);
        glGenBuffers(2,inkBuffers);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,inkBuffers[0]);
        glBufferData(GL_SHADER_STORAGE_BUFFER,MaxInkItems*32,nullptr,GL_DYNAMIC_DRAW);
        std::array<std::uint32_t,MaxInkItems*4> emptyHistory{};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,inkBuffers[1]);
        glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(emptyHistory),emptyHistory.data(),GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
        glGenVertexArrays(1,&vao);
        if (!programs[0] || !programs[1] || !programs[2] || !programs[3] || !inkBuffers[0] || !inkBuffers[1] || !vao) {
            error = "Glass navigation could not initialize its GPU shaders. Turn the mode off to use standard navigation.";
            return false;
        }
    }
    if (width >= w && height >= h && framebuffers[0]) return true;
    // Grow in small buckets and retain capacity through reveal/hide animation.
    // Shader valid extents exclude unused pixels from every filtered sample.
    w = std::max(width, ((w+63)/64)*64);
    h = std::max(height, ((h+63)/64)*64);
    // Bounded to 4M source pixels (24 MiB including the two half-size RGBA8
    // targets). One reusable panel allocation, not one full-screen allocation
    // for every surface. Reject oversized requests rather than emulate on CPU.
    if (w <= 0 || h <= 0 || double(w)*h > 4194304.0) {
        error = "Glass navigation exceeded its GPU surface budget. Turn the mode off to use standard navigation.";
        return false;
    }
    ReleaseTargets();
    GLState::PixelUnpackState unpack;
    unpack.ConfigureTightCpuUpload();
    for (int i=0;i<3;++i) {
        const int tw = i ? std::max(1,(w+1)/2) : w;
        const int th = i ? std::max(1,(h+1)/2) : h;
        glGenTextures(1,&textures[i]); glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,tw,th,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&framebuffers[i]); glBindFramebuffer(GL_FRAMEBUFFER,framebuffers[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            error = "Glass navigation GPU allocation failed. Turn the mode off to use standard navigation.";
            unpack.Restore(); ReleaseTargets(); return false;
        }
    }
    unpack.Restore(); width = w; height = h; return true;
}
void GlassChrome::Draw(const Surface& s) {
    if (!error.empty()) return;
    const int fw = int(s.displaySize.x*s.scale.x), fh = int(s.displaySize.y*s.scale.y);
    if (fw <= 0 || fh <= 0) return;
    const float left = std::max({s.min.x,s.clip.x,s.displayPos.x});
    const float right = std::min({s.max.x,s.clip.z,s.displayPos.x+s.displaySize.x});
    const float top = std::max({s.min.y,s.clip.y,s.displayPos.y});
    const float bottom = std::min({s.max.y,s.clip.w,s.displayPos.y+s.displaySize.y});
    if (right <= left || bottom <= top) return;
    const int x = std::clamp(int(std::ceil((left-s.displayPos.x)*s.scale.x)),0,fw);
    const int y = std::clamp(int(std::ceil(fh-(bottom-s.displayPos.y)*s.scale.y)),0,fh);
    const int w = std::clamp(int(std::floor((right-s.displayPos.x)*s.scale.x)),0,fw)-x;
    const int h = std::clamp(int(std::floor(fh-(top-s.displayPos.y)*s.scale.y)),0,fh)-y;
    if (w <= 0 || h <= 0) return;
    // Include a bounded blur/refraction neighborhood, not just pixels inside
    // the material. A separate bounded output apron holds the soft UI shadow;
    // it never changes image-processing data or the rail's hit/layout bounds.
    const int padding = int(std::ceil(16.f*std::max(.5f,s.scale.x*s.dpi)));
    const int captureX = std::max(0,x-padding), captureY = std::max(0,y-padding);
    const int captureW = std::min(fw,x+w+padding)-captureX;
    const int captureH = std::min(fh,y+h+padding)-captureY;
    const GLState::FramebufferState target(true);
    const bool srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB) != GL_FALSE;
    glDisable(GL_FRAMEBUFFER_SRGB);
    glActiveTexture(GL_TEXTURE0); glBindSampler(0,0); glBindSampler(1,0);
    const int reserveW = std::max(captureW,std::min(fw,int(std::ceil((s.max.x-s.min.x)*s.scale.x))+2*padding));
    const int reserveH = std::max(captureH,std::min(fh,int(std::ceil((s.max.y-s.min.y)*s.scale.y))+2*padding));
    if (Ensure(reserveW,reserveH)) {
        glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,target.drawFramebuffer);
        glReadBuffer(target.drawBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffers[0]); glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(captureX,captureY,captureX+captureW,captureY+captureH,
            0,0,captureW,captureH,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        const int lw = std::max(1,(captureW+1)/2), lh = std::max(1,(captureH+1)/2);
        glUseProgram(programs[0]); glBindVertexArray(vao);
        glViewport(0,0,lw,lh);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffers[1]);
        glBindTexture(GL_TEXTURE_2D,textures[0]);
        glUniform2f(0,2.f/width,0.f);
        glUniform2f(1,float(captureW)/width,float(captureH)/height); glDrawArrays(GL_TRIANGLES,0,3);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffers[2]);
        glBindTexture(GL_TEXTURE_2D,textures[1]);
        glUniform2f(0,0.f,1.f/((height+1)/2));
        glUniform2f(1,float(lw)/((width+1)/2),float(lh)/((height+1)/2)); glDrawArrays(GL_TRIANGLES,0,3);
        // Keep the light/shadow in physical pixels at every display scale.
        // Respect caller clipping and the screen edge; no shadow to the left.
        const int shadowPad=int(std::ceil(s.radius*s.scale.x*.8f));
        const int outputRight=std::min({fw,x+w+shadowPad,
            int(std::floor((s.clip.z-s.displayPos.x)*s.scale.x))});
        const int outputBottom=std::max({0,y-shadowPad,
            int(std::ceil(fh-(s.clip.w-s.displayPos.y)*s.scale.y))});
        // The top toolbar owns the pixels above the rail. Do not cast a
        // decorative shadow into that separately styled control surface.
        const int outputTop=y+h;
        target.Restore(); glViewport(x,outputBottom,outputRight-x,outputTop-outputBottom);
        glUseProgram(programs[1]);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,textures[2]);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,textures[0]);
        glUniform2f(0,float(w),float(h));
        glUniform1f(2,s.radius*s.scale.x);
        glUniform2f(5,float(lw)/((width+1)/2),float(lh)/((height+1)/2));
        glUniform2f(6,float(x-captureX)/captureW,float(y-captureY)/captureH);
        glUniform2f(7,float(w)/captureW,float(h)/captureH);
        glUniform2f(8,float(captureW)/width,float(captureH)/height);
        glUniform4f(9,float(outputRight-x),float(outputTop-outputBottom),0.f,float(y-outputBottom));
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES,0,3);
    } else {
        // Failed glass is not CPU-emulated. Keep an opaque emergency navigation
        // surface readable this frame; the shell presents the latched error.
        target.Restore();
        GLfloat previousClear[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE,previousClear);
        glEnable(GL_SCISSOR_TEST); glScissor(x,y,w,h);
        glClearColor(s.tint.x,s.tint.y,s.tint.z,1.f); glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(previousClear[0],previousClear[1],previousClear[2],previousClear[3]);
    }
    target.Restore(true);
    if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    glUseProgram(0); glBindVertexArray(0);
    for (int i=0;i<2;++i) { glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,0); }
    glActiveTexture(GL_TEXTURE0);
    const GLenum reportedError = glGetError();
    if (reportedError != GL_NO_ERROR && error.empty()) {
        error = "OpenGL reported error " + std::to_string(reportedError) +
            " during glass navigation. Turn the mode off to use standard navigation.";
    }
    captureValid=error.empty();
    if (captureValid) {
        captureOrigin=ImVec2(float(captureX),float(captureY));
        captureSize=ImVec2(float(captureW),float(captureH));
        captureSoftSize=ImVec2(float((captureW+1)/2),float((captureH+1)/2));
        captureDisplayPos=s.displayPos; captureDisplaySize=s.displaySize; captureScale=s.scale;
        PrepareInk();
        if (glGetError()!=GL_NO_ERROR) {
            inkReady=false; captureValid=false;
            error="Glass navigation could not prepare its GPU foreground contrast. Turn the mode off to use standard navigation.";
        }
    }
    // The following ImDrawCallback_ResetRenderState reestablishes ImGui state.
}
void GlassChrome::Shutdown() {
    ReleaseTargets();
    for (auto& program : programs) { if (program) glDeleteProgram(program); program=0; }
    if (vao) glDeleteVertexArrays(1,&vao);
    glDeleteBuffers(2,inkBuffers); inkBuffers[0]=inkBuffers[1]=0;
    vao=0; surfaces.clear(); inks.clear(); captureValid=false; inkReady=false; error.clear(); enabled=false;
}
}
