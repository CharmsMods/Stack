/*
Simplified BSD License
http://www.opensource.org/licenses/bsd-license.html

Copyright (c) 2010-2011, Pascal Getreuer
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
      
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/
// Adapted from Pascal Getreuer's dmzhangwu.c, 2013-07-06 (BSD-2-Clause).
// Copyright (c) 2010-2011, Pascal Getreuer. All rights reserved.
// See legal/third_party/ZhangWu-BSD-2-Clause.txt for the license.
// Changes: C++ ownership, cancellation, interleaved output, mirrored statistics
// at boundaries, and nonnegative variance to contain floating-point roundoff.
#include "FloatDemosaicInternal.h"

namespace Raw::Demosaic::Internal {
void Lmmse(const Input& in, std::vector<float>& rgb) {
    const auto count=in.samples.size();
    std::vector<std::array<float,2>> diff(count), smooth(count);
    constexpr std::array<float,5> interpolation{-0.25f,0.5f,0.5f,0.5f,-0.25f};
    constexpr std::array<float,9> filter{
        0.03125f,0.0703125f,0.1171875f,0.1796875f,0.203125f,
        0.1796875f,0.1171875f,0.0703125f,0.03125f};
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=static_cast<std::size_t>(y)*in.width+x;
            float h=0,v=0;
            for(int k=-2;k<=2;++k) {
                h+=interpolation[k+2]*in.At(x+k,y);
                v+=interpolation[k+2]*in.At(x,y+k);
            }
            const float sign=in.Cfa(x,y)==1 ? -1.f : 1.f;
            diff[i]={sign*(h-in.samples[i]),sign*(v-in.samples[i])};
        }
    }
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=static_cast<std::size_t>(y)*in.width+x;
            for(int k=-4;k<=4;++k) {
                smooth[i][0]+=filter[k+4]*diff[in.Index(x+k,y)][0];
                smooth[i][1]+=filter[k+4]*diff[in.Index(x,y+k)][1];
            }
        }
    }
    rgb.assign(count*3,0);
    constexpr float epsilon=0.1f/(255.f*255.f);
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=static_cast<std::size_t>(y)*in.width+x;
            float g=in.samples[i];
            if(in.Cfa(x,y)!=1) {
                std::array<float,2> estimate{},variance{};
                for(int d=0;d<2;++d) {
                    double sum=0,squared=0,noise=0;
                    for(int k=-4;k<=4;++k) {
                        const auto j=in.Index(x+(d==0?k:0),y+(d==1?k:0));
                        const double value=smooth[j][d];
                        sum+=value; squared+=value*value;
                        const double residual=value-diff[j][d];
                        noise+=residual*residual;
                    }
                    const float p=static_cast<float>(std::max(0.0,squared/8-sum*sum/72));
                    const float r=static_cast<float>(noise/9)+epsilon;
                    const float mean=static_cast<float>(sum/9);
                    const float gain=p/(p+r);
                    estimate[d]=mean+gain*(diff[i][d]-mean);
                    variance[d]=(1-gain)*p+epsilon;
                }
                g+=(variance[1]*estimate[0]+variance[0]*estimate[1])/(variance[0]+variance[1]);
            }
            rgb[3*i+1]=g;
        }
    }
    // Reuse difference storage for green-red / green-blue, known at CFA sites.
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=static_cast<std::size_t>(y)*in.width+x;
            diff[i]={0,0};
            if(in.Cfa(x,y)!=1) diff[i][in.Cfa(x,y)/2]=rgb[3*i+1]-in.samples[i];
        }
    }
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            if(in.Cfa(x,y)==1) continue;
            const auto i=static_cast<std::size_t>(y)*in.width+x;
            const int opposite=1-in.Cfa(x,y)/2;
            diff[i][opposite]=0.25f*(diff[in.Index(x-1,y-1)][opposite]+diff[in.Index(x+1,y-1)][opposite]+
                diff[in.Index(x-1,y+1)][opposite]+diff[in.Index(x+1,y+1)][opposite]);
        }
    }
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=static_cast<std::size_t>(y)*in.width+x;
            if(in.Cfa(x,y)==1) for(int c=0;c<2;++c)
                diff[i][c]=0.25f*(diff[in.Index(x-1,y)][c]+diff[in.Index(x+1,y)][c]+
                    diff[in.Index(x,y-1)][c]+diff[in.Index(x,y+1)][c]);
            rgb[3*i]=rgb[3*i+1]-diff[i][0];
            rgb[3*i+2]=rgb[3*i+1]-diff[i][1];
        }
    }
}
} // namespace Raw::Demosaic::Internal
