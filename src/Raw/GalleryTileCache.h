#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Stack::GalleryTileCache {
struct Snapshot {
    std::uint64_t bytes = 0, limitBytes = 1024ull*1024*1024, hits = 0, misses = 0;
    std::size_t entries = 0;
    bool ready = false, writable = false;
};
// UI reads only atomics; all other calls belong on workers. This owner manages
// only its versioned disposable tile directory, never catalogs or project data.
Snapshot Status();
void Initialize();
void SetLimitMiB(unsigned mib);
void Clear();
std::string Key(const std::filesystem::path& source, std::uint64_t bytes,
    std::int64_t modified, int edge);
bool Read(const std::string& key, std::vector<unsigned char>& pixels, int& width, int& height);
void Write(const std::string& key, const std::vector<unsigned char>& pixels, int width, int height);
}
