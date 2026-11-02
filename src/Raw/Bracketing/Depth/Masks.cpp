#include "Depth.h"
#include "NodeMath/ContractTypes.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace Raw::Bracketing::Depth {
namespace {
// Keep only modes separated by a real valley in the confident depth histogram.
// A continuous sloping surface need not yield a set of depth cutouts.
std::vector<float> Boundaries(const ReconstructionDepth& d) {
    constexpr int bins=48;
    std::array<float,bins> hist{},smooth{};
    for(std::size_t p=0;p<d.confidence.size();++p) if(d.confidence[p]>=.30f)
        hist[std::min(bins-1,int(d.inverseDepth[p]*bins))]+=d.confidence[p];
    for(int i=0;i<bins;++i) for(int j=-2;j<=2;++j)
        smooth[i]+=hist[std::clamp(i+j,0,bins-1)]*(3-std::abs(j));
    std::vector<int> peaks;
    for(int i=0;i<bins;++i)
        if(smooth[i]>0 && (!i || smooth[i]>smooth[i-1]) && (i==bins-1 || smooth[i]>=smooth[i+1]))
            peaks.push_back(i);
    std::stable_sort(peaks.begin(),peaks.end(),[&](int a,int b){return smooth[a]>smooth[b];});
    std::vector<int> modes;
    for(int p:peaks) {
        if(smooth[p]<smooth[peaks.front()]*.06f)break;
        if(std::all_of(modes.begin(),modes.end(),[&](int q){
            return std::abs(p-q)>=7 && *std::min_element(smooth.begin()+std::min(p,q),
                smooth.begin()+std::max(p,q)+1)<.75f*std::min(smooth[p],smooth[q]);}))modes.push_back(p);
        if(modes.size()==4)break;
    }
    std::sort(modes.begin(),modes.end());
    std::vector<float> cuts;
    for(std::size_t i=1;i<modes.size();++i) {
        const auto start=smooth.begin()+modes[i-1],end=smooth.begin()+modes[i]+1;
        const float valley=*std::min_element(start,end);
        const auto first=std::find(start,end,valley);
        auto last=first;while(last+1<end && *(last+1)==valley)++last;
        cuts.push_back(float((first-smooth.begin()+last-smooth.begin()+1)*.5/bins));
    }
    return cuts;
}

cv::Mat SharpGuide(const RawImageData& raw,cv::Size size) {
    cv::Mat out(size,CV_32F);
    const int w=raw.metadata.visibleWidth,h=raw.metadata.visibleHeight;
    for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x){
        const int sx=std::clamp(int((x+.5)*w/size.width),0,w-1),sy=std::clamp(int((y+.5)*h/size.height),0,h-1);
        float signal=0;
        if(raw.reconstructedCameraRgb) {
            const auto p=3*(std::size_t(sy)*w+sx);
            signal=(raw.linearFloatBuffer[p]+2*raw.linearFloatBuffer[p+1]+raw.linearFloatBuffer[p+2])*.25f;
        }else{
            const int bx=sx&~1,by=sy&~1;
            const auto& v=*raw.normalizedMosaicBuffer;
            for(int j=0;j<2;++j)for(int i=0;i<2;++i)
                signal+=.25f*v[std::size_t(std::min(h-1,by+j))*w+std::min(w-1,bx+i)];
        }
        out.at<float>(y,x)=std::log2(.002f+std::max(0.f,signal));
    }
    return out;
}
}
std::vector<DepthMask> ExtractMasks(const ReconstructionDepth& d,const RawImageData& raw,
    const std::function<bool()>& cancel) {
    std::vector<DepthMask> masks;
    if(!d.Available())return masks;
    const auto cuts=Boundaries(d);if(cuts.empty())return masks;
    const int bands=int(cuts.size()+1);
    cv::Mat labels(d.height,d.width,CV_16U,cv::Scalar(0));
    struct Piece {int band;};
    std::vector<Piece> pieces(1);
    std::vector<int> counts(bands);
    const auto measured = std::count_if(d.confidence.begin(),d.confidence.end(),[](float c){return c>=.30f;});
    cv::Mat known(d.height,d.width,CV_8U);
    for (std::size_t p=0;p<d.confidence.size();++p) known.data[p]=d.confidence[p]>=.30f;
    cv::Mat measuredNeighbors;
    cv::boxFilter(known,measuredNeighbors,CV_32F,{5,5},{-1,-1},false);
    for(int band=0;band<bands;++band){
        cv::Mat selected(d.height,d.width,CV_8U);
        for(std::size_t p=0;p<d.confidence.size();++p){
            const auto v=d.inverseDepth[p];
            const bool interior=(band==0 || v>cuts[band-1]+.018f) && (band==bands-1 || v<cuts[band]-.018f);
            selected.data[p]=d.confidence[p]>=.30f && interior;
        }
        // Reject isolated votes and mixed depth neighborhoods. Never fill a
        // pixel merely because its neighbors share a label.
        cv::Mat neighbors;
        cv::boxFilter(selected,neighbors,CV_32F,{5,5},{-1,-1},false);
        selected.setTo(0,(neighbors<6) | (neighbors<.72f*measuredNeighbors));
        cv::Mat components,stats,centers;
        const int n=cv::connectedComponentsWithStats(selected,components,stats,centers,8);
        std::vector<int> accepted;
        for(int i=1;i<n;++i) if(stats.at<int>(i,cv::CC_STAT_AREA)>=std::max(12,int(selected.total()/450)))
            accepted.push_back(i);
        std::stable_sort(accepted.begin(),accepted.end(),[&](int a,int b){return stats.at<int>(a,cv::CC_STAT_AREA)>stats.at<int>(b,cv::CC_STAT_AREA);});
        if(accepted.size()>8)accepted.resize(8);
        int area=0;
        for(int i:accepted)area+=stats.at<int>(i,cv::CC_STAT_AREA);
        // Percentile-normalized measurements pile up at zero and one. A few
        // scattered extreme tracks are not a second scene depth.
        if(area<std::max(12,int(.02*measured)))continue;
        for(int i:accepted){labels.setTo(int(pieces.size()),components==i);pieces.push_back({band});}
        counts[band]=int(accepted.size());
    }
    if(std::count_if(counts.begin(),counts.end(),[](int n){return n>0;})<2)return {};
    const auto scale=std::min(1.,1536./std::max(raw.metadata.visibleWidth,raw.metadata.visibleHeight));
    const cv::Size size(std::max(1,int(std::lround(raw.metadata.visibleWidth*scale))),
                        std::max(1,int(std::lround(raw.metadata.visibleHeight*scale))));
    auto guide=SharpGuide(raw,size);cv::Mat coarse;cv::resize(guide,coarse,labels.size(),0,0,cv::INTER_AREA);
    cv::Mat refined(size,CV_16U,cv::Scalar(0)),alpha(size,CV_8U,cv::Scalar(0));
    for(int y=0;y<size.height;++y){
        if(cancel && cancel())return {};
        for(int x=0;x<size.width;++x){
            const float gx=(x+.5f)*labels.cols/size.width-.5f,gy=(y+.5f)*labels.rows/size.height-.5f;
            const int nx=std::clamp(int(std::round(gx)),0,labels.cols-1),ny=std::clamp(int(std::round(gy)),0,labels.rows-1);
            if(!labels.at<uint16_t>(ny,nx))continue;
            std::array<int,4> ids{};std::array<float,4> votes{};float total=0;
            for(int j=0;j<2;++j)for(int i=0;i<2;++i){
                const int ix=int(std::floor(gx))+i,iy=int(std::floor(gy))+j;
                const int px=std::clamp(ix,0,labels.cols-1),py=std::clamp(iy,0,labels.rows-1);
                const float delta=std::abs(guide.at<float>(y,x)-coarse.at<float>(py,px));
                const float weight=(1-std::abs(gx-ix))*(1-std::abs(gy-iy))/(1+16*delta*delta);
                const int slot=j*2+i;ids[slot]=labels.at<uint16_t>(py,px);votes[slot]=weight;total+=weight;
            }
            int winner=0;float best=0;
            for(int k=0;k<4;++k)if(ids[k]){
                float sum=0;for(int i=0;i<4;++i)if(ids[k]==ids[i])sum+=votes[i];
                if(sum>best){best=sum;winner=ids[k];}
            }
            if(best<=total*.50f)continue;
            refined.at<uint16_t>(y,x)=static_cast<uint16_t>(winner);
            alpha.at<uint8_t>(y,x)=cv::saturate_cast<uint8_t>(510*(best/total-.5f));
        }
    }
    const auto publish=[&](const cv::Mat& selection,const cv::Mat& support,const std::string& name,const std::string& kind){
        const auto rect=cv::boundingRect(selection);if(rect.empty())return;
        DepthMask m;m.name=name;m.width=size.width;m.height=size.height;m.x=rect.x;m.y=rect.y;m.w=rect.width;m.h=rect.height;
        auto bytes=std::make_shared<DepthMask::Bytes>(rect.area());
        cv::Mat dest(rect.size(),CV_8U,bytes->data());alpha(rect).copyTo(dest,selection(rect));
        // Reblending or changing output scale can refine alpha without changing
        // which measured surface this node identifies.
        const std::string geometry=kind+" "+std::to_string(support.cols)+" "+std::to_string(support.rows);
        m.id=Stack::NodeMath::Sha256ContentIdentity({geometry,std::string_view(
            reinterpret_cast<const char*>(support.data),support.total())}).substr(7);
        m.pixels=std::move(bytes);masks.push_back(std::move(m));
    };
    const auto first=int(std::find_if(counts.begin(),counts.end(),[](int n){return n>0;})-counts.begin());
    const auto last=bands-1-int(std::find_if(counts.rbegin(),counts.rend(),[](int n){return n>0;})-counts.rbegin());
    for(int b=last;b>=first;--b){
        if(!counts[b])continue;
        const std::string name=b==last?"Near depth":b==first?"Far depth":"Middle depth "+std::to_string(bands-1-b);
        cv::Mat band=cv::Mat::zeros(size,CV_8U),support=cv::Mat::zeros(labels.size(),CV_8U);
        for(int i=1;i<int(pieces.size());++i)if(pieces[i].band==b){
            cv::bitwise_or(band,refined==i,band);
            cv::bitwise_or(support,labels==i,support);
        }
        publish(band,support,name,"band");
        if(counts[b]>1){int n=0;for(int i=1;i<int(pieces.size());++i)if(pieces[i].band==b)
            publish(refined==i,labels==i,name+" / Piece "+std::to_string(++n),"piece");}
    }
    return masks;
}
}
