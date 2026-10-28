#include "Editor/Internal/RawLab/RawLabGalleryLayoutAnimation.h"
#include "Editor/Internal/RawLab/RawLabGalleryHitTesting.h"
#include "Editor/Internal/RawLab/RawLabGalleryGridSubmission.h"

#include <cmath>
#include <cstdlib>
#include <new>
#include <iostream>
#include <utility>

namespace {
bool countPoseAllocations = false;
std::size_t poseAllocations = 0;
}
void* operator new(std::size_t size) {
    if (countPoseAllocations) ++poseAllocations;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

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
bool TestClippedGridSubmission() {
    using Stack::Editor::RawLabInternal::SubmitGalleryGrid;
    const ImVec2 tile(158, 148);
    constexpr float gap = 16;
    // Compare real ImGui layout state against the former all-widget path,
    // including an incomplete last row and a viewport wholly outside the group.
    for (const float rowGap : {0.f, 4.f, 16.f})
    for (const int count : {1, 19, 10000}) {
        const int columns = 7;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, rowGap));
        ImGui::BeginChild("grid-submission-check", ImVec2(900, 500));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        auto* window = ImGui::GetCurrentWindow();
        const auto before = window->DC;
        for (int index = 0; index < count; ++index) {
            if (index % columns) ImGui::SameLine(0.f, gap);
            ImGui::Dummy(tile);
        }
        const ImVec2 oldAfter = ImGui::GetCursorScreenPos();
        const ImVec2 oldMaximum = window->DC.CursorMaxPos;
        window->DC = before;
        int submitted = 0;
        SubmitGalleryGrid(count, columns, tile, gap, {1, 3}, [&](int index) {
            const ImVec2 position = ImGui::GetCursorScreenPos();
            if (std::abs(position.x - (origin.x + (index % columns) * (tile.x + gap))) > .01f ||
                std::abs(position.y - (origin.y + (index / columns) * (tile.y + rowGap))) > .01f)
                submitted = -100000;
            ++submitted;
            ImGui::Dummy(tile);
        });
        const bool correct = submitted == std::max(0, std::min(count, 21) - 7) &&
            std::abs(oldAfter.y - ImGui::GetCursorScreenPos().y) < .01f &&
            std::abs(oldMaximum.x - window->DC.CursorMaxPos.x) < .01f &&
            std::abs(oldMaximum.y - window->DC.CursorMaxPos.y) < .01f;
        window->DC = before;
        int outside = 0;
        SubmitGalleryGrid(count, columns, tile, gap, {20000, 20001}, [&](int) { ++outside; });
        const bool extent = outside == 0 && std::abs(oldMaximum.y - window->DC.CursorMaxPos.y) < .01f;
        ImGui::EndChild();
        ImGui::PopStyleVar();
        if (!Check(correct && extent, "clipped grid preserves tile positions/extents and only submits visible rows")) return false;
    }
    return true;
}
bool TestPresentedStorageReuse() {
    GalleryLayoutAnimation motion;
    std::vector<std::string> keys;
    for (int i = 0; i < 64; ++i)
        keys.push_back(std::string(40 + i % 5, 'x') + "/capture-" + std::to_string(i));
    const auto image = [](int i, bool visible) {
        const float x = visible ? 20.f + (i % 8) * 120.f : 2000.f + i * 120.f;
        const float y = 20.f + (i / 8) * 80.f;
        return ImRect({x,y},{x+100,y+60});
    };
    const auto observe = [&](int visibleCount, bool reverse) {
        for (int order = 0; order < 64; ++order) {
            const int i = reverse ? 63-order : order;
            const auto rect = image(i, i < visibleCount);
            motion.ObserveSlot(keys[i],rect,clip);
            motion.Observe(keys[i],rect,clip);
        }
        motion.EndFrame();
    };
    const auto texture = [](const std::string&) { return GalleryLayoutAnimation::Texture{7,100,60}; };
    ImDrawList draw(ImGui::GetDrawListSharedData());
    const auto prepareDraw = [&] { draw._ResetForNewFrame(); draw.PushClipRect(clip.Min,clip.Max); };
    motion.BeginFrame("folder",true,1,0.,0,false); observe(1,false);
    motion.BeginFrame("folder",false,2,1.,1,false); observe(64,false);
    motion.BeginFrame("folder",false,3,1.20,1,false); observe(64,false);
    // Sorting may put a longer key into a formerly shorter slot. Warm the
    // unchanged identity set before checking steady animation allocations.
    // Longer paths may still grow a reused string; do not require zero growth.
    for (int warm = 0; warm < 2; ++warm) { prepareDraw(); motion.Draw(&draw,clip,texture); }
    prepareDraw(); poseAllocations = 0; countPoseAllocations = true;
    motion.Draw(&draw,clip,texture);
    countPoseAllocations = false;
    if (!Check(poseAllocations < keys.size() && draw.VtxBuffer.Size == 64*4,
        "warm animation reuses path storage instead of allocating every key again")) return false;
    motion.BeginFrame("folder",false,4,1.21,1,false); observe(16,true);
    std::vector<std::string> resolved;
    prepareDraw();
    motion.Draw(&draw,clip,[&](const std::string& key) { resolved.push_back(key); return texture(key); });
    if (!Check(resolved.size() == 16 && draw.VtxBuffer.Size == 16*4,
        "shrinking participation removes stale presented entries")) return false;
    for (int order = 0; order < 16; ++order) {
        const int i = 15-order;
        ImRect displayed;
        if (!Check(resolved[order] == keys[i] && motion.DisplayRect(keys[i],image(i,true),displayed) &&
            motion.ImageAt(displayed.GetCenter()) == keys[i],
            "resorted reused slots retain the complete current key and picking pose")) return false;
    }
    prepareDraw(); motion.Draw(&draw,ImRect({0,0},{0,0}),texture);
    if (!Check(draw.VtxBuffer.empty() && motion.ImageAt({70,50}).empty(),
        "empty clipping cannot retain a previously rendered picking pose")) return false;
    motion.Reset();
    prepareDraw(); motion.Draw(&draw,clip,texture);
    return Check(!motion.Active() && draw.VtxBuffer.empty() && motion.ImageAt({70,50}).empty(),
        "reset releases reused presentation identities");
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
        motion.BeginFrame("folder", grid, frame++, time + .11, mode, false);
        Observe(motion, destination);
        const float halfway = origin + (destination - origin) * .875f;
        if (!Check(std::abs(DrawX(motion) - halfway) < .01f, "sort responds promptly with eased elapsed-time motion")) return false;
        motion.BeginFrame("folder", grid, frame++, time + .23, mode, false);
        Observe(motion, destination);
        if (!Check(!motion.Active() && !motion.Protects("image"),
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
    if (!Check(!motion.Active(),
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
    motion.BeginFrame("folder",true,3,1.066,1,false); slots(1200,20);
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
bool TestStackMotionAndInputGeometry() {
    GalleryLayoutAnimation motion;
    const ImRect origin({20, 50}, {140, 130});
    const ImRect destination({420, 350}, {540, 430});
    const ImRect hidden({420, 350}, {540, 350});
    motion.BeginFrame("folder", true, 1, 0., 0, false);
    motion.SetViewport(clip);
    motion.ObserveSlot("image", origin, clip);
    motion.Observe("image", origin, clip);
    motion.EndFrame();
    motion.BeginFrame("folder", true, 2, 1., 1, false);
    motion.ObserveSlot("image", destination, hidden);
    motion.EndFrame();
    motion.BeginFrame("folder", true, 3, 1.066, 1, false);
    motion.ObserveSlot("image", destination, hidden);
    motion.EndFrame();
    ImDrawList draw(ImGui::GetDrawListSharedData()); draw._ResetForNewFrame();
    draw.PushClipRect(clip.Min, clip.Max);
    motion.Draw(&draw, clip, [](const auto&) { return GalleryLayoutAnimation::Texture{7, 120, 80}; });
    if (!Check(draw.VtxBuffer.Size == 4, "a photo moving into a stack remains a complete image")) return false;
    for (const auto& command : draw.CmdBuffer) {
        if (command.ElemCount && !Check(command.ClipRect.w == clip.Max.y,
            "the destination's collapsed strip does not crop the moving image")) return false;
    }
    const ImVec2 displayed = draw.VtxBuffer[0].pos;
    ImRect hit;
    if (!Check(motion.DisplayRect("image", destination, hit) &&
        std::abs(hit.Min.x - displayed.x) < .01f && std::abs(hit.Min.y - displayed.y) < .01f,
        "input geometry follows the moving photo")) return false;
    if (!Check(motion.ImageAt(hit.GetCenter()) == "image" && motion.ImageAt(destination.GetCenter()).empty(),
        "hit testing uses displayed image identity rather than its destination slot")) return false;
    ImGui::SetCursorScreenPos(destination.Min);
    auto* tileDraw = ImGui::GetWindowDrawList();
    const int firstVertex = tileDraw->VtxBuffer.Size;
    {
        Stack::Editor::RawLabInternal::GalleryTileMotion tile(motion, "image", destination);
        Stack::Editor::RawLabInternal::GalleryThumbnailButton("moving-card", destination.GetSize(),
            destination.GetHeight(), ImGuiButtonFlags_None, &tile);
        if (!Check(std::abs(ImGui::GetItemRectMin().x - hit.Min.x) < .01f &&
            std::abs(ImGui::GetItemRectMin().y - hit.Min.y) < .01f,
            "the production tile registers its displayed hit rectangle")) return false;
        tileDraw->AddRectFilled(destination.Min, destination.Max, IM_COL32_WHITE);
    }
    if (!Check(tileDraw->VtxBuffer.Size >= firstVertex + 4 &&
        std::abs(tileDraw->VtxBuffer[firstVertex].pos.x - hit.Min.x) < .01f &&
        std::abs(tileDraw->VtxBuffer[firstVertex].pos.y - hit.Min.y) < .01f,
        "tile decoration follows the same pose as its hit rectangle")) return false;
    // A discovery during sorting must neither disappear into the overlay nor
    // fly in from an unrelated off-screen location.
    motion.ObserveSlot("new", origin, clip);
    return Check(!motion.Observe("new", origin, clip), "new ready thumbnails draw immediately in their actual slot");
}
// Expanding on the second mouse-down hides the still-active collapsed cover.
// Keep submitting that cover, as production does, while its members live in a
// separate window. A hidden cover must not retain capture after mouse release.
bool TestExpandedFilmstripClickHandoff(bool disablePointer = false) {
    using Stack::Editor::RawLabInternal::GalleryThumbnailButton;
    bool expanded = false;
    int memberClicks = 0, memberOpens = 0, singletonClicks = 0;
    const auto frame = [&](float x, float y, int buttonEvent) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        if (buttonEvent >= 0) io.AddMouseButtonEvent(0, buttonEvent != 0);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 200});
        ImGui::SetNextWindowSize({700, 350});
        const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::Begin("Filmstrip handoff drawer", nullptr, flags);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::BeginChild("content", {650, 300});
        const auto buttonFlags = ImGuiButtonFlags_PressedOnClickRelease |
            ImGuiButtonFlags_PressedOnDoubleClick;
        ImGui::SetCursorScreenPos({100, 400});
        if (GalleryThumbnailButton("cover", {120, 80}, expanded && !disablePointer ? 0.f : 80.f,
                buttonFlags, nullptr, !(expanded && disablePointer)) &&
            ImGui::IsMouseDoubleClicked(0)) expanded = true;
        ImGui::SetCursorScreenPos({300, 400});
        if (GalleryThumbnailButton("singleton", {120, 80}, 80.f, buttonFlags)) ++singletonClicks;
        if (expanded) {
            ImGui::SetNextWindowPos({100, 280});
            ImGui::SetNextWindowSize({120, 200});
            ImGui::Begin("Filmstrip handoff members", nullptr, flags);
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            ImGui::SetCursorScreenPos({100, 280});
            if (GalleryThumbnailButton("member", {120, 80}, 80.f, buttonFlags)) {
                ++memberClicks;
                if (ImGui::IsMouseDoubleClicked(0)) ++memberOpens;
            }
            ImGui::SetCursorScreenPos({100, 400});
            GalleryThumbnailButton("expanded cover", {120, 80}, 80.f, buttonFlags);
            ImGui::End();
        }
        ImGui::EndChild(); ImGui::End(); ImGui::Render();
    };
    // Warm up window hit testing and expire the previous scenario's click
    // chain before double-clicking the collapsed stack.
    const auto& io = ImGui::GetIO();
    const int settleFrames = static_cast<int>(io.MouseDoubleClickTime / io.DeltaTime) + 2;
    for (int frameIndex = 0; frameIndex < settleFrames; ++frameIndex)
        frame(150, 440, -1);
    frame(150, 440, 1); frame(150, 440, 0); frame(150, 440, 1);
    if (!Check(expanded, "double-click expands the collapsed stack")) return false;
    frame(150, 440, 0); frame(150, 440, -1);
    if (!Check(ImGui::GetActiveID() == 0,
        "hidden collapsed cover releases capture after expansion")) return false;
    // Both a member in the independent window and a singleton in the host
    // must remain clickable. Moving the pointer resets the double-click chain.
    frame(150, 320, -1); frame(150, 320, 1); frame(150, 320, 0);
    frame(150, 320, 1); frame(150, 320, 0);
    frame(350, 440, -1); frame(350, 440, 1);
    frame(350, 440, -1); frame(350, 440, 0);
    if (!Check(memberClicks == 2 && memberOpens == 1 && singletonClicks == 1,
        "expanded members open on double-click and neighboring singletons select")) return false;
    expanded = false;
    frame(150, 440, -1); frame(150, 440, -1);
    frame(150, 440, 1); frame(150, 440, 0); frame(150, 440, 1);
    frame(150, 440, 0); frame(150, 440, -1);
    return Check(expanded && ImGui::GetActiveID() == 0,
        "collapse and repeated expansion do not strand pointer capture");
}
}
int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr; io.DisplaySize = {1000, 700};
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    bool passed = TestPresentedStorageReuse() && TestClippedGridSubmission() && TestSortTransitions(true) && TestSortTransitions(false) &&
        TestInterruptions() && TestClippedAndUnloadedImages() && TestStackMotionAndInputGeometry();
    ImGui::EndFrame();
    passed = TestExpandedFilmstripClickHandoff() && passed;
    passed = TestExpandedFilmstripClickHandoff(true) && passed;
    ImGui::DestroyContext();
    if (!passed) return 1;
    std::cout << "Gallery layout animation tests passed (clipped grid: 14/10000 tiles submitted, identical extents; 9 sort changes in each view, interruptions, reduced motion, lifetime, expanded filmstrip click handoff).\n";
}
