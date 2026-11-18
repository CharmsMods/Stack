#pragma once
// Layout is authored by Raw/HslSelections::PackHslSelections.
#define STACK_HSL_SELECTION_GLSL R"GLSL(
uniform sampler2D uHslSelections;
uniform int uHslSelectionCount,uHslLimit;
uniform vec4 uHslImageRegion;
vec4 hslData(int i,int x){return texelFetch(uHslSelections,ivec2(x,i),0);}
float hslBand(vec4 b,float ev) {
    float d=max(b.x-ev,ev-b.y);return d<=0.0?1.0:b.z>0.0?1.0-smoothstep(0.0,b.z,d):0.0;
}
vec2 hslPoint(int i,int j) { vec4 p=hslData(i,44+j/2);return j%2==0?p.xy:p.zw; }
float hslWeight(int i,float hue,float ev,vec2 uv) {
    vec4 meta=hslData(i,0);if(meta.x<.5)return 0.0;
    float d=abs(hueDelta(hue-meta.y))-meta.z;
    float color=hslData(i,3).x>.5?1.0:d<=0.0?1.0:meta.w>0.0?1.0-smoothstep(0.0,meta.w,d):0.0;
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
vec3 resolveHsl(vec3 global,float hue,float ev,ivec2 segment,float t) {
    vec3 result=uHslLimit!=0?vec3(0.0):global;
    vec2 uv=uHslImageRegion.xy+vec2(vUV.x,1.0-vUV.y)*uHslImageRegion.zw;
    for(int i=0;i<16;++i) {
        if(i>=uHslSelectionCount)break;float w=hslWeight(i,hue,ev,uv);if(w<=0.0)continue;
        vec4 state=hslData(i,1);vec3 own=any(greaterThan(state.yzw,vec3(.5)))?hslOwn(i,segment,t):vec3(0.0);
        vec3 target=mix(global*state.x,own,state.yzw);
        result=mix(result,target,w);
    }return result;
}
)GLSL"
