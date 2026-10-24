#pragma once
#include "AlignmentExposureCohorts.h"
#include "ProcessingInternal.h"
#include "PreparedTileSampler.h"
#include <array>
#include <limits>

namespace Raw::Bracketing {
namespace AlignmentAnchorEvidence {
// A bounded ranking heuristic, not a motion acceptance test or confidence.
// Native patches avoid assuming a decimated preview's noise covariance. The
// supplied sensor model still assumes independent raw-site noise; row noise,
// inaccurate profiles and dominant moving foreground remain limitations.
struct Matrix {double xx=0,xy=0,yy=0;};
inline Matrix ToOrigin(Matrix m,const std::array<double,4>& j) {
    return {j[0]*j[0]*m.xx+2*j[0]*j[2]*m.xy+j[2]*j[2]*m.yy,
        j[0]*j[1]*m.xx+(j[0]*j[3]+j[1]*j[2])*m.xy+j[2]*j[3]*m.yy,
        j[1]*j[1]*m.xx+2*j[1]*j[3]*m.xy+j[3]*j[3]*m.yy};
}
inline double DirectionalScore(Matrix observed,Matrix noise,const std::array<double,4>& j) {
    auto information=ToOrigin({observed.xx-noise.xx,observed.xy-noise.xy,observed.yy-noise.yy},j);
    noise=ToOrigin(noise,j);
    const double denominator=noise.xx+noise.yy;
    if(!(denominator>0)||!std::isfinite(denominator))return std::numeric_limits<double>::quiet_NaN();
    // Keep the signed estimate until aggregation. Clipping every individual
    // noise fluctuation positive would systematically reward noisier frames.
    const double low=.5*(information.xx+information.yy-
        std::hypot(information.xx-information.yy,2*information.xy));
    return low/denominator;
}
inline double Median(std::vector<double> values) {
    std::sort(values.begin(),values.end());const auto n=values.size();
    return n%2?values[n/2]:.5*values[n/2-1]+.5*values[n/2];
}
enum class PatchStatus { Complete, Unavailable, Failed };
inline PatchStatus MeasurePatch(const PreparedSource& source,Mfd::RawCoordinate origin,
    PreparedTileSampler& sampler,double& score,std::string& error) {
    Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout))return PatchStatus::Unavailable;
    const auto mapped=source.alignment.Map(origin);
    if(!std::isfinite(mapped.x)||!std::isfinite(mapped.y)||mapped.x<0||mapped.y<0||
       mapped.x>=source.frame.activeExtent.width||mapped.y>=source.frame.activeExtent.height)return PatchStatus::Unavailable;
    Matrix observed,noise;
    for(unsigned site=0;site<4;++site) {
        const auto cfa=static_cast<Mfd::CfaSite>(site);const auto plane=layout.RawToPlane(mapped,cfa);
        const auto extent=layout.PlaneExtent(cfa,source.frame.activeExtent);
        if(!std::isfinite(plane.x)||!std::isfinite(plane.y)||plane.x<3||plane.y<3||
           plane.x+3>=extent.width||plane.y+3>=extent.height)return PatchStatus::Unavailable;
        const auto cx=std::llround(plane.x),cy=std::llround(plane.y);
        std::array<double,49> signal{},variance{};
        for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
            const auto raw=layout.PlanePixelToRaw({cx+x,cy+y,cfa});
            if(raw.x<0||raw.y<0||raw.x>=source.frame.activeExtent.width||raw.y>=source.frame.activeExtent.height)return PatchStatus::Unavailable;
            Mfd::SameCfaTapInput tap;
            if(!sampler.ReadSample(source.frame,static_cast<std::uint64_t>(raw.x),static_cast<std::uint64_t>(raw.y),tap,&error)) {
                if(error.empty())error="Could not read prepared pixels for alignment-anchor evidence.";
                return PatchStatus::Failed;
            }
            if(tap.sampleFlags||!std::isfinite(tap.normalizedSample)||!std::isfinite(tap.comparisonGain)||tap.comparisonGain<=0)return PatchStatus::Unavailable;
            const auto p=(y+3)*7+x+3;signal[p]=tap.normalizedSample*tap.comparisonGain;
            Mfd::GainPropagatedVariance v;
            if(!Mfd::PropagateKnownGainVariance(source.noise.sites[site],tap.comparisonGain,
                Mfd::NonnegativeShotPilot(signal[p]),source.frame.calibration.usableSpanByCfaSite[site],v)||
               !std::isfinite(signal[p])||!std::isfinite(v.variance)||v.variance<=0)return PatchStatus::Unavailable;
            variance[p]=v.variance;
        }
        for(unsigned y=0;y<6;++y)for(unsigned x=0;x<6;++x) {
            const auto p=y*7+x;const double gx=(signal[p+1]-signal[p])*.5,gy=(signal[p+7]-signal[p])*.5;
            observed.xx+=gx*gx;observed.xy+=gx*gy;observed.yy+=gy*gy;
            // Forward derivatives share the center tap. Its covariance must
            // be subtracted off diagonal as well as on both diagonals.
            noise.xx+=(variance[p+1]+variance[p])*.25;
            noise.xy+=variance[p]*.25;
            noise.yy+=(variance[p+7]+variance[p])*.25;
        }
    }
    score=DirectionalScore(observed,noise,source.alignment.linear);
    return std::isfinite(score)?PatchStatus::Complete:PatchStatus::Unavailable;
}
} // namespace AlignmentAnchorEvidence

// Changes only the anchor/work order of existing eligible exposure cohorts.
// No source is enabled, excluded, rescaled, or substituted for the user's origin.
inline bool RankAlignmentAnchors(const ProcessingRequest& request,const PreparedDataset& data,
    AlignmentExposureCohorts& cohorts,std::vector<std::string>& diagnostics,std::string& error) {
    using namespace AlignmentAnchorEvidence;
    if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
    if(request.recipe.aiAlignment||!request.alignmentProposals.empty()||data.origin>=data.sources.size())return true;
    auto pending=cohorts;PreparedTileSampler sampler(data.directory,8);
    std::vector<bool> visited(data.sources.size());
    for(const auto medianAnchor:cohorts.order) {
        if(visited[medianAnchor]||cohorts.anchorForSource[medianAnchor]!=medianAnchor||
           medianAnchor==data.origin||cohorts.memberCount[medianAnchor]<2||
           !(data.sources[medianAnchor].scale<data.sources[data.origin].scale))continue;
        std::vector<std::size_t> members;
        for(const auto i:cohorts.order)if(cohorts.anchorForSource[i]==medianAnchor){members.push_back(i);visited[i]=true;}
        const auto keep=[&](const std::string& reason){diagnostics.push_back(data.sources[medianAnchor].id+": kept median alignment anchor; "+reason+".");};
        if(std::any_of(members.begin(),members.end(),[&](auto i){return data.sources[i].localMotion!=nullptr;}))continue;
        bool comparable=true;
        for(const auto i:members) {
            const auto& s=data.sources[i];
            comparable=comparable&&s.enabled&&s.exposureCalibrated&&s.alignmentDiagnostic.accepted&&
                s.noise.quality!=Mfd::NoiseModelQuality::GenericLowConfidence&&s.noise.quality!=Mfd::NoiseModelQuality::Unavailable&&
                s.noise.preparedFrameCacheKey==s.frame.cacheKey;
            for(const auto& site:s.noise.sites)comparable=comparable&&Mfd::ValidateSiteNoiseProfile(site);
            for(const auto v:s.alignment.linear)comparable=comparable&&std::isfinite(v);
        }
        if(!comparable){keep("comparable image geometry/noise evidence unavailable");continue;}
        std::vector<std::vector<double>> values(members.size());std::array<bool,4> quadrants{};
        // Fixed sampling budget, not a confidence threshold. Nearest native
        // CFA centers retain raw noise statistics but have subpixel phase
        // sensitivity; broad patches/distributed aggregation mitigate, not
        // eliminate, that limitation and modest parallax/foreground changes.
        std::vector<std::array<double,64>> patches(members.size());
        std::vector<std::array<bool,64>> available(members.size());
        // Source-major traversal prevents a cohort larger than the bounded
        // tile cache from rereading every source tile for every patch.
        for(std::size_t m=0;m<members.size();++m)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
            const Mfd::RawCoordinate point{(x+.5)*data.width/8.,(y+.5)*data.height/8.};
            const auto p=y*8+x;
            const auto status=MeasurePatch(data.sources[members[m]],point,sampler,patches[m][p],error);
            if(status==PatchStatus::Failed)return false;
            available[m][p]=status==PatchStatus::Complete;
        }
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            const auto p=y*8+x;bool common=true;
            for(const auto& mask:available)common=common&&mask[p];
            if(!common)continue;
            quadrants[(y>=4?2:0)+(x>=4?1:0)]=true;
            for(std::size_t m=0;m<members.size();++m)values[m].push_back(patches[m][p]);
        }
        const auto commonCount=values.front().size();
        if(!std::all_of(quadrants.begin(),quadrants.end(),[](bool v){return v;})) {
            keep(std::to_string(commonCount)+" common patches cover "+std::to_string(std::count(quadrants.begin(),quadrants.end(),true))+" of 4 quadrants");continue;
        }
        std::vector<double> scores;for(auto& v:values)scores.push_back(Median(std::move(v)));
        const auto selection=SelectMeasuredAlignmentAnchor(members,scores,medianAnchor);
        if(selection==medianAnchor){keep("signed directional evidence has no unique different preference across "+std::to_string(commonCount)+" common patches");continue;}
        for(const auto i:members)pending.anchorForSource[i]=selection;
        const auto first=std::find(pending.order.begin(),pending.order.end(),medianAnchor);
        const auto chosen=std::find(pending.order.begin(),pending.order.end(),selection);
        std::iter_swap(first,chosen);
        diagnostics.push_back(data.sources[selection].id+": selected image-derived alignment anchor from "+
            std::to_string(commonCount)+" common patches.");
    }
    cohorts=std::move(pending);return true;
}
} // namespace Raw::Bracketing
