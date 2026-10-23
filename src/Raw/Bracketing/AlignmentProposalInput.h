#pragma once
#include "Processor.h"
#include "Raw/MultiFrameDenoise/LocalMotion.h"

namespace Raw::Bracketing {
bool ValidateAlignmentProposals(const ProcessingRequest&,std::string& error);
// Loads one bounded pair; callback ownership releases the arrays at pair end.
bool AttachAlignmentProposal(const ProcessingRequest&,const std::string& sourceId,
    Mfd::BidirectionalLocalMotionRequest&,std::string& error);
}
