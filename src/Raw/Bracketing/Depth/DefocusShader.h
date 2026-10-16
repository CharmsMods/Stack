#pragma once
namespace Raw::Bracketing::Depth::Shaders {
inline constexpr auto Defocus=R"GLSL(#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) readonly buffer Rgb {float rgb[];};
layout(std430,binding=1) readonly buffer Depth {float depth[];};
layout(std430,binding=2) buffer Work {vec4 work[];};
layout(std430,binding=3) buffer Output {float result[];};
layout(std430,binding=4) buffer Samples {vec2 samples[];};
uniform ivec2 size,mapSize;uniform vec2 sensorSize;
uniform vec4 coordinates; // sensor origin xy and pixel step xy
uniform vec4 settings; // focus, range, radius in output pixels, layer
uniform int mode;
vec2 sampleDepth(vec2 raw){vec2 uv=clamp((raw+.5)*vec2(mapSize)/sensorSize-.5,vec2(0),vec2(mapSize-1));
 ivec2 p=ivec2(uv);vec2 f=fract(uv);float sum=0,d=0,c=0,nearD=0,farD=1;int count=mapSize.x*mapSize.y;
 for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec2 q=min(p+ivec2(x,y),mapSize-1);int i=q.y*mapSize.x+q.x;
  float w=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y),confidence=depth[count+i];if(confidence<.25||w<1e-6)continue;
  nearD=max(nearD,depth[i]);farD=min(farD,depth[i]);sum+=w;d+=w*depth[i];c+=w*confidence;}
 return sum<.75||nearD-farD>.15?vec2(0):vec2(d/sum,c);}
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,size)))return;
 int i=p.y*size.x+p.x,n=size.x*size.y;
 if(mode==0){samples[i]=sampleDepth(coordinates.xy+vec2(p)*coordinates.zw);work[2*n+i]=vec4(rgb[3*i],rgb[3*i+1],rgb[3*i+2],1);return;}
 if(mode==4){vec3 value=work[2*n+i].rgb;result[3*i]=value.r;result[3*i+1]=value.g;result[3*i+2]=value.b;return;}
 float center=settings.w/15,sigma=.5*settings.z*clamp((abs(center-settings.x)-settings.y*.5)/max(.05,max(settings.x,1-settings.x)),0.,1.);
 if(mode==1){float alpha=samples[i].y<.3?0:max(0.,1-abs(samples[i].x-center)*15);
  work[i]=vec4(rgb[3*i],rgb[3*i+1],rgb[3*i+2],1)*alpha;return;}
 int radius=sigma>.25?int(ceil(3*sigma)):0;vec4 sum=vec4(0);float weight=0;
 for(int k=-radius;k<=radius;++k){ivec2 q=clamp(p+(mode==2?ivec2(k,0):ivec2(0,k)),ivec2(0),size-1);
  float w=radius==0?1:exp(-.5*float(k*k)/(sigma*sigma));sum+=w*work[(mode==2?0:n)+q.y*size.x+q.x];weight+=w;}
 sum/=weight;if(mode==2){work[n+i]=sum;return;}
 vec2 d=samples[i];if(d.y<.3||d.x>center+1.01/15)return;
 float alpha=clamp(sum.a,0.,1.);if(alpha>=1e-6)work[2*n+i]=vec4(sum.rgb+(1-alpha)*work[2*n+i].rgb,1);
})GLSL";
}
