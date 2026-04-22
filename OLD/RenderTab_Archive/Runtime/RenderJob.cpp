#include "RenderJob.h"

#include <algorithm>
#include <string>

RenderJob::RenderJob()
    : m_State(RenderJobState::Idle)
    , m_StatusText("Live viewport idle.")
    , m_LastResetReason("Preview not reset yet.")
    , m_PreviewRequested(false)
    , m_FinalRequested(false)
    , m_FinalState(RenderJobState::Idle)
    , m_FinalStatusText("No final render queued.")
    , m_FinalSampleCount(0)
    , m_FinalSampleTarget(0) {
}

const char* RenderJob::GetStateLabel() const {
    switch (m_State) {
    case RenderJobState::Idle:
        return "Idle";
    case RenderJobState::Queued:
        return "Queued";
    case RenderJobState::Running:
        return "Running";
    case RenderJobState::Canceled:
        return "Canceled";
    case RenderJobState::Completed:
        return "Completed";
    case RenderJobState::Failed:
        return "Failed";
    }

    return "Unknown";
}

const char* RenderJob::GetFinalStateLabel() const {
    switch (m_FinalState) {
    case RenderJobState::Idle:
        return "Idle";
    case RenderJobState::Queued:
        return "Queued";
    case RenderJobState::Running:
        return "Running";
    case RenderJobState::Canceled:
        return "Canceled";
    case RenderJobState::Completed:
        return "Completed";
    case RenderJobState::Failed:
        return "Failed";
    }

    return "Unknown";
}

void RenderJob::StartPreview() {
    m_PreviewRequested = true;
    m_State = RenderJobState::Queued;
    m_StatusText = "Live viewport queued.";
}

void RenderJob::MarkRunning() {
    m_PreviewRequested = true;
    m_State = RenderJobState::Running;
    m_StatusText = "Live viewport running.";
}

void RenderJob::CancelPreview() {
    m_PreviewRequested = false;
    m_State = RenderJobState::Canceled;
    m_StatusText = "Live viewport paused.";
}

void RenderJob::CompletePreview() {
    m_PreviewRequested = false;
    m_State = RenderJobState::Completed;
    m_StatusText = "Preview sample target reached.";
}

void RenderJob::FailPreview(const std::string& error) {
    m_PreviewRequested = false;
    m_State = RenderJobState::Failed;
    m_StatusText = error.empty() ? "Preview failed." : error;
}

void RenderJob::ResetToIdle() {
    m_PreviewRequested = false;
    m_State = RenderJobState::Idle;
    m_StatusText = "Live viewport idle.";
}

void RenderJob::SetLastResetReason(const std::string& reason) {
    m_LastResetReason = reason.empty() ? std::string("Preview reset.") : reason;
}

void RenderJob::QueueFinal(const RenderFinalRenderSettings& settings) {
    m_FinalRequested = true;
    m_FinalState = RenderJobState::Queued;
    m_FinalSampleCount = 0;
    m_FinalSampleTarget = static_cast<unsigned int>(std::max(settings.sampleTarget, 1));
    m_FinalOutputName = settings.outputName.empty() ? "Final Render" : settings.outputName;
    m_FinalStatusText = "Final still render queued.";
}

void RenderJob::MarkFinalRunning(unsigned int sampleCount, unsigned int sampleTarget) {
    m_FinalRequested = true;
    m_FinalState = RenderJobState::Running;
    m_FinalSampleCount = sampleCount;
    m_FinalSampleTarget = std::max(sampleTarget, 1u);
    m_FinalStatusText =
        "Rendering final still " +
        std::to_string(m_FinalSampleCount) +
        " / " +
        std::to_string(m_FinalSampleTarget) +
        " samples.";
}

void RenderJob::CompleteFinal(const std::string& assetFileName) {
    m_FinalRequested = false;
    m_FinalState = RenderJobState::Completed;
    m_FinalSampleCount = m_FinalSampleTarget;
    m_LatestFinalAssetFileName = assetFileName;
    m_FinalStatusText = assetFileName.empty()
        ? "Final still render completed."
        : "Final still render saved to the Library.";
}

void RenderJob::FailFinal(const std::string& error) {
    m_FinalRequested = false;
    m_FinalState = RenderJobState::Failed;
    m_FinalStatusText = error.empty() ? "Final still render failed." : error;
}

void RenderJob::CancelFinal() {
    m_FinalRequested = false;
    m_FinalState = RenderJobState::Canceled;
    m_FinalStatusText = "Final still render canceled.";
}

void RenderJob::ClearLatestFinalAssetFileName() {
    m_FinalRequested = false;
    m_FinalState = RenderJobState::Idle;
    m_FinalSampleCount = 0;
    m_FinalSampleTarget = 0;
    m_FinalStatusText = "No final render queued.";
    m_LatestFinalAssetFileName.clear();
}
