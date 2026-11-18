#pragma once
#include "Processor.h"
#include "Raw/MultiFrameDenoise/LocalMotion.h"

namespace Raw::Bracketing {
struct PreparedSource;
bool ValidateAlignmentProposals(const ProcessingRequest&,std::string& error);
// Replaces both proposal callbacks (including clearing an absent pair). Loads
// one bounded pair; callback ownership releases the arrays at pair end.
// The actual native pair reference is explicit: origin-based proposals must
// be rebased through accepted anchor geometry for an exposure-cohort pair.
bool AttachAlignmentProposal(const ProcessingRequest&,const PreparedSource& pairReference,
    const PreparedSource& pairSource,
    Mfd::BidirectionalLocalMotionRequest&,std::string& error);
}
