#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace Raw::Bracketing {

// Image-exposure similarity convention, not evidence of calibration confidence.
inline constexpr double kImageExposureSpanEv=.15;

struct AlignmentExposureCohortInput {
    bool enabled=false,calibrated=false;
    double scale=1.;
};

struct AlignmentExposureCohorts {
    std::vector<std::size_t> order,anchorForSource,memberCount;
};

// Scores describe comparable common image patches, not capture confidence.
// An exact/roundoff tie or absent positive information preserves the original
// median rather than introducing a new preference or removing a capture.
inline std::size_t SelectMeasuredAlignmentAnchor(const std::vector<std::size_t>& members,
    const std::vector<double>& scores,std::size_t median) {
    if(members.empty()||members.size()!=scores.size())return median;
    for(double score:scores)if(!std::isfinite(score))return median;
    const auto best=std::max_element(scores.begin(),scores.end());
    if(!(*best>0))return median;
    std::size_t tied=0;
    for(double score:scores)if(std::abs(score-*best)<=64*std::numeric_limits<double>::epsilon()*
        std::max({1.,std::abs(score),std::abs(*best)}))++tied;
    return tied==1?members[std::size_t(best-scores.begin())]:median;
}

// Indices are stable input order, not presumed capture timestamps. This plan
// neither changes persisted groups/contribution curves nor enables a source.
inline AlignmentExposureCohorts BuildAlignmentExposureCohorts(
    const std::vector<AlignmentExposureCohortInput>& inputs,std::size_t origin) {
    AlignmentExposureCohorts result;
    result.anchorForSource.assign(inputs.size(),origin);
    result.memberCount.assign(inputs.size(),0);
    if(origin>=inputs.size())return result;
    struct Ranked {std::size_t index;double ev;};
    std::vector<Ranked> ranked;
    std::vector<std::size_t> unmeasured;
    for(std::size_t i=0;i<inputs.size();++i) {
        const auto& input=inputs[i];
        // The fixed origin remains a coordinate anchor even when its pixels
        // are excluded from output. No other disabled capture participates.
        if(!input.enabled&&i!=origin)continue;
        if(input.calibrated&&std::isfinite(input.scale)&&input.scale>0)
            ranked.push_back({i,std::log2(input.scale)});
        else unmeasured.push_back(i);
    }
    std::sort(ranked.begin(),ranked.end(),[](const Ranked& a,const Ranked& b) {
        return a.ev==b.ev?a.index<b.index:a.ev<b.ev;
    });
    for(std::size_t first=0;first<ranked.size();) {
        auto end=first+1;
        // Compare to the cohort minimum, never chain adjacent near neighbors.
        while(end<ranked.size()&&ranked[end].ev-ranked[first].ev<=kImageExposureSpanEv)++end;
        std::vector<std::size_t> members;
        for(auto i=first;i<end;++i)members.push_back(ranked[i].index);
        std::sort(members.begin(),members.end());
        const auto anchor=std::binary_search(members.begin(),members.end(),origin)?
            origin:members[members.size()/2];
        result.order.push_back(anchor);
        for(const auto index:members) {
            result.anchorForSource[index]=anchor;
            result.memberCount[index]=members.size();
            if(index!=anchor)result.order.push_back(index);
        }
        first=end;
    }
    // Provisional search gains are not evidence for sharing an anchor.
    for(const auto index:unmeasured) {
        result.order.push_back(index);
        result.anchorForSource[index]=origin;
        result.memberCount[index]=1;
    }
    return result;
}
} // namespace Raw::Bracketing
