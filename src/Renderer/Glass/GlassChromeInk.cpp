// SPDX-License-Identifier: GPL-3.0-only
#include "Renderer/GlassChrome.h"
#include "Renderer/GLLoader.h"
#include <algorithm>
namespace Stack::Renderer {
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
    const float softW=float(width), softH=float(height);
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
    glActiveTexture(GL_TEXTURE1); glBindSampler(1,0); glBindTexture(GL_TEXTURE_2D,sourceTexture);
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

}
