#pragma once
#include "App/UiPerformanceTrace.h"
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include <fstream>
#include "App/UiResponsivenessCapture.h"

class EditorModule;

// State for the opt-in, bounded diagnostic sequence. Normal application runs
// do not construct it. Actions stay with AppShell and the editor's public API.
struct UiPerformanceCapture {
    struct Frame {
        std::string scenario;
        Stack::UiPerformance::FrameSample sample;
        double gpuMs = -1;
        std::array<double, 7> shellPhases {};
        std::uint64_t qpc = 0;
        double intervalMs = 0;
        bool scanBusy = false, thumbnailsBusy = false, processingBusy = false, moving = false;
    };
    struct GpuQuery { unsigned int start = 0, end = 0; std::size_t frame = static_cast<std::size_t>(-1); bool pending = false; };
    std::array<GpuQuery,16> queries {};
    int query = -1;
    bool gpuActive = false;
    std::array<double, 7> shellPhases {};
    std::filesystem::path project, output;
    std::vector<Frame> frames;
    std::chrono::steady_clock::time_point started, nextTick;
    int step = -1, frame = 0, warmup = 0, editNode = -1, dragNode = -1;
    int onlyStep = -1;
    std::uint64_t firstWorkspace = 0, secondWorkspace = 0;
    std::uint64_t hdrPresents = 0, glPresents = 0;
    bool loadQueued = false, ready = false, passed = false, finished = false;
    bool visible = false;
    std::string workload = "idle";
    UiResponsivenessCapture responsiveness;
    std::chrono::steady_clock::time_point previousFrame;
    std::uint64_t frameQpc = 0;
    double frameIntervalMs = 0;
    bool qualityCapture = false;
    bool imageSurfaceCapture = false;
    bool qualityRawOpened = false;
    int qualityStage = 0, qualityStableFrames = 0, qualityTone = -1, qualityLocalEv = -1;
    std::uint64_t qualityRequiredRevision = 0;
    float qualityInitialImageWidth = 0;
    bool qualityWheelQueued = false;
    std::chrono::steady_clock::time_point qualityStageStarted;
    std::ofstream qualityStream;
    std::string failure;
    float pointerX = 0, pointerY = 0;
    std::string Scenario() const;
    void BeginGpu();
    void EndGpu();
    void CloseGpu();
};

bool LoadUiViewportQualityFixture(EditorModule& editor, const std::filesystem::path& source,
    int& toneNode, int& localEvNode);
