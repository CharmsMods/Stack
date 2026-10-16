#pragma once

namespace Stack::Renderer::FilmGrain::Shaders {
inline constexpr const char* Vertex=R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTexCoord;
out vec2 uv;
void main(){uv=aTexCoord;gl_Position=vec4(aPos,0,1);}
)";
// Independently implemented marked Boolean-Poisson model, motivated by
// Newson/Faraj/Galerne/Delon, IPOL 2017, doi:10.5201/ipol.2017.192.
// Coverage is homogeneous; the separate response pass supplies tone dependence.
inline constexpr const char* Field=R"(
#version 330 core
in vec2 uv;
out vec4 field;
uniform vec2 nativeSize, footprint;
uniform vec2 sampleOrigin, sampleSpan;
uniform float radius,variation,maximumFactor,coverage,opticalBlur,secondMoment;
uniform vec3 channelSize;
uniform int seed,frameIndex;
uniform int samples;
uniform bool scalarField;
uint hash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float random(uint x){return (float(hash(x)>>8)+.5)/16777216.;}
uint address(ivec2 cell,uint stream){return hash(uint(cell.x)^hash(uint(cell.y)+0x9e3779b9u)^uint(seed)^hash(uint(frameIndex))^hash(stream));}
float normal(uint key){return sqrt(-2.*log(random(key)))*cos(6.28318530718*random(key+1u));}
bool occupied(vec2 point,float size,uint stream){
    float mean=radius*size;
    float maximum=variation<.000001 ? mean : mean*maximumFactor;
    float moment=secondMoment*size*size;
    float lambda=-log(1.-coverage)*maximum*maximum/(3.14159265359*moment);
    ivec2 cell=ivec2(floor(point/maximum));
    float sigma=sqrt(log(1.+variation*variation));
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        ivec2 c=cell+ivec2(x,y);uint key=address(c,stream);
        float target=random(key),probability=exp(-lambda),cdf=probability;
        int count=0;
        // Authored parameter bounds keep lambda below 12. The tail beyond
        // 64 particles is negligible; the bound keeps execution finite.
        for(int k=1;k<=64;++k){if(target<=cdf)break;count=k;probability*=lambda/float(k);cdf+=probability;}
        for(int k=0;k<count;++k){
            uint mark=hash(key+uint(k)*0x68bc21ebu+17u);
            vec2 center=(vec2(c)+vec2(random(mark),random(mark+1u)))*maximum;
            float r=min(maximum,mean*exp(sigma*normal(mark+2u)-.5*sigma*sigma));
            vec2 d=point-center;
            if(dot(d,d)<r*r)return true;
        }
    }
    return false;
}
float integrate(vec2 point,float size,uint stream){
    float sum=0.;
    for(int i=0;i<64;++i){
        if(i>=samples)break;
        // Fixed stratified offsets produce a deterministic optical/pixel
        // integral without a screen-dependent dither or animated wall clock.
        float index=float(i)+.5;
        vec2 box=vec2(fract(index*.754877666),fract(index*.569840296))-.5;
        float u=max(.000001,fract(index*.438579));
        float angle=6.28318530718*fract(index*.328173);
        vec2 gaussian=sqrt(-2.*log(u))*vec2(cos(angle),sin(angle))*opticalBlur;
        sum+=occupied(point+box*footprint+gaussian,size,stream) ? 1. : 0.;
    }
    float value=(sum/float(samples)-coverage)/sqrt(coverage*(1.-coverage));
    // Finite integration has a 1/N variance floor at coarse preview scales.
    // An area-law covariance approximation suppresses that numerical floor.
    // This is not an exact equality to downsampling a native simulation.
    float filterArea=footprint.x*footprint.y+12.*opticalBlur*opticalBlur;
    float correlationArea=4.*3.14159265359*secondMoment*size*size;
    float fraction=correlationArea/(filterArea+correlationArea);
    return value*sqrt(fraction/max(fraction+(1.-fraction)/float(samples),.000001));
}
void main(){
    vec2 point=sampleOrigin+uv*sampleSpan;
    field=vec4(integrate(point,1.,1u),0,0,0);
    if(!scalarField)field.yzw=vec3(integrate(point,channelSize.r,2u),
        integrate(point,channelSize.g,3u),integrate(point,channelSize.b,4u));
}
)";
inline constexpr const char* Composite=R"(
#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D image,grain;
uniform sampler2D responseCurve;
uniform int responseSegments;
uniform bool responseActive;
uniform float deviation,colorAmount,correlation,shadows,midtones,highlights,pivot,exposureFloor,amount;
uniform vec3 channelStrength,components,luma;
uniform vec4 fieldVariance;
uniform int encoding,outputMode;
uniform bool scalar,premultiplied,compensateMean;
vec3 decode(vec3 x){return mix(x/12.92,pow(max((x+.055)/1.055,vec3(0)),vec3(2.4)),step(vec3(.04045),x));}
vec3 encode(vec3 x){return mix(x*12.92,1.055*pow(max(x,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),x));}
vec4 curveTexel(int index){int width=textureSize(responseCurve,0).x;return texelFetch(responseCurve,ivec2(index%width,index/width),0);}
float cubic(float a,float b,float c,float d,float t){float u=1.-t;return u*u*u*a+3.*u*u*t*b+3.*u*t*t*c+t*t*t*d;}
float curveResponse(float x){
    if(!responseActive)return 1.;
    int lo=0,hi=responseSegments-1;
    for(int k=0;k<31;++k){if(lo>=hi)break;int mid=lo+(hi-lo)/2;
        if(x>curveTexel(mid*2).z)lo=mid+1;else hi=mid;}
    vec4 p=curveTexel(lo*2),h=curveTexel(lo*2+1);
    if(x<=p.x)return p.y*3.;if(x>=p.z)return p.w*3.;
    float a=0.,b=1.;
    for(int k=0;k<18;++k){float m=(a+b)*.5;
        if(cubic(p.x,h.x,h.z,p.z,m)<x)a=m;else b=m;}
    float span=p.z-p.x;
    bool linearX=abs(h.x-(p.x+span/3.))<.000001 && abs(h.z-(p.z-span/3.))<.000001;
    float t=linearX ? (x-p.x)/span : (a+b)*.5;
    return 3.*clamp(cubic(p.y,h.y,h.w,p.w,t),0.,1.);
}
float response(float t){
    t=clamp(t,0.,1.);
    float zones=t<pivot ? mix(shadows,midtones,smoothstep(0.,pivot,t)) :
        mix(midtones,highlights,smoothstep(pivot,1.,t));
    return zones*curveResponse(t);
}
void main(){
    vec4 source=texture(image,uv);
    if(amount<=0. || deviation<=0. || source.a<=0.){result=source;return;}
    vec3 original=premultiplied ? source.rgb/max(source.a,.000001) : source.rgb;
    vec3 base=original;
    if(encoding==1 && !scalar)base=decode(base);
    vec4 g=texture(grain,uv);
    float common=sqrt(clamp(1.-colorAmount+colorAmount*correlation,0.,1.));
    float separate=sqrt(max(0.,colorAmount*(1.-correlation)));
    vec3 noise=scalar ? vec3(g.x) : common*g.xxx+separate*g.yzw;
    float tone=scalar ? base.r : dot(base,luma);
    vec3 strength=deviation*response(tone)*(scalar ? vec3(1) : channelStrength);
    vec3 changed;
    if(outputMode==2)changed=.5+noise*strength;
    else if(outputMode==1)changed=base+noise*strength;
    else {
        vec3 d=2.30258509299*strength;
        // Gaussian moment correction reduces the density model's bright bias.
        // The field is non-Gaussian, so this is an approximation, not a promise
        // that every realization preserves every image's exact mean.
        vec3 variance=scalar ? vec3(fieldVariance.x) : common*common*fieldVariance.xxx+separate*separate*fieldVariance.yzw;
        vec3 correction=compensateMean ? .5*d*d*variance : vec3(0);
        changed=base+max(base,vec3(exposureFloor))*(exp(-noise*d-correction)-1.);
    }
    changed=mix(base,changed,amount);
    if(encoding==1 && !scalar)changed=encode(changed);
    changed=scalar ? vec3(changed.r,0,0) : mix(original,changed,components);
    if(premultiplied)changed*=source.a;
    result=vec4(changed,source.a);
}
)";
}
