#pragma once
namespace Stack::Renderer::SceneDetailShaders {
inline constexpr const char* Vertex=R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTexCoord;
out vec2 uv;
void main(){uv=aTexCoord;gl_Position=vec4(aPos,0,1);}
)";
inline constexpr const char* Initialize=R"(
#version 330 core
in vec2 uv;
layout(location=0) out vec2 field;
uniform sampler2D uOriginal;
uniform vec3 uLuma;
void main(){vec4 c=texelFetch(uOriginal,ivec2(gl_FragCoord.xy),0);float y=dot(c.rgb,uLuma);field=vec2(log2(max(y,1e-12)/.18),0);}
)";
inline constexpr const char* Filter=R"(
#version 330 core
in vec2 uv;
layout(location=0) out vec2 field;
uniform sampler2D uOriginal;
uniform sampler2D uField;
uniform sampler2D uPrevious;
uniform sampler2D uGains;
uniform vec3 uLuma;
uniform vec2 uStep;
uniform float uEdge;
uniform int uVertical;
uniform int uDetail;
uniform int uBand;
uniform vec3 uTarget;
uniform int uTargetEnabled;
float ev(vec3 rgb){return log2(max(dot(rgb,uLuma),1e-12)/.18);}
// Normalize interpolation over valid signal pixels. A nonpositive pixel's
// log placeholder must never leak into a neighboring positive guide sample.
float sampleBase(vec2 at){
    vec2 position=at*vec2(textureSize(uField,0))-.5;
    ivec2 start=ivec2(floor(position));vec2 fraction=fract(position);
    ivec2 limit=textureSize(uField,0)-1;
    float sum=0,weights=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        ivec2 pixel=clamp(start+ivec2(x,y),ivec2(0),limit);
        vec4 source=texelFetch(uOriginal,pixel,0);
        if(source.a<=0 || dot(source.rgb,uLuma)<=0)continue;
        float weight=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);
        sum+=texelFetch(uField,pixel,0).r*weight;weights+=weight;
    }
    return weights>1e-12?sum/weights:texture(uField,uv).r;
}
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 original=texelFetch(uOriginal,pixel,0);
    vec2 prior=texelFetch(uPrevious,pixel,0).rg;
    if(original.a<=0 || dot(original.rgb,uLuma)<=0){field=prior;return;}
    float center=ev(original.rgb),total=0,weightSum=0;
    float sigma=mix(4.0,.25,uEdge);
    for(int i=-2;i<=2;++i){
        vec2 at=clamp(uv+float(i)*uStep,vec2(0),vec2(1));
        vec4 sampleColor=texture(uOriginal,at);
        if(sampleColor.a<=0 || dot(sampleColor.rgb,uLuma)<=0) continue;
        float difference=ev(sampleColor.rgb)-center;
        float spatial=i==0?6.0:(abs(i)==1?4.0:1.0);
        float rangeWeight=mix(1.0,exp(-.5*difference*difference/(sigma*sigma)),uEdge);
        float weight=spatial*sampleColor.a*rangeWeight;
        total+=sampleBase(at)*weight;weightSum+=weight;
    }
    float base=weightSum>1e-12?total/weightSum:prior.r;
    float accumulated=prior.g;
    if(uVertical!=0 && uDetail!=0){
        float row=clamp((center+12.0)/24.0,0,1)*16.0;
        float gain=clamp(texture(uGains,vec2((float(uBand)+.5)/8.0,(row+.5)/17.0)).r,0.0,3.0);
        if(uTargetEnabled!=0){
            float weight=1-smoothstep(uTarget.y,uTarget.y+uTarget.z,abs(center-uTarget.x));
            gain=1+(gain-1)*weight;
        }
        accumulated+=(gain-1)*(prior.r-base);
    }
    field=vec2(base,accumulated);
}
)";
inline constexpr const char* Apply=R"(
#version 330 core
in vec2 uv;
layout(location=0) out vec4 color;
uniform sampler2D uOriginal;
uniform sampler2D uField;
uniform vec3 uLuma;
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    color=texelFetch(uOriginal,pixel,0);
    if(color.a>0 && dot(color.rgb,uLuma)>0) color.rgb*=exp2(texelFetch(uField,pixel,0).g);
}
)";
}
