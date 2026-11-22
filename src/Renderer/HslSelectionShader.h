#pragma once
// Layout is authored by Raw/HslSelections::PackHslSelections.
#define STACK_HSL_SELECTION_GLSL R"GLSL(
uniform sampler2D uHslSelections,uWarpPickMasks[8];
uniform int uHslSelectionCount,uHslLimit;
uniform vec4 uHslImageRegion;
vec4 hslData(int i,int x){return texelFetch(uHslSelections,ivec2(x,i),0);}
float hslBand(vec4 b,float ev) {
    float d=max(b.x-ev,ev-b.y);return d<=0.0?1.0:b.z>0.0?1.0-smoothstep(0.0,b.z,d):0.0;
}
vec2 hslPoint(int i,int j) { vec4 p=hslData(i,44+j/2);return j%2==0?p.xy:p.zw; }
float hslWeight(int i,float hue,float ev,vec2 uv,float chroma) {
    vec4 meta=hslData(i,0);if(meta.x<.5)return 0.0;
    float d=abs(hueDelta(hue-meta.y))-meta.z;
    float type=hslData(i,3).x;
    float color=type>1.5?1.0-smoothstep(.02,.08,chroma):type>.5?1.0:d<=0.0?1.0:meta.w>0.0?1.0-smoothstep(0.0,meta.w,d):0.0;
    vec4 wedge=hslData(i,109);
    if(wedge.x>=0.0 && type<.5 && meta.w>0.0 && meta.z>0.0)color=smoothstep(0.0,min(meta.w,meta.z),-d);
    if(wedge.x>=0.0)color*=smoothstep(0.0,min(.035,wedge.y),wedge.y-abs(chroma-wedge.x));
    if(wedge.z>0.0){
        vec2 p=vec2(uv.x,1.0-uv.y);
        if(wedge.z<1.5)color*=texture(uWarpPickMasks[0],p).r;
        else if(wedge.z<2.5)color*=texture(uWarpPickMasks[1],p).r;
        else if(wedge.z<3.5)color*=texture(uWarpPickMasks[2],p).r;
        else if(wedge.z<4.5)color*=texture(uWarpPickMasks[3],p).r;
        else if(wedge.z<5.5)color*=texture(uWarpPickMasks[4],p).r;
        else if(wedge.z<6.5)color*=texture(uWarpPickMasks[5],p).r;
        else if(wedge.z<7.5)color*=texture(uWarpPickMasks[6],p).r;
        else if(wedge.z<8.5)color*=texture(uWarpPickMasks[7],p).r;
    }
    if(color<=0.0)return 0.0;
    vec4 sizes=hslData(i,2);int count=int(sizes.z);
    if(sizes.z>=0.0) {
        if(count<3)return 0.0;
        vec4 bounds=hslData(i,108);
        if(any(lessThan(uv,bounds.xy)) || any(greaterThan(uv,bounds.zw)))return 0.0;
    }
    float included=sizes.x<.5?1.0:0.0,excluded=0.0;
    for(int j=0;j<16;++j) {
        if(j>=int(max(sizes.x,sizes.y)))break;
        if(j<int(sizes.x))included=max(included,hslBand(hslData(i,12+j),ev));
        if(j<int(sizes.y))excluded=max(excluded,hslBand(hslData(i,28+j),ev));
    }
    float weight=color*included*(1.0-excluded);if(weight<=0.0 || sizes.z<0.0)return weight;
    bool inside=false;float distanceSquared=1e20;vec2 a=hslPoint(i,count-1);
    for(int j=0;j<128;++j) {
        if(j>=count)break;vec2 b=hslPoint(i,j);
        if((a.y>uv.y)!=(b.y>uv.y) && uv.x<(b.x-a.x)*(uv.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
        if(sizes.w>0.0) {
            vec2 delta=b-a;float t=clamp(dot(uv-a,delta)/max(dot(delta,delta),1e-12),0.0,1.0);
            vec2 offset=uv-a-t*delta;distanceSquared=min(distanceSquared,dot(offset,offset));
        }
        a=b;
    }
    return inside?weight*(sizes.w>0.0?smoothstep(0.0,sizes.w,sqrt(distanceSquared)):1.0):0.0;
}
vec3 hslOwn(int index,ivec2 segment,float t) {
    if(segment.x<0)return vec3(0.0);
    vec3 a=hslData(index,4+segment.x).xyz,b=hslData(index,4+segment.y).xyz;
    float delta=hueDelta(b.x-a.x);if(delta==-180.0)delta=180.0;
    return vec3(a.x+t*delta,mix(a.yz,b.yz,t));
}
vec3 resolveHsl(vec3 global,float hue,float ev,ivec2 segment,float t,float chroma,out vec3 grade) {
    grade=vec3(0.0);
    vec3 result=uHslLimit!=0?vec3(0.0):global;
    vec2 uv=uHslImageRegion.xy+vec2(vUV.x,1.0-vUV.y)*uHslImageRegion.zw;
    for(int i=0;i<16;++i) {
        if(i>=uHslSelectionCount)break;float w=hslWeight(i,hue,ev,uv,chroma);if(w<=0.0)continue;
        grade+=w*hslData(i,3).yzw;
        vec4 state=hslData(i,1);vec3 own=any(greaterThan(state.yzw,vec3(.5)))?hslOwn(i,segment,t):vec3(0.0);
        vec3 target=mix(state.x<0.0?result:global*state.x,own,state.yzw);
        result=mix(result,target,w);
    }return result;
}
)GLSL"
