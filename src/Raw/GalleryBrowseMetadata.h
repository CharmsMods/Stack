#pragma once
#include "Raw/RawWorkspace.h"
namespace Stack::RawWorkspace {
// Low-priority scene grouping uses a small camera JPEG, never a full RAW
// development or a persistent offscreen pixel cache.
void RefreshGalleryBrowseMetadata(const ManagedLayout& layout, SourceRecord& source,
    const CancellationPredicate& canceled);
}
