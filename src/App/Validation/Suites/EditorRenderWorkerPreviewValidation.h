#pragma once

struct GLFWwindow;

namespace Stack::Validation {

bool ValidateEditorRenderWorkerPreviewBatch(GLFWwindow* sharedWindow);
bool ValidateEditorRenderWorkerTileBatch(GLFWwindow* sharedWindow);
bool ValidateTransactionalOutputUpload();
bool ValidateRawViewportRegions();
bool ValidateRawViewportDenoiseReuse();
bool ValidateRawViewportDetailDrawing();
bool ValidateRawViewportTransitions();
bool ValidateRawViewportPresentation();
bool ValidateRawViewportCalibration(GLFWwindow* sharedWindow);
bool ValidateRawViewportInteraction(GLFWwindow* sharedWindow);

} // namespace Stack::Validation
