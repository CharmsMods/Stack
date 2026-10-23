#pragma once
#include "LocalDetailEvidence.h"
#include "LocalDetailTransform.h"
#include "ProcessingInternal.h"
#include "PreparedTileSampler.h"
#include <memory>

namespace Raw::Bracketing {
struct LocalDetailPatchResult {
    std::array<std::vector<double>,4> correction;
    std::array<double,4> measurementBound{},uncertaintyBound{};
    std::array<double,4> baselineMeasurementBound{},baselineUncertaintyBound{};
    double captureSupport=0;
    bool accepted=false,mayExpand=false;
    bool centerRejected=false,sampleRejected=false;
};
// One immutable dataset/request/source group, owned by a single tile worker.
// Collisions only discard reuse; complete coordinates are checked on every hit.
class LocalDetailPatchCache {
public:
    LocalDetailPatchCache(const ProcessingRequest&,const PreparedDataset&,const std::vector<std::size_t>&);
    ~LocalDetailPatchCache();
    LocalDetailPatchCache(const LocalDetailPatchCache&)=delete;
    LocalDetailPatchCache& operator=(const LocalDetailPatchCache&)=delete;
private:
    struct State;
    std::unique_ptr<State> m_State;
    friend bool EvaluateLocalDetailPatch(const ProcessingRequest&,const PreparedDataset&,
        const std::vector<std::size_t>&,PreparedTileSampler&,unsigned,unsigned,
        const LocalDetailTransform&,LocalDetailPatchResult&,std::string&,LocalDetailPatchCache*);
};
bool EvaluateLocalDetailPatch(const ProcessingRequest&,const PreparedDataset&,
    const std::vector<std::size_t>& sources,PreparedTileSampler&,unsigned centerX,unsigned centerY,
    const LocalDetailTransform&,LocalDetailPatchResult&,std::string&,LocalDetailPatchCache* cache=nullptr);
}
