#include "Renderer/Internal/RawViewportGpuTiming.h"
#include "Renderer/GLHelpers.h"

namespace Raw {
ViewportGpuTiming::~ViewportGpuTiming() { Reset(false); }
void ViewportGpuTiming::End() {
    if (m_Spans.empty() || m_Spans.back().end) return;
    auto& span = m_Spans.back();
    span.cpuMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - span.cpuStart).count();
    glGenQueries(1, &span.end);
    glQueryCounter(span.end, GL_TIMESTAMP);
}
void ViewportGpuTiming::Reset(bool enabled) {
    End();
    for (auto& span : m_Spans) {
        if (span.start) glDeleteQueries(1, &span.start);
        if (span.end) glDeleteQueries(1, &span.end);
    }
    m_Spans.clear();
    if (m_ServiceStart) glDeleteQueries(1,&m_ServiceStart);
    m_ServiceStart = 0;
    m_Enabled = enabled;
    if (enabled) { glGenQueries(1,&m_ServiceStart); glQueryCounter(m_ServiceStart,GL_TIMESTAMP); }
}
void ViewportGpuTiming::Mark(ViewportStage stage) {
    if (!m_Enabled) return;
    End();
    Span span {stage};
    span.cpuStart = std::chrono::steady_clock::now();
    glGenQueries(1, &span.start);
    glQueryCounter(span.start, GL_TIMESTAMP);
    m_Spans.push_back(span);
}
ViewportGpuTiming::Batch ViewportGpuTiming::Capture() {
    End();
    Batch batch;
    batch.spans = std::move(m_Spans);
    batch.serviceStart = m_ServiceStart;
    m_ServiceStart = 0;
    if (batch.serviceStart) {
        glGenQueries(1,&batch.serviceEnd);
        glQueryCounter(batch.serviceEnd,GL_TIMESTAMP);
        glFlush();
    }
    m_Enabled = false;
    return batch;
}
void ViewportGpuTiming::Release(Batch& batch) {
    for (const auto& span : batch.spans) {
        if (span.start) glDeleteQueries(1,&span.start);
        if (span.end) glDeleteQueries(1,&span.end);
    }
    if (batch.serviceStart) glDeleteQueries(1,&batch.serviceStart);
    if (batch.serviceEnd) glDeleteQueries(1,&batch.serviceEnd);
    batch = {};
}
bool ViewportGpuTiming::Poll(Batch& batch, Measurement& measurement) {
    if (!batch.serviceEnd) return true;
    GLint ready = GL_FALSE;
    glGetQueryObjectiv(batch.serviceEnd,GL_QUERY_RESULT_AVAILABLE,&ready);
    if (!ready) return false;
    measurement = {};
    GLuint64 begin=0,end=0;
    glGetQueryObjectui64v(batch.serviceStart,GL_QUERY_RESULT,&begin);
    glGetQueryObjectui64v(batch.serviceEnd,GL_QUERY_RESULT,&end);
    measurement.gpuServiceMs = end>=begin ? double(end-begin)/1000000.0 : 0;
    for (const auto& span : batch.spans) {
        glGetQueryObjectui64v(span.start,GL_QUERY_RESULT,&begin);
        glGetQueryObjectui64v(span.end,GL_QUERY_RESULT,&end);
        const auto i=static_cast<std::size_t>(span.stage);
        measurement.cpu[i] += span.cpuMs;
        measurement.gpu[i] += end>=begin ? double(end-begin)/1000000.0 : 0;
        measurement.executed[i] = true;
    }
    Release(batch);
    return true;
}
ViewportStageCosts ViewportGpuTiming::Collect(const std::function<bool()>& canceled) {
    auto batch=Capture();
    Measurement measurement;
    // Explicit calibration and validation may wait. Foreground rendering
    // captures numeric evidence and polls it on later worker iterations.
    while (!Poll(batch,measurement)) {
        if (canceled && canceled()) { Release(batch); return {}; }
        GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        if (!fence) { Release(batch); return {}; }
        const auto status=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000);
        glDeleteSync(fence);
        if (status==GL_WAIT_FAILED) { Release(batch); return {}; }
    }
    ViewportStageCosts costs {};
    for (std::size_t i=0;i<costs.size();++i) costs[i]=std::max(measurement.cpu[i],measurement.gpu[i]);
    return costs;
}
}
