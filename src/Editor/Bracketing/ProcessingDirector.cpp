#include "ProcessingPresentation.h"
#include <algorithm>
#include <functional>

namespace Stack::Editor {
namespace {
std::string EvidenceKey(const Raw::Bracketing::ProcessingProgress& progress,const std::shared_ptr<const Raw::Bracketing::ProcessingEvidence>& evidence) {
    std::string key=std::to_string(progress.occurrence)+"/"+progress.captureId;
    if(evidence) {
        key+=evidence->provisional?"/provisional":"/measured";
        for(const auto& raster:evidence->rasters)key+="/"+raster.label;
    }
    return key;
}
}
ProcessingSnapshot ProcessingDirector::Select(const ProcessingSnapshot& current,double now,bool reduced) {
    if(readyAt_<0) {
        auto evidenceKey=EvidenceKey(current.progress,current.evidence);
        const double dwell=reduced?.1:2.2;
        if(!hasDisplayed_||liveEvidence_==evidenceKey||now-entered_>=dwell) {
            if(!hasDisplayed_||liveEvidence_!=evidenceKey) {
                if(hasDisplayed_&&displayed_.evidence)seen_.insert(std::hash<std::string>{}(liveEvidence_));
                liveOccurrence_=current.progress.occurrence;liveEvidence_=std::move(evidenceKey);entered_=now;
            }
            displayed_={current.progress,current.evidence};hasDisplayed_=true;
        }
        if(displayed_.evidence&&now-entered_>=dwell)seen_.insert(std::hash<std::string>{}(liveEvidence_));
        auto out=current;out.progress=displayed_.progress;out.evidence=displayed_.evidence;
        return out;
    }
    auto out=current;
    const double duration=deadline_-readyAt_-.5;
    if(!ending_.empty()&&duration>0&&now<deadline_-.5) {
        const auto index=std::min(ending_.size()-1,std::size_t(std::max(0.,now-readyAt_)/duration*ending_.size()));
        out.progress=ending_[index].progress;out.evidence=ending_[index].evidence;
    }
    return out;
}
void ProcessingDirector::Ready(const ProcessingSnapshot& current,double now,bool reduced) {
    if(readyAt_>=0)return;
    readyAt_=now;
    if(!reduced)for(const auto& m:current.milestones) {
        if(!m.evidence||seen_.count(std::hash<std::string>{}(EvidenceKey(m.progress,m.evidence))))continue;
        if(m.progress.stage==Raw::Bracketing::ProcessingStage::RawPreview||m.progress.stage==Raw::Bracketing::ProcessingStage::Completed)continue;
        ending_.push_back(m);
    }
    // Two readable final observations fit the six-second handoff budget.
    if(ending_.size()>2) {
        const auto mask=std::find_if(ending_.rbegin(),ending_.rend(),[](const auto& m) {
            return m.progress.stage==Raw::Bracketing::ProcessingStage::LocalAlignment&&m.evidence->rasters.size()>=2;
        });
        const bool keepMask=mask!=ending_.rend()&&std::distance(ending_.rbegin(),mask)>=2;
        ProcessingMilestone preserved;if(keepMask)preserved=*mask;
        ending_.erase(ending_.begin(),ending_.end()-2);
        if(keepMask)ending_.front()=std::move(preserved);
    }
    deadline_=now+(reduced?.15:std::min(6.,ending_.size()*2.6+.8));
}
void ProcessingDirector::Skip(double now){if(readyAt_>=0)deadline_=std::min(deadline_,now+.15);}
float ProcessingDirector::Finish(double now) const {
    if(readyAt_<0)return 0;
    const auto duration=std::min(.5,deadline_-readyAt_);
    return float(std::clamp((now-(deadline_-duration))/std::max(.001,duration),0.,1.));
}
}
