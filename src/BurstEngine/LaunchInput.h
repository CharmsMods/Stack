#pragma once
#include <filesystem>
#include <vector>
namespace Stack::BurstEngine {
std::vector<std::filesystem::path> ReadSelectionManifest(const std::filesystem::path&);
// Native multi-select picker. Cancellation returns an empty selection.
std::vector<std::filesystem::path> PickSelection(void* owner=nullptr);
}
