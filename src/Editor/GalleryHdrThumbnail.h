#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Raw { struct RawImageData; }

namespace Stack::GalleryHdr {
// The job owns immutable source inputs. It never holds an editor or window.
struct Request {
    std::filesystem::path source, project;
    std::uint64_t projectRevision = 0;
    std::shared_ptr<const Raw::RawImageData> comparison;
    bool preset = false, savedBracket = false;
    int edge = 512;
};
struct Job {
    std::string key;
    std::uint64_t sourceGeneration = 0;
    std::atomic<bool> canceled{false}, complete{false};
    int lastDemandFrame = -1; // UI thread only
    // Published with complete. Lower-left rows of extended sRGB, straight alpha.
    std::vector<float> pixels;
    int width = 0, height = 0;
};
void Render(const Request& request, Job& job);
}
