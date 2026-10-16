#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Raw {
// Reference-sensor coordinates, before orientation. Inverse depth is relative:
// zero is the far end of the measured range, one is the near end, never metres.
struct ReconstructionDepth {
    unsigned width=0,height=0,sensorWidth=0,sensorHeight=0;
    unsigned acceptedPairs=0;
    float measuredFraction=0;
    std::string message;
    std::vector<float> inverseDepth,confidence;
    bool Available() const {
        return acceptedPairs&&width&&height&&sensorWidth&&sensorHeight&&
            inverseDepth.size()==std::size_t(width)*height&&confidence.size()==inverseDepth.size();
    }
};
struct DepthSample {float inverseDepth=0,confidence=0;};
DepthSample SampleReconstructionDepth(const ReconstructionDepth&,double sensorX,double sensorY);
// Additional geometric rejection in the same reference grid. Unknown depth
// leaves existing correspondence decisions intact.
struct DepthCorrespondenceSupport {
    unsigned width=0,height=0;
    std::vector<std::uint8_t> rejected;
};
}
