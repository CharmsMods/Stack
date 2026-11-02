#pragma once
#include "Persistence/RawProjectModel.h"

namespace Stack::Project {
// Older automatic jobs recorded only their last storage transaction.
inline bool MatchesAutomaticBracketRevision(const MultiFrameSourceSet& set,
    const std::string& projectId, std::uint64_t editRevision, std::uint64_t storageRevision) {
    const auto marker = set.settings.find("autoBracket");
    if (marker == set.settings.end() || !marker->is_object()) return false;
    const auto owner = marker->find("projectId");
    if (owner != marker->end() && *owner != projectId) return false;
    const auto edit = marker->find("editRevision");
    if (edit != marker->end()) return edit->is_number_unsigned() && *edit == editRevision;
    const auto stored = marker->find("storageRevision");
    return stored != marker->end() && stored->is_number_unsigned() && *stored == storageRevision;
}
}
