#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Stack::NodeMath {

inline constexpr int kFieldMeanAlgorithmVersion = 1;

struct FieldMeanResult {
    bool valid = false;
    double value = 0.0;
    std::size_t sampleCount = 0;
    std::string error;
};

class FieldMeanAccumulator {
public:
    bool Add(float sample);
    bool Add(const float* samples, std::size_t count);
    FieldMeanResult Finish() const;

private:
    double m_Sum = 0.0;
    double m_Compensation = 0.0;
    std::size_t m_Count = 0;
    std::string m_Error;
};

FieldMeanResult ComputeFieldMean(const std::vector<float>& samples);

} // namespace Stack::NodeMath
