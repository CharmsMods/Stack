#pragma once

namespace Stack::Renderer::RawImageBackdropNoiseShaders {
inline constexpr const char* Analysis = R"(#version 430 core
in vec2 uv;
layout(location=0) out vec4 fine;
layout(location=1) out vec4 coarse;
uniform sampler2D image, previous;
uniform vec4 rect, textureRect;
uniform vec2 previousOffset, previousScale, nativeExtent;
uniform float fadeAmount;
uniform bool encodedSrgb;
vec3 decode(vec3 x) { vec3 a=abs(x); return sign(x)*mix(a/12.92,pow((a+.055)/1.055,vec3(2.4)),step(vec3(.04045),a)); }
vec3 encode(vec3 x) { vec3 a=abs(x); return sign(x)*mix(12.92*a,1.055*pow(a,vec3(1.0/2.4))-.055,step(vec3(.0031308),a)); }
vec3 samplePhoto(vec2 p) {
    vec4 b = textureLod(image, p, 0.0);
    if (fadeAmount < 1.0) {
        vec4 a = textureLod(previous, previousOffset+p*previousScale, 0.0);
        float alpha = mix(a.a,b.a,fadeAmount);
        vec3 ar = encodedSrgb ? decode(a.rgb) : a.rgb;
        vec3 br = encodedSrgb ? decode(b.rgb) : b.rgb;
        vec3 rgb = alpha > .000001 ? mix(ar*a.a,br*b.a,fadeAmount)/alpha : vec3(0);
        b.rgb = encodedSrgb ? encode(rgb) : rgb;
    }
    if (any(isnan(b.rgb)) || any(isinf(b.rgb))) return vec3(0);
    // Measure noise actually visible in the SDR preview, not hidden values
    // beyond its display range that would overstate grain in clipped skies.
    b.rgb = clamp(b.rgb, vec3(0), vec3(1));
    return b.rgb;
}
vec3 noiseComponents(vec3 rgb) {
    // Orthonormal brightness and color-difference axes retain grayscale noise
    // without inventing independent RGB speckles. Independent RGB stays so.
    return vec3((rgb.r+rgb.g+rgb.b)*.5773502692,
                (rgb.r-rgb.b)*.7071067812,
                (rgb.r-2.0*rgb.g+rgb.b)*.4082482905);
}
void sort16(inout vec3 values[16]) {
    // Fixed bitonic network, component-wise. No data-dependent sorting loop.
    for (int span=2; span<=16; span*=2)
        for (int stride=span/2; stride>0; stride/=2)
            for (int i=0; i<16; ++i) {
                int j=i^stride;
                if (j>i) {
                    vec3 lo=min(values[i],values[j]), hi=max(values[i],values[j]);
                    bool ascending=(i&span)==0;
                    values[i]=ascending ? lo : hi;
                    values[j]=ascending ? hi : lo;
                }
            }
}
vec3 robustSigma(inout vec3 values[16]) {
    sort16(values);
    vec3 center=.5*(values[7]+values[8]);
    for (int i=0; i<16; ++i) values[i]=abs(values[i]-center);
    sort16(values);
    // Gaussian MAD calibration, with a bounded small-sample correction.
    return .5*(values[7]+values[8])*(1.4826022185*16.0/15.2);
}
vec3 haar(vec2 p, vec2 stepSize, vec2 lo, vec2 hi) {
    vec3 a=samplePhoto(clamp(p,lo,hi));
    vec3 b=samplePhoto(clamp(p+vec2(stepSize.x,0),lo,hi));
    vec3 c=samplePhoto(clamp(p+vec2(0,stepSize.y),lo,hi));
    vec3 d=samplePhoto(clamp(p+stepSize,lo,hi));
    return noiseComponents(.5*(a-b-c+d));
}
void main() {
    vec2 local=(uv-rect.xy)/max(rect.zw-rect.xy,vec2(.000001));
    if (any(lessThan(local,vec2(0))) || any(greaterThan(local,vec2(1)))) discard;
    vec2 dimensions=vec2(textureSize(image,0));
    vec2 texel=1.0/dimensions;
    vec2 span=abs(textureRect.zw-textureRect.xy);
    vec2 stepSize=texel;
    vec2 lo=min(textureRect.xy,textureRect.zw)+.5*texel;
    vec2 hi=max(lo,max(textureRect.xy,textureRect.zw)-.5*texel);
    // Measurements and their texel-center phase follow the photograph, not
    // the screen. A new proxy can change the estimate, never the grain phase.
    vec2 p=mix(textureRect.xy,textureRect.zw,local);
    p=(floor(p*dimensions)+.5)/dimensions;
    // Move the measurement inward, rather than measuring the repeated edge
    // texels or the Gaussian surround. Small patches use their actual domain.
    vec2 inset=min((hi-lo)*.5,7.0*stepSize);
    p=clamp(p,lo+inset,hi-inset);
    vec3 first[16], second[16];
    float brightness = 0.0;
    for (int i=0; i<16; ++i) {
        vec2 offset=(vec2(i%4,i/4)-vec2(1.5))*3.0*stepSize;
        brightness += dot(samplePhoto(clamp(p+offset,lo,hi)),vec3(.2126,.7152,.0722))/16.0;
        first[i]=haar(p+offset,stepSize,lo,hi);
        second[i]=haar(p+offset,2.0*stepSize,lo,hi);
    }
    vec3 s1=robustSigma(first), s2=robustSigma(second);
    vec3 v1=s1*s1, v2=s2*s2;
    // Fit the energies of the native fields AFTER approximate proxy filtering,
    // rather than fitting native correlations to already-reduced pixels. A
    // unit-variance [1,2,1]/sqrt(6) field has r(1)=2/3 and r(2)=1/6. For q-wide
    // box averages, count those neighboring pairs both within and across blocks.
    // Fractional footprints interpolate the pair counts. Actual proxy filters
    // can differ; this cannot recover their lost sensor information.
    vec2 footprint=max(vec2(1),nativeExtent*(rect.zw-rect.xy)/max(vec2(1),span*dimensions));
    vec2 squaredFootprint=footprint*footprint;
    vec2 correlatedEnergy=(footprint+(4.0/3.0)*max(footprint-1.0,vec2(0))+
        (1.0/3.0)*max(footprint-2.0,vec2(0)))/squaredFootprint;
    vec2 adjacent=((2.0/3.0)+(1.0/3.0)*clamp(footprint-1.0,0.0,1.0))/squaredFootprint;
    vec2 next=max(2.0-footprint,vec2(0))/(6.0*squaredFootprint);
    vec2 firstEnergy=correlatedEnergy-adjacent, secondEnergy=correlatedEnergy-next;
    float firstCorrelated=firstEnergy.x*firstEnergy.y;
    float secondCorrelated=secondEnergy.x*secondEnergy.y;
    float whiteEnergy=1.0/(footprint.x*footprint.y);
    // At q=1 these are the original Haar energies 1/9, 25/36 and 1.
    // Bound the correlated component so white variance cannot become negative.
    vec3 smoothVariance=clamp((v2-v1)/max(.00000001,secondCorrelated-firstCorrelated),
        vec3(0),v1/max(.00000001,firstCorrelated));
    vec3 whiteVariance=max(vec3(0),(v1-smoothVariance*firstCorrelated)/max(.00000001,whiteEnergy));
    // Keep measured display brightness beside each local variance. Noise is
    // learned from these measurements, not a fixed shadow/highlight multiplier.
    fine=vec4(whiteVariance,brightness);
    coarse=vec4(smoothVariance,brightness);
})";

inline constexpr const char* Filter = R"(#version 430 core
in vec2 uv;
layout(location=0) out vec4 fine;
layout(location=1) out vec4 coarse;
uniform sampler2D fineInput, coarseInput;
uniform vec2 direction;
uniform bool rejectOutliers;
vec3 median9(inout vec3 values[9]) {
    // Component-wise insertion sort on a fixed, small neighborhood.
    for (int i=1; i<9; ++i)
        for (int j=i; j>0; --j) {
            vec3 lo=min(values[j-1],values[j]), hi=max(values[j-1],values[j]);
            values[j-1]=lo; values[j]=hi;
        }
    return values[4];
}
void main() {
    vec4 centerFine=textureLod(fineInput,uv,0.0);
    vec4 centerCoarse=textureLod(coarseInput,uv,0.0);
    if (rejectOutliers) {
        vec2 texel=1.0/vec2(textureSize(fineInput,0));
        vec3 f[9], c[9];
        for (int y=-1; y<=1; ++y)
            for (int x=-1; x<=1; ++x) {
                int i=(y+1)*3+x+1;
                vec2 p=uv+vec2(x,y)*texel;
                vec4 neighborFine=textureLod(fineInput,p,0.0);
                vec3 neighborCoarse=textureLod(coarseInput,p,0.0).rgb;
                // Reject isolated estimates within the same brightness region.
                // Crossing a bright sky/dark ground boundary must not replace
                // the region's noise with its neighbor's different strength.
                bool sameBrightness=abs(neighborFine.a-centerFine.a)<=.08;
                f[i]=sameBrightness ? neighborFine.rgb : centerFine.rgb;
                c[i]=sameBrightness ? neighborCoarse : centerCoarse.rgb;
            }
        fine=vec4(median9(f),centerFine.a);
        coarse=vec4(median9(c),centerCoarse.a);
    } else {
        const float weights[4]=float[4](.27068215,.21674532,.11128076,.03663285);
        vec3 f=vec3(0), c=vec3(0);
        float total=0.0;
        for (int i=-3; i<=3; ++i) {
            vec2 p=uv+direction*float(i);
            vec4 neighborFine=textureLod(fineInput,p,0.0);
            float difference=(neighborFine.a-centerFine.a)/.08;
            float weight=weights[abs(i)]*exp(-.5*difference*difference);
            f+=weight*neighborFine.rgb;
            c+=weight*textureLod(coarseInput,p,0.0).rgb;
            total+=weight;
        }
        fine=vec4(f/total,centerFine.a); coarse=vec4(c/total,centerCoarse.a);
    }
})";

inline constexpr const char* BrightnessProfile = R"(#version 430 core
in vec2 uv;
layout(location=0) out vec4 fine;
layout(location=1) out vec4 coarse;
uniform sampler2D fineInput, coarseInput;
uniform int bins;
void main() {
    float brightness=(gl_FragCoord.x-.5)/float(bins-1);
    ivec2 size=textureSize(fineInput,0);
    float nearestSquared=1.0;
    for (int y=0; y<size.y; ++y) for (int x=0; x<size.x; ++x) {
        float delta=texelFetch(fineInput,ivec2(x,y),0).a-brightness;
        nearestSquared=min(nearestSquared,delta*delta);
    }
    vec3 f=vec3(0), c=vec3(0);
    float total=0.0;
    for (int y=0; y<size.y; ++y) for (int x=0; x<size.x; ++x) {
        vec4 measured=texelFetch(fineInput,ivec2(x,y),0);
        float delta=measured.a-brightness;
        // Normalize a Gaussian neighborhood in brightness. Subtracting the
        // nearest measurement keeps sparse/unobserved bins finite and uses
        // the closest observed brightness instead of inventing a noise floor.
        float weight=exp(-.5*max(0.0,delta*delta-nearestSquared)/(.045*.045));
        f+=weight*measured.rgb;
        c+=weight*texelFetch(coarseInput,ivec2(x,y),0).rgb;
        total+=weight;
    }
    fine=vec4(f/total,1);
    coarse=vec4(c/total,1);
})";

inline constexpr const char* Seed = R"(#version 430 core
in vec2 uv;
layout(location=0) out vec4 fine;
layout(location=1) out vec4 coarse;
uint hash(uint x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
vec3 gaussian(ivec2 point, uint seed) {
    ivec2 p=(point%512+512)%512;
    float signValue=p.x<256 ? 1.0 : -1.0;
    p.x%=256;
    uint h=hash(uint(p.x)+uint(p.y)*512u+seed);
    uvec3 channels=uvec3(h,h^0x68bc21ebu,h^0x02e5be93u);
    vec3 a, b;
    for (int i=0; i<3; ++i) {
        a[i]=(float(hash(channels[i])>>8)+.5)/16777216.0;
        b[i]=(float(hash(channels[i]^0x967a889bu)>>8)+.5)/16777216.0;
    }
    return signValue*sqrt(-2.0*log(a))*cos(6.2831853072*b);
}
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy);
    vec3 sum=vec3(0);
    for (int y=-1; y<=1; ++y)
        for (int x=-1; x<=1; ++x)
            sum+=gaussian(p+ivec2(x,y),0x9e3779b9u)*float((x==0?2:1)*(y==0?2:1));
    fine=vec4(gaussian(p,0x243f6a88u),1);
    // Divide by 6, not 16: this is a unit-variance correlated random field.
    coarse=vec4(sum/6.0,1);
})";

inline constexpr const char* Synthesis = R"(
uniform sampler2D noiseFineProfile, noiseCoarseProfile, noiseFineGrain, noiseCoarseGrain;
uniform sampler2D noiseFineBrightness, noiseCoarseBrightness;
uniform vec2 noisePhysicalSize, noiseNativeExtent;
uniform bool noiseEnabled;
vec4 blurNoiseRetention(float deviation) {
    if (deviation<.0001) return vec4(1);
    float variance=deviation*deviation;
    if (deviation>2.0) {
        // Gaussian kernel energy and its convolution with r(0), r(1), r(2).
        float covariance=1.0/(6.2831853072*variance);
        float energy=.5*covariance;
        float c=1.0+(4.0/3.0)*exp(-.5/variance)+(1.0/3.0)*exp(-2.0/variance);
        float e=1.0+(4.0/3.0)*exp(-.25/variance)+(1.0/3.0)*exp(-1.0/variance);
        return vec4(covariance,energy,covariance*c*c,energy*e*e);
    }
    // At large zoom a small screen blur can leave considerable source grain.
    // Sum the discrete kernel rather than adding a second full noise signal.
    float weights[7]; weights[0]=1.0;
    float total=1.0;
    for (int i=1; i<=6; ++i) {
        weights[i]=exp(-.5*float(i*i)/variance);
        total+=2.0*weights[i];
    }
    float diagonal=0.0, adjacent=0.0, next=0.0;
    for (int i=-6; i<=6; ++i) {
        float a=weights[abs(i)];
        diagonal+=a*a;
        if (i+1<=6) adjacent+=a*weights[abs(i+1)];
        if (i+2<=6) next+=a*weights[abs(i+2)];
    }
    float cWhite=1.0/total, rWhite=diagonal/(total*total);
    float cCoarse=(1.0+(4.0/3.0)*weights[1]+(1.0/3.0)*weights[2])/total;
    float rCoarse=(diagonal+(4.0/3.0)*adjacent+(1.0/3.0)*next)/(total*total);
    return vec4(cWhite*cWhite,rWhite*rWhite,cCoarse*cCoarse,rCoarse*rCoarse);
}
uint grainHash(uint x) {
    x ^= x>>16; x *= 0x7feb352du; x ^= x>>15; x *= 0x846ca68bu; return x^(x>>16);
}
vec2 grainAddress(vec2 coordinate, ivec2 cell) {
    uint h = grainHash(uint(cell.x)^grainHash(uint(cell.y)+0x9e3779b9u));
    // Each overlapping photo-space cell addresses an independently shifted,
    // reflected/rotated field. The atlas no longer repeats every 512 pixels.
    vec2 q = coordinate;
    if ((h&1u)!=0u) q.x = -q.x;
    if ((h&2u)!=0u) q.y = -q.y;
    if ((h&4u)!=0u) q = q.yx;
    vec2 offset = vec2(float((h>>3)&511u), float((h>>12)&511u));
    return (q+offset)/vec2(textureSize(noiseFineGrain,0));
}
void sampleGrain(vec2 coordinate, float lod, out vec3 fine, out vec3 coarse) {
    vec2 grid = coordinate/256.0;
    ivec2 cell = ivec2(floor(grid));
    vec2 f = smoothstep(vec2(0),vec2(1),fract(grid));
    fine = vec3(0); coarse = vec3(0);
    float energy = 0.0;
    for (int y=0; y<2; ++y) for (int x=0; x<2; ++x) {
        float weight = (x==0 ? 1.0-f.x : f.x)*(y==0 ? 1.0-f.y : f.y);
        vec2 p = grainAddress(coordinate,cell+ivec2(x,y));
        fine += weight*textureLod(noiseFineGrain,p,lod).rgb;
        coarse += weight*textureLod(noiseCoarseGrain,p,lod).rgb;
        energy += weight*weight;
    }
    // Blending decorrelated fields otherwise makes grain weaker at cell joins.
    float normalization = inversesqrt(max(.000001,energy));
    fine *= normalization; coarse *= normalization;
}
vec3 matchedNoise(float blurredWeight, float sigma, vec3 visibleColor) {
    if (!noiseEnabled || blurredWeight<=0.0) return vec3(0);
    vec2 photoSpan=max(photo.zw-photo.xy,vec2(.000001));
    vec2 imagePoint=(uv-photo.xy)/photoSpan;
    vec2 profilePoint=clamp(imagePoint,vec2(0),vec2(1));
    // Smooth variance over a wider photo neighborhood farther into the
    // surround. Clamping the finest edge row alone stretches its measurement
    // errors and fine/coarse differences into straight bands.
    vec2 outside=max(max(-imagePoint,imagePoint-1.0),vec2(0));
    vec2 outsideCells=outside*vec2(textureSize(noiseFineProfile,0));
    float profileLod=log2(1.0+.5*length(outsideCells));
    vec3 fineVariance=textureLod(noiseFineProfile,profilePoint,profileLod).rgb;
    vec3 coarseVariance=textureLod(noiseCoarseProfile,profilePoint,profileLod).rgb;
    // Near the photograph, retain its local measured strength. Farther out,
    // choose strength from the photo's brightness-conditioned noise curve at
    // the actual blended backdrop brightness, before adding synthetic grain.
    float brightness=clamp(dot(visibleColor,vec3(.2126,.7152,.0722)),0.0,1.0);
    float bins=float(textureSize(noiseFineBrightness,0).x);
    vec2 brightnessPoint=vec2((brightness*(bins-1.0)+.5)/bins,.5);
    vec2 photoSize=photoSpan*screenSize;
    float distance=length(outside*photoSize)/max(.000001,min(photoSize.x,photoSize.y));
    float brightnessWeight=smoothstep(0.0,.35,distance);
    fineVariance=mix(fineVariance,texture(noiseFineBrightness,brightnessPoint).rgb,brightnessWeight);
    coarseVariance=mix(coarseVariance,texture(noiseCoarseBrightness,brightnessPoint).rgb,brightnessWeight);
    vec3 fine=sqrt(max(vec3(0),fineVariance));
    vec3 coarse=sqrt(max(vec3(0),coarseVariance));
    // Only native photo coordinates address the random fields. Do not use a
    // measured pitch, preview texture extent, screen grid or animation time.
    vec2 coordinate=imagePoint*noiseNativeExtent;
    vec2 footprint=noiseNativeExtent/max(photoSpan*noisePhysicalSize,vec2(.000001));
    float reduction=max(1.0,max(footprint.x,footprint.y));
    // Explicit LOD avoids derivatives across the edge-blend conditional.
    // Minification averages the same grain rather than replacing its pattern.
    float lod=log2(reduction);
    vec2 nativePerLogical=noiseNativeExtent/max(photoSpan*screenSize,vec2(.000001));
    vec4 retained=blurNoiseRetention(sigma*max(nativePerLogical.x,nativePerLogical.y)/reduction);
    float sharpWeight=1.0-blurredWeight;
    vec2 remaining=vec2(sharpWeight*sharpWeight)+
        2.0*sharpWeight*blurredWeight*retained.xz+
        blurredWeight*blurredWeight*retained.yw;
    vec2 replacement=sqrt(clamp(vec2(1)-remaining,0.0,1.0));
    vec3 fineGrain, coarseGrain;
    sampleGrain(coordinate,lod,fineGrain,coarseGrain);
    vec3 components=fine*fineGrain*replacement.x+coarse*coarseGrain*replacement.y;
    vec3 rgb=vec3(components.x*.5773502692+components.y*.7071067812+components.z*.4082482905,
                  components.x*.5773502692-components.z*.8164965809,
                  components.x*.5773502692-components.y*.7071067812+components.z*.4082482905);
    // Account for sharp/blur covariance. With negligible remaining blur noise,
    // this becomes sqrt(1-(1-t)^2), maintaining strength through the join.
    return rgb;
}
)";
}
