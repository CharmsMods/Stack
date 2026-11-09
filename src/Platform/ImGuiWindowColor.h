#pragma once
#include "Hdr/UiTarget.h"
#include "Hdr/PhotoPairs.h"
#include "Hdr/GainMapPreview.h"

namespace Stack::Platform {
// Same attribute locations, atlas binding and alpha convention as the
// pinned OpenGL3 backend. Decode vertex color before interpolation in HDR.
inline const std::string UiVertexShader=std::string(R"(
layout(location=0) in vec2 Position;
layout(location=1) in vec2 UV;
layout(location=2) in vec4 Color;
uniform mat4 ProjMtx;
uniform bool StackLinearTarget;
out vec2 Frag_UV;
out vec4 Frag_Color;
)") + Hdr::Ui::DecodeShader + R"(
void main() {
    Frag_UV=UV;
    Frag_Color=StackLinearTarget ? vec4(stackUiLinear(Color.rgb),Color.a) : Color;
    gl_Position=ProjMtx*vec4(Position,0,1);
})";
inline const std::string UiFragmentShader=std::string(R"(
in vec2 Frag_UV;
in vec4 Frag_Color;
uniform sampler2D Texture;
uniform bool StackLinearTarget;
uniform sampler2D StackPhotoBase;
uniform bool StackHasPhotoBase;
uniform float StackPhotoHeadroom;
layout(location=0) out vec4 Out_Color;
)") + Hdr::Ui::DecodeShader + Hdr::GainMapPreviewShader + R"(
void main() {
    vec4 texel=texture(Texture,Frag_UV);
    if(StackLinearTarget) {
        texel.rgb=stackUiLinear(texel.rgb);
        if(StackPhotoHeadroom<1000.0/203.0) {
            if(StackHasPhotoBase)
                texel.rgb=stackGainMapPreview(stackUiLinear(texture(StackPhotoBase,Frag_UV).rgb),texel.rgb,StackPhotoHeadroom);
            else if(max(texel.r,max(texel.g,texel.b))>1.0) {
                texel.rgb=stackUnpairedHdrPreview(texel.rgb,StackPhotoHeadroom);
            }
        }
    }
    Out_Color=Frag_Color*texel;
})";
}
