#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Raw::Bracketing {
struct CaptureContentIdentity {
    std::string sha256;
    std::uint64_t bytes = 0;
};

// Order-independent identity shared by Gallery's queue and selected-file plans.
// Empty means a missing fingerprint or fewer than two independent captures.
std::string SourceSetIdentity(const std::vector<CaptureContentIdentity>& sources);
} // namespace Raw::Bracketing
