#pragma once
#include "ProcessingInternal.h"
#include <memory>

namespace Raw::Bracketing {
// Prepared-session owner. CPU correspondence is authoritative; GPU buffers
// exist only while combining a tile and retire through the context executor.
class TemporalGpu {
public:
    explicit TemporalGpu(const ProcessingRequest&);
    ~TemporalGpu();
    bool Available() const;
    // False with no error requests the complete equivalent CPU calculation.
    bool Combine(const ProcessingRequest&,const PreparedDataset&,const Mfd::PreparedRawTile&,
        bool detailFusion,std::vector<std::vector<Observation>>&,std::string&);
    void Report(BracketingAnalysis&) const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
