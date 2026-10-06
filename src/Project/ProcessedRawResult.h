#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Raw {
struct RawImageData;
namespace Hdr { struct Result; }
}

namespace Stack::Project {

struct MfdAdoptedRawResult {
    std::string projectId;
    std::string sourceSetId;
    std::uint64_t inputRevision = 0;
    std::uint64_t contentHash = 0;
    std::shared_ptr<const Raw::RawImageData> rawData;
};

struct HdrAdoptedRawResult {
    std::string projectId;
    std::string sourceSetId;
    std::uint64_t inputRevision = 0;
    std::uint64_t contentHash = 0;
    std::shared_ptr<const Raw::RawImageData> rawData;
    std::shared_ptr<const Raw::Hdr::Result> result;
};

} // namespace Stack::Project
