#pragma once

#include <cstddef>
#include <string>

namespace Raw {

struct RawGpuPreprocessTelemetry {
    double sensorUploadMs = 0.0;
    double metadataBuildMs = 0.0;
    double metadataUploadMs = 0.0;
    double gpuDispatchSubmitMs = 0.0;
    double cpuNormalizationMs = 0.0;
    double cpuVarianceMs = 0.0;
    double correctedUploadMs = 0.0;
    double varianceUploadMs = 0.0;
    std::size_t sensorUploadBytes = 0;
    std::size_t metadataUploadBytes = 0;
    std::size_t correctedUploadBytes = 0;
    std::size_t varianceUploadBytes = 0;
    bool sensorUploadCacheHit = false;
    bool correctedCacheHit = false;
    bool gpuDispatched = false;
    bool varianceGenerated = false;
    bool cpuFallback = false;
    std::string fallbackReason;
};

} // namespace Raw
