#include "Renderer/CooperativeGpuScheduling.h"
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
}
int main() {
    using namespace Stack::Renderer;
    using Clock = CooperativeGpuSubmission::Clock;
    const auto start = Clock::time_point{};
    CooperativeGpuSubmission active(400, start);
    Check(!active.Checkpoint(true, false, start), "One small submission may proceed");
    Check(active.Checkpoint(true, false, start), "Fast CPU cannot enqueue unbounded active work");
    active.Drained(start);
    Check(active.Checkpoint(true, false, start + std::chrono::microseconds(626)),
        "High-refresh time budget expires before one millisecond");
    CooperativeGpuSubmission idle(60, start);
    for (int i = 0; i < 7; ++i) Check(!idle.Checkpoint(false, false, start), "Idle work keeps useful batching");
    Check(idle.Checkpoint(false, false, start), "Idle viewport backlog is also count bounded");
    idle.Drained(start);
    Check(idle.Checkpoint(false, false, start + std::chrono::milliseconds(2)), "Slow submission has a time bound");
    idle.Drained(start);
    Check(!idle.Checkpoint(false, true, start) && idle.Checkpoint(false, true, start),
        "Native refinement retains its two-checkpoint bound");
    CooperativeGpuSubmission unknown(0, start);
    Check(!unknown.Checkpoint(false, false, start + std::chrono::milliseconds(1)), "Missing refresh uses finite default");
    Check(unknown.Checkpoint(false, false, start + std::chrono::milliseconds(2)), "Default time budget stays bounded");
    Check(CooperativeRasterPixels(true) == 128ll * 1024 && CooperativeRasterPixels(false) == 512ll * 1024,
        "Interaction has smaller indivisible raster submissions");
    Check(2560ll * 1440 > CooperativeRasterPixels(false) && 1920ll * 1080 > CooperativeRasterPixels(true),
        "Ordinary monitor-sized previews participate in cooperative drawing");
    std::cout << "Cooperative GPU count/time limits and raster admission passed\n";
}
