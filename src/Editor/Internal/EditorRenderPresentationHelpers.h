#pragma once
#include "Renderer/RenderPipeline.h"
#include "Raw/RawImageAnalysis.h"
#include <chrono>
namespace Stack::EditorRendering::Internal {
inline constexpr int kRawWorkspaceGraphScopeMaxDimension = 192;
inline constexpr int kRawNativeRefinementMaximumRetryCount = 2;
inline constexpr int kRawWorkspaceGradingScopeMaxDimension = 768;
inline double MillisecondsBetween(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

inline Stack::RawAnalysis::CurrentFrameInputStats ToRawCurrentFrameInputStats(const RenderTextureStats& stats) {
    Stack::RawAnalysis::CurrentFrameInputStats rawStats;
    rawStats.valid = stats.valid;
    rawStats.p001Luma = stats.p001Luma;
    rawStats.p01Luma = stats.p01Luma;
    rawStats.p05Luma = stats.p05Luma;
    rawStats.p50Luma = stats.p50Luma;
    rawStats.p95Luma = stats.p95Luma;
    rawStats.p99Luma = stats.p99Luma;
    rawStats.p999Luma = stats.p999Luma;
    rawStats.logAverageLuma = stats.logAverageLuma;
    rawStats.dynamicRangeEv = stats.dynamicRangeEv;
    rawStats.validPixelPercent = stats.validPixelPercent;
    rawStats.hdrPixelPercent = stats.hdrPixelPercent;
    rawStats.displayClipPercent = stats.displayClipPercent;
    return rawStats;
}

inline bool PollSharedTextureFence(GLsync& fence, bool& failed) {
    failed = false;
    if (!fence) {
        return true;
    }
    const GLenum waitResult = glClientWaitSync(fence, 0, 0);
    if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED) {
        glDeleteSync(fence);
        fence = nullptr;
        return true;
    }
    if (waitResult == GL_TIMEOUT_EXPIRED) {
        return false;
    }
    glDeleteSync(fence);
    fence = nullptr;
    failed = true;
    return false;
}

}
