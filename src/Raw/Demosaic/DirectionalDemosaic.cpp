#include "FloatDemosaicInternal.h"

namespace Raw::Demosaic::Internal {
void Directional(const Input& in, DemosaicMethod method, std::vector<float>& rgb) {
    const auto count = in.samples.size();
    std::vector<float> green(count);
    const bool weighted = method == DemosaicMethod::EdgeWeighted;
    const bool ratios = method == DemosaicMethod::SmoothHue;
    for (int y=0; y<in.height; ++y) {
        in.Check(y);
        for (int x=0; x<in.width; ++x) {
            const auto i = static_cast<std::size_t>(y)*in.width+x;
            const float c = in.At(x,y);
            if (in.Cfa(x,y)==1) { green[i]=c; continue; }
            const float l=in.At(x-1,y), r=in.At(x+1,y);
            const float u=in.At(x,y-1), d=in.At(x,y+1);
            const float dh=2*c-in.At(x-2,y)-in.At(x+2,y);
            const float dv=2*c-in.At(x,y-2)-in.At(x,y+2);
            const float h=0.5f*(l+r)+0.25f*dh;
            const float v=0.5f*(u+d)+0.25f*dv;
            const float gh=std::abs(l-r)+std::abs(dh);
            const float gv=std::abs(u-d)+std::abs(dv);
            if (weighted) {
                const float epsilon=1e-6f*(1+std::abs(c));
                const float wh=1/(epsilon+gh), wv=1/(epsilon+gv);
                green[i]=(wh*h+wv*v)/(wh+wv);
            } else {
                green[i]=gh<gv ? h : gv<gh ? v : 0.5f*(h+v);
            }
        }
    }
    rgb.resize(count*3);
    for (int y=0; y<in.height; ++y) {
        in.Check(y);
        for (int x=0; x<in.width; ++x) {
            const auto i = static_cast<std::size_t>(y)*in.width+x;
            const int c=in.Cfa(x,y);
            const float g=green[i];
            rgb[3*i+1]=g;
            for (int target : {0,2}) {
                if (c==target) { rgb[3*i+target]=in.samples[i]; continue; }
                float sum=0, weights=0;
                // Red/blue samples are axial at green and diagonal otherwise.
                for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
                    if (in.Cfa(x+dx,y+dy)!=target) continue;
                    const auto j=in.Index(x+dx,y+dy);
                    const float ng=green[j], nc=in.samples[j];
                    const float w=weighted ? 1/(1e-5f+std::abs(g-ng)) : 1;
                    float estimate=g+nc-ng;
                    // Ratios are ill-conditioned at zero and for signed samples.
                    // Use additive color differences there, without clamping HDR.
                    if (ratios && g>0.001f && ng>0.001f && nc>=0)
                        estimate=g*(nc/ng);
                    sum+=w*estimate;
                    weights+=w;
                }
                rgb[3*i+target]=sum/weights;
            }
        }
    }
}

void RefineChroma(const Input& in, std::vector<float>& rgb, int iterations) {
    std::vector<float> next(rgb.size());
    for (int pass=0; pass<iterations; ++pass) {
        for (int y=0; y<in.height; ++y) {
            in.Check(y);
            for (int x=0; x<in.width; ++x) {
                const auto i=static_cast<std::size_t>(y)*in.width+x;
                next[3*i+1]=rgb[3*i+1];
                for (int c : {0,2}) {
                    if (in.Cfa(x,y)==c) { next[3*i+c]=in.samples[i]; continue; }
                    std::array<float,9> difference{};
                    int k=0;
                    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                        const auto j=in.Index(x+dx,y+dy);
                        difference[k++]=rgb[3*j+c]-rgb[3*j+1];
                    }
                    std::nth_element(difference.begin(),difference.begin()+4,difference.end());
                    next[3*i+c]=rgb[3*i+1]+difference[4];
                }
            }
        }
        rgb.swap(next);
    }
}
} // namespace Raw::Demosaic::Internal
