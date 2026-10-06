#pragma once

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace FileDialogs {

enum class RasterImageFormat {
    Png = 0,
    Bmp = 1
};

struct RasterImageSaveResult {
    std::string path;
    // The native picker confirms this exact path before format normalization.
    std::string selectedPath;
    RasterImageFormat format = RasterImageFormat::Png;

    explicit operator bool() const { return !path.empty(); }
};

void SetOwnerWindow(GLFWwindow* window, std::function<void()> beforeDialog = {});
std::string OpenImageFileDialog(const char* title = "Load Source Image");
std::string OpenRasterImageFileDialog(const char* title = "Load Image");
std::string OpenLutFileDialog(const char* title = "Load LUT");
std::string SaveLutFileDialog(const char* title = "Save LUT", const char* defaultFileName = "generated_lut.cube");
std::string SavePngFileDialog(const char* title = "Save PNG Image", const char* defaultFileName = "image.png");
// Pick only. The caller reviews the normalized target and obtains any extra
// replacement decision before starting a save.
RasterImageSaveResult SaveGraphImageFileDialog(
    const char* title,
    const char* defaultFileName,
    RasterImageFormat preferredFormat,
    bool allowBmp = true);
std::string OpenLibraryBundleFileDialog(const char* title = "Import Library Bundle");
std::string SaveLibraryBundleFileDialog(const char* title = "Export Library Bundle", const char* defaultFileName = "modular_studio_library.stacklib");
std::string OpenRenderSceneFileDialog(const char* title = "Load Render Scene Snapshot");
std::string SaveRenderSceneFileDialog(const char* title = "Save Render Scene Snapshot", const char* defaultFileName = "render_scene.renderscene");
std::string OpenRenderGltfFileDialog(const char* title = "Import glTF Scene");
std::string OpenWebProjectFileDialog(const char* title = "Import Web Project (.mns.json)");
std::string OpenProjectFileDialog(const char* title = "Load Project (.stack/.comp)");
// Opens packed .stack projects or the project.stack document inside a current
// working project folder. The dispatcher normalizes project.stack to its folder.
std::string OpenStackProjectFileDialog(const char* title = "Open Stack Project");
std::string SaveProjectFileDialog(const char* title = "Save Project (.stack)", const char* defaultFileName = "project.stack");
std::string SaveProjectBundleDialog(const char* title = "Save Working Project", const char* defaultFileName = "Project");
std::string OpenThemePresetFileDialog(const char* title = "Import Theme Preset");
std::string SaveThemePresetFileDialog(const char* title = "Export Theme Preset", const char* defaultFileName = "theme_preset.stacktheme.json");
// These shell dialogs return UTF-8 paths. Use std::filesystem::u8path when
// converting their results to native filesystem paths on Windows.
std::string OpenFolderDialog(const char* title = "Select Folder");
std::vector<std::string> OpenMultipleFilesDialog(const char* title = "Select Files", const char* filter = "All Files\0*.*\0");

} // namespace FileDialogs
