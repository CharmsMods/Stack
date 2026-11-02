#pragma once
#include "../Processor.h"

namespace Raw::Bracketing::Sr {
inline constexpr unsigned MaxAdaptiveScale=4;
struct ScaleEvidence {
    double scale=1;
    bool reconstruct=false;
    unsigned tested=0,supporting=0;
    std::string Summary() const;
};
bool MeasureResolution(const ProcessingRequest&,const BracketingAnalysis&,ScaleEvidence&,std::string&);
}
