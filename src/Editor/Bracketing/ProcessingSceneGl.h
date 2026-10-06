#pragma once
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <GLFW/glfw3.h>

namespace Stack::Editor::ProcessingGl {
using BlendSeparate=void(APIENTRY*)(GLenum,GLenum,GLenum,GLenum);
using BlendEquation=void(APIENTRY*)(GLenum,GLenum);
using MultisampleImage=void(APIENTRY*)(GLenum,GLsizei,GLenum,GLsizei,GLsizei,GLboolean);
inline auto BlendFunction(){return reinterpret_cast<BlendSeparate>(glfwGetProcAddress("glBlendFuncSeparate"));}
inline auto EquationFunction(){return reinterpret_cast<BlendEquation>(glfwGetProcAddress("glBlendEquationSeparate"));}
inline auto MultisampleFunction(){return reinterpret_cast<MultisampleImage>(glfwGetProcAddress("glTexImage2DMultisample"));}
constexpr GLenum TextureMultisample=0x9100;
struct Guard {
    Renderer::GLState::FramebufferState framebuffer{true};
    Renderer::GLState::PixelUnpackState unpack;
    GLint program=0,vao=0,buffer=0,active=0,texture=0,multitexture=0,sampler=0;
    GLint srcRgb=0,dstRgb=0,srcAlpha=0,dstAlpha=0,eqRgb=0,eqAlpha=0;
    GLboolean blend,depth,scissor,cull,srgb,stencil,multisample,colorMask[4];
    GLfloat clear[4];
    Guard() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(0x85B5,&vao);
        glGetIntegerv(0x8894,&buffer);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        glGetIntegerv(0x9104,&multitexture);glGetIntegerv(0x8919,&sampler);glBindSampler(0,0);
        glGetIntegerv(0x80C9,&srcRgb);glGetIntegerv(0x80C8,&dstRgb);
        glGetIntegerv(0x80CB,&srcAlpha);glGetIntegerv(0x80CA,&dstAlpha);
        glGetIntegerv(0x8009,&eqRgb);glGetIntegerv(0x883D,&eqAlpha);
        stencil=glIsEnabled(GL_STENCIL_TEST);glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
        blend=glIsEnabled(GL_BLEND);depth=glIsEnabled(GL_DEPTH_TEST);scissor=glIsEnabled(GL_SCISSOR_TEST);
        cull=glIsEnabled(GL_CULL_FACE);srgb=glIsEnabled(0x8DB9);multisample=glIsEnabled(0x809D);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glDisable(GL_STENCIL_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(0x8DB9);
        glEnable(GL_BLEND);glEnable(0x809D);
        if(auto fn=BlendFunction())fn(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        if(auto fn=EquationFunction())fn(0x8006,0x8006);
        unpack.ConfigureTightCpuUpload();
    }
    ~Guard() {
        framebuffer.Restore(true);unpack.Restore();glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glBindTexture(GL_TEXTURE_2D,texture);glBindTexture(TextureMultisample,multitexture);glBindSampler(0,sampler);
        glActiveTexture(active);glClearColor(clear[0],clear[1],clear[2],clear[3]);
        if(auto fn=BlendFunction())fn(srcRgb,dstRgb,srcAlpha,dstAlpha);
        if(auto fn=EquationFunction())fn(eqRgb,eqAlpha);
        const auto restore=[](GLenum cap,GLboolean on){if(on)glEnable(cap);else glDisable(cap);};
        restore(GL_STENCIL_TEST,stencil);glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        restore(GL_BLEND,blend);restore(GL_DEPTH_TEST,depth);restore(GL_SCISSOR_TEST,scissor);
        restore(GL_CULL_FACE,cull);restore(0x8DB9,srgb);restore(0x809D,multisample);
    }
};
inline constexpr const char* VertexShader=R"(
#version 330 core
layout(location=0) in vec4 Clip;
layout(location=1) in vec2 UV;
out vec2 uv;
void main(){gl_Position=Clip;uv=UV;}
)";
inline constexpr const char* FragmentShader=R"(
#version 330 core
in vec2 uv;out vec4 color;
uniform sampler2D Image;
uniform int Kind;
uniform vec4 Tint;
uniform vec3 WarpX,WarpY;
uniform vec4 Crop;
uniform int Fullscreen;
void main(){
    if(Fullscreen!=0){color=texture(Image,vec2(uv.x,1.-uv.y));color.a=Tint.a;return;}
    vec2 gradient=max(fwidth(uv),vec2(.00001));
    vec2 edges=min(uv,1.-uv)/gradient;
    float distance=min(edges.x,edges.y);
    float coverage=smoothstep(-.65,.65,distance);
    float border=1.-smoothstep(.8,1.8,distance);
    vec2 position=Crop.xy+uv*Crop.zw;
    vec2 sampleUv=vec2(dot(WarpX,vec3(position,1)),dot(WarpY,vec3(position,1)));
    bool inside=all(greaterThanEqual(sampleUv,vec2(0)))&&all(lessThanEqual(sampleUv,vec2(1)));
    vec4 sampleColor=texture(Image,clamp(sampleUv,0.,1.));
    float known=inside?sampleColor.a:0.;
    float diagonal=(uv.x/gradient.x+uv.y/gradient.y)/9.;
    float hatch=1.-smoothstep(.12,.24,abs(fract(diagonal)-.5));
    vec3 unknown=vec3(.035+hatch*.045);
    vec3 base=Kind<0?vec3(.025):mix(unknown,sampleColor.rgb,known);
    if(Kind==2)base=mix(vec3(.025),mix(vec3(.5),Tint.rgb,hatch),sampleColor.r*known);
    if(Kind==1||Kind==4||Kind==5)base=mix(unknown,mix(vec3(.025),Tint.rgb,sampleColor.r),known);
    if(Kind==3)base=mix(unknown,mix(vec3(.055,.07,.09),vec3(.94,.9,.8),sampleColor.r),known);
    base=mix(base,Tint.rgb,border*.65);
    color=vec4(base,coverage*Tint.a);
}
)";
}
