#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

// Diagnostic-only real processing load. Sources are read-only; results and
// scratch belong to this capture. Cancellation never joins from a draw call.
class UiProcessingLoad {
public:
    UiProcessingLoad(std::vector<std::filesystem::path> sources,std::filesystem::path output);
    ~UiProcessingLoad();
    void Cancel() { stop.store(true); }
    std::atomic<bool> stop{false}, busy{false}, done{false}, failed{false};
    std::atomic<unsigned> requests{0}, completed{0};
private:
    std::thread worker;
};
