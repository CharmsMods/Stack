#pragma once

namespace Stack::Renderer::LocalToneGuideShaders {

// Vector guided regression: He et al., Guided Image Filtering, Eqs. 18-21.
// https://people.csail.mit.edu/kaiming/publications/pami12guidedfilter.pdf
// Target-conditioned chroma, confidence support and scalar-delta fallback are
// safeguards for tone processing; this is not a general texture-transfer filter.

inline constexpr const char* ColorInitialize = R"(
#version 330 core
layout(location=0) out vec4 scalarMoments;
layout(location=1) out vec4 joint0;
layout(location=2) out vec4 joint1;
layout(location=3) out vec4 joint2;
uniform sampler2D uInput0;
uniform sampler2D uEvidenceColor;
uniform sampler2D uEvidenceConfidence;
uniform int uHasEvidence;
uniform ivec2 uSourceSize;
uniform ivec2 uAnalysisSize;
uniform vec3 uLuma;
void main() {
    ivec2 cell=ivec2(gl_FragCoord.xy);
    ivec2 first=cell*uSourceSize/uAnalysisSize;
    ivec2 end=(cell+1)*uSourceSize/uAnalysisSize;
    vec3 scalar=vec3(0.0);
    joint0=joint1=vec4(0.0);
    joint2=vec4(0.0,0.0,1.0,0.0);
    for (int y=first.y;y<end.y;++y) for (int x=first.x;x<end.x;++x) {
        vec4 color=texelFetch(uInput0,ivec2(x,y),0);
        if (any(isnan(color)) || any(isinf(color)) || !(color.a>0.0)) continue;
        float luminance=dot(color.rgb,uLuma);
        if (!(luminance>0.0) || isnan(luminance) || isinf(luminance)) continue;
        float ev=log2(luminance)-log2(0.18);
        if (isnan(ev) || isinf(ev)) continue;
        float coverage=clamp(color.a,0.0,1.0);
        scalar+=vec3(ev,ev*ev,1.0)*coverage;
        vec2 chroma;
        float reliability=1.0;
        if (uHasEvidence!=0) {
            vec3 evidence=sampleSceneColorChroma(uEvidenceColor,uEvidenceConfidence,
                (vec2(x,y)+vec2(0.5))/vec2(uSourceSize),uLuma);
            chroma=evidence.xy; reliability=evidence.z;
        } else if (!sceneColorChroma(color.rgb,uLuma,chroma)) {
            joint2.z=0.0;continue;
        }
        float w=coverage*reliability;
        joint2.z=min(joint2.z,reliability);
        float u=chroma.x,v=chroma.y;
        joint0+=w*vec4(1.0,ev,u,v);
        joint1+=w*vec4(ev*ev,ev*u,ev*v,u*u);
        joint2+=w*vec4(u*v,v*v,0.0,0.0);
    }
    scalarMoments=vec4(scalar,float(end.x-first.x)*float(end.y-first.y));
}
)";

inline constexpr const char* ColorBox = R"(
#version 330 core
layout(location=0) out vec4 field0;
layout(location=1) out vec4 field1;
layout(location=2) out vec4 field2;
layout(location=3) out vec4 field3;
uniform sampler2D uInput0;
uniform sampler2D uInput1;
uniform sampler2D uInput2;
uniform sampler2D uInput3;
uniform ivec2 uDirection;
uniform int uRadius;
// 0: four moment fields, 1: moments to two coefficient fields, 2: coefficients.
uniform int uMode;
uniform float uEpsilon;
uniform float uColorProtection;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    ivec2 size=textureSize(uInput0,0);
    int coordinate=uDirection.x!=0 ? pixel.x : pixel.y;
    int extent=uDirection.x!=0 ? size.x : size.y;
    int first=max(-uRadius,-coordinate),last=min(uRadius,extent-coordinate-1);
    vec4 m=vec4(0.0),j0=vec4(0.0),j1=vec4(0.0),j2=vec4(0.0);
    float reliableSupport=1.0;
    for (int offset=first;offset<=last;++offset) {
        ivec2 p=pixel+offset*uDirection;
        m+=texelFetch(uInput0,p,0);
        j0+=texelFetch(uInput1,p,0);
        if (uMode!=2) {
            j1+=texelFetch(uInput2,p,0);
            vec4 lastMoment=texelFetch(uInput3,p,0);
            j2+=lastMoment;
            reliableSupport=min(reliableSupport,lastMoment.z);
        }
    }
    float count=float(last-first+1);
    m/=count; j0/=count; j1/=count; j2/=count;
    if (uMode!=2) {
        // Every model that contributes at a confidence hole must abstain.
        // The extra grid cell also covers bilinear coefficient evaluation.
        // Averaging these model coefficients makes confidence transitions
        // spatially coherent instead of puncturing the final gain per pixel.
        for (int side=-1;side<=1;side+=2) {
            ivec2 p=clamp(pixel+side*(uRadius+1)*uDirection,ivec2(0),size-1);
            reliableSupport=min(reliableSupport,texelFetch(uInput3,p,0).z);
        }
        j2.z=reliableSupport;
    }
    field0=m;field1=j0;field2=j1;field3=j2;
    if (uMode!=1) return;
    field0=vec4(0.0,0.0,0.0,1.0);field1=field2=field3=vec4(0.0);
    if (!(m.z>0.0) || !(m.w>0.0)) return;
    float average=m.x/m.z;
    float variance=max(0.0,m.y/m.z-average*average);
    float a=variance/(variance+uEpsilon);
    float coverage=clamp(m.z/m.w,0.0,1.0);
    field0=vec4(a*coverage,(1.0-a)*average*coverage,coverage,1.0);
    if (!(j0.x>1e-8)) return;
    vec3 mean=j0.yzw/j0.x;
    vec4 second=j1/j0.x;
    vec2 rest=j2.xy/j0.x;
    float varE=max(0.0,second.x-mean.x*mean.x);
    float varU=max(0.0,second.w-mean.y*mean.y);
    float varV=max(0.0,rest.y-mean.z*mean.z);
    vec2 cov=second.yz-mean.x*mean.yz;
    cov=clamp(cov,-sqrt(varE*vec2(varU,varV)),sqrt(varE*vec2(varU,varV)));
    float uv=clamp(rest.x-mean.y*mean.z,-sqrt(varU*varV),sqrt(varU*varV));
    // Schur complement of the regularized 3x3 guided-filter covariance. A
    // 0.04 opponent-coordinate floor suppresses weak chromatic noise. It is
    // independent of exposure; no log-chroma division near black is needed.
    const float chromaEpsilon=0.0016;
    float uu=varU+chromaEpsilon,vv=varV+chromaEpsilon;
    float determinant=uu*vv-uv*uv;
    if (!(determinant>0.0) || isnan(determinant) || isinf(determinant)) return;
    vec2 beta=vec2(vv*cov.x-uv*cov.y,uu*cov.y-uv*cov.x)/determinant;
    float explained=max(0.0,dot(cov,beta));
    // Consistent positive weights make the covariance PSD analytically. Guard
    // cancellation in finite-precision second moments without amplifying it.
    if (explained>varE && explained>0.0) {beta*=varE/explained;explained=varE;}
    float residual=max(0.0,varE-explained);
    float jointA=residual/(residual+uEpsilon);
    vec2 jointC=(1.0-jointA)*beta;
    float jointB=(1.0-jointA)*mean.x-dot(jointC,mean.yz);
    float reliableA=varE/(varE+uEpsilon);
    float reliableB=(1.0-reliableA)*mean.x;
    vec4 delta=vec4(jointA-reliableA,jointC,jointB-reliableB);
    if (any(isnan(delta)) || any(isinf(delta))) return;
    // Only the color's explanatory contribution is added to the all-pixel
    // scalar fit. Uneven reliability cannot substitute a different regional
    // brightness average. Zero covariance gives exactly zero color correction.
    float support=clamp(min(j0.x/m.z,reliableSupport),0.0,1.0);
    field1=delta*(coverage*support*uColorProtection);
}
)";

} // namespace Stack::Renderer::LocalToneGuideShaders
