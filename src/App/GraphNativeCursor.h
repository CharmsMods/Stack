#pragma once
struct GLFWwindow;
namespace ImGuiExtras { struct GraphCursorSnapshot; }
namespace GraphNativeCursor {
void Apply(GLFWwindow* window,const ImGuiExtras::GraphCursorSnapshot& cursor,bool captured);
// Call before destroying the ImGui context or terminating GLFW.
void Shutdown(GLFWwindow* window);
}
