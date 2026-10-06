#include "GraphNativeCursor.h"
#include "Utils/GraphCursor.h"
#include "Utils/GraphCursorBitmap.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace GraphNativeCursor {
namespace {
struct Key {
    ImU32 dot=0,accent=0;
    int scale=0,hover=0;
    bool operator==(const Key& other) const {
        return dot==other.dot && accent==other.accent && scale==other.scale && hover==other.hover;
    }
};
struct Entry { Key key; GLFWcursor* cursor=nullptr; };
std::array<Entry,32> cache{};
unsigned next=0;
GLFWcursor* applied=nullptr;
bool ownsCursor=false,previousNoCursorChange=false;

GLFWcursor* Resolve(GLFWwindow* window,const ImGuiExtras::GraphCursorSnapshot& state) {
    const Key key{ImGui::ColorConvertFloat4ToU32(state.dotColor),ImGui::ColorConvertFloat4ToU32(state.glowColor),
        int(std::round(std::clamp(state.scale,0.5f,4.0f)*32)),int(std::round(std::clamp(state.glow,0.0f,1.0f)*8))};
    for (const auto& entry:cache) if (entry.cursor && entry.key==key) return entry.cursor;
    const auto bitmap=ImGuiExtras::BuildGraphCursorBitmap(key.scale/32.0f,
        ImGui::ColorConvertU32ToFloat4(key.dot),ImGui::ColorConvertU32ToFloat4(key.accent),key.hover/8.0f);
    GLFWimage image{bitmap.size,bitmap.size,const_cast<unsigned char*>(bitmap.pixels.data())};
    auto* cursor=glfwCreateCursor(&image,bitmap.hotspot,bitmap.hotspot);
    if (!cursor) return nullptr;
    auto& entry=cache[next++%cache.size()];
    if (entry.cursor==applied) { glfwSetCursor(window,nullptr); applied=nullptr; }
    if (entry.cursor) glfwDestroyCursor(entry.cursor);
    entry={key,cursor};
    return cursor;
}
void Restore(GLFWwindow* window) {
    if (!ownsCursor) return;
    if (!previousNoCursorChange) ImGui::GetIO().ConfigFlags&=~ImGuiConfigFlags_NoMouseCursorChange;
    glfwSetCursor(window,nullptr);
    applied=nullptr; ownsCursor=false;
}
}
void Apply(GLFWwindow* window,const ImGuiExtras::GraphCursorSnapshot& state,bool captured) {
    if (!window) return;
    if (!state.visible) {
        Restore(window);
        if (!captured) glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
        return;
    }
    if (!ownsCursor) {
        previousNoCursorChange=(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NoMouseCursorChange)!=0;
        ownsCursor=true;
    }
    // Prevent the ImGui platform backend from replacing our native dot with an
    // arrow at the start of every frame. Restore its ownership outside the graph.
    ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    if (captured) return;
    if (state.nativeDot) {
        auto* cursor=Resolve(window,state);
        if (cursor!=applied) { glfwSetCursor(window,cursor); applied=cursor; }
        glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
    } else glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_HIDDEN);
}
void Shutdown(GLFWwindow* window) {
    if (window) Restore(window);
    for (auto& entry:cache) { if (entry.cursor) glfwDestroyCursor(entry.cursor); entry={}; }
    next=0;
}
}
