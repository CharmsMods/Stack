#version 330 core
// Stack glass engine material v3. Based on frozen material v2.
// v2 SHA256 9e13de6ea1a84c1ff9ef4e2d8fc5ac3631a69b1b39880a04584dddba2110e101
// Adds clear-water environment response, semantic resting fill and layer opacity.
// Mode 0 retains the calibrated v2 appearance; mode 1 uses a consistent incident
// light field independently of backdrop brightness. It remains a screen-space model.
// This is NOT Apple's implementation or an exact dielectric path tracer.
// Inputs/outputs DISPLAY-LINEAR. Geometry in local physical framebuffer pixels.
// Immutable source with a valid mip pyramid; never sample the draw attachment.
// Mode 0: isolated opaque reference composite. Mode 1: premultiplied layer.
out vec4 fragColor;
uniform sampler2D uBackdrop;
uniform vec4 uSourceRectPx; // source framebuffer origin.xy and extent.zw (supports ROI)
uniform vec2 uLocalOriginPx;
uniform int uShapeCount;
uniform vec4 uShapes[4]; // local center.xy, half extents.zw
uniform vec4 uRadii;
uniform float uMergePx;
uniform float uMaterialize;
uniform float uBodyRadiusPx;
uniform float uCrownHeightPx;
uniform float uCrownAnisotropy; // x/y crown slope ratio; 1 gives isotropic dome
uniform float uEdgeWidthPx;
uniform float uEdgeReachPx;
uniform float uEdgePeak;
uniform float uEdgeRegularization;
uniform int uProfileKind; // 0: C2 monotone; 1: compact C2 fold; 2: rounded circular drop
uniform float uBevelWidthPx;
uniform float uBevelHeightPx;
uniform vec2 uTilt;
uniform float uIor;
uniform float uBodyReflection;
uniform float uRimReflection;
uniform float uInnerShade;
uniform float uInnerShadeWidthPx;
uniform float uInnerShadeInsetPx;
uniform float uLipOffsetPx;
uniform float uLipWidthPx;
uniform float uLipGain;
uniform float uOuterContour;
uniform float uContentReflection; // bounded chromatic environment response; not RGB IOR
uniform float uNeutralContrast; // neutral gamma = 1 + contrast; preserves white endpoints
uniform float uChromaGamma; // linear-channel exponent on chromatic content; 1 disables
uniform float uAdaptation; // 0 controlled studio; 1 source-adaptive broad light/caustic energy
uniform float uEnvironmentGain;
uniform vec4 uLightShape; // upper center/width, lower center/width in reflected direction.y
uniform vec2 uLightGain; // upper and lower area light strength
uniform int uUseLightProfile;
uniform vec4 uLightProfile[4]; // 16 nonnegative cubic B-spline environment controls
uniform float uAmbient;
uniform float uBodyPower;
uniform float uRoughnessLod;
uniform float uTintAmount;
uniform vec3 uTintLinear;
uniform float uShadowOpacity;
uniform float uShadowSigmaPx;
uniform vec2 uShadowOffsetPx;
uniform float uCausticGain;
uniform float uCausticWidthPx;
uniform float uContactGain;
uniform vec2 uContactLocalPx;
uniform float uContactRadiusPx;
uniform float uMaxMip;
uniform int uFilterQuality; // 0 isotropic; 1 bounded 4-tap anisotropic
uniform int uOutputMode; // 0 opaque reference, 1 premultiplied material+shadow layer
uniform vec2 uLogStrain; // symmetric trace-free log strain: xx, xy
// Optional graph compound. Ordinary controls keep the frozen four-lobe path.
uniform int uJoinedCount;
uniform vec4 uJoinedShapes[64]; // local center.xy, half extents.zw
uniform vec4 uJoinedMetrics[64]; // radius, inverse strain xx/xy/yy
uniform float uWaterMode; // 0 calibrated digital material, 1 clear-water environment
uniform float uLayerOpacity; // whole premultiplied layer, including shadow
uniform float uRestFillOpacity; // explicit semantic resting-control fill
uniform vec3 uRestFillLinear; // display-linear; not a fabricated optical response
uniform int uDebug; // 0 result, 1 normal, 2 displacement, 3 reflection, 4 footprint, 5 shadow

struct Field { float d; vec2 g; };
float signNZ(float x) { return x < 0.0 ? -1.0 : 1.0; }
Field roundedBox(vec2 p, vec2 h, float radius) {
    float r=clamp(radius,0.0,min(h.x,h.y));
    vec2 q=abs(p)-h+r, v=max(q,0.0);
    float l=length(v);
    vec2 sg=vec2(signNZ(p.x),signNZ(p.y));
    vec2 g=l>1e-7 ? v/l*sg : (q.x>q.y?vec2(sg.x,0):vec2(0,sg.y));
    return Field(l+min(max(q.x,q.y),0.0)-r,g);
}
Field undeformedField(vec2 p) {
    Field f=roundedBox(p-uShapes[0].xy,uShapes[0].zw,uRadii[0]);
    for(int i=1;i<4;++i) {
        if(i>=uShapeCount) break;
        Field b=roundedBox(p-uShapes[i].xy,uShapes[i].zw,uRadii[i]);
        if(uMergePx<=0.0) { if(b.d<f.d) f=b; }
        else {
            float h=clamp(0.5+0.5*(b.d-f.d)/uMergePx,0.0,1.0);
            f=Field(mix(b.d,f.d,h)-uMergePx*h*(1.0-h),mix(b.g,f.g,h));
        }
    }
    return f;
}
// Positive definite, area-preserving strain. No rotation-angle state, so a
// drag reversal cannot flip a heading by 180 degrees. All lobes share the same
// object transform. The distance correction is first-order at the contour;
// this is an authored implicit surface, not an exact elliptic distance solver.
Field field(vec2 p) {
    if(uJoinedCount>0) {
        Field joined=Field(1e20,vec2(0));
        for(int i=0;i<64;++i) {
            if(i>=uJoinedCount)break;
            vec4 s=uJoinedShapes[i],m=uJoinedMetrics[i];
            mat2 inverseShape=mat2(m.y,m.z,m.z,m.w);
            Field b=roundedBox(inverseShape*(p-s.xy),s.zw,m.x);
            vec2 gradient=inverseShape*b.g;
            float scale=sqrt((dot(gradient,gradient)+1e-4)/(dot(b.g,b.g)+1e-4));
            b=Field(b.d/scale,gradient/scale);
            if(i==0 || uMergePx<=0.0) { if(b.d<joined.d)joined=b; }
            else {
                float h=clamp(.5+.5*(b.d-joined.d)/uMergePx,0.0,1.0);
                joined=Field(mix(b.d,joined.d,h)-uMergePx*h*(1.0-h),mix(b.g,joined.g,h));
            }
        }
        return joined;
    }
    float r=length(uLogStrain);
    float e=exp(r), c=.5*(e+1.0/e);
    float sh=r<1e-4 ? 1.0+r*r/6.0 : .5*(e-1.0/e)/r;
    mat2 inverseShape=mat2(c-sh*uLogStrain.x,-sh*uLogStrain.y,
                          -sh*uLogStrain.y,c+sh*uLogStrain.x);
    Field f=undeformedField(inverseShape*p);
    vec2 transformed=inverseShape*f.g;
    float scale=sqrt((dot(transformed,transformed)+1e-4)/(dot(f.g,f.g)+1e-4));
    return Field(f.d/scale,transformed/scale);
}
float quintic(float t) { t=clamp(t,0.0,1.0); return t*t*t*(t*(t*6.0-15.0)+10.0); }
float edgeReach(float depth) {
    float u=clamp(depth/max(uEdgeWidthPx,0.01),0.0,1.0);
    if(uProfileKind==0) return uEdgeReachPx*(1.0-quintic(u));
    if(uProfileKind==2) {
        // Rationalized quarter-circle-like shoulder. Finite boundary slope,
        // C1 clear-center join; unlike a sine/quintic it makes a long thin fold.
        float e=clamp(uEdgeRegularization,.01,.5);
        float s=sqrt(e*e+(1.0-e*e)*u*(2.0-u));
        return uEdgeReachPx*(1.0+e)*(1.0-u)*(1.0-u)/(1.0+s);
    }
    float peak=clamp(uEdgePeak,0.02,0.98);
    return uEdgeReachPx*quintic(u<peak?u/peak:(1.0-u)/(1.0-peak));
}
vec2 opticalPoint(vec2 p, Field f) {
    // Smooth suppression at a stationary smooth-union neck. Do not normalize(0).
    vec2 n=f.g*inversesqrt(dot(f.g,f.g)+0.0016);
    return p-n*edgeReach(max(0.0,-f.d))*clamp(uMaterialize,0.0,1.0);
}
float fresnel(float cosI) {
    float n=max(uIor,1.00001), c=clamp(cosI,0.0,1.0);
    float st=(1.0-c*c)/(n*n), ct=sqrt(max(0.0,1.0-st));
    float rs=(c-n*ct)/(c+n*ct), rp=(n*c-ct)/(n*c+ct);
    return 0.5*(rs*rs+rp*rp);
}
vec3 normalFor(Field f, bool edge) {
    float depth=max(0.0,-f.d);
    float q=max(0.0,1.0-depth/max(uBodyRadiusPx,0.01));
    float slope=2.0*uCrownHeightPx/max(uBodyRadiusPx,0.01)*q;
    if(edge) {
        float b=max(uBevelWidthPx,0.01), e=clamp(1.0-depth/b,0.0,1.0);
        float pointSlope=(uBevelHeightPx/b)*e/sqrt(max(0.001,1.0-e*e));
        // A subpixel bevel must not be evaluated as an infinitely narrow bright
        // sample. Integrate the circular bevel slope across its covered pixel
        // footprint. Keep the fitted v2 mode byte-for-byte available.
        float footprint=max(.001,.5*fwidth(f.d));
        float d0=max(0.0,depth-footprint),d1=depth+footprint;
        float e0=clamp(1.0-d0/b,0.0,1.0),e1=clamp(1.0-d1/b,0.0,1.0);
        float averageSlope=uBevelHeightPx*(sqrt(max(0.0,1.0-e1*e1))-sqrt(max(0.0,1.0-e0*e0)))/max(.001,d1-d0);
        slope+=mix(pointSlope,averageSlope,clamp(uWaterMode,0.0,1.0));
    }
    vec2 crown=f.g*slope;
    if(!edge) crown.x*=uCrownAnisotropy;
    return normalize(vec3(crown+uTilt,1.0));
}
float lightPoint(int index) {
    int i=clamp(index,0,15);
    return uLightProfile[i/4][i%4];
}
float lightCurve(float y) {
    float x=clamp(y*.5+.5,0.0,1.0)*15.0;
    int i=int(floor(x)); float t=fract(x),t2=t*t,t3=t2*t;
    vec4 basis=vec4(pow(1.0-t,3.0),3.0*t3-6.0*t2+4.0,
                    -3.0*t3+3.0*t2+3.0*t+1.0,t3)/6.0;
    return dot(basis,vec4(lightPoint(i-1),lightPoint(i),lightPoint(i+1),lightPoint(i+2)));
}
vec3 environment(vec3 reflection) {
    // Broad area lights and a dark gap, parameterized in WORLD light directions.
    // All highlight positions follow the normal, including deformed/merged shapes.
    float upper=exp(-pow((reflection.y-uLightShape.x)/max(uLightShape.y,.01),2.0));
    float lower=exp(-pow((reflection.y-uLightShape.z)/max(uLightShape.w,.01),2.0));
    float side=0.80+0.20*exp(-pow((reflection.x+0.28)/0.85,2.0));
    float energy=uUseLightProfile!=0?lightCurve(reflection.y):uAmbient+uLightGain.x*upper+uLightGain.y*lower;
    return vec3(max(0.0,energy*side)*uEnvironmentGain);
}
vec3 waterEnvironment(vec3 r) {
    // A fixed, directional area-light environment describes the shallow crown.
    // Its coordinates live in reflected direction space, never screen pixels
    // or source luminance. The user may edit this studio-light rig per design.
    float key=exp(-pow((r.y-uLightShape.x)/max(uLightShape.y,.01),2.0)
                  -pow((r.x+.25)/.62,2.0));
    float fill=exp(-pow((r.y-uLightShape.z)/max(uLightShape.w,.01),2.0)
                   -pow((r.x-.15)/.85,2.0));
    return .35*environment(r)+vec3(uEnvironmentGain*(uLightGain.x*key+uLightGain.y*fill));
}
vec2 sourceUV(vec2 p) {
    vec2 t=vec2(textureSize(uBackdrop,0));
    return clamp((p+uLocalOriginPx-uSourceRectPx.xy)/uSourceRectPx.zw,0.5/t,1.0-0.5/t);
}
vec3 sampleAt(vec2 p,float lod) { return textureLod(uBackdrop,sourceUV(p),clamp(lod,0.0,uMaxMip)).rgb; }
vec3 filteredSource(vec2 p,vec2 dx,vec2 dy,out float major) {
    // Jacobian is in source TEXELS, so cropped/downsampled backdrops remain correct.
    vec2 texelsPerPx=vec2(textureSize(uBackdrop,0))/uSourceRectPx.zw;
    vec2 a=dx*texelsPerPx,b=dy*texelsPerPx;
    float xx=a.x*a.x+b.x*b.x, yy=a.y*a.y+b.y*b.y, xy=a.x*a.y+b.x*b.y;
    float disc=sqrt(max(0.0,(xx-yy)*(xx-yy)+4.0*xy*xy));
    float major2=max(1.0,0.5*(xx+yy+disc));
    float minor2=max(1.0,0.5*(xx+yy-disc));
    major=sqrt(major2);
    float roughnessLod=uRoughnessLod*clamp(uMaterialize,0.0,1.0);
    if(uFilterQuality==0) return sampleAt(p,max(0.5*log2(major2),roughnessLod));
    vec2 eigen=abs(xy)>1e-6?normalize(vec2(major2-yy,xy)):(xx>=yy?vec2(1,0):vec2(0,1));
    float minor=max(sqrt(minor2),major/4.0);
    float lod=max(log2(minor),roughnessLod);
    float span=sqrt(max(0.0,major2-minor*minor));
    vec2 stepPx=eigen*span/texelsPerPx;
    return 0.25*(sampleAt(p-stepPx*.375,lod)+sampleAt(p-stepPx*.125,lod)
                +sampleAt(p+stepPx*.125,lod)+sampleAt(p+stepPx*.375,lod));
}
float shadowAt(vec2 p) {
    float d=field(p-uShadowOffsetPx).d;
    float sigma=max(uShadowSigmaPx,0.25);
    // Smooth signed-distance approximation to a blurred projected silhouette.
    return uShadowOpacity*(1.0-smoothstep(-1.6*sigma,2.4*sigma,d));
}

void main() {
    vec2 p=gl_FragCoord.xy-uLocalOriginPx;
    Field f=field(p);
    float aa=max(0.001,0.5*fwidth(f.d));
    float coverage=1.0-smoothstep(-aa,aa,f.d);
    vec2 sampled=opticalPoint(p,f);
    // Never place these derivatives inside a divergent edge/material branch.
    vec2 dx=dFdx(sampled),dy=dFdy(sampled);
    float major;
    vec3 transmitted=filteredSource(sampled,dx,dy,major);
    vec3 original=sampleAt(p,0.0);
    float activation=clamp(uMaterialize,0.0,1.0);
    float depth=max(0.0,-f.d);
    float q=clamp(1.0-depth/max(uBodyRadiusPx,0.01),0.0,1.0);
    vec3 bodyN=normalFor(f,false),edgeN=normalFor(f,true);
    vec3 bodyR=reflect(vec3(0,0,-1),bodyN),edgeR=reflect(vec3(0,0,-1),edgeN);
    // Projected crown coordinate avoids a broad grey side-wall on saturated
    // tracks. The smooth lighting gradient follows the vertical surface slope.
    float bodyWeight=uBodyReflection*pow(clamp(q*abs(f.g.y),0.0,1.0),max(uBodyPower,.05));
    float f0=fresnel(1.0);
    float rimWeight=uRimReflection*max(0.0,(fresnel(edgeN.z)-f0)/(1.0-f0));
    bodyWeight=clamp(bodyWeight,0.0,.85)*activation;
    rimWeight=clamp(rimWeight,0.0,.98)*activation;
    float shadeBand=exp(-pow(depth/max(uInnerShadeWidthPx,.01),2.0));
    float side=1.0-pow(abs(f.g.y),2.0);
    shadeBand=exp(-pow((depth-uInnerShadeInsetPx)/max(uInnerShadeWidthPx,.01),2.0));
    transmitted*=1.0-clamp(uInnerShade*shadeBand*side*activation,0.0,.8);
    transmitted=mix(transmitted,uTintLinear,clamp(uTintAmount*activation,0.0,1.0));
    float colorMax=max(transmitted.r,max(transmitted.g,transmitted.b));
    float colorMin=min(transmitted.r,min(transmitted.g,transmitted.b));
    float chroma=smoothstep(.025,.15,colorMax-colorMin);
    // A clear center means identity GEOMETRY, not necessarily identity tone.
    // This transfer is intentionally authored; it never changes the scene/export.
    float transmissionGamma=mix(1.0+uNeutralContrast,uChromaGamma,chroma);
    transmitted=pow(max(transmitted,vec3(0.0)),vec3(mix(1.0,transmissionGamma,activation)));
    colorMax=max(transmitted.r,max(transmitted.g,transmitted.b));
    vec2 texelScale=vec2(textureSize(uBackdrop,0))/uSourceRectPx.zw;
    float ambientLod=log2(max(1.0,2.0*uBodyRadiusPx*max(texelScale.x,texelScale.y)));
    vec2 groupCenter=vec2(0.0);
    for(int i=0;i<4;++i) { if(i>=uShapeCount) break; groupCenter+=uShapes[i].xy; }
    groupCenter/=float(max(uShapeCount,1));
    vec3 sceneMean=sampleAt(groupCenter,ambientLod);
    float sceneLuma=dot(sceneMean,vec3(.2126,.7152,.0722));
    float adaptive=mix(1.0,clamp(sceneLuma/.78,.003,1.0),clamp(uAdaptation,0.0,1.0));
    vec3 contextColor=mix(vec3(1.0),transmitted/max(colorMax,.001),clamp(uContentReflection,0.0,1.0)*chroma);
    vec3 surface=mix(transmitted,environment(bodyR)*contextColor*adaptive,bodyWeight);
    surface=mix(surface,environment(edgeR)*contextColor*mix(.12,1.0,adaptive),rimWeight);
    // Keep incident light separate from the transmitted backdrop. The same
    // dielectric response works on light, middle-gray and dark content; there
    // is no luminance threshold, per-theme material switch or body light cutoff.
    float waterBody=clamp(fresnel(bodyN.z)*uBodyReflection,0.0,.98)*activation;
    float waterEdge=clamp(max(0.0,fresnel(edgeN.z)-fresnel(bodyN.z))*uRimReflection,0.0,.98)*activation;
    float waterF=min(.98,waterBody+waterEdge);
    vec3 waterLight=(waterEnvironment(bodyR)*waterBody+waterEnvironment(edgeR)*waterEdge)/max(waterF,1e-6);
    vec3 waterSurface=transmitted*(1.0-waterF)+waterLight*waterF;
    surface=mix(surface,waterSurface,clamp(uWaterMode,0.0,1.0));
    // Meniscus strip lights: a narrow optical lip, separate from the broad face.
    // Their strength varies around the normal field; there is no white outline.
    vec2 lipGradient=f.g+uTilt;
    vec2 lipDirection=lipGradient*inversesqrt(dot(lipGradient,lipGradient)+0.0016);
    float upperLip=pow(max(dot(lipDirection,normalize(vec2(-.26,1.0))),0.0),3.0);
    float lowerLip=pow(max(dot(lipDirection,normalize(vec2(.12,-1.0))),0.0),3.0);
    // Pixel-footprint convolution approximation conserves narrow-glint energy
    // when the same profile is drawn at a small physical pixel size.
    float lipSigma=max(uLipWidthPx,.01);
    float filteredSigma=sqrt(lipSigma*lipSigma+(2.0*aa)*(2.0*aa)/12.0);
    float lipBand=(lipSigma/filteredSigma)*exp(-0.5*pow((depth-uLipOffsetPx)/filteredSigma,2.0));
    float lip=clamp(uLipGain*lipBand*(upperLip+.92*lowerLip)*activation,0.0,.95);
    float outer=clamp(uOuterContour*exp(-pow(depth/max(.45,.45*uLipWidthPx),2.0))*activation,0.0,.8);
    surface*=1.0-outer;
    surface=mix(surface,vec3(1.35)*uEnvironmentGain,lip);
    float contact=exp(-dot(p-uContactLocalPx,p-uContactLocalPx)/max(1.0,uContactRadiusPx*uContactRadiusPx));
    surface+=vec3(uContactGain*contact*activation)*(0.15+0.85*pow(q,2.0));
    // Resting fill is a selected control style, not glass tint or source blur.
    surface=mix(surface,uRestFillLinear,clamp(uRestFillOpacity*(1.0-activation),0.0,1.0));
    float shadow=shadowAt(p)*activation;
    float caustic=uCausticGain*exp(-pow(max(f.d,0.0)/max(uCausticWidthPx,.01),2.0));
    caustic*=pow(max(-f.g.y,0.0),3.0)*activation*mix(adaptive,1.0,clamp(uWaterMode,0.0,1.0));
    vec3 under=original*(1.0-shadow)+vec3(caustic);
    vec3 result=mix(under,surface,coverage);
    if(uDebug==1) result=mix(vec3(.08),edgeN*.5+.5,coverage);
    if(uDebug==2) result=vec3((sampled-p)/max(uEdgeReachPx,1.0)*.5+.5,.5);
    if(uDebug==3) result=vec3(bodyWeight,rimWeight,shadeBand*uInnerShade)*coverage;
    if(uDebug==4) result=vec3(log2(max(major,1.0))/6.0)*coverage;
    if(uDebug==5) result=vec3(shadow);
    if(uOutputMode==1 && uDebug==0) {
        // Blend ONE, ONE_MINUS_SRC_ALPHA over the same base; outside glass only
        // shadow/caustic contribute. Emission is allowed in premultiplied RGB.
        float a=coverage+(1.0-coverage)*shadow;
        fragColor=vec4(surface*coverage+vec3(caustic)*(1.0-coverage),a) * clamp(uLayerOpacity,0.0,1.0);
    } else fragColor=vec4(result,1.0);
}
