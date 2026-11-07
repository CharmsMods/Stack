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
const ProcessingSnapshot& ProcessingDirector::PrepareSelection(const ProcessingSnapshot& current,const ProcessingMilestone* milestone) {
    const auto& progress=milestone?milestone->progress:current.progress;
    const auto& evidence=milestone?milestone->evidence:current.evidence;
    if(!hasSelected_||selectedSequence_!=current.progress.sequence||selectedPublication_!=current.publishedAt||
       selected_.progress.sequence!=progress.sequence||selected_.progress.stage!=progress.stage||
       selected_.progress.occurrence!=progress.occurrence||selected_.evidence!=evidence) {
        selected_=current;
        selected_.progress=progress;selected_.evidence=evidence;
        selectedSequence_=current.progress.sequence;selectedPublication_=current.publishedAt;
        hasSelected_=true;
    }
    return selected_;
}
const ProcessingSnapshot& ProcessingDirector::Select(const ProcessingSnapshot& current,double now,bool reduced) {
    if(readyAt_<0) {
        if(!hasEvidenceKey_||keyOccurrence_!=current.progress.occurrence||keyCapture_!=current.progress.captureId||keyEvidence_!=current.evidence) {
            inputEvidenceKey_=EvidenceKey(current.progress,current.evidence);
            keyOccurrence_=current.progress.occurrence;keyCapture_=current.progress.captureId;keyEvidence_=current.evidence;
            hasEvidenceKey_=true;
        }
        const auto& evidenceKey=inputEvidenceKey_;
        const double dwell=reduced?.1:2.2;
        if(!hasDisplayed_||liveEvidence_==evidenceKey||now-entered_>=dwell) {
            if(!hasDisplayed_||liveEvidence_!=evidenceKey) {
                if(hasDisplayed_&&displayed_.evidence)seen_.insert(std::hash<std::string>{}(liveEvidence_));
                liveOccurrence_=current.progress.occurrence;liveEvidence_=evidenceKey;entered_=now;
            }
            displayed_={current.progress,current.evidence};hasDisplayed_=true;
        }
        if(displayed_.evidence&&now-entered_>=dwell)seen_.insert(std::hash<std::string>{}(liveEvidence_));
        return PrepareSelection(current,&displayed_);
    }
    const double duration=deadline_-readyAt_-.5;
    if(!ending_.empty()&&duration>0&&now<deadline_-.5) {
        const auto index=std::min(ending_.size()-1,std::size_t(std::max(0.,now-readyAt_)/duration*ending_.size()));
        return PrepareSelection(current,&ending_[index]);
    }
    return PrepareSelection(current,nullptr);
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
