#pragma once
namespace Raw::Bracketing::Depth::Shaders {
// Complete Bayer cells, area averaged before sqrt variance stabilization.
// These guides serve correspondence only; original RAW values supply output.
inline constexpr auto Guide=R"GLSL(#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) readonly buffer Input {float inputData[];};
layout(std430,binding=1) buffer Output {float result[];};
uniform ivec2 tileSize,tileOrigin,guideSize;
uniform int stride;
void main(){ivec2 q=ivec2(gl_GlobalInvocationID.xy),p=q*stride;
 if(any(greaterThanEqual(p,tileSize)))return;
 ivec2 dst=(tileOrigin+p)/stride;if(any(greaterThanEqual(dst,guideSize)))return;
 float value=0,weight=0;int n=tileSize.x*tileSize.y;
 for(int y=0;y<stride;++y)for(int x=0;x<stride;++x){ivec2 s=p+ivec2(x,y);
  if(any(greaterThanEqual(s,tileSize)))continue;int i=s.y*tileSize.x+s.x;
  float v=inputData[i]*inputData[n+i];
  if(inputData[2*n+i]!=0||isnan(v)||isinf(v))continue;
  value+=v;weight+=1;}
 result[dst.y*guideSize.x+dst.x]=weight>=float(stride*stride)*.75?sqrt(max(0.,value/weight)):-1.;
})GLSL";
inline constexpr auto Pyramid=R"GLSL(#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) readonly buffer Input {float source[];};
layout(std430,binding=1) buffer Output {float result[];};
uniform ivec2 size;uniform int srcOffset,dstOffset;
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy),outSize=size/2;
 if(any(greaterThanEqual(p,outSize)))return;
 float sum=0,weight=0;const float k[5]=float[5](1,4,6,4,1);
 // Pixel-center convention: next-level center is 2*p+0.5.
 for(int y=-2;y<=3;++y)for(int x=-2;x<=3;++x){
  float wx=(x>=-2&&x<=2?k[x+2]:0)+(x>=-1&&x<=3?k[x+1]:0);
  float wy=(y>=-2&&y<=2?k[y+2]:0)+(y>=-1&&y<=3?k[y+1]:0);
  ivec2 q=clamp(2*p+ivec2(x,y),ivec2(0),size-1);float v=source[srcOffset+q.y*size.x+q.x];
  if(v>=0){sum+=wx*wy*v;weight+=wx*wy;}}
 result[dstOffset+p.y*outSize.x+p.x]=weight>=768?sum/weight:-1.;
})GLSL";
}
