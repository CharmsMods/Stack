#pragma once
#include "ProcessingInternal.h"
#include <memory>

namespace Raw::Bracketing {
// Prepared-session owner. CPU correspondence is authoritative; GPU buffers
// are leased per tile and may be reused only within an active operation.
class TemporalGpu {
public:
    // The owner must outlive the scope. Finish joins no workers: callers must
    // finish their tile work first. Destruction drains on exceptional exits.
    class Operation {
    public:
        explicit Operation(TemporalGpu&);
        ~Operation();
        Operation(const Operation&)=delete;
        Operation& operator=(const Operation&)=delete;
        bool Finish(std::string& error);
    private:
        TemporalGpu* owner_;
    };
    struct WorkspaceStats {
        std::uint64_t residentBytes=0,peakBytes=0,allocations=0,reuses=0;
        std::size_t activeOperations=0;
        bool idle=false;
    };
    WorkspaceStats Workspaces() const;
    explicit TemporalGpu(const ProcessingRequest&);
    ~TemporalGpu();
    bool Available() const;
    bool Ready(std::string& error) const;
    // GPU-only: failure returns an error and leaves groups unchanged.
    // Original-frame selection within fusion is not a backend fallback.
    bool Combine(const ProcessingRequest&,const PreparedDataset&,const Mfd::PreparedRawTile&,
        bool detailFusion,std::vector<std::vector<Observation>>&,std::string&);
    void Report(BracketingAnalysis&) const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
