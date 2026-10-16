#pragma once
namespace Raw::Bracketing::Depth::Shaders {
// Independently implemented symmetric Jacobi eigensolvers. Double precision
// is core in GL 4.3 and protects small-baseline normal equations. Image work
// stays float; these small systems do not require vendor-specific extensions.
inline constexpr auto GeometryMath=R"GLSL(
bool finiteD(double v){return !isnan(v)&&!isinf(v);}
dmat3 eig3(dmat3 inputMatrix,out dvec3 values){
 // Scalar arrays avoid broken dynamic double-vector writes on some GL 4.3
 // drivers. The same Jacobi rotations are used as in the nine-value solve.
 double a[9],v[9];for(int y=0;y<3;++y)for(int x=0;x<3;++x){a[y*3+x]=inputMatrix[x][y];v[y*3+x]=x==y?1:0;}
 for(int it=0;it<16;++it)for(int p=0;p<2;++p)for(int q=p+1;q<3;++q){
  double b=a[p*3+q];if(abs(b)<1e-25)continue;
  double tau=(a[q*3+q]-a[p*3+p])/(2*b),t=sign(tau)/(abs(tau)+sqrt(1+tau*tau));if(t==0)t=1;
  double c=1/sqrt(1+t*t),s=t*c;a[p*3+p]-=t*b;a[q*3+q]+=t*b;a[p*3+q]=a[q*3+p]=0;
  for(int k=0;k<3;++k)if(k!=p&&k!=q){double x=a[k*3+p],y=a[k*3+q];
   a[k*3+p]=a[p*3+k]=c*x-s*y;a[k*3+q]=a[q*3+k]=s*x+c*y;}
  for(int k=0;k<3;++k){double x=v[k*3+p],y=v[k*3+q];v[k*3+p]=c*x-s*y;v[k*3+q]=s*x+c*y;}}
 int order[3]=int[3](0,1,2);for(int i=0;i<2;++i)for(int j=i+1;j<3;++j)
  if(a[order[j]*4]>a[order[i]*4]){int k=order[i];order[i]=order[j];order[j]=k;}
 int x=order[0],y=order[1],z=order[2];values=dvec3(a[x*4],a[y*4],a[z*4]);
 return dmat3(dvec3(v[x],v[3+x],v[6+x]),dvec3(v[y],v[3+y],v[6+y]),dvec3(v[z],v[3+z],v[6+z]));}
void svd3(dmat3 a,out dmat3 u,out dvec3 s,out dmat3 v){dvec3 e;v=eig3(transpose(a)*a,e);s=sqrt(max(e,dvec3(0)));
 u=dmat3(1);if(s.x>1e-18)u[0]=normalize(a*v[0]);
 if(s.y>1e-18)u[1]=normalize(a*v[1]-dot(a*v[1],u[0])*u[0]);
 else u[1]=normalize(cross(u[0],abs(u[0].x)<.8?dvec3(1,0,0):dvec3(0,1,0)));
 u[2]=normalize(cross(u[0],u[1]));}
// All matrices use GLSL column-major layout. F maps reference rays to source
// epipolar lines: b^T F a = 0.
dmat3 rank2(dmat3 f){dmat3 u,v;dvec3 s;svd3(f,u,s,v);return outerProduct(u[0],v[0])*s.x+outerProduct(u[1],v[1])*s.y;}
void smallest9(inout double a[81],out double result[9]){
 double v[81];for(int i=0;i<81;++i)v[i]=(i%10==0)?1:0;
 for(int sweep=0;sweep<24;++sweep){double off=0;
  for(int p=0;p<8;++p)for(int q=p+1;q<9;++q){double b=a[p*9+q];off+=abs(b);if(abs(b)<1e-22)continue;
   double tau=(a[q*9+q]-a[p*9+p])/(2*b);double t=sign(tau)/(abs(tau)+sqrt(1+tau*tau));if(t==0)t=1;
   double c=1/sqrt(1+t*t),s=t*c,ap=a[p*9+p],aq=a[q*9+q];
   a[p*9+p]=ap-t*b;a[q*9+q]=aq+t*b;a[p*9+q]=0;a[q*9+p]=0;
   for(int k=0;k<9;++k)if(k!=p&&k!=q){double x=a[k*9+p],y=a[k*9+q];
    a[k*9+p]=a[p*9+k]=c*x-s*y;a[k*9+q]=a[q*9+k]=s*x+c*y;}
   for(int k=0;k<9;++k){double x=v[k*9+p],y=v[k*9+q];v[k*9+p]=c*x-s*y;v[k*9+q]=s*x+c*y;}}
  if(off<1e-20)break;}
 int index=0;for(int i=1;i<9;++i)if(a[i*9+i]<a[index*9+index])index=i;
 for(int i=0;i<9;++i)result[i]=v[i*9+index];}
double sampson(dmat3 f,dvec2 a,dvec2 b){dvec3 x=dvec3(a,1),y=dvec3(b,1),fx=f*x,fy=transpose(f)*y;
 double d=dot(y,fx);return d*d/max(1e-24,dot(fx.xy,fx.xy)+dot(fy.xy,fy.xy));}
vec3 triangulate(dmat3 r,dvec3 t,double f,double g,dvec2 a,dvec2 b){
 dvec3 ray=dvec3(a/f,1),other=dvec3(b/g,1),rot=r*ray,c=cross(other,rot),off=cross(other,t);
 double den=dot(c,c);if(den<1e-22||rot.z<=0)return vec3(0);
 double z=-dot(c,off)/den;dvec3 p=z*rot+t;if(z<=0||p.z<=0||!finiteD(z))return vec3(0);
 double err=length(g*p.xy/p.z-b),shift=length(g*rot.xy/rot.z-b);
 if(!finiteD(err))return vec3(0);return vec3(1/z,shift,err);}
)GLSL";
}
