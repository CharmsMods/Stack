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
    backdrops.clear(); objects.clear(); inks.clear();
    activeBackdrop=nullptr; captureValid=false; inkReady=false;
    if (!requested && enabled) { Shutdown(); }
    enabled = requested;
}
bool GlassChrome::AddBackdrop(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 tint, const LightingFrame* lighting) {
    if (!enabled || !error.empty() || !draw || max.x <= min.x || max.y <= min.y ||
        ImGui::GetWindowViewport()!=ImGui::GetMainViewport()) return false;
    auto* viewport = ImGui::GetMainViewport();
    if (lighting && (!std::isfinite(lighting->min.x) || !std::isfinite(lighting->min.y) ||
        !std::isfinite(lighting->max.x) || !std::isfinite(lighting->max.y) ||
        lighting->max.x<=lighting->min.x || lighting->max.y<=lighting->min.y ||
        lighting->min.x<viewport->Pos.x || lighting->min.y<viewport->Pos.y ||
        lighting->max.x>viewport->Pos.x+viewport->Size.x ||
        lighting->max.y>viewport->Pos.y+viewport->Size.y)) return false;
    const ImVec2 clipMin = draw->GetClipRectMin(), clipMax = draw->GetClipRectMax();
    backdrops.push_back({this,min,max,viewport->Pos,viewport->Size,ImGui::GetIO().DisplayFramebufferScale,
        tint,ImVec4(clipMin.x,clipMin.y,clipMax.x,clipMax.y),viewport->DpiScale});
    auto& backdrop=backdrops.back();
    BuildLayout(backdrop);
    backdrop.lightOrigin=ImVec2(float(backdrop.layout.x),float(backdrop.layout.y));
    backdrop.lightSize=ImVec2(float(backdrop.layout.width),float(backdrop.layout.height));
    if (lighting) {
        backdrop.lightOrigin=ImVec2((lighting->min.x-viewport->Pos.x)*backdrop.scale.x,
            backdrop.layout.framebufferHeight-(lighting->max.y-viewport->Pos.y)*backdrop.scale.y);
        backdrop.lightSize=ImVec2((lighting->max.x-lighting->min.x)*backdrop.scale.x,
            (lighting->max.y-lighting->min.y)*backdrop.scale.y);
    }
    draw->AddCallback(BackdropCallback,&backdrops.back());
    draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    return true;
}
GlassChrome::MaterialProfile GlassChrome::ResolveMaterial(MaterialRole role) {
    MaterialProfile material;
    if (role==MaterialRole::NavigationControl || role==MaterialRole::SliderThumb) {
        material.studioWindow=ImVec4(.5735764364f,.8191520443f,.8191520443f,.9781476007f);
    }
    if (role==MaterialRole::NavigationControl) material.studioStrength=.65f;
    if (role==MaterialRole::SliderThumb) material.flatCapFraction=.25f;
    return material;
}
bool GlassChrome::AddObject(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 tint, float radius, float elevation, MaterialRole role) {
    if (!enabled || !error.empty() || !draw || backdrops.empty() || max.x <= min.x || max.y <= min.y ||
        ImGui::GetWindowViewport()!=ImGui::GetMainViewport()) return false;
    const Backdrop& backdrop=backdrops.back();
    const ImVec2 clipMin = draw->GetClipRectMin(), clipMax = draw->GetClipRectMax();
    const MaterialProfile material=ResolveMaterial(role);
    ObjectLayout layout;
    // This same immutable layout is drawn later. Hidden project rows allocate
    // no callback/reset, while a just-offclip object's visible shadow survives.
    if (!BuildLayout(backdrop,min,max,ImVec4(clipMin.x,clipMin.y,clipMax.x,clipMax.y),radius,elevation,material,layout)) return false;
    objects.push_back({this,&backdrop,tint,layout,material});
    draw->AddCallback(ObjectCallback,&objects.back());
    draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    return true;
}
void GlassChrome::BuildLayout(Backdrop& s) {
    auto& layout=s.layout;
    layout.framebufferWidth=int(s.displaySize.x*s.scale.x);
    layout.framebufferHeight=int(s.displaySize.y*s.scale.y);
    const int fw=layout.framebufferWidth, fh=layout.framebufferHeight;
    if (fw<=0 || fh<=0) return;
    const float left=std::max({s.min.x,s.clip.x,s.displayPos.x});
    const float right=std::min({s.max.x,s.clip.z,s.displayPos.x+s.displaySize.x});
    const float top=std::max({s.min.y,s.clip.y,s.displayPos.y});
    const float bottom=std::min({s.max.y,s.clip.w,s.displayPos.y+s.displaySize.y});
    if (right<=left || bottom<=top) return;
    const int x=std::clamp(int(std::floor((left-s.displayPos.x)*s.scale.x)),0,fw);
    const int y=std::clamp(int(std::floor(fh-(bottom-s.displayPos.y)*s.scale.y)),0,fh);
    const int w=std::clamp(int(std::ceil((right-s.displayPos.x)*s.scale.x)),0,fw)-x;
    const int h=std::clamp(int(std::ceil(fh-(top-s.displayPos.y)*s.scale.y)),0,fh)-y;
    if (w<=0 || h<=0) return;
    // Sampling-only neighborhood: no part of this padded crop paints chrome.
    const int padding=int(std::ceil(16.f*std::max(.5f,std::max(s.scale.x,s.scale.y)*s.dpi)));
    layout.x=std::max(0,x-padding); layout.y=std::max(0,y-padding);
    layout.width=std::min(fw,x+w+padding)-layout.x;
    layout.height=std::min(fh,y+h+padding)-layout.y;
    layout.reserveWidth=std::max(layout.width,std::min(fw,int(std::ceil((s.max.x-s.min.x)*s.scale.x))+2*padding));
    layout.reserveHeight=std::max(layout.height,std::min(fh,int(std::ceil((s.max.y-s.min.y)*s.scale.y))+2*padding));
}
bool GlassChrome::BuildLayout(const Backdrop& s,ImVec2 min,ImVec2 max,ImVec4 clip,
    float radius,float elevation,const MaterialProfile& material,ObjectLayout& layout) {
    const auto& capture=s.layout;
    if (s.scale.x<=0.f || s.scale.y<=0.f || capture.width<=0 || capture.height<=0) return false;
    // Full unclipped geometry defines the lens, even during column reveal.
    const float x=(min.x-s.displayPos.x)*s.scale.x;
    const float y=capture.framebufferHeight-(max.y-s.displayPos.y)*s.scale.y;
    const float w=(max.x-min.x)*s.scale.x, h=(max.y-min.y)*s.scale.y;
    layout.origin=ImVec2(x,y); layout.size=ImVec2(w,h);
    layout.radius=std::min(std::max(0.f,radius)*std::min(s.scale.x,s.scale.y),.5f*std::min(w,h));
    // Pressing lowers the rigid optical volume without resizing/deforming it.
    // The same finite gap drives both optical shading and shadow projection.
    const float lift=std::isfinite(elevation) ? std::clamp(elevation,0.f,1.f) : 1.f;
    layout.volume=ImVec2(std::max(.25f,material.halfDepthRatio*std::min(w,h)),material.airGapRatio*std::min(w,h)*lift);
    const float centerZ=layout.volume.x+layout.volume.y;
    const ImVec2 objectCenter(x-s.lightOrigin.x+.5f*w,y-s.lightOrigin.y+.5f*h);
    // A broad near-overhead studio fill approximates underside occlusion.
    // It is separate from the grazing reflection strip, not a traced caustic.
    const float fillHeight=8.f*std::max({s.lightSize.y,64.f,centerZ});
    const ImVec2 fillCenter(.5f*s.lightSize.x-.08f*fillHeight,.5f*s.lightSize.y+.45f*fillHeight);
    const float ratio=centerZ/(fillHeight-centerZ);
    layout.shadow=ImVec4((objectCenter.x-fillCenter.x)*ratio,(objectCenter.y-fillCenter.y)*ratio,
        1.f+ratio,std::max(.75f,.25f*fillHeight*ratio*.57735026919f));
    // The shader's signed penumbra has zero support beyond three sigma.
    // Include projection expansion and displacement independently per axis;
    // a radius-only apron is not sufficient for arbitrary object proportions.
    const float expansion=layout.shadow.z-1.f; // Use the exact scale sent to GLSL.
    layout.apron=ImVec2(std::ceil(std::max(.5f,std::abs(layout.shadow.x)+.5f*w*expansion+3.f*layout.shadow.w)),
        std::ceil(std::max(.5f,std::abs(layout.shadow.y)+.5f*h*expansion+3.f*layout.shadow.w)));
    const float left=std::max({s.min.x,s.clip.x,clip.x,s.displayPos.x});
    const float right=std::min({s.max.x,s.clip.z,clip.z,s.displayPos.x+s.displaySize.x});
    const float top=std::max({s.min.y,s.clip.y,clip.y,s.displayPos.y});
    const float bottom=std::min({s.max.y,s.clip.w,clip.w,s.displayPos.y+s.displaySize.y});
    if (right<=left || bottom<=top) return false;
    layout.left=std::max(int(std::floor(x-layout.apron.x)),int(std::ceil((left-s.displayPos.x)*s.scale.x)));
    const int outputRight=std::min(int(std::ceil(x+w+layout.apron.x)),int(std::floor((right-s.displayPos.x)*s.scale.x)));
    layout.bottom=std::max(int(std::floor(y-layout.apron.y)),int(std::ceil(capture.framebufferHeight-(bottom-s.displayPos.y)*s.scale.y)));
    const int outputTop=std::min(int(std::ceil(y+h+layout.apron.y)),int(std::floor(capture.framebufferHeight-(top-s.displayPos.y)*s.scale.y)));
    layout.width=outputRight-layout.left; layout.height=outputTop-layout.bottom;
    return layout.width>0 && layout.height>0;
}
void GlassChrome::BackdropCallback(const ImDrawList*, const ImDrawCmd* command) {
    const auto* backdrop = static_cast<const Backdrop*>(command->UserCallbackData);
    if (backdrop) backdrop->owner->CaptureBackdrop(*backdrop);
}
void GlassChrome::ObjectCallback(const ImDrawList*, const ImDrawCmd* command) {
    const auto* object = static_cast<const Object*>(command->UserCallbackData);
    if (object) object->owner->DrawObject(*object);
}
bool GlassChrome::BeginInk(ImDrawList* draw, ImVec2 min, ImVec2 max, ImGuiID id) {
    if (!enabled || !error.empty() || !draw || backdrops.empty() || max.x<=min.x || max.y<=min.y ||
        ImGui::GetWindowViewport()!=ImGui::GetMainViewport()) return false;
    const ImVec2 clipMin=draw->GetClipRectMin(), clipMax=draw->GetClipRectMax();
    // Offscreen project controls do not consume foreground slots. Identity
    // guards reset recycled slots without shifting other visible controls.
    if (max.x<=clipMin.x || min.x>=clipMax.x || max.y<=clipMin.y || min.y>=clipMax.y) return false;
    if (inks.size()>=MaxInkItems) {
        error="Glass navigation exceeded its foreground GPU budget. Turn the mode off to use standard navigation.";
        return false;
    }
    inks.push_back({this,&backdrops.back(),min,max,id,MaxInkItems,
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
    // Draw order supplies a fresh matching capture first. A hidden column or
    // failed capture must not reuse another column's or last frame's pixels.
    if (!captureValid || activeBackdrop!=ink.backdrop || !inkReady || !ink.usable ||
        !error.empty() || !programs[2]) return;
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
    const float framebufferH=float(int(captureDisplaySize.y*captureScale.y));
    const float uScale=captureSoftSize.x/(captureSize.x*softW);
    const float vScale=captureSoftSize.y/(captureSize.y*softH);
    std::array<bool,MaxInkItems> reserved{};
    ++inkEpoch;
    // Reserve every surviving current identity before assigning new ones. A
    // newly exposed project must not evict the later-registered panel toggle.
    for (Ink& ink : inks) {
        ink.slot=MaxInkItems;
        const ImVec2 min(std::max(ink.min.x,ink.clip.x),std::max(ink.min.y,ink.clip.y));
        const ImVec2 max(std::min(ink.max.x,ink.clip.z),std::min(ink.max.y,ink.clip.w));
        const float leftPx=(min.x-captureDisplayPos.x)*captureScale.x;
        const float rightPx=(max.x-captureDisplayPos.x)*captureScale.x;
        const float topPx=framebufferH-(min.y-captureDisplayPos.y)*captureScale.y;
        const float bottomPx=framebufferH-(max.y-captureDisplayPos.y)*captureScale.y;
        ink.usable=ink.backdrop==activeBackdrop && max.x>min.x && max.y>min.y && leftPx>=captureOrigin.x &&
            rightPx<=captureOrigin.x+captureSize.x && bottomPx>=captureOrigin.y &&
            topPx<=captureOrigin.y+captureSize.y;
        if (!ink.usable) continue;
        for (unsigned int slot=0;slot<MaxInkItems;++slot) if (inkIdentity[slot]==ink.id) {
            ink.slot=slot; reserved[slot]=true; break;
        }
    }
    std::array<bool,MaxInkItems> measured{};
    unsigned int itemIndex=0;
    for (Ink& ink : inks) {
        Item& item=items[itemIndex++];
        if (!ink.usable) continue;
        if (ink.slot==MaxInkItems) {
            // A duplicate identity, if supplied, shares its first assignment.
            for (unsigned int slot=0;slot<MaxInkItems;++slot) if (inkIdentity[slot]==ink.id) {
                ink.slot=slot; break;
            }
            if (ink.slot==MaxInkItems) {
                unsigned int oldest=MaxInkItems;
                for (unsigned int slot=0;slot<MaxInkItems;++slot)
                    if (!reserved[slot] && (oldest==MaxInkItems || inkLastUsed[slot]<inkLastUsed[oldest])) oldest=slot;
                if (oldest==MaxInkItems) { ink.usable=false; continue; }
                ink.slot=oldest; inkIdentity[oldest]=ink.id;
            }
            reserved[ink.slot]=true;
        }
        inkLastUsed[ink.slot]=inkEpoch;
        if (measured[ink.slot]) continue; // Exactly one GPU writer per slot.
        measured[ink.slot]=true;
        const ImVec2 min(std::max(ink.min.x,ink.clip.x),std::max(ink.min.y,ink.clip.y));
        const ImVec2 max(std::min(ink.max.x,ink.clip.z),std::min(ink.max.y,ink.clip.w));
        const float centerX=((min.x+max.x)*.5f-captureDisplayPos.x)*captureScale.x;
        const float centerY=framebufferH-((min.y+max.y)*.5f-captureDisplayPos.y)*captureScale.y;
        item={{(centerX-captureOrigin.x)*uScale,(centerY-captureOrigin.y)*vScale,
            (max.x-min.x)*captureScale.x*.3f*uScale,
            (max.y-min.y)*captureScale.y*.3f*vScale},{ink.id,1,ink.slot,0}};
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
    activeBackdrop=nullptr; captureValid=false; inkReady=false;
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
void GlassChrome::CaptureBackdrop(const Backdrop& s) {
    activeBackdrop=nullptr; captureValid=false; inkReady=false;
    if (!error.empty() || s.layout.width<=0 || s.layout.height<=0) return;
    const auto& layout=s.layout;
    const int captureX=layout.x, captureY=layout.y;
    const int captureW=layout.width, captureH=layout.height;
    const GLState::FramebufferState target(true);
    const bool srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB) != GL_FALSE;
    glDisable(GL_FRAMEBUFFER_SRGB);
    glActiveTexture(GL_TEXTURE0); glBindSampler(0,0); glBindSampler(1,0);
    if (Ensure(layout.reserveWidth,layout.reserveHeight)) {
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
        if (glGetError()==GL_NO_ERROR) {
            activeBackdrop=&s; captureValid=true;
            captureOrigin=ImVec2(float(captureX),float(captureY));
            captureSize=ImVec2(float(captureW),float(captureH));
            captureSoftSize=ImVec2(float(lw),float(lh));
            captureDisplayPos=s.displayPos; captureDisplaySize=s.displaySize; captureScale=s.scale;
            PrepareInk();
            if (glGetError()!=GL_NO_ERROR) {
                inkReady=false; captureValid=false; activeBackdrop=nullptr;
                error="Glass navigation could not prepare its GPU foreground contrast. Turn the mode off to use standard navigation.";
            }
        } else {
            error="Glass navigation could not capture its GPU backdrop. Turn the mode off to use standard navigation.";
        }
    }
    // Even allocation failure must leave this invisible column untouched. The
    // shell displays the latched GPU failure; there is no CPU material fallback.
    target.Restore(true);
    if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    glUseProgram(0); glBindVertexArray(0);
    for (int i=0;i<2;++i) { glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,0); }
    glActiveTexture(GL_TEXTURE0);
    // The following ImDrawCallback_ResetRenderState reestablishes ImGui state.
}
void GlassChrome::DrawObject(const Object& object) {
    if (!captureValid || activeBackdrop!=object.backdrop || !error.empty()) return;
    const auto& layout=object.layout;
    const float x=layout.origin.x, y=layout.origin.y;
    const float w=layout.size.x, h=layout.size.y;
    // No framebuffer switch or copy per object. ImGui's queued reset restores
    // the full viewport after this local draw, avoiding per-button GL queries.
    const bool srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB)!=GL_FALSE;
    glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glViewport(layout.left,layout.bottom,layout.width,layout.height);
    glUseProgram(programs[1]); glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0); glBindSampler(0,0); glBindTexture(GL_TEXTURE_2D,textures[2]);
    glActiveTexture(GL_TEXTURE1); glBindSampler(1,0); glBindTexture(GL_TEXTURE_2D,textures[0]);
    glUniform2f(0,w,h);
    glUniform1f(2,layout.radius);
    glUniform4f(3,object.tint.x,object.tint.y,object.tint.z,object.tint.w);
    glUniform2f(4,layout.volume.x,layout.volume.y);
    glUniform2f(5,captureSoftSize.x/((width+1)/2),captureSoftSize.y/((height+1)/2));
    glUniform2f(6,(x-captureOrigin.x)/captureSize.x,(y-captureOrigin.y)/captureSize.y);
    glUniform2f(7,w/captureSize.x,h/captureSize.y);
    glUniform2f(8,captureSize.x/width,captureSize.y/height);
    glUniform4f(9,float(layout.width),float(layout.height),x-layout.left,y-layout.bottom);
    glUniform4f(10,layout.shadow.x,layout.shadow.y,layout.shadow.z,layout.shadow.w);
    const auto& material=object.material;
    glUniform1f(11,material.studioStrength);
    const auto& window=material.studioWindow;
    glUniform4f(12,window.x,window.y,window.z,window.w);
    glUniform1f(13,material.flatCapFraction);
    const auto& backdrop=*object.backdrop;
    glUniform4f(14,captureOrigin.x-backdrop.lightOrigin.x,captureOrigin.y-backdrop.lightOrigin.y,
        backdrop.lightSize.x,backdrop.lightSize.y);
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES,0,3);
    if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    glUseProgram(0); glBindVertexArray(0);
    for (int i=0;i<2;++i) { glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,0); }
    glActiveTexture(GL_TEXTURE0);
    const GLenum reportedError=glGetError();
    if (reportedError!=GL_NO_ERROR) {
        captureValid=false; inkReady=false; activeBackdrop=nullptr;
        error="OpenGL reported error " + std::to_string(reportedError) +
            " during glass navigation. Turn the mode off to use standard navigation.";
    }
    // The following ImDrawCallback_ResetRenderState reestablishes ImGui state.
}
void GlassChrome::Shutdown() {
    ReleaseTargets();
    for (auto& program : programs) { if (program) glDeleteProgram(program); program=0; }
    if (vao) glDeleteVertexArrays(1,&vao);
    glDeleteBuffers(2,inkBuffers); inkBuffers[0]=inkBuffers[1]=0;
    inkIdentity.fill(0); inkLastUsed.fill(0); inkEpoch=0;
    vao=0; backdrops.clear(); objects.clear(); inks.clear(); activeBackdrop=nullptr;
    captureValid=false; inkReady=false; error.clear(); enabled=false;
}
}
