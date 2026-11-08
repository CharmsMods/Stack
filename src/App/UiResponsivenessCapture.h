#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include "UiProcessingLoad.h"

// Opt-in scenario state. This never exists outside the existing diagnostic owner.
struct UiResponsivenessCapture {
    bool enabled = false, initialized = false, scanBusy = false, thumbnailsBusy = false;
    bool processingBusy = false, moving = false, releaseMouse = false;
    int phase = -1, event = 0, cycle = 0, nextSource = 0, onlyPhase = -1;
    int click = 0, clickVisual = 0;
    std::string clickAction;
    double began = -1, phaseStarted = 0, lastLoad = -100, lastScan = -100;
    float pointerX = 900, pointerY = 500, dragStartX = 0;
    std::uint64_t workerWorkspace = 0, processingRequests = 0, processingCompletions = 0;
    bool processingOutstanding = false;
    std::vector<std::filesystem::path> sources;
    std::string scenario = "gallery_startup";
    std::shared_ptr<UiProcessingLoad> processing;
};
