#pragma once

struct GLFWwindow;

namespace Stack::Validation {

bool ValidateEditorRenderWorkerPreviewBatch(GLFWwindow* sharedWindow);
bool ValidateEditorRenderWorkerTileBatch(GLFWwindow* sharedWindow);
bool ValidateTransactionalOutputUpload();

} // namespace Stack::Validation
