#pragma once
#include "LocalDetailEvidence.h"
#include "LocalDetailTransform.h"
#include "ProcessingInternal.h"
#include "PreparedTileSampler.h"

namespace Raw::Bracketing {
struct LocalDetailPatchResult {
    std::array<std::vector<double>,4> correction;
    std::array<double,4> measurementBound{},uncertaintyBound{};
    std::array<double,4> baselineMeasurementBound{},baselineUncertaintyBound{};
    double captureSupport=0;
    bool accepted=false,mayExpand=false;
};
bool EvaluateLocalDetailPatch(const ProcessingRequest&,const PreparedDataset&,
    const std::vector<std::size_t>& sources,PreparedTileSampler&,unsigned centerX,unsigned centerY,
    const LocalDetailTransform&,LocalDetailPatchResult&,std::string&);
}
