#pragma once
#include "Processor.h"
#include "Raw/MultiFrameDenoise/GpuLocalMotion.h"
#include <algorithm>

namespace Raw::Bracketing {
// The motion algorithm and FP64 verification run on the caller's background
// thread. Only bounded score batches visit the render-owner context.
class GpuMotionSession {
public:
    explicit GpuMotionSession(const ProcessingRequest& request):request_(request) {}
    ~GpuMotionSession() {
        if(!evaluator_) return;
        try {
            std::string error;
            if(request_.executeOpenGlTask)
                request_.executeOpenGlTask([&](std::string&) {evaluator_.reset();return true;},error);
        } catch(...) { /* Context loss must not escape a destructor. */ }
        if(evaluator_) {evaluator_->AbandonContextResources();evaluator_.reset();}
    }
    bool BeginPair(const Mfd::CfaPlanePyramid& reference,const Mfd::CfaPlanePyramid& alternate,
        std::string& error) {
        return request_.executeOpenGlTask&&request_.executeOpenGlTask([&](std::string&) {
            if(!evaluator_) evaluator_=std::make_unique<Mfd::GpuLocalMotionDiscreteEvaluator>();
            evaluator_->BeginPair(reference,alternate);return true;
        },error);
    }
    bool Evaluate(const Mfd::LocalMotionDirectionRequest& direction,
        const std::vector<Mfd::LocalMotionDiscreteCandidate>& candidates,
        std::vector<Mfd::LocalMotionDiscreteScore>& scores,std::string& error) {
        scores.clear();scores.reserve(candidates.size());
        constexpr std::size_t batchLimit=1024;
        for(std::size_t begin=0;begin<candidates.size();begin+=batchLimit) {
            if(request_.shouldCancel&&request_.shouldCancel()) {error="Canceled";return false;}
            const auto end=std::min(candidates.size(),begin+batchLimit);
            std::vector<Mfd::LocalMotionDiscreteCandidate> batch(candidates.begin()+begin,candidates.begin()+end);
            std::vector<Mfd::LocalMotionDiscreteScore> result;
            if(!request_.executeOpenGlTask([&](std::string& taskError) {
                return evaluator_&&evaluator_->Evaluate(direction,batch,result,taskError);
            },error)||result.size()!=batch.size()) return false;
            scores.insert(scores.end(),result.begin(),result.end());
        }
        return true;
    }
    Mfd::GpuLocalMotionDiagnostics Diagnostics() const {
        return evaluator_?evaluator_->Diagnostics():Mfd::GpuLocalMotionDiagnostics{};
    }
private:
    const ProcessingRequest& request_;
    std::unique_ptr<Mfd::GpuLocalMotionDiscreteEvaluator> evaluator_;
};
}
