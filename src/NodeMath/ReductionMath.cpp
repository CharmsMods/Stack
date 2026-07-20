#include "NodeMath/ReductionMath.h"

#include <cmath>

namespace Stack::NodeMath {

bool FieldMeanAccumulator::Add(float sample) {
    if (!m_Error.empty()) return false;
    if (!std::isfinite(sample)) {
        m_Error = "Field Mean encountered a non-finite sample.";
        return false;
    }

    const double value = static_cast<double>(sample);
    const double total = m_Sum + value;
    if (std::abs(m_Sum) >= std::abs(value)) {
        m_Compensation += (m_Sum - total) + value;
    } else {
        m_Compensation += (value - total) + m_Sum;
    }
    m_Sum = total;
    ++m_Count;
    return true;
}

bool FieldMeanAccumulator::Add(const float* samples, std::size_t count) {
    if (samples == nullptr && count != 0) {
        m_Error = "Field Mean received an invalid sample buffer.";
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!Add(samples[index])) return false;
    }
    return true;
}

FieldMeanResult FieldMeanAccumulator::Finish() const {
    FieldMeanResult result;
    result.sampleCount = m_Count;
    if (!m_Error.empty()) {
        result.error = m_Error;
        return result;
    }
    if (m_Count == 0) {
        result.error = "Field Mean requires at least one sample.";
        return result;
    }
    result.value = (m_Sum + m_Compensation) / static_cast<double>(m_Count);
    if (!std::isfinite(result.value)) {
        result.value = 0.0;
        result.error = "Field Mean produced a non-finite result.";
        return result;
    }
    result.valid = true;
    return result;
}

FieldMeanResult ComputeFieldMean(const std::vector<float>& samples) {
    FieldMeanAccumulator accumulator;
    accumulator.Add(samples.data(), samples.size());
    return accumulator.Finish();
}

} // namespace Stack::NodeMath
