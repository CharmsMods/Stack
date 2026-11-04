#pragma once
#include <filesystem>
struct GLFWwindow;
namespace Stack::Validation {
// Requires a current main window with the HDR presenter enabled.
void CheckHdrGallery(GLFWwindow* window,const std::filesystem::path& original);
}
