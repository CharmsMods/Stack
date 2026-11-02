#include "AdaptiveResolution.h"
#include "Preparation.h"
#include "SamplingCoverage.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Raw::Bracketing::Sr {
namespace {
// A fixed physical patch on a fine working grid. Candidate cutoffs are
// expressed in cycles per original pixel, independent of that working grid.
constexpr int extent=16,grid=extent*MaxAdaptiveScale;
constexpr std::array<double,8> scales={1,1.25,1.5,1.75,2,2.5,3,4};
struct Tap {float x,y,value,variance;};
struct CapturePatch {std::vector<Tap> greens;float dx,dy,weight;};
struct Point {unsigned x,y;};

std::vector<Point> LocatePatches(const BracketingAnalysis& analysis) {
    const auto& data=*analysis.prepared;const unsigned w=data.width/2,h=data.height/2;
    std::vector<Point> result;
    if(w<18||h<18)return result;
    const unsigned nx=std::min(6u,w/9),ny=std::min(5u,h/9);
    const auto signal=[&](unsigned x,unsigned y){return analysis.guideEv[std::size_t(y)*w+x];};
    for(unsigned row=0;row<ny;++row)for(unsigned col=0;col<nx;++col) {
        const unsigned left=col*w/nx,right=(col+1)*w/nx,top=row*h/ny,bottom=(row+1)*h/ny;
        double best=0;Point point{};
        for(unsigned y=top+5;y+5<bottom;y+=std::max(1u,(bottom-top)/16))
            for(unsigned x=left+5;x+5<right;x+=std::max(1u,(right-left)/16)) {
                // Rank by local structure only. All scale tests below use
                // original sensor observations, never this merged guide.
                double texture=0;
                for(int j=-2;j<=2;++j)for(int i=-2;i<=2;++i) {
                    const double gx=signal(x+i+1,y+j)-signal(x+i,y+j);
                    const double gy=signal(x+i,y+j+1)-signal(x+i,y+j);
                    texture+=std::min(1.,gx*gx+gy*gy);
                }
                if(texture>best){best=texture;point={2*x-extent/2,2*y-extent/2};}
            }
        if(best>0)result.push_back(point);
    }
    return result;
}
bool ReadPatch(const BracketingAnalysis& analysis,const PreparedSource& source,Point p,
    PreparedTileSampler& sampler,const std::function<bool()>& cancel,CapturePatch& patch,std::string& error) {
    Tile tile;tile.originX=float(p.x);tile.originY=float(p.y);tile.width=tile.height=extent;
    tile.pixels.resize(extent*extent);
    const bool reference=&source==&analysis.prepared->sources[analysis.prepared->origin];
    const auto center=MapCapture(source,p.x+8.f,p.y+8.f,reference);
    if(center.z<=0||!std::isfinite(center.x)||!std::isfinite(center.y))return true;
    patch.dx=center.x-(p.x+8.f);patch.dy=center.y-(p.y+8.f);
    FrameRegion frame;
    if(!BuildFrameRegion(analysis,source,tile,sampler,frame,cancel,error))return false;
    double precision=0;
    for(int y=0;y<frame.height;++y)for(int x=0;x<frame.width;++x) {
        const int sx=frame.left+x,sy=frame.top+y;
        if(frame.colors[2*(sy&1)+(sx&1)]!=1)continue;
        const float rx=sx-patch.dx-p.x,ry=sy-patch.dy-p.y;
        if(rx<0||ry<0||rx>=extent||ry>=extent)continue;
        const auto m=MapCapture(source,p.x+rx,p.y+ry,reference);
        // Only locally translational, verified observations can certify
        // subpixel bandwidth. Rejection does not exclude them from normal HDR.
        if(m.z<=0||std::hypot(m.x-sx,m.y-sy)>.12f)continue;
        const auto& s=frame.samples[std::size_t(y)*frame.width+x];
        if(s.w<.9f||!std::isfinite(s.x))continue;
        const float variance=std::max(1e-10f,s.z+s.y*std::max(0.f,s.x));
        if(!std::isfinite(variance))continue;
        patch.greens.push_back({rx,ry,s.x,variance});
        precision+=m.z/variance;
    }
    if(patch.greens.size()<64)patch.greens.clear();
    else patch.weight=float(precision/patch.greens.size())*frame.detailPreference;
    return true;
}

float Sample(const cv::Mat& plane,float x,float y) {
    x*=MaxAdaptiveScale;y*=MaxAdaptiveScale;
    const int ix=int(x),iy=int(y);const float u=x-ix,v=y-iy;
    const float* a=plane.ptr<float>(iy),*b=plane.ptr<float>(iy+1);
    return (1-v)*((1-u)*a[ix]+u*a[ix+1])+v*((1-u)*b[ix]+u*b[ix+1]);
}
cv::Mat TrainingSpectrum(const std::vector<CapturePatch>& captures,std::size_t held,
    bool single,double& effective) {
    cv::Mat sum=cv::Mat::zeros(grid,grid,CV_32F),weights=sum.clone();
    double sumWeight=0,squaredWeight=0;
    for(std::size_t i=0;i<captures.size();++i)if(i!=held) {
        const auto& f=captures[i];
        sumWeight+=f.weight;squaredWeight+=double(f.weight)*f.weight;
        for(const auto& s:f.greens) {
            const float px=s.x*MaxAdaptiveScale,py=s.y*MaxAdaptiveScale;
            const int ix=int(px),iy=int(py);
            for(int dy=0;dy<2&&iy+dy<grid;++dy)for(int dx=0;dx<2&&ix+dx<grid;++dx) {
                const float w=(dx?px-ix:1-(px-ix))*(dy?py-iy:1-(py-iy))*f.weight;
                weights.at<float>(iy+dy,ix+dx)+=w;sum.at<float>(iy+dy,ix+dx)+=w*s.value;
            }
        }
        if(single)break; // captures are sorted by usable precision
    }
    if(squaredWeight<=0)return {};
    effective=sumWeight*sumWeight/squaredWeight;
    const double sigma=std::clamp(4./std::sqrt(effective),1.,2.5);
    cv::GaussianBlur(sum,sum,{},sigma);cv::GaussianBlur(weights,weights,{},sigma);
    double minWeight=0;cv::minMaxLoc(weights(cv::Rect(8,8,grid-16,grid-16)),&minWeight);
    if(minWeight<=0)return {};
    cv::max(weights,1e-20,weights);
    cv::divide(sum,weights,sum);cv::Mat spectrum;cv::dct(sum,spectrum);
    return spectrum;
}
// Use the best single training capture as the native baseline. Above native,
// compare progressively wider bands of the same multi-capture training image.
// The held-out capture supplies only the original samples used to score them.
double ValidateCapture(const std::vector<CapturePatch>& captures,std::size_t held) {
    double effective=0,singleCount=0;
    const auto single=TrainingSpectrum(captures,held,true,singleCount);
    const auto spectrum=TrainingSpectrum(captures,held,false,effective);
    if(single.empty()||spectrum.empty())return 0;
    std::array<double,16> previous{};double accepted=0;
    for(int level=-1;level<int(scales.size());++level) {
        const double scale=level<0?1.:scales[level];
        if(scale*scale>effective+.01)break;
        if(level>=0) {
            SamplingCoverage coverage(scale);
            for(std::size_t i=0;i<captures.size();++i)if(i!=held)
                coverage.Add(captures[i].dx,captures[i].dy,captures[i].weight);
            if(coverage.Confidence()<.18f)break;
        }
        cv::Mat band=(level<0?single:spectrum).clone();
        for(int y=0;y<grid;++y)for(int x=0;x<grid;++x) {
            // DCT bin k corresponds to k/(2*extent) cycles per sensor pixel.
            const double bin=std::max(x,y);
            band.at<float>(y,x)*=float(std::clamp(extent*scale+1-bin,0.,1.));
        }
        cv::Mat image;cv::idct(band,image);
        std::array<double,16> loss{};std::array<unsigned,16> counts{};
        for(const auto& s:captures[held].greens) {
            if(s.x<2||s.y<2||s.x>=14||s.y>=14)continue;
            const unsigned block=unsigned(s.y-2)/3*4+unsigned(s.x-2)/3;
            const double diff=Sample(image,s.x,s.y)-s.value;
            loss[block]+=std::min(100.,diff*diff/s.variance);++counts[block];
        }
        double gain=0,squared=0,base=0;unsigned n=0;
        for(unsigned b=0;b<16;++b)if(counts[b]>=2) {
            loss[b]/=counts[b];const double improvement=previous[b]-loss[b];
            gain+=improvement;squared+=improvement*improvement;base+=previous[b];++n;
        }
        if(n<8)break;
        if(level>=0&&(gain<=std::max(.05*n,.03*base)||
            gain<=2*std::sqrt(std::max(0.,(n*squared-gain*gain)/(n-1)))))break;
        previous=loss;if(level>=0)accepted=scale;
    }
    return accepted;
}
}
std::string ScaleEvidence::Summary() const {
    std::ostringstream text;
    text<<"Auto resolution "<<scale<<"x "<<(reconstruct?"camera RGB":"native Bayer")<<". "
        <<supporting<<" of "<<tested<<" measured regions supported "<<(scale>1?"the selected scale.":"native RGB reconstruction.");
    return text.str();
}
bool MeasureResolution(const ProcessingRequest& request,const BracketingAnalysis& analysis,
    ScaleEvidence& evidence,std::string& error) {
    const auto& data=*analysis.prepared;PreparedTileSampler sampler(data.directory,4);
    std::vector<double> votes;
    for(const auto point:LocatePatches(analysis)) {
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        std::vector<CapturePatch> patches;
        // Respect exposure groups and the local HDR weights the output is
        // actually allowed to use. A clipped or disabled group cannot vote.
        const unsigned gw=data.width/2;
        const float ev=analysis.guideEv[((point.y+8)/2)*gw+(point.x+8)/2];
        const auto shares=Evaluate(request.recipe.automatic?analysis.suggestion:request.recipe.knots,ev);
        for(const auto& source:data.sources) {
            if(!source.enabled||(!request.recipe.automatic&&(source.group>=shares.size()||shares[source.group]<=.001)))continue;
            CapturePatch patch{};
            if(!ReadPatch(analysis,source,point,sampler,request.shouldCancel,patch,error))return false;
            if(!patch.greens.empty())patches.push_back(std::move(patch));
        }
        if(patches.size()<3)continue;
        // Each validation capture is excluded from its own training image.
        std::stable_sort(patches.begin(),patches.end(),[](const auto& a,const auto& b){return a.weight>b.weight;});
        const auto first=ValidateCapture(patches,0);
        votes.push_back(first>0?std::min(first,ValidateCapture(patches,1)):0.);
    }
    evidence.tested=unsigned(votes.size());
    for(double scale:scales) {
        const unsigned n=unsigned(std::count_if(votes.begin(),votes.end(),[&](double v){return v>=scale;}));
        if(scale==1)evidence.supporting=n;
        if(n<std::max(3u,(evidence.tested+4)/5))break;
        evidence.reconstruct=true;evidence.scale=scale;evidence.supporting=n;
    }
    return true;
}
}
