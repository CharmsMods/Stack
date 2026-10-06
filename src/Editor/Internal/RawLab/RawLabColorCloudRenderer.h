#pragma once

#include "Editor/EditorModuleTypes.h"

#include <imgui.h>

namespace Stack::Editor::RawLabInternal {

struct RawLabColorCloudGpuVertex {
    float a = 0.0f;
    float b = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float sceneEv = 0.0f;
    float selectedCatch = 0.0f;
};

bool EnsureRawLabColorCloudRenderer(
    EditorModuleTypes::RawWorkspaceLabUiState& state);

void DrawRawLabColorCloudCallback(
    const ImDrawList* drawList,
    const ImDrawCmd* command);

} // namespace Stack::Editor::RawLabInternal
