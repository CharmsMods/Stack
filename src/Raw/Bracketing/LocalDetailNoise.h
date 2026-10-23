#pragma once
#include "ProcessingInternal.h"
#include "PreparedTileSampler.h"
#include <array>
#include <unordered_map>

namespace Raw::Bracketing {
// ||A||_2^2 <= ||A||_1 ||A||_infinity for A = interpolation * sqrt(sensor
// variance). Shared sensor taps accumulate in the same column. This remains
// conservative for fractional warps, spatially varying gain and signed taps.
class LocalDetailNoise {
public:
    struct SensorColumn {std::uint64_t index=0;double amplitude=0;};
    struct Row {
        std::array<SensorColumn,Mfd::kSameCfaTapCount> columns{};
        std::size_t count=0;
        double sum=0;
    };
    // Prepared contributions may be reused, but every patch still accumulates
    // shared sensor columns in its original sample/tap order.
    static bool PrepareRow(const PreparedDataset&,const PreparedSource&,PreparedTileSampler&,
        unsigned,unsigned,double,const Observation&,Row&,std::string&);
    void Add(const Observation&,const Row&);
    bool Add(const PreparedDataset&,const PreparedSource&,PreparedTileSampler&,
        unsigned,unsigned,double,const Observation&,std::string&);
    double MeasurementBound() const;
    double UncertaintyBound() const {return m_UncertaintyTrace;}
private:
    std::unordered_map<std::uint64_t,double> m_Columns;
    double m_MaximumRow=0,m_UncertaintyTrace=0;
};
}
