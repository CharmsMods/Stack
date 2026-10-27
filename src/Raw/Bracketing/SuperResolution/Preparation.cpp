#include "Preparation.h"
#include "../CaptureCorrespondence.h"
#include "SamplingCoverage.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing::Sr {
Vec4 MapCapture(const PreparedSource& source,float x,float y,bool reference) {
    const auto match=EvaluateCaptureCorrespondence(source,{x,y},reference);
    const double positionalVariance=std::max(0.,match.covarianceRaw.xxRawPixelsSquared+
        match.covarianceRaw.yyRawPixelsSquared);
    return {float(match.sourceRaw.x),float(match.sourceRaw.y),float(match.confidence),float(positionalVariance)};
}
bool BuildFrameRegion(const BracketingAnalysis& analysis,const PreparedSource& source,const Tile& tile,
    PreparedTileSampler& sampler,FrameRegion& out,const std::function<bool()>& cancel,std::string& error) {
    const auto& data=*analysis.prepared;
    if(!tile.width||!tile.height||!std::isfinite(tile.step)||tile.step<=0) {
        error="Invalid super-resolution output tile.";return false;
    }
    const bool reference=&source==&data.sources[data.origin];
    out.supportRequired=!reference&&bool(source.localMotion);
    if(!BuildCaptureSupportMask(source,{tile.originX,tile.originY},
        {tile.originX+(tile.width-1)*tile.step,tile.originY+(tile.height-1)*tile.step},
        reference,out.support,cancel,error))return false;
    auto& depth=out.depthSupport;
    out.depthSupportRequired=!reference&&HasCaptureDepthSupport(source);
    depth.width=depth.height=0;depth.originX=depth.originY=depth.step=0;depth.values.clear();
    if(out.depthSupportRequired) {
        if(tile.width>depth.values.max_size()/tile.height) {
            error="Super-resolution depth support exceeds addressable storage.";return false;
        }
        depth.values.resize(std::size_t(tile.width)*tile.height);
        for(unsigned y=0;y<tile.height;++y) {
            if(cancel&&cancel()){error="Canceled";return false;}
            for(unsigned x=0;x<tile.width;++x)
                depth.values[std::size_t(y)*tile.width+x]=CaptureDepthRejects(source,
                    {tile.originX+x*tile.step,tile.originY+y*tile.step})?0.f:1.f;
        }
        depth.width=tile.width;depth.height=tile.height;
        depth.originX=tile.originX;depth.originY=tile.originY;depth.step=tile.step;
    }
    const int guideWidth=data.width/2,guideHeight=data.height/2;
    const auto signal=[&](int x,int y){return .18*std::exp2(analysis.guideEv[std::size_t(std::clamp(y,0,guideHeight-1))*guideWidth+std::clamp(x,0,guideWidth-1)]);};
    // Reuse this capture scratch region. Every sample and mapping entry below
    // is overwritten, while capacity remains bounded by the source-region cap.
    out.mapStep=8;out.mapOriginX=std::floor(tile.originX/8)*8;out.mapOriginY=std::floor(tile.originY/8)*8;
    out.detailPreference=float(std::clamp(source.detailPreference,.1,1.));
    out.mapWidth=unsigned(std::ceil((tile.originX+(tile.width-1)*tile.step-out.mapOriginX)/8))+1;
    out.mapHeight=unsigned(std::ceil((tile.originY+(tile.height-1)*tile.step-out.mapOriginY)/8))+1;
    out.mapWidth=std::max(2u,out.mapWidth);out.mapHeight=std::max(2u,out.mapHeight);
    out.mapping.resize(std::size_t(out.mapWidth)*out.mapHeight);
    float loX=std::numeric_limits<float>::max(),loY=loX,hiX=-loX,hiY=-loX;
    for(unsigned y=0;y<out.mapHeight;++y)for(unsigned x=0;x<out.mapWidth;++x) {
        auto m=MapCapture(source,out.mapOriginX+x*8,out.mapOriginY+y*8,reference);
        if(!std::isfinite(m.x)||!std::isfinite(m.y)){m={0,0,0,0};}
        const int gx=int((out.mapOriginX+x*8)/2),gy=int((out.mapOriginY+y*8)/2);
        const double dx=(signal(gx+1,gy)-signal(gx-1,gy))*.25,dy=(signal(gx,gy+1)-signal(gx,gy-1))*.25;
        const double pilot=signal(gx,gy);
        // Trace of positional covariance bounds uncertainty in every gradient
        // direction. Keep this model risk separate from sensor noise.
        m.w=float(m.w*(dx*dx+dy*dy)+pilot*pilot*source.scaleVariance/(source.scale*source.scale));
        out.mapping[y*out.mapWidth+x]=m;
        loX=std::min(loX,m.x);loY=std::min(loY,m.y);hiX=std::max(hiX,m.x);hiY=std::max(hiY,m.y);
    }
    out.left=std::clamp(int(std::floor(loX))-4,0,int(data.width)-1);
    out.top=std::clamp(int(std::floor(loY))-4,0,int(data.height)-1);
    const int right=std::clamp(int(std::ceil(hiX))+5,out.left+1,int(data.width));
    const int bottom=std::clamp(int(std::ceil(hiY))+5,out.top+1,int(data.height));
    out.width=right-out.left;out.height=bottom-out.top;
    if(std::size_t(out.width)*out.height>4ull*1024*1024) {error="Super-resolution mapping exceeds the bounded source region.";return false;}
    out.samples.resize(std::size_t(out.width)*out.height);
    Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout);
    for(int s=0;s<4;++s){const auto site=layout.SiteAt(s%2,s/2);out.colors[s]=site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1;}
    for(int y=0;y<out.height;++y) {
        if(cancel&&cancel()){error="Canceled";return false;}
        for(int x=0;x<out.width;++x) {
            Mfd::SameCfaTapInput tap;
            if(!sampler.ReadSample(source.frame,out.left+x,out.top+y,tap,&error))return false;
            auto& s=out.samples[std::size_t(y)*out.width+x];s={0,0,0,-1};
            if(Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::Saturated)||
               Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::ExplicitDecoderClip)) {
                s.w=0;continue;
            }
            if(!std::isfinite(tap.normalizedSample)||!std::isfinite(tap.comparisonGain)||tap.comparisonGain<=0||
               Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::Defective)||
               Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::DecoderRepaired))continue;
            const auto site=layout.SiteAt(out.left+x,out.top+y);const auto i=static_cast<unsigned>(site);
            const auto& noise=source.noise.sites[i];const double gain=tap.comparisonGain*source.scale;
            s.x=float(tap.normalizedSample*gain);s.y=float(noise.shotScale*gain);
            // Resolved offsets already contain quantization. Keep the same
            // sensor variance as native temporal sampling, not a second copy
            // of the code-step variance (or a newly inferred noise model).
            const double offset=noise.offsetVariance+(noise.quantizationIncluded?0:noise.quantizationVariance);
            s.z=float(offset*gain*gain);
            s.w=float(std::clamp((.995-tap.normalizedSample)/.045,0.,1.));s.w=s.w*s.w*(3-2*s.w);
        }
    }
    return true;
}
float SamplingDiversity(const PreparedDataset& data) {
    // Coverage in the 2x2 Bayer phase domain. Several probe locations catch
    // rotations and local variation that a translation-only count would miss.
    float total=0;
    for(float py:{.25f,.5f,.75f})for(float px:{.25f,.5f,.75f}) {
        bool bins[16]{};unsigned n=0;
        const float x=data.width*px,y=data.height*py;
        for(const auto& source:data.sources)if(source.enabled) {
            auto m=MapCapture(source,x,y,&source==&data.sources[data.origin]);if(m.z<=0)continue;
            const float dx=m.x-x,dy=m.y-y;
            const int ix=std::min(3,int((dx-2*std::floor(dx/2))*2));
            const int iy=std::min(3,int((dy-2*std::floor(dy/2))*2));
            if(!bins[iy*4+ix]){bins[iy*4+ix]=true;++n;}
        }
        total+=float(n);
    }
    return total/9;
}
Tile MakeTile(unsigned x,unsigned y,unsigned w,unsigned h,unsigned scale,const BracketingAnalysis& analysis,std::size_t group) {
    Tile tile;tile.width=w;tile.height=h;tile.step=1.f/scale;
    tile.originX=(x+.5f)/scale-.5f;tile.originY=(y+.5f)/scale-.5f;
    tile.pixels.resize(std::size_t(w)*h);
    tile.samplingConfidence.resize(std::size_t(w)*h);
    const auto& data=*analysis.prepared;const int gw=data.width/2,gh=data.height/2;
    // Resolve geometry once on a sensor-anchored grid for this exposure group.
    // This costs O(captures * tile area / 256), not a motion-store lookup or
    // trigonometric evaluation for every reconstructed sample and tap.
    constexpr float coverageStep=16;
    const float coverageX=std::floor(tile.originX/coverageStep)*coverageStep;
    const float coverageY=std::floor(tile.originY/coverageStep)*coverageStep;
    const unsigned coverageWidth=std::max(2u,unsigned(std::ceil((tile.originX+(w-1)*tile.step-coverageX)/coverageStep))+1);
    const unsigned coverageHeight=std::max(2u,unsigned(std::ceil((tile.originY+(h-1)*tile.step-coverageY)/coverageStep))+1);
    std::vector<float> coverage(std::size_t(coverageWidth)*coverageHeight);
    for(unsigned yy=0;yy<coverageHeight;++yy)for(unsigned xx=0;xx<coverageWidth;++xx) {
        const float px=std::clamp(coverageX+xx*coverageStep,0.f,float(data.width-1));
        const float py=std::clamp(coverageY+yy*coverageStep,0.f,float(data.height-1));
        SamplingCoverage measured(scale);
        for(const auto& source:data.sources)if(source.enabled&&source.group==group) {
            const auto mapped=MapCapture(source,px,py,&source==&data.sources[data.origin]);
            if(mapped.x<0||mapped.y<0||mapped.x>data.width-1||mapped.y>data.height-1)continue;
            measured.Add(mapped.x-px,mapped.y-py,mapped.z*source.detailPreference);
        }
        coverage[std::size_t(yy)*coverageWidth+xx]=measured.Confidence();
    }
    const auto confidenceAt=[&](float px,float py) {
        const float u=std::clamp((px-coverageX)/coverageStep,0.f,float(coverageWidth-1));
        const float v=std::clamp((py-coverageY)/coverageStep,0.f,float(coverageHeight-1));
        const unsigned ix=std::min(unsigned(u),coverageWidth-2),iy=std::min(unsigned(v),coverageHeight-2);
        const float a=u-ix,b=v-iy;const auto i=std::size_t(iy)*coverageWidth+ix;
        return (1-b)*((1-a)*coverage[i]+a*coverage[i+1])+b*((1-a)*coverage[i+coverageWidth]+a*coverage[i+coverageWidth+1]);
    };
    // Average gradient products before choosing a direction. A single noisy
    // gradient is not reliable edge evidence. Interpolate the resulting SPD
    // matrices continuously so CFA-cell boundaries never become seams.
    const int firstX=int(std::floor(tile.originX/2)),firstY=int(std::floor(tile.originY/2));
    const int kw=int(std::ceil(w*tile.step/2))+3,kh=int(std::ceil(h*tile.step/2))+3;
    // Every 3x3 structure tensor revisits the same signal values. Cache the
    // exact float conversion once on this tile's two-cell halo, preserving
    // sensor-edge clamping and the subsequent accumulation order.
    const int signalWidth=kw+4,signalHeight=kh+4;
    std::vector<float> signals(std::size_t(signalWidth)*signalHeight);
    for(int yy=0;yy<signalHeight;++yy)for(int xx=0;xx<signalWidth;++xx) {
        const int gx=std::clamp(firstX+xx-2,0,gw-1),gy=std::clamp(firstY+yy-2,0,gh-1);
        signals[std::size_t(yy)*signalWidth+xx]=float(.18*std::exp2(analysis.guideEv[std::size_t(gy)*gw+gx]));
    }
    const auto signal=[&](int xx,int yy) {
        return signals[std::size_t(yy-firstY+2)*signalWidth+xx-firstX+2];
    };
    std::vector<Vec4> kernels(std::size_t(kw)*kh);
    for(int yy=0;yy<kh;++yy)for(int xx=0;xx<kw;++xx) {
        const int cx=firstX+xx,cy=firstY+yy;const float pilot=signal(cx,cy);
        float jxx=0,jxy=0,jyy=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const float gx=(signal(cx+dx+1,cy+dy)-signal(cx+dx-1,cy+dy))*.25f;
            const float gy=(signal(cx+dx,cy+dy+1)-signal(cx+dx,cy+dy-1))*.25f;
            jxx+=gx*gx/9;jxy+=gx*gy/9;jyy+=gy*gy/9;
        }
        const float delta=std::sqrt((jxx-jyy)*(jxx-jyy)+4*jxy*jxy);
        const float coherence=delta/std::max(1e-12f,jxx+jyy);
        const float theta=.5f*std::atan2(2*jxy,jxx-jyy),nx=std::cos(theta),ny=std::sin(theta);
        const float edge=coherence*std::clamp(std::sqrt((jxx+jyy+delta)*.5f)/std::max(.005f,pilot)*3,0.f,1.f);
        const float confidence=confidenceAt((firstX+xx)*2.f,(firstY+yy)*2.f);
        const float normal=.9f+confidence*(.55f-.15f*edge-.9f);
        const float tangent=.9f+confidence*(.55f+.60f*edge-.9f);
        const float a=1/(normal*normal),b=1/(tangent*tangent);
        kernels[std::size_t(yy)*kw+xx]={b+(a-b)*nx*nx,(a-b)*nx*ny,b+(a-b)*ny*ny,pilot};
    }
    const auto lerp=[](Vec4 a,Vec4 b,float t){return Vec4{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};};
    for(unsigned yy=0;yy<h;++yy)for(unsigned xx=0;xx<w;++xx) {
        const float cx=(tile.originX+xx*tile.step)/2-firstX,cy=(tile.originY+yy*tile.step)/2-firstY;
        const int ix=int(cx),iy=int(cy);const float fx=cx-ix,fy=cy-iy;
        const auto i=std::size_t(iy)*kw+ix;
        tile.pixels[std::size_t(yy)*w+xx].kernel=lerp(lerp(kernels[i],kernels[i+1],fx),lerp(kernels[i+kw],kernels[i+kw+1],fx),fy);
        tile.samplingConfidence[std::size_t(yy)*w+xx]=confidenceAt(tile.originX+xx*tile.step,tile.originY+yy*tile.step);
    }
    return tile;
}
}
