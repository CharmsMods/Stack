#include "Raw/RawSourceMetadata.h"
#include "Raw/RawLoader.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <mutex>

namespace Raw {
namespace {
struct MetadataEntry {
    std::filesystem::path path;
    std::uintmax_t size;
    std::filesystem::file_time_type modified;
    std::shared_ptr<const RawMetadata> metadata;
};
struct MetadataCache {
    std::mutex mutex;
    std::deque<MetadataEntry> entries;
};
}

std::shared_ptr<const RawMetadata> AcquireSourceMetadata(const std::string& sourcePath) {
    if (sourcePath.empty()) return {};
    const auto path = std::filesystem::u8path(sourcePath).lexically_normal();
    std::error_code error;
    const auto size = std::filesystem::file_size(path,error);
    if (error) return {};
    const auto modified = std::filesystem::last_write_time(path,error);
    if (error) return {};
    static MetadataCache cache;
    const auto sameFile = [&](const MetadataEntry& entry) {
        return entry.path == path && entry.size == size && entry.modified == modified;
    };
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto found = std::find_if(cache.entries.begin(),cache.entries.end(),sameFile);
        if (found != cache.entries.end()) {
            auto entry = std::move(*found);
            cache.entries.erase(found);
            cache.entries.push_front(std::move(entry));
            return cache.entries.front().metadata;
        }
    }
    // Probe outside the cache lock. Header inspection does not unpack sensor
    // samples, and unrelated projects must not wait on this file's IO.
    auto metadata = std::make_shared<RawMetadata>();
    RawLoader::LoadMetadata(sourcePath,*metadata);
    if (metadata->error.empty() &&
        ((metadata->visibleWidth > 0 ? metadata->visibleWidth : metadata->rawWidth) <= 0 ||
         (metadata->visibleHeight > 0 ? metadata->visibleHeight : metadata->rawHeight) <= 0))
        metadata->error = "The RAW header has no usable image dimensions.";
    // A changed file cannot publish its metadata under the earlier revision.
    if (std::filesystem::file_size(path,error) != size || error) return {};
    if (std::filesystem::last_write_time(path,error) != modified || error) return {};
    std::lock_guard<std::mutex> lock(cache.mutex);
    const auto found = std::find_if(cache.entries.begin(),cache.entries.end(),sameFile);
    if (found != cache.entries.end()) return found->metadata;
    cache.entries.push_front({path,size,modified,std::move(metadata)});
    constexpr std::size_t capacity = 16;
    if (cache.entries.size() > capacity) cache.entries.pop_back();
    return cache.entries.front().metadata;
}
}
