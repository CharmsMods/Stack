#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Raw {

struct RawColorCloudSample {
    std::array<float, 3> rgb {};
    float a = 0.0f;
    float b = 0.0f;
    float sceneEv = 0.0f;
};

struct RawColorCloudPacket {
    std::string sourceKey;
    std::size_t inputFingerprint = 0;
    int workingSpace = -1;
    int colorWarpVersion = 0;
    std::vector<RawColorCloudSample> samples;
};

} // namespace Raw
