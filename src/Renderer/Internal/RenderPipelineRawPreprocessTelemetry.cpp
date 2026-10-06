#include "Renderer/RenderPipeline.h"

unsigned int RenderPipeline::RenderRawPipelineWithTelemetry(
    int pipelineId,
    const Raw::RawImageData& raw,
    const Raw::RawDevelopSettings& settings,
    int previewMaxDimension) {
    Raw::RawGpuPipeline& pipeline = m_RawPipelines[pipelineId];
    const unsigned int texture =
        pipeline.Render(
            raw,
            settings,
            previewMaxDimension,
            m_ShouldCancelRender);
    const Raw::RawGpuPreprocessTelemetry& telemetry =
        pipeline.GetLastPreprocessTelemetry();

    m_LastGraphExecutionStats.rawSensorUploadMs +=
        telemetry.sensorUploadMs;
    m_LastGraphExecutionStats.rawMetadataBuildMs +=
        telemetry.metadataBuildMs;
    m_LastGraphExecutionStats.rawMetadataUploadMs +=
        telemetry.metadataUploadMs;
    m_LastGraphExecutionStats.rawGpuPreprocessSubmitMs +=
        telemetry.gpuDispatchSubmitMs;
    m_LastGraphExecutionStats.rawCpuNormalizationMs +=
        telemetry.cpuNormalizationMs;
    m_LastGraphExecutionStats.rawCpuVarianceMs +=
        telemetry.cpuVarianceMs;
    m_LastGraphExecutionStats.rawCorrectedUploadMs +=
        telemetry.correctedUploadMs;
    m_LastGraphExecutionStats.rawVarianceUploadMs +=
        telemetry.varianceUploadMs;
    m_LastGraphExecutionStats.rawSensorUploadBytes +=
        static_cast<std::uint64_t>(telemetry.sensorUploadBytes);
    m_LastGraphExecutionStats.rawMetadataUploadBytes +=
        static_cast<std::uint64_t>(telemetry.metadataUploadBytes);
    m_LastGraphExecutionStats.rawCorrectedUploadBytes +=
        static_cast<std::uint64_t>(telemetry.correctedUploadBytes);
    m_LastGraphExecutionStats.rawVarianceUploadBytes +=
        static_cast<std::uint64_t>(telemetry.varianceUploadBytes);
    if (telemetry.gpuDispatched) {
        ++m_LastGraphExecutionStats.rawGpuPreprocessDispatches;
    }
    if (telemetry.correctedCacheHit) {
        ++m_LastGraphExecutionStats.rawPreprocessCacheHits;
    }
    if (telemetry.cpuFallback) {
        ++m_LastGraphExecutionStats.rawCpuPreprocessFallbacks;
        m_LastGraphExecutionStats.lastRawPreprocessFallback =
            telemetry.fallbackReason;
    }
    return texture;
}
