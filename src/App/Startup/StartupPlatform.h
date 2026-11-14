#pragma once
#include <cstdint>
#include <vector>
struct GLFWwindow;
namespace Stack::Startup {
struct DesktopImage { int width=0, height=0; std::vector<unsigned char> rgba; };
// Native state is private to startup and is never serialized as a window preference.
class WindowPresentation {
public:
    DesktopImage Prepare(GLFWwindow* window);
    void Show();
    void Finish();
    bool DisplayChanged() const;
    bool Active() const { return active_; }
private:
    GLFWwindow* window_=nullptr;
    std::intptr_t style_=0, monitor_=0;
    int left_=0, top_=0, width_=0, height_=0;
    int restoreX_=0, restoreY_=0, restoreW_=0, restoreH_=0;
    bool active_=false;
};
}
