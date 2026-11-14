#pragma once
// Deliberately embedded, owner-local shaders: no shader file I/O during startup.
namespace Stack::Startup::Shaders {
inline constexpr const char* Fullscreen=R"GLSL(#version 430 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}
)GLSL";
inline constexpr const char* Background=R"GLSL(#version 430 core
in vec2 uv;out vec4 color;
layout(binding=0) uniform sampler2D desktop;
layout(binding=2) uniform sampler2D sharpDesktop;
layout(location=2) uniform float intro;
layout(location=0) uniform float seconds;
layout(location=1) uniform float aspect;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+1.),f.x),f.y);}
void main(){
 vec2 p=uv;float t=seconds;
 vec3 sky=mix(vec3(.018,.036,.043),vec3(.07,.12,.13),smoothstep(0.,.75,p.y));
 vec3 captured=texture(desktop,vec2(p.x,1.-p.y)).rgb;
 float fog=noise(p*vec2(4.,6.)+vec2(t*.035,0))*.65+noise(p*14.-vec2(t*.019,0))*.35;
 vec3 base=mix(captured*.78,sky,.12+fog*.12);
 float q=pow(clamp(t/.85,0.,1.),.78);
 vec3 anchor=mix(vec3(-2.8,.3,-3.4),vec3(.8,1.09,0),q);
 anchor.y+=4.*q*(1.-q)*1.15;
 vec3 v=anchor-vec3(0,1.12,2.5+max(0.,1.55/aspect-1.)*2.5);
 v.yz=mat2(cos(.105),sin(.105),-sin(.105),cos(.105))*v.yz;
 vec2 focus=vec2(v.x*1.5/aspect,v.y*1.5)/-v.z*.5+.5;
 base*=1.-.28*exp(-dot((p-focus)*vec2(aspect,1.),(p-focus)*vec2(aspect,1.))*12.);
 float ocean=smoothstep(2.,2.9,t)*(1.-smoothstep(.46,.57,p.y));
 float depth=max(.02,.56-p.y);
 float waves=sin(p.y*180.+sin(p.x*16.+t*.3)*2.+noise(p*22.)*4.-t*1.3)*.5+.5;
 float fine=sin(p.y*410.+p.x*40.+t*.9)*.5+.5;
 float reflectX=.64+sin(p.y*130.-t)*.012;
 float reflection=exp(-pow((p.x-reflectX)/(.016+depth*.2),2.))*(.3+waves*.7)*(.3+fine*.7);
 float ring=sin(length((p-vec2(.67,.29))*vec2(aspect,2.))*90.-(t-2.)*12.);
 float ripple=pow(max(0.,ring),12.)*exp(-length(p-vec2(.67,.29))*8.)*smoothstep(2.,2.35,t);
 vec3 sea=vec3(.023,.065,.072)+vec3(.012,.024,.025)*waves+reflection*vec3(.34,.16,.035)+ripple*.025;
 base=mix(base,sea,ocean*.58);
 float reveal=smoothstep(1.6,2.9,t);
 base=mix(base,sky,reveal*smoothstep(.45,.65,p.y)*.18);
 float mist=exp(-pow((p.y-.55)/.09,2.))*(.025+fog*.045)*reveal;
 base+=vec3(.65,.77,.77)*mist;
 base*=1.-.25*length((p-.5)*vec2(1.,.8));
 base+=(hash(floor(p*vec2(1920,1080)))-.5)*.009;
 color=vec4(mix(texture(sharpDesktop,vec2(p.x,1.-p.y)).rgb,base,intro),1);
}
)GLSL";
inline constexpr const char* Vertex=R"GLSL(#version 430 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec2 kind;
layout(location=0) uniform float seconds;
layout(location=1) uniform float aspect;
out vec3 world;out vec3 n;out vec2 uv;out vec2 type;
mat3 rz(float a){float c=cos(a),s=sin(a);return mat3(c,s,0,-s,c,0,0,0,1);}
mat3 ry(float a){float c=cos(a),s=sin(a);return mat3(c,0,-s,0,1,0,s,0,c);}
void main(){
 vec3 p=position;mat3 rot=mat3(1);type=kind;uv=texcoord;
 if(kind.x<1.5){
  float q=pow(clamp(seconds/.85,0.,1.),.78);float after=max(0.,seconds-.85);
  float swing=.68*sin(after*10.)*exp(-after*2.7);
  vec3 pivot=vec3(0,.51,0); // inner top of handle meets the peg
  if(kind.x<.5 && seconds>=.85){
    mat3 body=rz(.22*sin(after*16.)*exp(-after*3.8));
    p=body*(p-vec3(0,.353,0))+vec3(0,.353,0);rot=body;
  }
  mat3 turn=rz((1.-q)*-3.8+swing)*ry(-.28+(1.-q)*.7);
   vec3 anchor=mix(vec3(-2.8,.3,-3.4),vec3(.8,1.09,0),q);
   anchor.y+=4.*q*(1.-q)*1.15;
   anchor+=turn*vec3(0,.81,0);
   if(seconds>=.85) anchor=vec3(.8,1.9,0);
  p=turn*((p-pivot)*3.)+anchor;rot=turn*rot;
 }
 world=p;n=rot*normal;
 // Fixed 18mm-equivalent vertical field of view, retreat for narrow windows.
 vec3 camera=vec3(0,1.12,2.5+max(0.,1.55/aspect-1.)*2.5);
 vec3 v=p-camera;float pitch=.105;
 v.yz=mat2(cos(pitch),sin(pitch),-sin(pitch),cos(pitch))*v.yz;
 float near=.05,far=50.;
 gl_Position=vec4(v.x*1.5/aspect,v.y*1.5,-(far+near)/(far-near)*v.z-2.*far*near/(far-near),-v.z);
}
)GLSL";
inline constexpr const char* Material=R"GLSL(#version 430 core
in vec3 world;in vec3 n;in vec2 uv;in vec2 type;out vec4 color;
layout(binding=1) uniform sampler2D albedo;
layout(location=0) uniform float seconds;
float hash(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
float pigment(vec3 p){
 vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
 mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+1.),f.x),f.y),f.z);
}
void main(){
 if(seconds<=0.) discard;
 float t=seconds;float grain=hash(floor(world*180.));
 float paint=pigment(world*13.)*.7+pigment(world*41.)*.3;
 float arrival=0.;
 if(type.x>1.5){
   if(t<.85) discard;
   if(type.x>2.5) arrival=.85;
   else if(world.x>1.08 && world.z>-.2) arrival=.85+(1.95-world.y)*.26;
   else arrival=1.3+max(0.,-world.z)*.09+abs(world.x-1.1)*.09+max(0.,.12-world.y)*.23;
   arrival+=(paint-.5)*.16;
   if(type.x<2.5 && t<max(.85,arrival)) discard;
 }
 float q=pow(clamp(t/.85,0.,1.),.78);
 vec3 norm=normalize(n);vec3 lamp=mix(vec3(-2.8,.3,-3.4),vec3(.8,1.05,0),q);
 lamp.y+=4.*q*(1.-q)*1.15;
 vec3 delta=lamp-world;float light=max(0.,dot(norm,normalize(delta)))/(1.+dot(delta,delta)*.8);
 float ambient=.30+.28*max(0.,dot(norm,normalize(vec3(-.5,1,1))));
 vec3 base;
 if(type.x<1.5){
   base=texture(albedo,uv).rgb;
   if(type.y>.5) base=vec3(.35,.29,.17);
 }else{
   float wood=sin(world.x*97.+sin(world.z*3.7)*2.+sin(world.x*19.)*3.);
   base=type.x>2.5?vec3(.16,.19,.19):vec3(.22,.18,.13)*( .8+wood*.08+paint*.14);
 }
 vec3 lit=base*(vec3(.55,.76,.8)*ambient+vec3(1.5,.85,.32)*light);
 if(type.x<1.5 && type.y>1.5)lit=vec3(1.,.85,.38);
 if(type.x>1.5)lit*=mix(.5,1.,smoothstep(0.,.14,t-arrival));
 float fog=type.x<1.5?1.-smoothstep(0.,.62,t):1.-exp(-max(0.,-world.z)*.055);
 lit=mix(lit,vec3(.055,.10,.11),fog*.85);
 float alpha=type.x<1.5?smoothstep(0.,.20,t):1.;
 if(type.x<1.5 && type.y>.5 && type.y<1.5){
   lit=vec3(.4,.30,.16)+vec3(.2,.11,.03)*grain;
   alpha=(.13+.16*pow(1.-abs(norm.z),3.))*smoothstep(0.,.20,t);
 }
 color=vec4(lit,alpha);
}
)GLSL";
inline constexpr const char* Finish=R"GLSL(#version 430 core
in vec2 uv;out vec4 color;
layout(location=0) uniform float seconds;
layout(location=1) uniform float aspect;
layout(location=2) uniform float handoff;
void main(){
 float q=pow(clamp(seconds/.85,0.,1.),.78);
 vec3 a=mix(vec3(-2.8,.3,-3.4),vec3(.8,1.09,0),q);a.y+=4.*q*(1.-q)*1.15;
 vec3 v=a-vec3(0,1.12,2.5+max(0.,1.55/aspect-1.)*2.5);
 v.yz=mat2(cos(.105),sin(.105),-sin(.105),cos(.105))*v.yz;
 vec2 center=vec2(v.x*1.5/aspect,v.y*1.5)/-v.z*.5+.5;
 float d=length((uv-center)*vec2(aspect,1.));
 float glow=exp(-d*d*140.)*.24*smoothstep(0.,.65,seconds);
 float veil=sin(clamp(handoff,0.,1.)*3.14159265)*.25;
 color=vec4(mix(vec3(1.,.52,.16),vec3(.08,.12,.13),veil),clamp(glow+veil,0.,.85));
}
)GLSL";
}
