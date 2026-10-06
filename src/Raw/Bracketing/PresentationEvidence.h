#pragma once
#include "ProcessingInternal.h"
#include <chrono>
#include <mutex>

namespace Raw::Bracketing {
// Observers are optional and never feed values back into the processor.
void PublishEvidence(const ProcessingRequest&,ProcessingStage,
    std::shared_ptr<const ProcessingEvidence>,const std::string& capture={});
EvidenceRaster MakeEvidenceRaster(const std::string&,EvidenceKind,unsigned,unsigned,
    double rawWidth,double rawHeight,float minimum=0,float maximum=1);
void EvidenceColor(EvidenceRaster&,std::size_t,const std::array<double,3>&,const RawMetadata&);
void EvidenceScalar(EvidenceRaster&,std::size_t,float,bool known=true);
void ObserveAlignment(const ProcessingRequest&,const PreparedDataset&,const PreparedSource&);
void ObserveExposure(const ProcessingRequest&,const PreparedDataset&,const PreparedSource&);
void ObserveNoise(const ProcessingRequest&,const PreparedSource&,
    const std::vector<EvidencePoint>& observations={});
void ObserveMotion(const ProcessingRequest&,const PreparedSource&,const Mfd::LocalMotionGrid&,bool provisional);
void ObserveReliability(const ProcessingRequest&,const PreparedSource&,const Mfd::ReliabilityMap&,bool provisional);
std::size_t PresentationGroup(const ProcessingRequest&);

struct PresentationSample {
    std::array<double,3> rgb{};
    float first=0,second=0;
    bool known=true;
};
// One low-resolution atlas per active tiled stage. Tile readers see only their
// own completed production buffers; the mutex protects this observational atlas.
class ProcessingEvidenceStream {
public:
    void Begin(const ProcessingRequest&,ProcessingStage,unsigned width,unsigned height,const RawMetadata&);
    void Tile(const ProcessingRequest&,ProcessingStage,unsigned x,unsigned y,unsigned w,unsigned h,
        const std::function<PresentationSample(unsigned,unsigned)>&);
    void Flush(const ProcessingRequest&);
    void Details(const ProcessingEvidence&);
private:
    std::mutex mutex_;
    ProcessingStage stage_=ProcessingStage::Assets;
    unsigned rawWidth_=0,rawHeight_=0,step_=1;
    RawMetadata metadata_;
    ProcessingEvidence evidence_;
    std::chrono::steady_clock::time_point last_{};
};
}
