#pragma once
#include "Raw/Bracketing/Progress.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>

namespace Stack::Project {
struct ProcessingMilestone {
    Raw::Bracketing::ProcessingProgress progress;
    std::shared_ptr<const Raw::Bracketing::ProcessingEvidence> evidence;
};
struct ProcessingSnapshot {
    Raw::Bracketing::ProcessingProgress progress;
    std::map<std::string,std::shared_ptr<const Raw::Bracketing::ProcessingThumbnail>> images;
    double publishedAt=0;
    std::shared_ptr<const Raw::Bracketing::ProcessingEvidence> evidence;
    std::vector<ProcessingMilestone> milestones;
    std::size_t EvidenceBytes() const;
};
class ProcessingMailbox {
public:
    explicit ProcessingMailbox(std::uint64_t generation,std::string reference={}):generation_(generation),reference_(std::move(reference)){}
    void Publish(Raw::Bracketing::ProcessingProgress event);
    void DiscardEvidence();
    std::shared_ptr<const ProcessingSnapshot> Read() const {return std::atomic_load(&snapshot_);}
private:
    std::uint64_t generation_;
    std::shared_ptr<const ProcessingSnapshot> snapshot_;
    std::mutex mutex_;
    std::string reference_;
    std::map<std::string,std::uint64_t> imageAge_;
    std::uint64_t sequence_=0,occurrence_=0,lastInputSequence_=0;
};
}
