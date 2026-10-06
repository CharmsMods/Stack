#pragma once

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <string>
#include <vector>

struct RawRgbDenoiseAsyncResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string provider;
    double inferenceMilliseconds = 0.0;
    int completedTiles = 0;
    int totalTiles = 0;
    std::size_t modelFingerprint = 0;
    std::size_t applicationFingerprint = 0;
    int width = 0;
    int height = 0;
    float inputExposureGain = 1.0f;
    double meanAbsoluteModelDelta = 0.0;
    double meanAbsoluteSceneDelta = 0.0;
    std::shared_ptr<const std::vector<float>> modelOutputSrgbProxy;
    std::shared_ptr<const std::vector<float>> outputRgba;
};

// One owner's continuation. The render worker accesses the state and drains
// its future before releasing that owner; inference only writes its result.
struct RawRgbDenoiseState {
    ~RawRgbDenoiseState();

    bool IsCompletionReady() const;
    void RequestCancellation() noexcept;
    void CancelAndWait() noexcept;

    std::future<RawRgbDenoiseAsyncResult> future;
    bool pending = false;
    bool deferred = false;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::size_t modelFingerprint = 0;
    std::size_t applicationFingerprint = 0;
    std::size_t lastCompletedModelFingerprint = 0;
    std::string lastCompletedError;
    std::string status;
    std::string error;
};
