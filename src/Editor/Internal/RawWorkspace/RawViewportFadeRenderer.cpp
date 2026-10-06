#include "Editor/Internal/RawWorkspace/RawViewportFadeRenderer.h"
#include "Renderer/GLHelpers.h"

namespace Raw {
ViewportFadeRenderer::~ViewportFadeRenderer() { Shutdown(); }
void ViewportFadeRenderer::Shutdown() { if (m_Program) glDeleteProgram(m_Program); m_Program = 0; m_Commands.clear(); }
bool ViewportFadeRenderer::Initialize() {
    if (!m_Program) m_Program = GLHelpers::CreateShaderProgram(R"(
#version 330 core
layout(location=0) in vec2 Position;
layout(location=1) in vec2 UV;
layout(location=2) in vec4 Color;
uniform mat4 ProjMtx;
uniform bool VertexColor;
out vec2 uv;
out vec4 color;
void main() { uv=UV; color=VertexColor ? Color : vec4(1); gl_Position=ProjMtx*vec4(Position,0,1); }
)", R"(
#version 330 core
in vec2 uv;
in vec4 color;
uniform sampler2D Texture;
uniform sampler2D Previous;
uniform float Amount;
uniform int Encoded;
uniform vec2 PreviousOffset;
uniform vec2 PreviousScale;
out vec4 OutColor;
vec3 decode(vec3 x) { vec3 a=abs(x); return sign(x)*mix(a/12.92,pow((a+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),a)); }
vec3 encode(vec3 x) { vec3 a=abs(x); return sign(x)*mix(12.92*a,1.055*pow(a,vec3(1.0/2.4))-0.055,step(vec3(0.0031308),a)); }
void main() {
    vec4 a=texture(Previous,PreviousOffset+uv*PreviousScale), b=texture(Texture,uv);
    float alpha=mix(a.a,b.a,Amount);
    vec3 ar=Encoded!=0 ? decode(a.rgb) : a.rgb;
    vec3 br=Encoded!=0 ? decode(b.rgb) : b.rgb;
    vec3 rgb=alpha>0.000001 ? mix(ar*a.a,br*b.a,Amount)/alpha : vec3(0);
    OutColor=color*vec4(Encoded!=0 ? encode(rgb) : rgb,alpha);
}
)");
    return m_Program != 0;
}
void ViewportFadeRenderer::Bind(unsigned int previous, float amount, bool encoded, const float* projection, bool vertexColor,
    ImVec2 previousOffset, ImVec2 previousScale) const {
    glUseProgram(m_Program);
    glUniformMatrix4fv(glGetUniformLocation(m_Program,"ProjMtx"),1,GL_FALSE,projection);
    glUniform1i(glGetUniformLocation(m_Program,"VertexColor"),vertexColor ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_Program,"Texture"),0);
    glUniform1i(glGetUniformLocation(m_Program,"Previous"),1);
    glUniform1f(glGetUniformLocation(m_Program,"Amount"),amount);
    glUniform1i(glGetUniformLocation(m_Program,"Encoded"),encoded ? 1 : 0);
    glUniform2f(glGetUniformLocation(m_Program,"PreviousOffset"),previousOffset.x,previousOffset.y);
    glUniform2f(glGetUniformLocation(m_Program,"PreviousScale"),previousScale.x,previousScale.y);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,previous);
    glActiveTexture(GL_TEXTURE0);
}
bool ViewportFadeRenderer::Draw(ImDrawList* list, unsigned int previous, unsigned int current,
    ImVec2 minimum, ImVec2 maximum, float amount, bool encoded,
    ImVec2 uvMinimum, ImVec2 uvMaximum, ImU32 tint, float rounding, ImVec2 previousOffset, ImVec2 previousScale) {
    if (!Initialize()) return false;
    const auto* viewport = ImGui::GetWindowViewport();
    const int frame = ImGui::GetFrameCount();
    if (m_CommandFrame != frame) { m_Commands.clear(); m_CommandFrame = frame; }
    // Docked and detached views can enqueue separate draws in the same frame.
    // Keep each callback's projection and texture bindings stable until drawn.
    try { m_Commands.push_back({this, previous, amount, encoded, viewport->Pos, viewport->Size, previousOffset, previousScale}); }
    catch (...) { return false; }
    list->AddCallback([](const ImDrawList*, const ImDrawCmd* draw) {
        const auto& c = *static_cast<const Command*>(draw->UserCallbackData);
        const float l=c.displayPosition.x, r=l+c.displaySize.x;
        const float t=c.displayPosition.y, b=t+c.displaySize.y;
        const float projection[4][4] = {{2/(r-l),0,0,0},{0,2/(t-b),0,0},{0,0,-1,0},{(r+l)/(l-r),(t+b)/(b-t),0,1}};
        c.renderer->Bind(c.previous,c.amount,c.encoded,&projection[0][0],true,c.previousOffset,c.previousScale);
        // The backend's own shader may use different attribute locations.
        glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(ImDrawVert),(void*)offsetof(ImDrawVert,pos));
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(ImDrawVert),(void*)offsetof(ImDrawVert,uv));
        glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(ImDrawVert),(void*)offsetof(ImDrawVert,col));
    }, &m_Commands.back());
    list->AddImageRounded((ImTextureID)(intptr_t)current,minimum,maximum,uvMinimum,uvMaximum,tint,rounding);
    list->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    return true;
}
}
