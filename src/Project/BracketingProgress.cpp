#include "BracketingProgress.h"
#include <algorithm>

namespace Stack::Project {
using namespace Raw::Bracketing;
std::size_t ProcessingSnapshot::EvidenceBytes() const {
    std::set<const ProcessingEvidence*> counted;std::size_t bytes=0;
    const auto add=[&](const auto& value){if(value&&counted.insert(value.get()).second)bytes+=value->Bytes();};
    add(evidence);for(const auto& m:milestones)add(m.evidence);
    for(const auto& [id,image]:images)bytes+=image->rgba.size();
    return bytes;
}
void ProcessingMailbox::Publish(ProcessingProgress event) {
    if(event.generation&&event.generation!=generation_)return;
    std::lock_guard<std::mutex> lock(mutex_);
    if(event.sequence&&event.sequence<=lastInputSequence_)return;
    if(event.sequence)lastInputSequence_=event.sequence;
    const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto previous=Read();
    if(previous&&(previous->progress.stage==ProcessingStage::Canceled||previous->progress.stage==ProcessingStage::Failed))return;
    const bool same=previous&&previous->progress.stage==event.stage&&previous->progress.pass==event.pass;
    if(event.occurrence&&previous&&event.occurrence<previous->progress.occurrence)return;
    if(same&&!event.evidence&&!event.thumbnail&&!event.evidenceOnly&&
       event.captureId==previous->progress.captureId&&now-previous->publishedAt<.1&&(!event.total||event.completed<event.total))return;
    auto next=previous?std::make_shared<ProcessingSnapshot>(*previous):std::make_shared<ProcessingSnapshot>();
    if(!same) {++occurrence_;next->evidence.reset();}
    else if(!event.captureId.empty()&&previous->progress.captureId!=event.captureId)next->evidence.reset();
    event.generation=generation_;event.sequence=++sequence_;event.occurrence=occurrence_;
    if(event.evidenceOnly&&same) {
        auto state=previous->progress;state.sequence=event.sequence;
        if(!event.captureId.empty())state.captureId=event.captureId;
        next->progress=std::move(state);
    }else {
        if(same&&event.total==previous->progress.total&&event.captureId==previous->progress.captureId)
            event.completed=std::max(event.completed,previous->progress.completed);
        next->progress=event;
    }
    next->publishedAt=now;
    if(event.thumbnail&&event.thumbnail->width&&event.thumbnail->height&&event.thumbnail->width<=512&&event.thumbnail->height<=512&&
        event.thumbnail->rgba.size()==std::size_t(event.thumbnail->width)*event.thumbnail->height*4) {
        const auto& id=event.thumbnail->frameId;
        if(next->images.size()>=16&&!next->images.count(id)) {
            auto oldest=imageAge_.end();
            for(auto it=imageAge_.begin();it!=imageAge_.end();++it)
                if(it->first!=reference_&&(oldest==imageAge_.end()||it->second<oldest->second))oldest=it;
            if(oldest!=imageAge_.end()){next->images.erase(oldest->first);imageAge_.erase(oldest);}
        }
        next->images[id]=event.thumbnail;imageAge_[id]=sequence_;
    }
    if(event.evidence&&event.evidence->Bytes()<=4ull*1024*1024&&event.evidence->rasters.size()<=3&&
       event.evidence->vectors.size()<=512&&event.evidence->curve.size()<=256&&event.evidence->observations.size()<=256&&event.evidence->kernels.size()<=64) {
        bool valid=true;
        for(const auto& r:event.evidence->rasters)valid&=r.width>0&&r.height>0&&r.width<=512&&r.height<=512&&r.rgba.size()==std::size_t(r.width)*r.height*4;
        if(valid) {
            if(same&&event.stage==ProcessingStage::LocalAlignment&&next->evidence&&
               previous->progress.captureId==event.captureId&&event.evidence->vectors.empty()&&!event.evidence->rasters.empty()) {
                auto combined=std::make_shared<ProcessingEvidence>(*event.evidence);
                combined->vectors=next->evidence->vectors;next->evidence=std::move(combined);
            }else next->evidence=event.evidence;
        }
    }
    next->progress.thumbnail.reset();next->progress.evidence.reset();
    if(next->milestones.empty()||next->milestones.back().progress.occurrence!=occurrence_)
        next->milestones.push_back({next->progress,next->evidence});
    else next->milestones.back()={next->progress,next->evidence};
    while(next->milestones.size()>24||next->EvidenceBytes()>48ull*1024*1024) {
        if(next->milestones.size()<2)break;
        next->milestones.erase(next->milestones.begin());
    }
    std::shared_ptr<const ProcessingSnapshot> immutable=std::move(next);
    std::atomic_store(&snapshot_,std::move(immutable));
}
void ProcessingMailbox::DiscardEvidence() {
    std::lock_guard<std::mutex> lock(mutex_);auto current=Read();
    if(!current||(!current->evidence&&current->milestones.empty()))return;
    auto next=std::make_shared<ProcessingSnapshot>(*current);next->evidence.reset();next->milestones.clear();
    std::shared_ptr<const ProcessingSnapshot> immutable=std::move(next);std::atomic_store(&snapshot_,std::move(immutable));
}
}
