#pragma once
namespace Raw::Bracketing::Depth::Shaders {
// Joint image/depth support, following the bilateral-weight principle of
// Kopf et al., SIGGRAPH 2007. A bounded single pass uses only measured seeds;
// inferred values never seed more inference or reconstruction rejection.
inline constexpr auto Complete=R"GLSL(#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) readonly buffer Measured {vec2 measured[];};
layout(std430,binding=1) buffer Output {vec2 result[];};
layout(std430,binding=2) readonly buffer Guide {float guide[];};
layout(std430,binding=3) buffer Statistics {vec4 stats[10];};
uniform ivec2 size,guideSize;uniform int mode;
float signal(vec2 p){ivec2 q=ivec2(clamp((p+.5)*vec2(guideSize)/vec2(size),vec2(0),vec2(guideSize-1)));return guide[q.y*guideSize.x+q.x];}
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);
 if(mode==1){if(any(notEqual(p,ivec2(0))))return;int known=0;for(int i=0;i<size.x*size.y;++i)known+=result[i].y>=.3?1:0;
  stats[9].z=float(known)/float(size.x*size.y);return;}
 if(any(greaterThanEqual(p,size)))return;int i=p.y*size.x+p.x;vec2 center=measured[i];result[i]=center;
 if(center.y>=.3)return;float value=signal(vec2(p));if(value<0)return;
 float sum=0,squares=0,weight=0,confidence=0,available=0,total=0;vec4 sides=vec4(0);int contributors=0;
 for(int y=-12;y<=12;++y)for(int x=-12;x<=12;++x){ivec2 q=p+ivec2(x,y);if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,size)))continue;
  float spatial=exp(-float(x*x+y*y)/72.);total+=spatial;vec2 d=measured[q.y*size.x+q.x];if(d.y<.3)continue;
  float other=signal(vec2(q));if(other<0)continue;
  float delta=abs(other-value);
  for(int k=1;k<4;++k){float middle=signal(mix(vec2(p),vec2(q),float(k)*.25));delta=max(delta,middle<0?1:abs(middle-value));}
  float w=spatial*exp(-.5*delta*delta/(.02*.02));if(w<.01)continue;
  available+=w;confidence+=w*d.y;w*=d.y;weight+=w;sum+=w*d.x;squares+=w*d.x*d.x;contributors++;
  if(x<-1)sides.x+=w;if(x>1)sides.y+=w;if(y<-1)sides.z+=w;if(y>1)sides.w+=w;}
 if(contributors<8||available<total*.12||weight<=0||max(min(sides.x,sides.y),min(sides.z,sides.w))<weight*.1)return;
 float mean=sum/weight,variance=max(0.,squares/weight-mean*mean);
 if(variance>.0064)return;float c=min(.55,.85*confidence/available)*exp(-variance/.0064);
 if(c>=.3)result[i]=vec2(mean,c);
})GLSL";
}
