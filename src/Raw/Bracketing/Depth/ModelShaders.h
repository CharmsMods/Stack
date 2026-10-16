#pragma once
namespace Raw::Bracketing::Depth::Shaders {
inline constexpr auto Model=R"GLSL(
layout(local_size_x=1) in;
struct Track {vec4 xy;vec4 quality;};
struct Model {vec4 col[3];vec4 stats;};
layout(std430,binding=0) readonly buffer Input {Track tracks[];};
layout(std430,binding=1) buffer Models {Model models[];};
uniform int count,mode,first;uniform vec2 size;
uint random(inout uint state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
dvec4 normalized(int i){return (dvec4(tracks[i].xy)-dvec4((size-1)*.5,(size-1)*.5))/double(max(size.x,size.y));}
dmat3 getModel(int i){return dmat3(models[i].col[0].xyz,models[i].col[1].xyz,models[i].col[2].xyz);}
double residual(dmat3 m,dvec4 p,bool plane){if(!plane)return sampson(m,p.xy,p.zw);
 dvec3 q=m*dvec3(p.xy,1);if(abs(q.z)<1e-12)return 1e10;return dot(q.xy/q.z-p.zw,q.xy/q.z-p.zw);}
void accumulate(inout double a[81],double row[9],double weight){for(int y=0;y<9;++y)for(int x=0;x<9;++x)a[y*9+x]+=weight*row[y]*row[x];}
dmat3 fit(int id,bool plane,bool refine){
 double ata[81];for(int i=0;i<81;++i)ata[i]=0;
 uint state=uint(id+1)*747796405u;int samples=refine?count:(plane?4:8),valid=0;int selected[8];
 dmat3 old=refine?getModel(1536):dmat3(0);
 if(refine&&dot(old[0],old[0])+dot(old[1],old[1])+dot(old[2],old[2])<1e-20)return dmat3(0);
 for(int j=0;j<samples;++j){int index=j;
  if(!refine){index=-1;for(int retry=0;retry<128;++retry){int trial=int(random(state)%uint(count));bool duplicate=false;
    for(int k=0;k<j;++k)duplicate=duplicate||selected[k]==trial;
    if(tracks[trial].quality.x>.4&&!duplicate){index=trial;break;}}if(index<0)return dmat3(0);selected[j]=index;}
  if(tracks[index].quality.x<=.4)continue;dvec4 p=normalized(index);double weight=1;
  if(refine){double d=residual(old,p,false)*double(max(size.x,size.y)*max(size.x,size.y));if(d>.25)continue;weight=1/(1+d*16);}
  double row[9];
  if(plane){row=double[9](p.x,p.y,1,0,0,0,-p.z*p.x,-p.z*p.y,-p.z);accumulate(ata,row,weight);
   row=double[9](0,0,0,p.x,p.y,1,-p.w*p.x,-p.w*p.y,-p.w);}
  else row=double[9](p.z*p.x,p.z*p.y,p.z,p.w*p.x,p.w*p.y,p.w,p.x,p.y,1);
  accumulate(ata,row,weight);valid++;}
 if(valid<8&&(!plane||valid<4))return dmat3(0);
 double f[9];smallest9(ata,f);dmat3 m=transpose(dmat3(f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8]));
 return plane?m:rank2(m);}
void main(){int id=int(gl_GlobalInvocationID.x)+first;
 if(mode==1){if(id!=0)return;int f=0,h=1024;
  for(int i=1;i<1024;++i)if(models[i].stats.y>models[f].stats.y)f=i;
  for(int i=1025;i<1536;++i)if(models[i].stats.y>models[h].stats.y)h=i;
  models[1536]=models[f];models[1536].stats.z=models[h].stats.x;return;}
 bool plane=id>=1024&&mode==0;dmat3 m=fit(id,plane,mode==2);double scale=double(max(size.x,size.y));
 float inliers=0,score=0,total=0;
 bool nonzero=dot(m[0],m[0])+dot(m[1],m[1])+dot(m[2],m[2])>1e-20;
 for(int i=0;i<count;++i)if(tracks[i].quality.x>.4){total++;double e=nonzero?residual(m,normalized(i),plane)*scale*scale:1e20;
  if(e<(plane?.1225:.25)){inliers++;score+=float(1-e/(plane?.1225:.25));}}
 int dst=mode==2?1536:id;float planar=mode==2?models[1536].stats.z:0;
 for(int c=0;c<3;++c)models[dst].col[c]=vec4(m[c],0);models[dst].stats=vec4(inliers,score,planar,total);
})GLSL";
}
