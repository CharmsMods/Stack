// SPDX-License-Identifier: CDDL-1.0
// Float reference adaptations of LibRaw 0.22.1 PPG, VNG and AHD.
// Copyright 2019-2025 LibRaw LLC. dcraw copyright 1997-2018 Dave Coffin.
// PPG by Alain Desbiolles. AHD based on Hirakawa, Parks and Paul Lee.
// Distributed under LibRaw's CDDL option. See
// legal/third_party/LibRaw-Demosaic-NOTICE.txt and LibRaw-Demosaic-CDDL-1.0.txt.
// Stack changes: explicit input/ownership, float HDR data, mirrored boundaries,
// cancellation, separate image passes and preserved recorded CFA samples.
#include "FloatDemosaicInternal.h"
#include "ClassicDemosaicTerms.h"

namespace Raw::Demosaic::Internal {
namespace {
using Pixel=std::array<float,3>;
using Image=std::vector<Pixel>;
constexpr std::array<std::array<int,2>,8> Neighbors{{{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}}};
bool Inside(const Input& in,int x,int y,int border) {return x>=border && y>=border && x<in.width-border && y<in.height-border;}
void ColorFromGreen(const Input& in,const std::vector<float>& green,Image& image,bool ppg) {
    image.resize(in.samples.size());
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=in.Index(x,y);const float g=green[i];const int known=in.Cfa(x,y);
            auto& rgb=image[i];rgb={g,g,g};rgb[known]=in.At(x,y);
            if(known==1) for(int d=0;d<2;++d) {
                const int sx=d==0?1:0,sy=d==1?1:0,c=in.Cfa(x+sx,y+sy);
                rgb[c]=(in.At(x-sx,y-sy)+in.At(x+sx,y+sy)+2*g-green[in.Index(x-sx,y-sy)]-green[in.Index(x+sx,y+sy)])/2;
            } else {
                std::array<float,2> estimate{},difference{};
                for(int d=0;d<2;++d) {
                    const int sx=d==0?1:-1;
                    const float g1=green[in.Index(x-sx,y-1)],g2=green[in.Index(x+sx,y+1)];
                    const float c1=in.At(x-sx,y-1),c2=in.At(x+sx,y+1);
                    estimate[d]=(c1+c2+2*g-g1-g2)/2;
                    difference[d]=std::abs(c1-c2)+std::abs(g1-g)+std::abs(g2-g);
                }
                rgb[2-known]=!ppg || difference[0]==difference[1]?(estimate[0]+estimate[1])/2:estimate[difference[0]>difference[1]?1:0];
            }
        }
    }
}
void Flatten(const Image& image,std::vector<float>& rgb) {
    rgb.resize(image.size()*3);
    for(std::size_t i=0;i<image.size();++i) std::copy(image[i].begin(),image[i].end(),rgb.begin()+3*i);
}
std::vector<float> Green(const Input& in,int direction,bool ppg) {
    auto green=in.samples;
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            if(in.Cfa(x,y)==1) continue;
            std::array<float,2> estimate{},difference{};
            for(int d=0;d<2;++d) {
                const int dx=d==0?1:0,dy=d==1?1:0;
                auto sample=[&](int k){return in.At(x+k*dx,y+k*dy);};
                estimate[d]=((sample(-1)+sample(0)+sample(1))*2-sample(-2)-sample(2))/4;
                difference[d]=3*(std::abs(sample(-2)-sample(0))+std::abs(sample(2)-sample(0))+std::abs(sample(-1)-sample(1)))+
                    2*(std::abs(sample(3)-sample(1))+std::abs(sample(-3)-sample(-1)));
            }
            const int d=ppg?(difference[0]>difference[1]?1:0):direction,dx=d==0?1:0,dy=d==1?1:0;
            green[in.Index(x,y)]=std::clamp(estimate[d],std::min(in.At(x-dx,y-dy),in.At(x+dx,y+dy)),std::max(in.At(x-dx,y-dy),in.At(x+dx,y+dy)));
        }
    }
    return green;
}
Pixel Lab(const Pixel& camera,const std::array<float,9>& matrix) {
    Pixel rgb{},xyz{};
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) rgb[r]+=matrix[r*3+c]*camera[c];
    xyz[0]=(0.4124564f*rgb[0]+0.3575761f*rgb[1]+0.1804375f*rgb[2])/0.95047f;
    xyz[1]=0.2126729f*rgb[0]+0.7151522f*rgb[1]+0.0721750f*rgb[2];
    xyz[2]=(0.0193339f*rgb[0]+0.1191920f*rgb[1]+0.9503041f*rgb[2])/1.08883f;
    for(auto& v:xyz) v=v>0.008856f?std::pow(v,1.f/3.f):7.787f*v+16.f/116.f;
    Pixel lab{116*xyz[1]-16,500*(xyz[0]-xyz[1]),200*(xyz[1]-xyz[2])};
    // Retain the original AHD decision units, without clipping camera RGB or
    // imposing the original short-integer Lab range on signed/HDR data.
    for(auto& v:lab) v=std::trunc(v*64)/64;
    return lab;
}
}
void Ppg(const Input& in,std::vector<float>& rgb) {
    Image image;ColorFromGreen(in,Green(in,0,true),image,true);Flatten(image,rgb);
}
void Ahd(const Input& in,std::vector<float>& rgb) {
    std::array<Image,2> candidates,labs;
    for(int d=0;d<2;++d) {
        ColorFromGreen(in,Green(in,d,false),candidates[d],false);
        labs[d].resize(in.samples.size());
        for(std::size_t i=0;i<in.samples.size();++i) labs[d][i]=Lab(candidates[d][i],in.cameraToSrgb);
    }
    std::vector<std::array<int,2>> counts(in.samples.size());
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=in.Index(x,y);float l[2][4]{},chroma[2][4]{};
            for(int d=0;d<2;++d) for(int n=0;n<4;++n) {
                const auto& step=Neighbors[2*n+1];const auto j=in.Index(x+step[0],y+step[1]);
                l[d][n]=std::abs(labs[d][i][0]-labs[d][j][0]);
                const float a=labs[d][i][1]-labs[d][j][1],b=labs[d][i][2]-labs[d][j][2];
                chroma[d][n]=a*a+b*b;
            }
            // Neighbors above are N,E,S,W. Horizontal candidate contributes E/W.
            const float leps=std::min(std::max(l[0][1],l[0][3]),std::max(l[1][0],l[1][2]));
            const float ceps=std::min(std::max(chroma[0][1],chroma[0][3]),std::max(chroma[1][0],chroma[1][2]));
            for(int d=0;d<2;++d) for(int n=0;n<4;++n) if(l[d][n]<=leps && chroma[d][n]<=ceps) ++counts[i][d];
        }
    }
    rgb.resize(in.samples.size()*3);
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            std::array<int,2> sum{};const auto i=in.Index(x,y);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) for(int d=0;d<2;++d) sum[d]+=counts[in.Index(x+dx,y+dy)][d];
            for(int c=0;c<3;++c) rgb[3*i+c]=sum[0]==sum[1]?(candidates[0][i][c]+candidates[1][i][c])/2:candidates[sum[0]>sum[1]?0:1][i][c];
        }
    }
}
void Vng(const Input& in,bool fourColors,std::vector<float>& rgb) {
    auto color=[&](int x,int y){const int c=in.Cfa(x,y);return fourColors && c==1 && (y&1)?3:c;};
    std::vector<std::array<float,4>> linear(in.samples.size());
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) for(int c=0;c<(fourColors?4:3);++c) {
            auto& value=linear[in.Index(x,y)][c];
            if(color(x,y)==c) {value=in.At(x,y);continue;}
            float sum=0,weights=0;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(color(x+dx,y+dy)==c) {
                const float w=dx==0 || dy==0?2.f:1.f;sum+=w*in.At(x+dx,y+dy);weights+=w;
            }
            value=sum/weights;
        }
    }
    rgb.resize(in.samples.size()*3);
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            auto value=linear[in.Index(x,y)];const int known=color(x,y);
            if(Inside(in,x,y,2)) {
                std::array<float,8> gradients{};
                for(const auto& t:Classic::VngTerms) {
                    const int c=color(x+t[1],y+t[0]);if(color(x+t[3],y+t[2])!=c) continue;
                    const int diag=color(x+1,y)==c && color(x,y+1)==c?2:1;
                    if(std::abs(t[0]-t[2])==diag && std::abs(t[1]-t[3])==diag) continue;
                    const float diff=std::abs(linear[in.Index(x+t[1],y+t[0])][c]-linear[in.Index(x+t[3],y+t[2])][c])*(1<<t[4]);
                    for(int g=0;g<8;++g) if(t[5]&(1<<g)) gradients[g]+=diff;
                }
                const auto range=std::minmax_element(gradients.begin(),gradients.end());
                if(*range.second>0) {
                    const float threshold=*range.first+0.5f*(*range.second);float n=0;std::array<float,4> sum{};
                    for(int g=0;g<8;++g) if(gradients[g]<=threshold) {
                        const int dx=Neighbors[g][0],dy=Neighbors[g][1];auto sample=linear[in.Index(x+dx,y+dy)];
                        if(color(x+dx,y+dy)!=known && color(x+2*dx,y+2*dy)==known) sample[known]=(value[known]+linear[in.Index(x+2*dx,y+2*dy)][known])/2;
                        for(int c=0;c<4;++c) sum[c]+=sample[c];++n;
                    }
                    for(int c=0;c<4;++c) value[c]=in.At(x,y)+(sum[c]-sum[known])/n;
                }
            }
            if(fourColors) value[1]=(value[1]+value[3])/2;
            for(int c=0;c<3;++c) rgb[3*in.Index(x,y)+c]=value[c];
        }
    }
}
}
