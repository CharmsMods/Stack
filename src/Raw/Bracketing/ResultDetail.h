#pragma once
#include "Processor.h"

namespace Raw::Bracketing {
// Completed pixels are authoritative, including after a saved result acquires
// fresh source preparation. Missing historical auxiliary planes stay absent.
Preview ReadCompletedResultDetail(const ProcessingRequest&,const BracketingResult&,
    unsigned x,unsigned y,unsigned size);
}
