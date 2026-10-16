#pragma once
struct GLFWwindow;
namespace Stack::Validation {
bool ValidateGraphRendering(bool projectOwnershipOnly = false, bool viewportPreviewOnly = false);
bool ValidateGraphViewportRendering();
bool ValidateGraphViewportWorker(GLFWwindow* sharedWindow);
}
