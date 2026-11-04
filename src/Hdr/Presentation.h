#pragma once
#include <string>
struct GLFWwindow;
namespace Stack::Hdr {
// UI-thread operations. Targets belong to their window's current GL context.
void InstallPlatformHooks();
void ShutdownPresentation(GLFWwindow* main);
unsigned BeginWindow(GLFWwindow* window,int width,int height,bool main=false,bool opaqueWindow=true);
bool PresentWindow(GLFWwindow* window);
bool PreviewActive();
bool AnyPreviewActive();
bool PreviewActiveForCurrentWindow();
bool PreviewInSdr();
void SetPreviewInSdr(bool value);
const std::string& MainDisplayStatus();
}
