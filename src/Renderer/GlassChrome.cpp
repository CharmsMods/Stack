// SPDX-License-Identifier: GPL-3.0-only
#include "GlassChrome.h"
#include "GlassChromeShaders.h"
#include "GLHelpers.h"
#include "GLStateGuards.h"
#include <algorithm>
#include <cmath>

namespace Stack::Renderer {
void GlassChrome::BeginFrame(bool requested) {
    surfaces.clear();
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
void GlassChrome::ReleaseTargets() {
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
        glGenVertexArrays(1,&vao);
        if (!programs[0] || !programs[1] || !vao) {
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
    // the material. Only the final rail rectangle is written to the destination.
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
        target.Restore(); glViewport(x,y,w,h);
        glUseProgram(programs[1]);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,textures[2]);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,textures[0]);
        glUniform2f(0,float(w),float(h));
        glUniform4f(1,s.tint.x,s.tint.y,s.tint.z,s.tint.w);
        glUniform1f(2,s.radius*s.scale.x); glUniform1f(3,std::max(.5f,s.scale.x*s.dpi));
        glUniform2f(4,float(captureW)/width,float(captureH)/height);
        glUniform2f(5,float(lw)/((width+1)/2),float(lh)/((height+1)/2));
        glUniform2f(6,float(x-captureX)/captureW,float(y-captureY)/captureH);
        glUniform2f(7,float(w)/captureW,float(h)/captureH);
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
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
    // The following ImDrawCallback_ResetRenderState reestablishes ImGui state.
}
void GlassChrome::Shutdown() {
    ReleaseTargets();
    for (auto& program : programs) { if (program) glDeleteProgram(program); program=0; }
    if (vao) glDeleteVertexArrays(1,&vao);
    vao=0; surfaces.clear(); error.clear(); enabled=false;
}
}
