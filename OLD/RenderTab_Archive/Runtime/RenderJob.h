#pragma once

#include "RenderSettings.h"

#include <string>

enum class RenderJobState {
    Idle = 0,
    Queued,
    Running,
    Canceled,
    Completed,
    Failed
};

class RenderJob {
public:
    RenderJob();

    RenderJobState GetState() const { return m_State; }
    const std::string& GetStatusText() const { return m_StatusText; }
    const std::string& GetLastResetReason() const { return m_LastResetReason; }
    bool IsPreviewRequested() const { return m_PreviewRequested; }
    const char* GetStateLabel() const;
    bool IsFinalRequested() const { return m_FinalRequested; }
    bool IsFinalRunning() const { return m_FinalRequested && m_FinalState == RenderJobState::Running; }
    RenderJobState GetFinalState() const { return m_FinalState; }
    const char* GetFinalStateLabel() const;
    const std::string& GetFinalStatusText() const { return m_FinalStatusText; }
    unsigned int GetFinalSampleCount() const { return m_FinalSampleCount; }
    unsigned int GetFinalSampleTarget() const { return m_FinalSampleTarget; }
    const std::string& GetFinalOutputName() const { return m_FinalOutputName; }
    const std::string& GetLatestFinalAssetFileName() const { return m_LatestFinalAssetFileName; }

    void StartPreview();
    void MarkRunning();
    void CancelPreview();
    void CompletePreview();
    void FailPreview(const std::string& error);
    void ResetToIdle();
    void SetLastResetReason(const std::string& reason);
    void QueueFinal(const RenderFinalRenderSettings& settings);
    void MarkFinalRunning(unsigned int sampleCount, unsigned int sampleTarget);
    void CompleteFinal(const std::string& assetFileName);
    void FailFinal(const std::string& error);
    void CancelFinal();
    void ClearLatestFinalAssetFileName();

private:
    RenderJobState m_State = RenderJobState::Idle;
    std::string m_StatusText;
    std::string m_LastResetReason;
    bool m_PreviewRequested = false;
    bool m_FinalRequested = false;
    RenderJobState m_FinalState = RenderJobState::Idle;
    std::string m_FinalStatusText;
    unsigned int m_FinalSampleCount = 0;
    unsigned int m_FinalSampleTarget = 0;
    std::string m_FinalOutputName;
    std::string m_LatestFinalAssetFileName;
};
