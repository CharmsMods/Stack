#include "Editor/Internal/RawLab/RawLabGalleryLayoutAnimation.h"

#include <cmath>
#include <iostream>
#include <utility>

using Stack::Editor::RawLabInternal::GalleryLayoutAnimation;
namespace {
bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
const ImRect clip({0, 0}, {1000, 700});
void Observe(GalleryLayoutAnimation& motion, float x) {
    const ImRect image({x, 50}, {x + 120, 130});
    motion.ObserveSlot("image", image, clip);
    motion.Observe("image", image, clip);
    motion.EndFrame();
}
float DrawX(GalleryLayoutAnimation& motion) {
    ImDrawList draw(ImGui::GetDrawListSharedData());
    draw._ResetForNewFrame();
    draw.PushClipRect(clip.Min, clip.Max);
    motion.Draw(&draw, clip, [](const auto&) { return GalleryLayoutAnimation::Texture{7, 120, 80}; });
    return draw.VtxBuffer.empty() ? -1.f : draw.VtxBuffer[0].pos.x;
}
bool TestSortTransitions(bool grid) {
    GalleryLayoutAnimation motion;
    int frame = 1;
    double time = 1.;
    motion.BeginFrame("folder", grid, frame++, time, 0, false);
    Observe(motion, 20);
    for (std::uint64_t mode = 1; mode <= 9; ++mode) {
        const float origin = mode % 2 ? 20.f : 420.f;
        const float destination = mode % 2 ? 420.f : 20.f;
        time += 1.;
        motion.BeginFrame("folder", grid, frame++, time, mode, false);
        Observe(motion, destination);
        if (!Check(motion.Active() && std::abs(DrawX(motion) - origin) < .01f,
            "sort starts at the displayed position in either view")) return false;
        motion.BeginFrame("folder", grid, frame++, time + .16, mode, false);
        Observe(motion, destination);
        if (!Check(std::abs(DrawX(motion) - 220.f) < .01f, "sort passes through its midpoint")) return false;
        motion.BeginFrame("folder", grid, frame++, time + .33, mode, false);
        Observe(motion, destination);
        if (!Check(!motion.Active() && motion.ContentAlpha() == 1.f && !motion.Protects("image"),
            "sort settles exactly and releases its texture protection")) return false;
    }
    return true;
}
bool TestInterruptions() {
    GalleryLayoutAnimation motion;
    motion.BeginFrame("folder", true, 1, 0., 0, false); Observe(motion, 20);
    motion.BeginFrame("folder", true, 2, 1., 1, false); Observe(motion, 420);
    motion.BeginFrame("folder", true, 3, 1.16, 1, false); Observe(motion, 420);
    const float displayed = DrawX(motion);
    motion.SetViewport(clip);
    motion.PrepareSwitch("image");
    if (!Check(motion.AnchorPending() &&
        std::abs(motion.AnchorFraction().x - (displayed + 60.f) / clip.GetWidth()) < .001f,
        "a view switch during sorting anchors the displayed image position")) return false;
    motion.BeginFrame("folder", false, 4, 1.17, 2, false); Observe(motion, 700);
    if (!Check(std::abs(DrawX(motion) - displayed) < .01f,
        "a rapid sort/view reversal starts at the last presented geometry")) return false;
    motion.BeginFrame("folder", false, 5, 1.2, 3, true); Observe(motion, 400);
    if (!Check(!motion.Active() && motion.ContentAlpha() == 1.f,
        "reduced motion cancels an in-flight transition")) return false;
    motion.BeginFrame("folder", true, 6, 1.3, 4, false); Observe(motion, 100);
    motion.BeginFrame("folder", true, 9, 1.4, 5, false); Observe(motion, 500);
    if (!Check(!motion.Active(), "hiding the view discards stale motion")) return false;
    motion.BeginFrame("other-folder", false, 10, 1.5, 6, false); Observe(motion, 100);
    if (!Check(!motion.Active(), "changing source identity discards stale motion")) return false;
    GalleryLayoutAnimation empty;
    empty.BeginFrame("folder", true, 1, 0., 0, false); empty.EndFrame();
    empty.BeginFrame("folder", false, 2, 1., 1, false); empty.EndFrame();
    return Check(!empty.Active(), "an empty view starts no motion");
}
bool TestClippedAndUnloadedImages() {
    GalleryLayoutAnimation motion;
    const auto slots = [&](float a, float b) {
        for (const auto& entry : {std::pair<const char*,float>{"a",a}, {"b",b}}) {
            const ImRect image({entry.second,50},{entry.second+120,130});
            motion.ObserveSlot(entry.first,image,clip);
            if (image.Overlaps(clip)) motion.Observe(entry.first,image,clip);
        }
        motion.EndFrame();
    };
    motion.BeginFrame("folder",true,1,0.,0,false); slots(20,1200);
    motion.BeginFrame("folder",true,2,1.,1,false); slots(1200,20);
    if (!Check(motion.Protects("a") && motion.Protects("b"),
        "clipped source and destination slots participate by identity")) return false;
    motion.BeginFrame("folder",true,3,1.16,1,false); slots(1200,20);
    ImDrawList draw(ImGui::GetDrawListSharedData()); draw._ResetForNewFrame();
    draw.PushClipRect(clip.Min,clip.Max);
    int resolutions = 0;
    const auto texture = [&](const std::string& key) {
        ++resolutions;
        return key == "a" ? GalleryLayoutAnimation::Texture{7,120,80} : GalleryLayoutAnimation::Texture{};
    };
    motion.Draw(&draw,clip,texture);
    if (!Check(resolutions == 2 && draw.VtxBuffer.size() == 4,
        "moving a clipped image preserves its path while an unloaded thumbnail stays unloaded")) return false;
    motion.BeginFrame("folder",true,4,1.33,1,false); slots(1200,20);
    resolutions = 0; motion.Draw(&draw,clip,texture);
    return Check(!motion.Active() && resolutions == 0 && !motion.Protects("a"),
        "settled motion performs no texture resolution");
}
}
int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr; io.DisplaySize = {1000, 700};
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    const bool passed = TestSortTransitions(true) && TestSortTransitions(false) &&
        TestInterruptions() && TestClippedAndUnloadedImages();
    ImGui::EndFrame(); ImGui::DestroyContext();
    if (!passed) return 1;
    std::cout << "Gallery layout animation tests passed (9 sort changes in each view, interruptions, reduced motion, lifetime).\n";
}
