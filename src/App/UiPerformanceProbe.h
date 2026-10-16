#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace Stack::UiPerformance::Probe {
enum class Visual { Camera, Node, Catalog, Search, Workspace, Project, Panel, Rail, Toolbar, Controls, Image, ImageCamera, Count };
struct Region { float x = 0, y = 0, width = 0, height = 0; };
struct Observation { std::uint64_t key = 0; Region region; bool valid = false, settledKernels = false; };
void Enable(const std::filesystem::path& output);
bool Active();
void BeginFrame();
void Drawn(Visual visual, std::uint64_t key, Region region = {});
void AcceptedImage(std::uint64_t generation, std::uint64_t revision, const void* owner, bool settledKernels);
void ImageDrawn(std::uint64_t generation, Region region);
Observation Last(Visual visual);
void WatchNode(int nodeId);
void WatchEditor(const void* editor);
int WatchedNode();
std::uint64_t Key(float a, float b = 0, float c = 0);
std::uint64_t Key(const std::string& value);
bool BeginInteraction(Visual visual, const std::string& scenario, const std::string& action,
    const char* dispatch = "imgui-event");
void RequireKey(Visual visual, std::uint64_t key);
void Invalidated(const char* kind, const void* owner);
void Refinement(std::uint64_t content, std::uint64_t accepted, bool native, bool covered,
    int logicalWidth, int storageWidth, const void* owner);
struct ViewportTiming {
    std::uint64_t generation = 0, execution = 0;
    int edge = 0, storageWidth = 0, storageHeight = 0;
    double predictedMs = 0, queueMs = 0, cpuMs = 0, gpuMs = 0, completedMs = 0;
    bool interactive = false, cold = false, refinement = false, trained = false;
    std::uint64_t regionalPixels = 0, completePixels = 0;
};
void CompletedViewport(const ViewportTiming& timing, const void* owner);
void Presented();
void Finish();
} // namespace Stack::UiPerformance::Probe
