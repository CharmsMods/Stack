#pragma once
namespace Raw::Bracketing::Depth::Shaders {
inline constexpr auto DepthEvidence=R"GLSL(
layout(local_size_x=64) in;
struct Track {vec4 xy;vec4 quality;};struct Pose {vec4 rotation[3];vec4 translation;vec4 camera;vec4 stats;};
layout(std430,binding=0) readonly buffer Tracks {Track tracks[];};
layout(std430,binding=1) readonly buffer Poses {Pose poses[];};
layout(std430,binding=2) buffer Evidence {vec2 evidence[];};
uniform int count,view;uniform vec2 size;
void main(){uint i=gl_GlobalInvocationID.x;if(i>=uint(count))return;Track t=tracks[i];vec2 result=vec2(0);Pose p=poses[35];
 if(t.quality.x>.4){float pixels=max(size.x,size.y);vec4 q=(t.xy-vec4((size-1)*.5,(size-1)*.5))/pixels;
  vec3 d=triangulate(dmat3(p.rotation[0].xyz,p.rotation[1].xyz,p.rotation[2].xyz),dvec3(p.translation.xyz),double(p.camera.x),double(p.camera.y),dvec2(q.xy),dvec2(q.zw));
  if(d.x>0&&d.z*pixels<.5){float certainty=clamp((d.y*pixels-.2)/.8,0.,1.);
   result=vec2(d.x,t.quality.x*certainty*p.stats.w/(1+4*d.z*d.z*pixels*pixels));}}
 evidence[view*count+int(i)]=result;
})GLSL";
inline constexpr auto Fuse=R"GLSL(#version 430 core
layout(local_size_x=64) in;
layout(std430,binding=0) buffer Evidence {vec2 evidence[];};
layout(std430,binding=1) buffer Output {vec2 result[];};
layout(std430,binding=2) buffer Statistics {vec4 stats[10];};
layout(std430,binding=3) readonly buffer Guide {float guide[];};
layout(std430,binding=4) buffer Gates {float rejected[];};
uniform int count,views,mode;uniform ivec2 size,guideSize;
float median(float v[8],int n){for(int i=1;i<n;++i){float t=v[i];int j=i-1;while(j>=0&&v[j]>t){v[j+1]=v[j];j--;}v[j+1]=t;}return v[n/2];}
void main(){uint i=gl_GlobalInvocationID.x;
 if(mode==5){if(i!=0)return;int anchor=0;float best=0;
  for(int v=0;v<views;++v){float weight=0;for(int p=0;p<count;++p){float c=evidence[v*count+p].y;if(c>.3)weight+=c;}
   if(weight>best){best=weight;anchor=v;}}
  stats[9]=vec4(float(anchor),best,0,0);return;}
 if(mode==0){if(i>=uint(views))return;int anchor=int(stats[9].x);if(i==uint(anchor)){stats[i]=vec4(1,1,0,0);return;}
  uint histogram[256];for(int k=0;k<256;++k)histogram[k]=0;int overlap=0;
  for(int p=0;p<count;++p){vec2 a=evidence[anchor*count+p],b=evidence[int(i)*count+p];if(min(a.y,b.y)<.3||min(a.x,b.x)<=0)continue;
   float ratio=log(a.x/b.x);if(abs(ratio)>=8)continue;histogram[int(clamp((ratio+8)*16,0.,255.))]++;overlap++;}
  if(overlap<64){stats[i]=vec4(0);return;}int sum=0;float ratio=0;
  for(int k=0;k<256;++k){sum+=int(histogram[k]);if(sum>=overlap/2){ratio=(float(k)+.5)/16-8;break;}}
  int agree=0;for(int p=0;p<count;++p){vec2 a=evidence[anchor*count+p],b=evidence[int(i)*count+p];
   if(min(a.y,b.y)>=.3&&min(a.x,b.x)>0&&abs(log(a.x/b.x)-ratio)<.3)agree++;}
  stats[i]=vec4(exp(ratio),agree>=float(overlap)*.65?1:0,float(overlap),float(agree));return;}
 if(mode==1){if(i>=uint(count))return;float d[8];int n=0;
  for(int v=0;v<views;++v){vec2 e=evidence[v*count+int(i)];if(stats[v].y>0&&e.y>.3)d[n++]=e.x*stats[v].x;}
  result[i]=vec2(0);if(n==0)return;float m=median(d,n),sum=0,weight=0;int support=0;
  for(int v=0;v<views;++v){vec2 e=evidence[v*count+int(i)];e.x*=stats[v].x;
   if(stats[v].y>0&&e.y>.3&&abs(e.x/m-1)<.25){sum+=e.x*e.y;weight+=e.y;support++;}}
  if(weight>0)result[i]=vec2(sum/weight,views==1?weight*.65:min(1.,weight/2)*float(support)/float(n));return;}
 if(mode==2){if(i!=0)return;uint histogram[512];for(int k=0;k<512;++k)histogram[k]=0;
  int measured=0;float lo=1e20,hi=-1e20;for(int p=0;p<count;++p)if(result[p].y>=.3){float d=log(max(1e-20,result[p].x));lo=min(lo,d);hi=max(hi,d);measured++;}
  if(measured<64||measured<float(count)*.08||hi-lo<.01){stats[8]=vec4(0);return;}
  for(int p=0;p<count;++p)if(result[p].y>=.3)histogram[int(clamp((log(result[p].x)-lo)/(hi-lo)*511,0.,511.))]++;
  int sum=0;float low=lo,high=hi,middle=lo;
  for(int k=0;k<512;++k){int prev=sum;sum+=int(histogram[k]);float value=mix(lo,hi,(float(k)+.5)/512);
   if(prev<measured/20&&sum>=measured/20)low=value;if(prev<measured/2&&sum>=measured/2)middle=value;
   if(prev<measured*19/20&&sum>=measured*19/20)high=value;}
  low=exp(low);high=exp(high);stats[8]=vec4(low,high,float(measured)/float(count),high-low>=.15*exp(middle)?1:0);return;}
 if(mode==3){if(i>=uint(count))return;vec4 s=stats[8];result[i].x=s.w>0?clamp((result[i].x-s.x)/(s.y-s.x),0.,1.):0;return;}
 // Reconstruction gates use physical relative inverse depth before the
 // percentile display normalization clips its near/far tails.
 if(mode==4){if(i>=uint(count))return;vec2 fused=result[i];float value=fused.x;int support=0,validViews=0;
  for(int v=0;v<views;++v)if(stats[v].y>0){validViews++;vec2 e=evidence[v*count+int(i)];
   if(e.y>.3&&abs(e.x*stats[v].x/value-1)<.25)support++;}
  for(int v=0;v<views;++v){vec2 e=evidence[v*count+int(i)];rejected[v*count+int(i)]=
   validViews>=3&&support>=2&&fused.y>.6&&e.y>.5&&stats[v].y>0&&abs(e.x*stats[v].x/value-1)>.35?1:0;}return;}
})GLSL";
}
