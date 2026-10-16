#pragma once
namespace Raw::Bracketing::Depth::Shaders {
inline constexpr auto Pose=R"GLSL(
layout(local_size_x=1) in;
struct Track {vec4 xy;vec4 quality;};struct Model {vec4 col[3];vec4 stats;};
struct Pose {vec4 rotation[3];vec4 translation;vec4 camera;vec4 stats;};
layout(std430,binding=0) readonly buffer Input {Track tracks[];};
layout(std430,binding=1) readonly buffer Models {Model models[];};
layout(std430,binding=2) buffer Poses {Pose poses[];};
uniform int count,mode;uniform vec2 size;
dvec4 point(int i){return (dvec4(tracks[i].xy)-dvec4((size-1)*.5,(size-1)*.5))/double(max(size.x,size.y));}
dmat3 matrix(){return dmat3(models[1536].col[0].xyz,models[1536].col[1].xyz,models[1536].col[2].xyz);}
dmat3 camera(double f){return dmat3(f,0,0,0,f,0,0,0,1);}
double essentialError(dmat3 f,float lf,float ratio){double a=double(exp(lf)),b=a*double(exp(ratio));
 dmat3 e=camera(b)*f*camera(a);dvec3 values;eig3(transpose(e)*e,values);dvec2 s=sqrt(max(values.xy,dvec2(0)));
 return (s.x-s.y)/max(1e-20,s.x+s.y);}
double focalSquared(dmat3 f,dvec3 e){return -f[2][2]*(e.x*f[2][1]-e.y*f[2][0])/
 (e.x*(f[0][1]*f[0][2]+f[1][1]*f[1][2])-e.y*(f[0][0]*f[0][2]+f[1][0]*f[1][2]));}
vec3 depthAt(Pose p,int i){dvec4 q=point(i);return triangulate(dmat3(p.rotation[0].xyz,p.rotation[1].xyz,p.rotation[2].xyz),
 dvec3(p.translation.xyz),double(p.camera.x),double(p.camera.y),q.xy,q.zw);}
void main(){int id=int(gl_GlobalInvocationID.x);float pixels=max(size.x,size.y);dmat3 f=matrix();
 if(mode==1){if(id!=0)return;int best=-1;float score=-1;
  for(int i=0;i<35;++i)if(poses[i].stats.y>0){float s=poses[i].camera.z/(1+poses[i].camera.w*4);if(s>score){score=s;best=i;}}
  poses[35].stats=vec4(0);if(best<0)return;Pose chosen=poses[best];float agreement=1;int plausible=0;
  for(int j=0;j<35;++j){Pose p=poses[j];if(p.stats.y<=0||p.camera.w>chosen.camera.w+.15||p.camera.z<chosen.camera.z*.8)continue;
   double mean=0;float n=0;
   for(int i=0;i<count;++i)if(tracks[i].quality.x>.4){vec3 a=depthAt(chosen,i),b=depthAt(p,i);
    if(min(a.x,b.x)>0&&max(a.z,b.z)*pixels<.5){mean+=double(log(a.x/b.x));n++;}}
   if(n<chosen.camera.z*.75){poses[35].stats.z=1;return;}mean/=double(n);float good=0;double deviation=0;
   for(int i=0;i<count;++i)if(tracks[i].quality.x>.4){vec3 a=depthAt(chosen,i),b=depthAt(p,i);
    if(min(a.x,b.x)>0&&max(a.z,b.z)*pixels<.5){float d=abs(log(a.x/b.x)-float(mean));good+=d<.2?1:0;deviation+=double(d);}}
   if(good<n*.85||deviation/double(n)>.1){poses[35].stats.z=2;return;}
   agreement=min(agreement,1-float(deviation/double(n))*3);plausible++;}
  poses[35]=chosen;poses[35].stats.w=agreement*(plausible<2?.8:1.);return;}
 poses[id].stats=vec4(0);
 vec4 counts=models[1536].stats;if(counts.w<40||counts.x<counts.w*.65||counts.z>=counts.w*.85)return;
 float diagonal=length(size)/pixels,lo=log(diagonal*.15),hi=log(diagonal*20),limit=log(1.5);
 double a=0,b=0;
 if(id==34){dmat3 u,v;dvec3 s;svd3(f,u,s,v);a=sqrt(focalSquared(f,u[2]));b=sqrt(focalSquared(transpose(f),v[2]));}
 else {float lf=mix(lo,hi,float(id%17)/16),ratio=0,fs=(hi-lo)/32,rs=limit/4;double best=essentialError(f,lf,ratio);
  for(int it=0;it<28;++it){float nf=lf,nr=ratio;
   for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y){if(id<17&&x!=0)continue;
    float tf=clamp(lf+float(x)*fs,lo,hi),tr=clamp(ratio+float(y)*rs,-limit,limit);double e=essentialError(f,tf,tr);
    if(e<best){best=e;nf=tf;nr=tr;}}
   if(nf==lf&&nr==ratio){fs*=.5;rs*=.5;}lf=nf;ratio=nr;}
  if(best>.02||lf<=lo+.001||lf>=hi-.001||abs(ratio)>=limit-.001)return;a=double(exp(lf));b=a*double(exp(ratio));}
 if(!finiteD(a)||!finiteD(b)||a<=double(exp(lo))||a>=double(exp(hi))||b/a<1/1.5||b/a>1.5)return;
 dmat3 u,v;dvec3 singular;svd3(camera(b)*f*camera(a),u,singular,v);if(determinant(v)<0)v[2]=-v[2];
 dmat3 w=dmat3(0,1,0,-1,0,0,0,0,1);Pose candidate;float bestCount=0;
 for(int choice=0;choice<4;++choice){dmat3 r=u*(choice<2?w:transpose(w))*transpose(v);dvec3 t=u[2]*(choice%2==0?1:-1);
  float valid=0,sumError=0,sumShift=0;
  for(int i=0;i<count;++i)if(tracks[i].quality.x>.4){dvec4 q=point(i);if(sampson(f,q.xy,q.zw)*double(pixels*pixels)>.25)continue;
   vec3 d=triangulate(r,t,a,b,q.xy,q.zw);if(d.x>0&&d.z*pixels<.5&&d.y*pixels>.2){valid++;sumError+=d.z*pixels;sumShift+=min(32.,d.y*pixels);}}
  if(valid>bestCount){bestCount=valid;for(int k=0;k<3;++k)candidate.rotation[k]=vec4(r[k],0);
   candidate.translation=vec4(t,0);candidate.camera=vec4(a,b,valid,sumError/max(1.,valid));candidate.stats=vec4(sumShift/max(1.,valid),1,0,1);}}
 if(bestCount<30||bestCount<counts.x*.6||candidate.stats.x<.3125||candidate.camera.w>.25)return;
 poses[id]=candidate;
})GLSL";
}
