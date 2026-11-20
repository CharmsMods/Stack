#pragma once
#include <imgui.h>
#include <cstdint>
namespace Stack::Renderer::MaskPickerBlur {
void Draw(ImDrawList* draw,ImGuiID slot,ImVec2 low,ImVec2 high,std::uint64_t revision);
void Shutdown();
}

