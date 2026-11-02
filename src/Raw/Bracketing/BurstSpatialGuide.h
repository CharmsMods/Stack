#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::Bracketing {
struct PreparedDataset;
struct ProcessingRequest;
struct Observation;
// Four same-colour neighbour directions: horizontal, vertical and the two
// diagonals. Zero means insufficient evidence or a protected detail.
struct BurstSpatialGuide {
    static constexpr unsigned stride=8;
    unsigned width=0,height=0;
    std::vector<std::array<std::uint8_t,4>> cells;
    std::array<float,4> At(float x,float y) const;
};
bool MeasureSpatialGuide(const ProcessingRequest&,PreparedDataset&,std::vector<std::string>&,std::string&);
Observation SmoothBurstSample(const Observation* row,unsigned pitch,
    const std::array<float,4>& agreement);
}
