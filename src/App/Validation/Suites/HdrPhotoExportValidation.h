#pragma once
#include <filesystem>
struct GLFWwindow;
namespace Stack::Validation {
void ValidatePhotoExports(GLFWwindow* window, const std::filesystem::path& raw,
    const std::filesystem::path& outputFolder, bool graphOutputOnly = false);
}
