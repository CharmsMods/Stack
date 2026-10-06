#pragma once
#include "ProcessingInternal.h"

namespace Raw::Bracketing {
void CanonicalizeGroupTile(std::vector<std::vector<Observation>>&);
// Rebuildable measurement cache. It is independent of authored contribution
// curves and belongs to the prepared analysis, never to the source captures.
bool ReadGroupTile(const ProcessingRequest&,const PreparedDataset&,unsigned,unsigned,
    std::vector<std::vector<Observation>>&);
bool WriteGroupTile(const ProcessingRequest&,const PreparedDataset&,unsigned,unsigned,
    const std::vector<std::vector<Observation>>&,std::string&);
}
