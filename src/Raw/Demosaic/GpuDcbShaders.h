#pragma once
// GPU adaptation of Jacek Gozdz's 2010 DCB and Luis Sanz Rodriguez's color
// refinement in the BSD-3-Clause LibRaw implementation. Full copyright and
// license: legal/third_party/DCB-BSD-3-Clause.txt. Causal sweeps preserve the
// original row-major dependency direction through converged GPU iterations.
namespace Raw::Demosaic::Shaders {
inline constexpr const char* DcbInitialize = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=vec4(0); v[cfa(p)]=65535*raw(p);
    if(!interior(p,6)) for(int c=0;c<3;++c) {
        if(c==cfa(p)) continue;
        float sum=0,n=0;
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) if(cfa(p+ivec2(x,y))==c) {
            sum+=raw(p+ivec2(x,y)); ++n;
        }
        v[c]=65535*sum/n;
    }
    return v;
}
)GLSL";
inline constexpr const char* DcbCandidate = R"GLSL(
float green(ivec2 p) {
    if(!interior(p,2) || cfa(p)==1) return 0;
    ivec2 d=mode==0?ivec2(1,0):ivec2(0,1);
    return (a(p-d).g+a(p+d).g)/2;
}
vec4 evaluate(ivec2 p) {
    vec4 v=vec4(0);v.g=green(p);
    if(!interior(p,1)) return v;
    if(cfa(p)!=1) {
        int c=2-cfa(p);
        v[c]=(4*v.g-green(p+ivec2(1,1))-green(p+ivec2(-1,1))-green(p+ivec2(1,-1))-green(p+ivec2(-1,-1))+
            a(p+ivec2(1,1))[c]+a(p+ivec2(-1,1))[c]+a(p+ivec2(1,-1))[c]+a(p+ivec2(-1,-1))[c])/4;
    } else {
        int c=cfa(p+ivec2(1,0)),d=2-c;
        v[c]=(a(p+ivec2(-1,0))[c]+a(p+ivec2(1,0))[c])/2;
        v[d]=(a(p+ivec2(0,-1))[d]+a(p+ivec2(0,1))[d])/2;
        if(mode==0) v[d]=(2*v.g-green(p+ivec2(0,1))-green(p+ivec2(0,-1))+a(p+ivec2(0,1))[d]+a(p+ivec2(0,-1))[d])/2;
        else v[c]=(2*v.g-green(p+ivec2(1,0))-green(p+ivec2(-1,0))+a(p+ivec2(1,0))[c]+a(p+ivec2(-1,0))[c])/2;
    }
    return v;
}
)GLSL";
inline constexpr const char* DcbDecide = R"GLSL(
precise float variation(sampler2D t,ivec2 p,int c,int d) {
    vec4 axial=vec4(texelFetch(t,p+ivec2(0,2),0)[c],texelFetch(t,p+ivec2(0,-2),0)[c],
        texelFetch(t,p+ivec2(-2,0),0)[c],texelFetch(t,p+ivec2(2,0),0)[c]);
    vec4 diagonal=vec4(texelFetch(t,p+ivec2(1,1),0)[d],texelFetch(t,p+ivec2(1,-1),0)[d],
        texelFetch(t,p+ivec2(-1,1),0)[d],texelFetch(t,p+ivec2(-1,-1),0)[d]);
    // Keep maxA - minA + maxB - minB in that order. Summing two separately
    // rounded ranges can reverse a close horizontal/vertical decision.
    return max(max(axial.x,axial.y),max(axial.z,axial.w))-
        min(min(axial.x,axial.y),min(axial.z,axial.w))+
        max(max(diagonal.x,diagonal.y),max(diagonal.z,diagonal.w))-
        min(min(diagonal.x,diagonal.y),min(diagonal.z,diagonal.w));
}
vec4 evaluate(ivec2 p) {
    vec4 v=a(p);
    if(interior(p,2) && cfa(p)!=1) {
        int c=cfa(p),d=2-c;
        float original=variation(inputA,p,c,d),h=variation(inputB,p,d,c),vertical=variation(inputC,p,d,c);
        v.g=abs(original-h)<abs(original-vertical)?b(p).g:cc(p).g;
    }
    return v;
}
)GLSL";
// Original image in C, current causal iterate in A. Future samples must always
// come from C, including on later iterations. This is not a Jacobi change to DCB.
inline constexpr const char* DcbCausalHelpers = R"GLSL(
vec4 sweep(ivec2 p,ivec2 delta) {return delta.y<0 || (delta.y==0 && delta.x<0)?a(p+delta):cc(p+delta);}
vec4 changed(ivec2 p,vec4 v) {if(any(notEqual(v,a(p)))) atomicOr(groupStatus,1u);return v;}
float direction(sampler2D t,ivec2 p) {
    return 4*texelFetch(t,p,0).a+2*(texelFetch(t,p+ivec2(0,1),0).a+texelFetch(t,p+ivec2(0,-1),0).a+
        texelFetch(t,p+ivec2(1,0),0).a+texelFetch(t,p+ivec2(-1,0),0).a)+
        texelFetch(t,p+ivec2(0,2),0).a+texelFetch(t,p+ivec2(0,-2),0).a+
        texelFetch(t,p+ivec2(2,0),0).a+texelFetch(t,p+ivec2(-2,0),0).a;
}
)GLSL";
inline constexpr const char* DcbNyquist = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=cc(p);
    if(interior(p,2) && cfa(p)!=1) {
        int c=cfa(p);
        float gs=sweep(p,ivec2(0,2)).g+sweep(p,ivec2(0,-2)).g+sweep(p,ivec2(-2,0)).g+sweep(p,ivec2(2,0)).g;
        float cs=cc(p+ivec2(0,2))[c]+cc(p+ivec2(0,-2))[c]+cc(p+ivec2(-2,0))[c]+cc(p+ivec2(2,0))[c];
        v.g=float(double(gs)/4.0lf+double(v[c])-double(cs)/4.0lf);
    }
    return changed(p,v);
}
)GLSL";
inline constexpr const char* DcbMap = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=a(p);
    if(interior(p,1)) {
        float l=a(p+ivec2(-1,0)).g,r=a(p+ivec2(1,0)).g,u=a(p+ivec2(0,-1)).g,d=a(p+ivec2(0,1)).g;
        if(v.g>(l+r+u+d)/4) v.a=float(min(l,r)+l+r<min(u,d)+u+d);
        else v.a=float(max(l,r)+l+r>max(u,d)+u+d);
    }
    return v;
}
)GLSL";
inline constexpr const char* DcbCorrect = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=a(p);
    if(interior(p,mode==0?2:4) && cfa(p)!=1) {
        float current=direction(inputA,p);
        float h=(a(p+ivec2(-1,0)).g+a(p+ivec2(1,0)).g)/2;
        float vertical=(a(p+ivec2(0,-1)).g+a(p+ivec2(0,1)).g)/2;
        if(mode!=0) {
            int c=cfa(p);
            h+=v[c]-(a(p+ivec2(2,0))[c]+a(p+ivec2(-2,0))[c])/2;
            vertical+=v[c]-(a(p+ivec2(0,2))[c]+a(p+ivec2(0,-2))[c])/2;
        }
        v.g=((16-current)*h+current*vertical)/16;
    }
    return v;
}
)GLSL";
inline constexpr const char* DcbColor = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=a(p); if(!interior(p,1)) return v;
    if(mode==0 && cfa(p)!=1) {
        int c=2-cfa(p);
        v[c]=(4*v.g-a(p+ivec2(1,1)).g-a(p+ivec2(-1,1)).g-a(p+ivec2(1,-1)).g-a(p+ivec2(-1,-1)).g+
            a(p+ivec2(1,1))[c]+a(p+ivec2(-1,1))[c]+a(p+ivec2(1,-1))[c]+a(p+ivec2(-1,-1))[c])/4;
    } else if(mode==1 && cfa(p)==1) {
        int c=cfa(p+ivec2(1,0)),d=2-c;
        v[c]=(2*v.g-a(p+ivec2(1,0)).g-a(p+ivec2(-1,0)).g+a(p+ivec2(1,0))[c]+a(p+ivec2(-1,0))[c])/2;
        v[d]=(2*v.g-a(p+ivec2(0,1)).g-a(p+ivec2(0,-1)).g+a(p+ivec2(0,1))[d]+a(p+ivec2(0,-1))[d])/2;
    }
    return v;
}
)GLSL";
inline constexpr const char* DcbSmooth = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=cc(p);
    if(interior(p,2)) {
        vec3 sum=vec3(0);
        const ivec2 offsets[8]=ivec2[8](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
            ivec2(-1,-1),ivec2(1,1),ivec2(1,-1),ivec2(-1,1));
        for(int i=0;i<8;++i) sum+=sweep(p,offsets[i]).rgb;
        sum/=8;v.r=sum.r+(v.g-sum.g);v.b=sum.b+(v.g-sum.g);
    }
    return changed(p,v);
}
)GLSL";
inline constexpr const char* DcbRestore = R"GLSL(
vec4 evaluate(ivec2 p) {vec4 v=a(p);v.r=b(p).r;v.b=b(p).b;return v;}
)GLSL";
inline constexpr const char* DcbRefine = R"GLSL(
float ratioGreen(ivec2 p,ivec2 step,int c) {
    float base=(cc(p-step).g+cc(p+step).g)/(2*cc(p)[c]);
    float f1=base,f2=base,f3=base,f4=base;
    if(cc(p-2*step)[c]>0) {
        f1=2*cc(p-step).g/(cc(p-2*step)[c]+cc(p)[c]);
        f2=(cc(p-step).g+cc(p-3*step).g)/(2*cc(p-2*step)[c]);
    }
    if(cc(p+2*step)[c]>0) {
        f3=2*cc(p+step).g/(cc(p+2*step)[c]+cc(p)[c]);
        f4=(cc(p+step).g+cc(p+3*step).g)/(2*cc(p+2*step)[c]);
    }
    return (5*base+3*f1+f2+3*f3+f4)/13;
}
vec4 evaluate(ivec2 p) {
    vec4 v=cc(p);
    if(interior(p,4) && cfa(p)!=1) {
        int c=cfa(p);
        if(v[c]>1) {
            float current=direction(inputC,p);
            v.g=v[c]*(current*ratioGreen(p,ivec2(0,1),c)+(16-current)*ratioGreen(p,ivec2(1,0),c))/16;
        }
        float low=3.402823e38,high=-3.402823e38;
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) if(x!=0 || y!=0) {
            float n=sweep(p,ivec2(x,y)).g;low=min(low,n);high=max(high,n);
        }
        v.g=clamp(v.g,low,high);
    }
    return changed(p,v);
}
)GLSL";
inline constexpr const char* DcbKnownChroma = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec2 v=vec2(0);int c=cfa(p);
    if(interior(p,1) && c!=1) v[c/2]=a(p)[c]-a(p).g;
    return vec4(v,0,1);
}
)GLSL";
inline constexpr const char* DcbChromaDiagonal = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=a(p);if(!interior(p,3) || cfa(p)==1) return v;
    int c=1-cfa(p)/2;float sum=0,weights=0;
    for(int y=-1;y<=1;y+=2) for(int x=-1;x<=1;x+=2) {
        ivec2 step=ivec2(x,y);
        // Original DCB uses the southwest third sample for the southeast
        // weight. Keep that published implementation's asymmetric expression.
        ivec2 farWeight=(x==1 && y==1)?ivec2(-3,3):3*step;
        float n=a(p+step)[c],opposite=a(p-step)[c],far=a(p+farWeight)[c];
        float weight=1/(1+abs(n-opposite)+abs(n-far)+abs(opposite-far));
        float estimate=1.325*n-0.175*a(p+3*step)[c]-0.075*a(p+ivec2(x,3*y))[c]-0.075*a(p+ivec2(3*x,y))[c];
        sum+=weight*estimate;weights+=weight;
    }
    v[c]=sum/weights;return v;
}
)GLSL";
inline constexpr const char* DcbChromaAxial = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=a(p);if(!interior(p,3) || cfa(p)!=1) return v;
    const ivec2 offsets[4]=ivec2[4](ivec2(0,-1),ivec2(1,0),ivec2(-1,0),ivec2(0,1));
    for(int c=0;c<2;++c) {
        float sum=0,weights=0;
        for(int d=0;d<4;++d) {
            ivec2 step=offsets[d];float n=a(p+step)[c],opposite=a(p-step)[c],far=a(p+3*step)[c];
            float weight=1/(1+abs(n-opposite)+abs(n-far)+abs(opposite-far));
            sum+=weight*(0.875*n+0.125*far);weights+=weight;
        }
        v[c]=sum/weights;
    }
    return v;
}
)GLSL";
inline constexpr const char* DcbFinalColor = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec4 v=cc(p);
    if(interior(p,6)) {
        v.r=b(p).r+v.g;v.b=b(p).g+v.g;
        vec2 low=vec2(3.402823e38),high=vec2(-3.402823e38);
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) if(x!=0 || y!=0) {
            vec2 n=sweep(p,ivec2(x,y)).rb;low=min(low,n);high=max(high,n);
        }
        v.rb=clamp(v.rb,low,high);
    }
    return changed(p,v);
}
)GLSL";
inline constexpr const char* DcbFinish = R"GLSL(
vec4 evaluate(ivec2 p) {return vec4(restoreSample(p,a(p).rgb/65535),1);}
)GLSL";
}
