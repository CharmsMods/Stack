#include "App/Validation/ValidationSuites.h"

#include "Raw/RawLoader.h"

#include <algorithm>
#include <iostream>
#include <set>

namespace Stack::Validation {

bool ValidateRawDecodeCancellation(const std::string& path) {
    Raw::RawImageData decoded;
    int totalChecks = 0;
    int hashEndCheck = 0;
    int copyStartCheck = 0;
    auto countChecks = [&] {
        ++totalChecks;
        if (hashEndCheck == 0 && !decoded.metadata.sourceContentSha256.empty()) hashEndCheck = totalChecks;
        if (copyStartCheck == 0 && (!decoded.rawBuffer.empty() ||
            !decoded.linearUInt16Buffer.empty() || !decoded.linearFloatBuffer.empty())) copyStartCheck = totalChecks;
        return false;
    };
    if (!Raw::RawLoader::LoadFile(path, decoded, countChecks) || copyStartCheck == 0) {
        std::cerr << "RAW cancellation baseline failed: " << decoded.metadata.error << '\n';
        return false;
    }
    const auto sourceIdentity = decoded.metadata.sourceContentSha256;
    std::set<int> stopChecks { 1, 2, 3, totalChecks / 2, totalChecks - 1, totalChecks,
        copyStartCheck, copyStartCheck + 1 };
    // Exercise hashing, LibRaw callbacks, metadata, and the first pixel rows.
    for (int i = 0; i < 10; ++i) stopChecks.insert(hashEndCheck + i);
    for (int stop : stopChecks) {
        if (stop <= 0 || stop > totalChecks) continue;
        int checks = 0;
        const bool loaded = Raw::RawLoader::LoadFile(path, decoded, [&] { return ++checks == stop; });
        if (loaded || decoded.metadata.error != "RAW load canceled." ||
            decoded.metadata.sourcePath != path || decoded.rawBuffer.capacity() != 0 ||
            decoded.linearUInt16Buffer.capacity() != 0 || decoded.linearFloatBuffer.capacity() != 0 ||
            decoded.metadata.dngGainMaps.capacity() != 0 || !decoded.contentIdentity.empty()) {
            std::cerr << "RAW cancellation failed at check " << stop << ": " << decoded.metadata.error << '\n';
            return false;
        }
    }
    if (!Raw::RawLoader::LoadFile(path, decoded) || decoded.metadata.sourceContentSha256 != sourceIdentity) {
        std::cerr << "RAW decode after cancellation failed: " << decoded.metadata.error << '\n';
        return false;
    }
    std::cout << "RAW cancellation passed at " << stopChecks.size() << " checkpoints; "
        << totalChecks << " checks per complete decode, pixel copy begins at " << copyStartCheck << ".\n";
    return true;
}

} // namespace Stack::Validation
