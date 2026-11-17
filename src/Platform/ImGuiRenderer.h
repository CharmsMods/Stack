#pragma once
struct ImDrawData;
namespace Stack::Platform {
// Final-window compositor only: default lower-left clip origin, explicit
// destination framebuffer, and caller-owned state after return. No GL state
// readback or restoration. Other callers retain the normal ImGui backend.
void RenderOwnedUiDrawData(ImDrawData* data);
}
