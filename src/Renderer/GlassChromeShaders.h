// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ThirdParty/LiquidGlass/LiquidGlassShaders.h"
namespace Stack::Renderer::GlassChromeShaders {
inline constexpr const char* Vertex = R"(#version 430 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
inline constexpr const char* Blur=Stack::ThirdParty::LiquidGlass::Blur;
inline constexpr const char* Material=Stack::ThirdParty::LiquidGlass::Material;
// Original Stack foreground shaders. One bounded compute dispatch chooses
// stable ink for all visible controls; glyph drawing never reads pixels to CPU.
inline constexpr const char* InkMeasure = R"(#version 430 core
layout(local_size_x=64) in;
layout(binding=1) uniform sampler2D backdrop;
layout(location=0) uniform int itemCount;
layout(location=1) uniform vec2 validExtent;
struct Item { vec4 region; uvec4 identity; };
layout(std430,binding=1) readonly buffer Items { Item items[]; };
layout(std430,binding=2) buffer History { uvec4 states[]; };
vec3 toLinear(vec3 value) {
    return mix(value/12.92,pow((value+.055)/1.055,vec3(2.4)),step(vec3(.04045),value));
}
void main() {
    uint index=gl_GlobalInvocationID.x;
    if(index>=uint(itemCount)) return;
    Item item=items[index];
    if(item.identity.y==0u) return; // Off-capture/clipped items keep no new evidence.
    uint slot=item.identity.z;
    vec2 halfTexel=.5/vec2(textureSize(backdrop,0));
    float luminance=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        vec2 p=clamp(item.region.xy+vec2(x,y)*item.region.zw,halfTexel,validExtent-halfTexel);
        luminance+=dot(toLinear(textureLod(backdrop,p,0.0).rgb),vec3(.2126,.7152,.0722));
    }
    luminance/=9.0;
    uint dark=luminance>.19 ? 1u : 0u;
    if(states[slot].x==item.identity.x) {
        // Hysteresis avoids rapid black/white flips as fine detail moves.
        dark=states[slot].y;
        if(luminance>.22) dark=1u;
        else if(luminance<.16) dark=0u;
    }
    states[slot]=uvec4(item.identity.x,dark,0u,0u);
})";
inline constexpr const char* InkVertex = R"(#version 430 core
layout(location=0) in vec2 Position;
layout(location=1) in vec2 UV;
layout(location=2) in vec4 Color;
layout(location=0) uniform mat4 projection;
out vec2 atlasUv; out float opacity;
void main() {
    atlasUv=UV; opacity=Color.a;
    gl_Position=projection*vec4(Position,0.0,1.0);
})";
inline constexpr const char* InkFragment = R"(#version 430 core
layout(binding=0) uniform sampler2D atlas;
layout(location=4) uniform int itemIndex;
layout(std430,binding=2) readonly buffer History { uvec4 states[]; };
in vec2 atlasUv; in float opacity;
out vec4 color;
void main() {
    // Fragment-stage SSBOs are guaranteed by OpenGL4.3; vertex SSBOs are not.
    float ink=states[itemIndex].y!=0u ? .035 : .98;
    color=vec4(vec3(ink),opacity*texture(atlas,atlasUv).a);
}
)";
}
