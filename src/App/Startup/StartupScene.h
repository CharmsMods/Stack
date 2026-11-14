#pragma once
#include "StartupPlatform.h"
#include <filesystem>
namespace Stack::Startup {
// All calls run on the main GL context, before the shell binds its frame target.
// Render establishes framebuffer/program/VAO zero and leaves depth/blend disabled.
class Scene {
public:
    bool Initialize(const std::filesystem::path& assets, const DesktopImage& desktop, int width, int height);
    void Render(float seconds, float handoff, float aspect, float intro = 1.f);
    void PrimeWindow(int width, int height);
    void Shutdown();
    unsigned Texture() const { return color_; }
    bool Ready() const { return color_!=0; }
private:
    unsigned geometry_=0, backdrop_=0, finish_=0, vao_=0, vbo_=0;
    unsigned color_=0, depth_=0, framebuffer_=0, desktop_=0, sharpDesktop_=0, albedo_=0;
    int width_=0,height_=0,vertices_=0,opaqueVertices_=0;
};
}
