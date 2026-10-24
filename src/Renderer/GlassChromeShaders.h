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
}
