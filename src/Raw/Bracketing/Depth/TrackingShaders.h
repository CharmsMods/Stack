#pragma once
namespace Raw::Bracketing::Depth::Shaders {
// Shi & Tomasi (CVPR 1994): select the smaller structure-tensor eigenvalue.
// Wronski et al. (SIGGRAPH 2019), section 3.2: pyramid/block initialization
// followed by subpixel LK. Brightness elimination follows the linear
// appearance formulation discussed by Baker & Matthews (IJCV 2004).
// This implementation adds an independent reverse-track closure test.
inline constexpr auto Track=R"GLSL(#version 430 core
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Reference {float ref[];};
layout(std430,binding=1) readonly buffer Alternate {float alt[];};
// Match coordinates are in guide pixels. quality=(confidence,NCC,closure,lambda).
struct Track {vec4 xy;vec4 quality;};
layout(std430,binding=2) buffer Tracks {Track tracks[];};
layout(std430,binding=3) readonly buffer Seeds {Track seeds[];};
uniform ivec2 seedGrid;
uniform ivec2 size,grid;uniform int offsets[5];uniform int dense,first;
float pixel(bool reverse,ivec2 p,int level){ivec2 s=size>>level;p=clamp(p,ivec2(0),s-1);
 int i=offsets[level]+p.y*s.x+p.x;return reverse?alt[i]:ref[i];}
float sampleImage(bool reverse,vec2 p,int level){ivec2 q=ivec2(floor(p));vec2 f=fract(p);
 float a=pixel(reverse,q,level),b=pixel(reverse,q+ivec2(1,0),level),c=pixel(reverse,q+ivec2(0,1),level),d=pixel(reverse,q+1,level);
 if(min(min(a,b),min(c,d))<0)return -1;
 return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);}
bool inside(vec2 p,int level){vec2 s=vec2(size>>level);return all(greaterThan(p,vec2(7)))&&all(lessThan(p,s-8));}
float corner(vec2 p){mat2 h=mat2(0);
 for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){vec2 q=p+vec2(x,y);
  vec2 g=.5*vec2(sampleImage(false,q+vec2(1,0),0)-sampleImage(false,q-vec2(1,0),0),
   sampleImage(false,q+vec2(0,1),0)-sampleImage(false,q-vec2(0,1),0));h+=outerProduct(g,g);}
 float tr=h[0][0]+h[1][1];return .5*(tr-sqrt(max(0.,tr*tr-4*determinant(h))));}
float correlation(bool reverse,vec2 a,vec2 b,int level){
 float sa=0,sb=0,aa=0,bb=0,ab=0,n=0;
 for(int y=-4;y<=4;++y)for(int x=-4;x<=4;++x){vec2 d=vec2(x,y);
  float v=sampleImage(reverse,a+d,level),w=sampleImage(!reverse,b+d,level);if(v<0||w<0)continue;
  sa+=v;sb+=w;aa+=v*v;bb+=w*w;ab+=v*w;n++;}
 if(n<65)return -1;return (ab-sa*sb/n)/sqrt(max(1e-12,(aa-sa*sa/n)*(bb-sb*sb/n)));}
// Forward-additive LK with local affine brightness eliminated each iteration.
// Normalized patches tolerate exposure differences; robust weights limit
// isolated RAW noise and focus/occlusion mismatches. No EXIF exposure is used.
bool refine(bool reverse,vec2 a,inout vec2 b,int level,out float lambda){
 lambda=0;
 for(int it=0;it<20;++it){if(!inside(a,level)||!inside(b,level))return false;
  float sa=0,sb=0,aa=0,bb=0,ab=0,n=0;
  for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x){vec2 d=vec2(x,y);
   float v=sampleImage(reverse,a+d,level),w=sampleImage(!reverse,b+d,level);if(v<0||w<0)continue;
   sa+=v;sb+=w;aa+=v*v;bb+=w*w;ab+=v*w;n++;}
  if(n<100)return false;
  float gain=clamp((ab-sa*sb/n)/max(1e-8,bb-sb*sb/n),.25,4.),bias=sa/n-gain*sb/n;
  float sigma=max(.005,sqrt(max(0.,(aa-sa*sa/n)/n))*.25);
  mat2 h=mat2(0);vec2 rhs=vec2(0);
  for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x){vec2 d=vec2(x,y),q=b+d;
   float v=sampleImage(reverse,a+d,level),w=sampleImage(!reverse,q,level);if(v<0||w<0)continue;
   vec2 g=.5*gain*vec2(sampleImage(!reverse,q+vec2(1,0),level)-sampleImage(!reverse,q-vec2(1,0),level),
    sampleImage(!reverse,q+vec2(0,1),level)-sampleImage(!reverse,q-vec2(0,1),level));
   float r=v-(gain*w+bias),weight=min(1.,sigma/max(1e-8,abs(r)));
   h+=weight*outerProduct(g,g);rhs+=weight*g*r;}
  float tr=h[0][0]+h[1][1];lambda=.5*(tr-sqrt(max(0.,tr*tr-4*determinant(h))));
  if(lambda<1e-6||determinant(h)<1e-12)return false;
  vec2 delta=inverse(h)*rhs;float len=length(delta);if(len>2)delta*=2/len;b+=delta;
  if(dot(delta,delta)<.000025)break;
 }return true;}
void main(){uint id=gl_GlobalInvocationID.x+uint(first);if(id>=uint(grid.x*grid.y))return;
 ivec2 cell=ivec2(int(id)%grid.x,int(id)/grid.x);
 vec2 a=(vec2(cell)+.5)*vec2(size)/vec2(grid)-.5;
 float score=corner(a);
 if(dense==0){vec2 center=a;for(int y=-6;y<=6;y+=2)for(int x=-6;x<=6;x+=2){vec2 q=center+vec2(x,y);float s=corner(q);if(s>score){score=s;a=q;}}}
 tracks[id].xy=vec4(a,a);tracks[id].quality=vec4(0);
 if(!inside(a,0)||score<(dense==0?.00002:.000002))return;
 int top=3;while(top>0&&!inside((a+.5)/float(1<<top)-.5,top))--top;
 vec2 displacement=vec2(0);
 if(dense!=0){top=1;float bestScore=-2;ivec2 cell=ivec2((a+.5)*vec2(seedGrid)/vec2(size));
  for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 c=clamp(cell+ivec2(x,y),ivec2(0),seedGrid-1);Track s=seeds[c.y*seedGrid.x+c.x];
   if(s.quality.x<=.4)continue;vec2 d=s.xy.zw-s.xy.xy;float score=correlation(false,(a+.5)*.5-.5,(a+d+.5)*.5-.5,1);
   if(score>bestScore){bestScore=score;displacement=d;}}
  if(bestScore<.5)return;}
 vec2 b=(a+displacement+.5)/float(1<<top)-.5,ap=(a+.5)/float(1<<top)-.5,best=b;float ncc=-2;
 int search=dense==0?8:2;
 for(int y=-search;y<=search;++y)for(int x=-search;x<=search;++x){vec2 q=b+vec2(x,y);if(!inside(q,top))continue;
  float c=correlation(false,ap,q,top);if(c>ncc){ncc=c;best=q;}}
 b=best;float lambda=0;
 for(int level=top;level>=0;--level){ap=(a+.5)/float(1<<level)-.5;
  if(level<top)b=(b+.5)*2-.5;if(!refine(false,ap,b,level,lambda))return;}
 ncc=correlation(false,a,b,0);vec2 back=a;float ignored;
 if(!refine(true,b,back,0,ignored))return;
 float closure=length(back-a);
 tracks[id].xy=vec4(a,b);tracks[id].quality=vec4(0,ncc,closure,lambda);
 if(ncc<.85||closure>.5||lambda<.00003)return;
 tracks[id].quality.x=clamp((ncc-.8)/.15,0.,1.)*exp(-2*closure*closure);
})GLSL";
}
