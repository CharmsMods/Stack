#pragma once
#include "ProcessingInternal.h"
#include "PreparedTileSampler.h"
#include <unordered_map>

namespace Raw::Bracketing {
// ||A||_2^2 <= ||A||_1 ||A||_infinity for A = interpolation * sqrt(sensor
// variance). Shared sensor taps accumulate in the same column. This remains
// conservative for fractional warps, spatially varying gain and signed taps.
class LocalDetailNoise {
public:
    bool Add(const PreparedDataset&,const PreparedSource&,PreparedTileSampler&,
        unsigned,unsigned,double,const Observation&,std::string&);
    double MeasurementBound() const;
    double UncertaintyBound() const {return m_UncertaintyTrace;}
private:
    std::unordered_map<std::uint64_t,double> m_Columns;
    double m_MaximumRow=0,m_UncertaintyTrace=0;
};
}
