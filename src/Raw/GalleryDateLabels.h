#pragma once
#include <cstdint>
#include <string>
namespace Stack::RawWorkspace {
struct GalleryDateLabel { std::string key,label,exact; };
// Refresh once per visible presentation, then request cached labels.
// No file/network work or per-photo clock reads.
std::uint64_t GalleryCalendarRevision();
const GalleryDateLabel& FriendlyGalleryDate(std::int64_t timestamp);
}
